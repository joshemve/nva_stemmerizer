#include "StemSplitter.h"

#include "AudioFileIO.h"
#include "OnnxBackend.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace stemmerizer::dsp
{

// PIMPL: forwards every call to OnnxBackend. The PIMPL boundary is still
// useful — it keeps ORT and Eigen headers out of the rest of the codebase,
// so e.g. PluginEditor doesn't recompile when we change inference details.
struct StemSplitter::Impl
{
    OnnxBackend backend;
};

StemSplitter::StemSplitter()  : impl (std::make_unique<Impl>()) {}
StemSplitter::~StemSplitter() = default;

bool StemSplitter::loadModel (const std::string& weightsDir, ModelVariant variant)
{
    lastError.clear();
    modelLoaded = false;

    // We expect .onnx files now (one per variant), placed next to the old
    // .gguf files by the updated fetch_weights.py script. Filename mapping
    // lives in StemSplitter.h (modelWeightFile()), so users can drop new
    // model variants in without rebuilding.
    const fs::path file = fs::path (weightsDir) / modelWeightFile (variant);
    if (! fs::exists (file))
    {
        lastError = "Weights file not found: " + file.string();
        return false;
    }

    const int n = numStems (variant);
    if (! impl->backend.loadModel (file.string(), n, OnnxBackend::Backend::Auto))
    {
        lastError = impl->backend.errorMessage();
        return false;
    }

    loadedVariant = variant;
    modelLoaded   = true;
    return true;
}

SplitResult StemSplitter::split (const float* audio,
                                 int64_t numFrames,
                                 int numChannels,
                                 int sampleRate,
                                 const SplitOptions& /*opts*/,
                                 ProgressCallback onProgress,
                                 const std::atomic<bool>& cancel)
{
    if (! modelLoaded)
    {
        SplitResult r; r.errorMessage = "No model loaded."; return r;
    }
    return impl->backend.split (audio, numFrames, numChannels, sampleRate,
                                std::move (onProgress), cancel);
}

SplitResult StemSplitter::splitFile (const std::string& inputPath,
                                     const SplitOptions& opts,
                                     ProgressCallback onProgress,
                                     const std::atomic<bool>& cancel)
{
    SplitResult result;
    AudioFileIO::DecodedAudio decoded;
    std::string err;
    if (! AudioFileIO::decode (inputPath, decoded, err))
    {
        result.errorMessage = "Failed to decode input: " + err;
        return result;
    }
    if (onProgress) onProgress ({ 0.01f, "loaded", 0, 0 });

    return split (decoded.interleaved.data(),
                  decoded.numFrames, decoded.numChannels, decoded.sampleRate,
                  opts, std::move (onProgress), cancel);
}

} // namespace stemmerizer::dsp
