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

    // Source-domain (session sample rate) read cursor, fractional.
    double srcPos        = (double) transport.position();
    const long long len  = snap.numFrames;
    const bool loopOn    = transport.isLoopOn();
    const double lpS     = (double) transport.loopStart();
    const double lpE     = (double) transport.loopEnd();
    const bool useOrig   = playOriginal && ! snap.original.empty();
    const bool anySolo   = ! useOrig && mix.anySoloed();

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

        if (useOrig)
        {
            readFrameLerp (snap.original.data(), snap.numChannels, len, srcPos, L, R);
        }
        else
        {
            const int N = (int) snap.stems.size();
            for (int s = 0; s < N; ++s)
            {
                const auto& slot = mix.slot (s);
                if (slot.muted.load (std::memory_order_relaxed)) continue;
                if (anySolo && ! slot.soloed.load (std::memory_order_relaxed)) continue;

                const float gain = slot.gain.load (std::memory_order_relaxed);
                const float pan  = slot.pan.load  (std::memory_order_relaxed);
                float sL = 0.f, sR = 0.f;
                readFrameLerp (snap.stems[(size_t) s].interleaved.data(),
                               snap.stems[(size_t) s].numChannels, len, srcPos,
                               sL, sR);

                float gL, gR;
                panGains (pan, gL, gR);
                L += sL * gain * gL;
                R += sR * gain * gR;
            }
        }

        outL[i] = L;
        outR[i] = R;

        srcPos += srcSamplesPerOutputSample;

        if (loopOn && srcPos >= lpE && lpE > lpS)
            srcPos = lpS + std::fmod (srcPos - lpS, lpE - lpS);
    }

    // Commit the advance to the transport in source-domain integer samples.
    const long long delta = (long long) std::floor (srcPos) - transport.position();
    transport.advance (delta);
}

} // namespace stemmerizer::dsp
