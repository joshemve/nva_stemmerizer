#pragma once

#include <juce_core/juce_core.h>

#include <optional>

namespace stemmerizer::dsp
{

struct KeyResult
{
    int  rootPitchClass { 0 };   // 0=C, 1=C#/Db, ..., 11=B
    bool isMinor        { false };
    float confidence    { 0.f }; // 0..1, gap to second-best candidate
};

struct BpmResult
{
    float bpm        { 0.f };
    float confidence { 0.f };    // 0..1, autocorr peak / noise floor
};

/// Estimate BPM via spectral-flux onset envelope + autocorrelation.
/// Returns nullopt if the result isn't confident enough to show.
/// `interleaved` is numFrames * numChannels samples in [-1, 1] range.
std::optional<BpmResult> analyzeBpm (const float* interleaved,
                                     long long   numFrames,
                                     int         numChannels,
                                     int         sampleRate);

/// Estimate musical key via chromagram + Krumhansl-Schmuckler profiles.
/// Returns nullopt if the gap between best and second-best key is too small.
std::optional<KeyResult> analyzeKey (const float* interleaved,
                                     long long   numFrames,
                                     int         numChannels,
                                     int         sampleRate);

/// Pretty-print "C minor", "F# major", etc. Returns empty for null.
juce::String keyName (const std::optional<KeyResult>& k);

} // namespace stemmerizer::dsp
