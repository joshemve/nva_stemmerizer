#pragma once

#include <string>
#include <vector>

namespace stemmerizer::dsp::AudioFileIO
{

struct DecodedAudio
{
    int                sampleRate  { 44100 };
    int                numChannels { 2 };
    long long          numFrames   { 0 };
    std::vector<float> interleaved;     // length = numFrames * numChannels
};

enum class ExportFormat { Wav24, Wav16, Wav32f, Flac, Mp3 };

/// Decode WAV/FLAC/MP3/AIFF/OGG using JUCE's AudioFormatManager.
/// Returns true on success; on failure sets `errorOut`.
bool decode (const std::string& path, DecodedAudio& out, std::string& errorOut);

/// Encode and write a stem to disk. Caller picks format and bit-depth.
/// `interleaved` is length = numFrames * numChannels (matches DecodedAudio).
bool encode (const std::string& path,
             ExportFormat format,
             const float* interleaved,
             long long numFrames,
             int numChannels,
             int sampleRate,
             std::string& errorOut);

} // namespace stemmerizer::dsp::AudioFileIO
