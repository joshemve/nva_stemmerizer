#pragma once

#include "StemMixState.h"
#include "StemSession.h"
#include "Transport.h"

namespace stemmerizer::dsp
{

/// Pure DSP class — given a session snapshot + mix state + transport
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
///
/// Instance state holds a short A/B crossfade so toggling `playOriginal`
/// between blocks doesn't click. Callers that don't keep an instance can
/// use the static `render(...)` wrapper, which forwards to a static-local
/// instance — fine since renderer state is process-global anyway.
class MixRenderer
{
public:
    MixRenderer() = default;

    /// Instance render. See file header.
    void renderBlock (const StemSession::Snapshot& snap,
                      const StemMixState&          mix,
                      Transport&                   transport,
                      bool                         playOriginal,
                      double                       srcSamplesPerOutputSample,
                      float* outL,
                      float* outR,
                      int    numFrames);

    /// Back-compat wrapper. Forwards to a static-local instance so old
    /// callers (PluginProcessor) keep working unchanged.
    static void render (const StemSession::Snapshot& snap,
                        const StemMixState&          mix,
                        Transport&                   transport,
                        bool                         playOriginal,
                        double                       srcSamplesPerOutputSample,
                        float* outL,
                        float* outR,
                        int    numFrames);

private:
    // ---- A/B crossfade state ---------------------------------------
    // When `playOriginal` flips between successive renderBlock() calls,
    // we ramp the *from* signal down and the *to* signal up over
    // `fadeTotalFrames` source-domain frames. `fadeFramesRemaining`
    // counts the remaining frames; while > 0 we mix both signals.
    bool      lastPlayOriginal       { false };
    bool      fadeFromOriginal       { false };
    long long fadeFramesRemaining    { 0 };
    long long fadeTotalFrames        { 0 };

    // Identity of the snapshot we last serviced — used to reset fade
    // state across snapshot swaps (new split loaded, sample rate change,
    // numStems change), since carrying a fade across content is nonsense.
    const void* lastSnapPtr          { nullptr };
    long long   lastSnapNumFrames    { 0 };
    int         lastSnapSampleRate   { 0 };
    int         lastSnapNumStems     { 0 };
    bool        haveLast             { false };
};

} // namespace stemmerizer::dsp
