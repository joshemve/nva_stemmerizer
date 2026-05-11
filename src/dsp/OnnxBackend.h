#pragma once

#include "StemSplitter.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace stemmerizer::dsp
{

/// GPU-accelerated inference backend powered by ONNX Runtime with the
/// DirectML execution provider (with automatic CPU fallback). Lives behind
/// the StemSplitter PIMPL so the rest of the codebase is agnostic to the
/// neural-net backbone.
///
/// Design goals:
///   * Single backend for every Windows GPU vendor (NV/AMD/Intel iGPU) via
///     DirectML — no per-vendor build, no CUDA install for end users.
///   * Graceful CPU fallback when the DML provider can't be created
///     (very old Windows, no D3D12-capable GPU, headless server, etc.).
///   * Lock-free hot path during inference; one-time allocations in
///     loadModel() so split() is allocation-free in steady state.
class OnnxBackend
{
public:
    enum class Backend { Auto, DirectML, Cpu };

    OnnxBackend();
    ~OnnxBackend();

    OnnxBackend (const OnnxBackend&)            = delete;
    OnnxBackend& operator= (const OnnxBackend&) = delete;

    /// Load an ONNX-converted htdemucs model from disk. `modelFile` is the
    /// full path to e.g. <weights>/htdemucs.onnx. `numSources` is 4 for
    /// htdemucs / htdemucs_ft, 6 for htdemucs_6s — used to size the
    /// output stem buffers correctly.
    bool loadModel (const std::string& modelFile,
                    int numSources,
                    Backend preferred);

    /// The execution provider that was actually selected at session-create
    /// time. Even if the caller asked for DirectML, we may have fallen
    /// back to CPU — the UI shows this so users know if their GPU is
    /// engaged. Only meaningful after a successful loadModel().
    Backend activeBackend() const noexcept { return active; }

    /// Pretty name for the active backend (used in error dialogs / status).
    std::string activeBackendName() const;

    /// Whether a model is loaded.
    bool isLoaded() const noexcept;

    /// Stem count of the loaded model (matches what was passed to
    /// loadModel()).
    int numSources() const noexcept { return sources; }

    /// Last error string from loadModel() / split().
    const std::string& errorMessage() const noexcept { return lastError; }

    /// Run inference on a full audio buffer. The pipeline:
    ///   1. Resample/upmix input to stereo @ 44.1 kHz (model rate).
    ///   2. Chunk into 7.81-second segments with 25% overlap.
    ///   3. For each segment: STFT -> ORT Run -> ISTFT.
    ///   4. Hann-windowed overlap-add into per-stem output buffers.
    ///   5. Resample stems back to the host's original sample rate.
    SplitResult split (const float* audio,
                       long long numFrames,
                       int numChannels,
                       int sampleRate,
                       ProgressCallback onProgress,
                       const std::atomic<bool>& cancel);

private:
    struct Impl;                       // hides ORT + Eigen + demucs.onnx
    std::unique_ptr<Impl> impl;        // headers from this class's API

    Backend     active   { Backend::Cpu };
    int         sources  { 4 };
    std::string lastError;
};

} // namespace stemmerizer::dsp
