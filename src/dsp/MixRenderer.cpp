#include "MixRenderer.h"

#include <algorithm>
#include <cmath>

namespace stemmerizer::dsp
{

namespace
{
    /// Equal-power pan. pan in [-1, +1].
    inline void panGains (float pan, float& gL, float& gR) noexcept
    {
        const float p = std::max (-1.f, std::min (1.f, pan));
        const float a = (p + 1.f) * 0.25f * 3.14159265358979323846f;
        gL = std::cos (a);
        gR = std::sin (a);
    }

    /// Linear-interpolated read from an interleaved buffer at a fractional
    /// frame index. Clamps to [0, numFrames-1]. Returns one stereo pair.
    inline void readFrameLerp (const float* interleaved,
                               int          numChannels,
                               long long    numFrames,
                               double       framePos,
                               float&       l,
                               float&       r) noexcept
    {
        if (numFrames <= 0) { l = r = 0.f; return; }
        if (framePos < 0.0)        framePos = 0.0;
        if (framePos > (double) (numFrames - 1)) framePos = (double) (numFrames - 1);

        const long long i0 = (long long) framePos;
        const long long i1 = std::min<long long> (i0 + 1, numFrames - 1);
        const float frac   = (float) (framePos - (double) i0);

        const float l0 = interleaved[(size_t) (i0 * numChannels)];
        const float l1 = interleaved[(size_t) (i1 * numChannels)];
        if (numChannels == 1) { l = r = l0 + (l1 - l0) * frac; return; }
        const float r0 = interleaved[(size_t) (i0 * numChannels + 1)];
        const float r1 = interleaved[(size_t) (i1 * numChannels + 1)];
        l = l0 + (l1 - l0) * frac;
        r = r0 + (r1 - r0) * frac;
    }

    /// Sample the "original" branch at srcPos into (L, R).
    inline void sampleOriginal (const StemSession::Snapshot& snap,
                                double srcPos, float& L, float& R) noexcept
    {
        if (snap.original.empty()) { L = R = 0.f; return; }
        readFrameLerp (snap.original.data(), snap.numChannels,
                       snap.numFrames, srcPos, L, R);
    }

