#pragma once

#include "StemMixState.h"
#include "StemSession.h"
#include "Transport.h"

namespace stemmerizer::dsp
{

/// Pure DSP function — given a session snapshot + mix state + transport
/// position + A/B flag + (sessionSR / hostSR) ratio, render N frames of
/// the mix into a stereo destination buffer.
///
/// Called from the audio thread. NO allocations, NO locks.
///
/// Position math: the per-sample loop steps the *source-domain*
/// (session sample rate) read position by `srcStep = sessionSR / hostSR`
/// each output frame, with linear interpolation between samples. After
/// the block, transport.position is advanced by the source-domain delta
/// consumed (an integer count of source samples that elapsed).
///
/// `outL` / `outR` point to distinct stereo planes (length numFrames
/// each). The render OVERWRITES whatever's in them.
class MixRenderer
{
public:
    static void render (const StemSession::Snapshot& snap,
                        const StemMixState&          mix,
                        Transport&                   transport,
                        bool                         playOriginal,
                        double                       srcSamplesPerOutputSample,
                        float* outL,
                        float* outR,
                        int    numFrames);
};

} // namespace stemmerizer::dsp
