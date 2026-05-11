#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace stemmerizer::dsp
{

enum class ModelVariant
{
    Htdemucs4Stem,      // vocals / drums / bass / other
    Htdemucs4StemFt,    // fine-tuned, slower, marginally better
    Htdemucs6Stem       // adds guitar, piano
};

inline const char* modelDisplayName (ModelVariant v) noexcept
{
    switch (v)
    {
        case ModelVariant::Htdemucs4Stem:   return "4-Stem (fast)";
        case ModelVariant::Htdemucs4StemFt: return "4-Stem (high quality)";
        case ModelVariant::Htdemucs6Stem:   return "6-Stem (vocals/drums/bass/guitar/piano/other)";
    }
    return "?";
}

inline const char* modelWeightFile (ModelVariant v) noexcept
{
    // ONNX format — produced by scripts/fetch_weights.py via the
    // demucs.onnx PyTorch->ONNX converter.
    switch (v)
    {
        case ModelVariant::Htdemucs4Stem:   return "htdemucs.onnx";
        case ModelVariant::Htdemucs4StemFt: return "htdemucs_ft.onnx";
        case ModelVariant::Htdemucs6Stem:   return "htdemucs_6s.onnx";
    }
    return "htdemucs.onnx";
}

inline int numStems (ModelVariant v) noexcept
{
    return v == ModelVariant::Htdemucs6Stem ? 6 : 4;
}

inline const char* stemName (ModelVariant v, int idx) noexcept
{
    static const char* k4[] = { "drums", "bass", "other", "vocals" };
    static const char* k6[] = { "drums", "bass", "other", "vocals", "guitar", "piano" };
    if (v == ModelVariant::Htdemucs6Stem) return (idx >= 0 && idx < 6) ? k6[idx] : "?";
    return (idx >= 0 && idx < 4) ? k4[idx] : "?";
}

/// One source stem produced by the splitter.
struct StemBuffer
{
    std::string name;                       // e.g. "vocals"
    int sampleRate { 44100 };
    int numChannels { 2 };
    std::vector<float> interleaved;         // size = numChannels * numFrames
    int64_t numFrames() const noexcept
    {
        return numChannels > 0 ? (int64_t) interleaved.size() / numChannels : 0;
    }
};

/// Result of a finished split job.
struct SplitResult
{
    bool success { false };
    std::string errorMessage;
    std::vector<StemBuffer> stems;          // length = numStems(model)
    double processingSeconds { 0.0 };
};

/// User-tweakable processing parameters.
struct SplitOptions
{
    ModelVariant model { ModelVariant::Htdemucs4Stem };
    int numThreads { 0 };                   // 0 = auto (use all cores)
    bool shifts { false };                  // tta/shift trick (slower, ~0.2dB SDR)
    int  shiftIters { 1 };
    bool segmentOverlap { true };
    float overlapAmount { 0.25f };          // 0..0.5
};

/// Progress reporting. Called from the worker thread (synchronize before
/// touching the UI).
struct ProgressReport
{
    float fraction { 0.f };                 // 0..1
    std::string stage;                      // "loading", "processing", "writing"
    int currentSegment { 0 };
    int totalSegments { 0 };
};

using ProgressCallback = std::function<void(const ProgressReport&)>;

/// The thing that actually does it. Owns a single demucs model context and
/// processes one input file at a time. Safe to reuse across many jobs.
class StemSplitter
{
public:
    StemSplitter();
    ~StemSplitter();

    StemSplitter (const StemSplitter&)            = delete;
    StemSplitter& operator= (const StemSplitter&) = delete;

    /// Load a model. Heavy — call once and keep the splitter alive.
    /// `weightsDir` should be the resolved on-disk directory containing the
    /// .gguf weight files (typically the install dir's `weights/` subfolder).
    /// Returns false + sets `errorMessage()` on failure.
    bool loadModel (const std::string& weightsDir, ModelVariant variant);

    /// Whether a model is currently loaded.
    bool isModelLoaded() const noexcept { return modelLoaded; }

    /// Currently loaded variant (only meaningful if isModelLoaded()).
    ModelVariant currentModel() const noexcept { return loadedVariant; }

    /// Last error message (loadModel or split).
    const std::string& errorMessage() const noexcept { return lastError; }

    /// Process audio data already in memory. Stereo float, interleaved or
    /// planar — pass `interleaved=true` accordingly.
    SplitResult split (const float* audio,
                       int64_t numFrames,
                       int numChannels,
                       int sampleRate,
                       const SplitOptions& opts,
                       ProgressCallback onProgress,
                       const std::atomic<bool>& cancel);

    /// Process a file from disk (WAV / FLAC / MP3 supported).
    SplitResult splitFile (const std::string& inputPath,
                           const SplitOptions& opts,
                           ProgressCallback onProgress,
                           const std::atomic<bool>& cancel);

    struct Impl;   // public so internal helpers in StemSplitter.cpp can see it

private:
    std::unique_ptr<Impl> impl;

    bool         modelLoaded   { false };
    ModelVariant loadedVariant { ModelVariant::Htdemucs4Stem };
    std::string  lastError;
};

} // namespace stemmerizer::dsp
