#include "MixRenderer.h"

#include <algorithm>
#include <cmath>

namespace stemmerizer::dsp
{

namespace
{
    /// Equal-power pan law. pan in [-1, +1].
    inline void panGains (float pan, float& gL, float& gR) noexcept
    {
        const float p = std::max (-1.f, std::min (1.f, pan));
        // map [-1..+1] -> angle in [0..pi/2]
        const float a = (p + 1.f) * 0.25f * 3.14159265358979323846f;
        gL = std::cos (a);
        gR = std::sin (a);
    }

    /// Read one stereo frame from an interleaved buffer at the given index.
    inline void readFrame (const float* interleaved,
                           int          numChannels,
                           long long    frame,
                           float&       l,
                           float&       r) noexcept
    {
        const float v0 = interleaved[(size_t) (frame * numChannels)];
        if (numChannels == 1)        { l = r = v0; return; }
        const float v1 = interleaved[(size_t) (frame * numChannels + 1)];
        l = v0; r = v1;
    }
}

long long MixRenderer::render (const StemSession::Snapshot& snap,
                               const StemMixState&          mix,
                               Transport&                   transport,
                               float* outL,
                               float* outR,
                               int    numFrames)
{
    // Empty session — emit silence and don't advance.
    if (snap.numFrames <= 0 || (snap.stems.empty() && snap.original.empty()))
    {
        std::fill (outL, outL + numFrames, 0.f);
        std::fill (outR, outR + numFrames, 0.f);
        return transport.position();
    }

    // Stopped — emit silence but DON'T zero the position.
    if (! transport.isPlaying())
    {
        std::fill (outL, outL + numFrames, 0.f);
        std::fill (outR, outR + numFrames, 0.f);
        return transport.position();
    }

    long long pos = transport.position();
    const long long len = snap.numFrames;
    const bool loopOn   = transport.isLoopOn();
    const long long lpS = transport.loopStart();
    const long long lpE = transport.loopEnd();
    const bool useOrig  = snap.playOriginal && ! snap.original.empty();
    const bool anySolo  = ! useOrig && mix.anySoloed();

    for (int i = 0; i < numFrames; ++i)
    {
        // Emit silence past the end (and stop the transport).
        if (pos >= len)
        {
            outL[i] = outR[i] = 0.f;
            continue;
        }

        float L = 0.f, R = 0.f;

        if (useOrig)
        {
            readFrame (snap.original.data(), snap.numChannels, pos, L, R);
        }
        else
        {
            const int N = (int) snap.stems.size();
            for (int s = 0; s < N; ++s)
            {
                const auto& slot = mix.slot (s);
                const bool muted  = slot.muted.load (std::memory_order_relaxed);
                const bool soloed = slot.soloed.load (std::memory_order_relaxed);
                if (muted) continue;
                if (anySolo && ! soloed) continue;

                const float gain = slot.gain.load (std::memory_order_relaxed);
                const float pan  = slot.pan.load  (std::memory_order_relaxed);
                float sL = 0.f, sR = 0.f;
                readFrame (snap.stems[(size_t) s].interleaved.data(),
                           snap.stems[(size_t) s].numChannels, pos, sL, sR);

                float gL, gR;
                panGains (pan, gL, gR);
                L += sL * gain * gL;
                R += sR * gain * gR;
            }
        }

        outL[i] = L;
        outR[i] = R;

        ++pos;

        if (loopOn && pos >= lpE && lpE > lpS) pos = lpS;
    }

    // Update the transport's stored position once at the end of the block.
    return transport.advance (pos - transport.position());
}

} // namespace stemmerizer::dsp
