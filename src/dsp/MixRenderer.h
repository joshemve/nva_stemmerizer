#pragma once

#include "StemMixState.h"
#include "StemSession.h"
#include "Transport.h"

namespace stemmerizer::dsp
{

/// Pure DSP function — given a session snapshot + mix state + transport
/// position, render N frames of the mix into a stereo destination buffer.
/// Called from the audio thread; no allocations, no locks.
///
/// `outL` / `outR` point to distinct stereo planes (length numFrames
/// each). The render OVERWRITES whatever's in them.
///
/// Returns the new transport position after writing.
class MixRenderer
{
public:
    static long long render (const StemSession::Snapshot& snap,
                             const StemMixState&          mix,
                             Transport&                   transport,
                             float* outL,
                             float* outR,
                             int    numFrames);
};

} // namespace stemmerizer::dsp