    /// Sample the "stems mix" branch at srcPos into (L, R), respecting
    /// mute/solo/gain/pan.
    inline void sampleStems (const StemSession::Snapshot& snap,
                             const StemMixState&          mix,
                             bool                         anySolo,
                             double srcPos, float& L, float& R) noexcept
    {
        L = R = 0.f;
        const int N = (int) snap.stems.size();
        for (int s = 0; s < N; ++s)
        {
            const auto& slot = mix.slot (s);
            if (slot.muted.load (std::memory_order_relaxed)) continue;
            if (anySolo && ! slot.soloed.load (std::memory_order_relaxed)) continue;

            // Audit safety: hard-cap per-stem gain at +6 dB (2.0×) on the
            // audio thread. The fader UI already clamps inputs, but state
            // restored from a project file, future automation paths, or
            // bugs upstream could still feed in a wild value. A 90+ dB
            // gain through speakers would be physically dangerous, so we
            // never trust the bare atomic — we always clamp before
            // multiplying into the mix bus.
            constexpr float kAudioThreadMaxGain = 2.0f;   // mirrors StemRow kMaxGain
            const float gainRaw = slot.gain.load (std::memory_order_relaxed);
            const float gain    = std::max (0.f, std::min (kAudioThreadMaxGain, gainRaw));
            const float pan     = slot.pan.load (std::memory_order_relaxed);
            float sL = 0.f, sR = 0.f;
            readFrameLerp (snap.stems[(size_t) s].interleaved.data(),
                           snap.stems[(size_t) s].numChannels,
                           snap.numFrames, srcPos, sL, sR);

            float gL, gR;
            panGains (pan, gL, gR);
            L += sL * gain * gL;
            R += sR * gain * gR;
        }
    }
}

void MixRenderer::render (const StemSession::Snapshot& snap,
                          const StemMixState&          mix,
                          Transport&                   transport,
                          bool                         playOriginal,
                          double                       srcSamplesPerOutputSample,
                          float* outL,
                          float* outR,
                          int    numFrames)
{
    // Process-global singleton: fade state must persist across calls,
    // and there's only ever one active renderer in this plugin.
    static MixRenderer s_instance;
    s_instance.renderBlock (snap, mix, transport, playOriginal,
                            srcSamplesPerOutputSample, outL, outR, numFrames);
}

void MixRenderer::renderBlock (const StemSession::Snapshot& snap,
                               const StemMixState&          mix,
                               Transport&                   transport,
                               bool                         playOriginal,
                               double                       srcSamplesPerOutputSample,
                               float* outL,
                               float* outR,
                               int    numFrames)
{
    // Empty session — emit silence and don't advance.
    if (snap.numFrames <= 0 || (snap.stems.empty() && snap.original.empty()))
    {
        std::fill (outL, outL + numFrames, 0.f);
        std::fill (outR, outR + numFrames, 0.f);
        return;
    }

    if (! transport.isPlaying())
    {
        std::fill (outL, outL + numFrames, 0.f);
        std::fill (outR, outR + numFrames, 0.f);
        return;
    }

    // ---- A/B crossfade state machine -------------------------------
    // Reset fade tracking if we're looking at a different snapshot
    // (new split loaded, SR / frame count / stem count changed).
    const int  curNumStems = (int) snap.stems.size();
    const bool snapMatches = haveLast
                           && lastSnapPtr        == (const void*) &snap
                           && lastSnapNumFrames  == snap.numFrames
                           && lastSnapSampleRate == snap.sampleRate
                           && lastSnapNumStems   == curNumStems;
    if (! snapMatches)
    {
        lastPlayOriginal     = playOriginal;
        fadeFramesRemaining  = 0;
        fadeFromOriginal     = playOriginal;
        haveLast             = true;
    }
    else if (playOriginal != lastPlayOriginal)
    {
        // Toggle detected — start a fade from the previous source to the
        // new one. 10 ms in source-domain frames, clamped to a sane min.
        fadeFromOriginal    = lastPlayOriginal;
        fadeTotalFrames     = std::max<long long> (64, snap.sampleRate / 100);
        fadeFramesRemaining = fadeTotalFrames;
        lastPlayOriginal    = playOriginal;
    }
    lastSnapPtr        = (const void*) &snap;
    lastSnapNumFrames  = snap.numFrames;
    lastSnapSampleRate = snap.sampleRate;
    lastSnapNumStems   = curNumStems;

    // Source-domain (session sample rate) read cursor, fractional.
    // Audit N2: seed from BOTH the integer pos and the sub-sample
    // remainder Transport carries between blocks. Without the phase
    // term, every block truncates the fraction and a non-integer SR
    // ratio (44.1 k session in a 48 k host etc.) drifts hundreds of ms
    // over a full track. Capture pos NOW so commitBlock() below can
    // detect a mid-block seek (audit N5).
    const long long blockStartPos = transport.position();
    double srcPos        = (double) blockStartPos + transport.phaseFrac();
    const long long len  = snap.numFrames;
    const bool loopOn    = transport.isLoopOn();
    const double lpS     = (double) transport.loopStart();
    const double lpE     = (double) transport.loopEnd();
    const bool useOrig   = playOriginal && ! snap.original.empty();
    const bool anySolo   = mix.anySoloed();

    for (int i = 0; i < numFrames; ++i)
    {
        // Past end of session — silence (and let the transport advance
        // signal end-of-file below).
        if (srcPos >= (double) len)
        {
            outL[i] = outR[i] = 0.f;
            srcPos += srcSamplesPerOutputSample;
            continue;
        }

        float L = 0.f, R = 0.f;

        if (fadeFramesRemaining > 0)
        {
            // Mix BOTH branches and linearly ramp from -> to.
            // t = 0 at fade start (full "from"), t = 1 at end (full "to").
            const float t = 1.f - (float) fadeFramesRemaining / (float) fadeTotalFrames;
            float fromL, fromR, toL, toR;
            if (fadeFromOriginal) sampleOriginal (snap, srcPos, fromL, fromR);
            else                  sampleStems    (snap, mix, anySolo, srcPos, fromL, fromR);
            if (useOrig)          sampleOriginal (snap, srcPos, toL, toR);
            else                  sampleStems    (snap, mix, anySolo, srcPos, toL, toR);
            L = fromL * (1.f - t) + toL * t;
            R = fromR * (1.f - t) + toR * t;
            --fadeFramesRemaining;
        }
        else if (useOrig)
        {
            sampleOriginal (snap, srcPos, L, R);
        }
        else
        {
            sampleStems (snap, mix, anySolo, srcPos, L, R);
        }

        outL[i] = L;
        outR[i] = R;

        srcPos += srcSamplesPerOutputSample;

        if (loopOn && srcPos >= lpE && lpE > lpS)
            srcPos = lpS + std::fmod (srcPos - lpS, lpE - lpS);
    }

    // Commit the block: pos += integer-part-consumed, phase = leftover
    // fractional. CAS against blockStartPos so a concurrent seek wins
    // (audit N5). Auto-stop-at-end-of-stream is handled inside
    // commitBlock when loop is off.
    const long long newPosInt = (long long) std::floor (srcPos);
    const double    newFrac   = srcPos - (double) newPosInt;
    transport.commitBlock (blockStartPos, newPosInt, newFrac);
}

} // namespace stemmerizer::dsp
