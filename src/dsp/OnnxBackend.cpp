#include "OnnxBackend.h"

#include "AudioFileIO.h"

// Windows.h must precede any ORT header so we control NOMINMAX + lean+mean
// and so the LoadLibraryEx / GetModuleHandleEx symbols are available.
#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#endif

// demucs.onnx public types (lives behind the PIMPL).
#include <onnxruntime_cxx_api.h>
#include "demucs.hpp"

// DirectML execution provider factory. Header ships in the
// Microsoft.ML.OnnxRuntime.DirectML NuGet package.
#include "dml_provider_factory.h"

#include <Eigen/Dense>
#include <unsupported/Eigen/CXX11/Tensor>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace stemmerizer::dsp
{

namespace
{
    constexpr int kModelSampleRate = 44100;

#ifdef _WIN32
    /// Ensure onnxruntime.dll is loaded BEFORE any ORT C++ wrapper code
    /// touches it. The DLL ships beside the plugin binary (VST3 bundle or
    /// standalone exe), not in the host's directory — so a normal
    /// load-time import would fail at module-load. With /DELAYLOAD the
    /// import is resolved on first call; we beat that by pre-loading here
    /// with an explicit absolute path derived from our own module handle.
    /// Idempotent + thread-safe.
    void ensureOnnxRuntimeLoaded()
    {
        static std::once_flag once;
        std::call_once (once, []()
        {
            // 1. Find OUR module path. GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
            //    on the address of this static function returns the HMODULE
            //    of whatever DLL/EXE contains us — i.e. the Stemmerizer
            //    plugin DLL when hosted, or Stemmerizer.exe standalone.
            HMODULE selfModule = nullptr;
            ::GetModuleHandleExW (
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&ensureOnnxRuntimeLoaded),
                &selfModule);

            wchar_t selfPathW [MAX_PATH] = {};
            if (selfModule != nullptr)
                ::GetModuleFileNameW (selfModule, selfPathW, MAX_PATH);

            std::filesystem::path selfDir;
            if (selfPathW[0] != 0)
                selfDir = std::filesystem::path (selfPathW).parent_path();

            // 2. Allow per-user override (developer / power user
            //    redirecting to a custom build of ORT).
            wchar_t envBuf[MAX_PATH] = {};
            if (::GetEnvironmentVariableW (L"STEMMERIZER_ORT_DIR",
                                           envBuf, MAX_PATH) > 0)
                selfDir = envBuf;

            // 3. Build the full path and load it. LoadLibraryExW with an
            //    absolute path bypasses Windows's search-order entirely,
            //    so it doesn't matter where the host process lives.
            if (! selfDir.empty())
            {
                const auto fullPath = (selfDir / L"onnxruntime.dll").wstring();
                HMODULE h = ::LoadLibraryExW (fullPath.c_str(), nullptr,
                                              LOAD_WITH_ALTERED_SEARCH_PATH);
                if (h != nullptr) return;
            }

            // 4. Last-ditch: try the default search (e.g. user dropped the
            //    DLL into PATH or System32). If this also fails, the next
            //    ORT call will throw with a clear "couldn't find DLL"
            //    error which we surface to the user.
            ::LoadLibraryW (L"onnxruntime.dll");
        });
    }
#else
    inline void ensureOnnxRuntimeLoaded() {}
#endif

    /// Read a .onnx file from disk into a byte buffer. demucs.onnx's
    /// `load_model()` overload takes `std::vector<char>`, which we feed.
    bool readFileBytes (const std::string& path, std::vector<char>& out, std::string& err)
    {
        std::ifstream f (path, std::ios::binary | std::ios::ate);
        if (! f.is_open()) { err = "cannot open " + path; return false; }
        const std::streamsize sz = f.tellg();
        if (sz <= 0) { err = "empty file " + path; return false; }
        f.seekg (0, std::ios::beg);
        out.resize ((size_t) sz);
        if (! f.read (out.data(), sz)) { err = "read error on " + path; return false; }
        return true;
    }

    /// Convert host-layout interleaved audio to a 2xN Eigen matrix at the
    /// model's required sample rate (44.1 kHz). Mono inputs are duplicated;
    /// multi-channel are downmixed L/R. Linear resampling is used because
    /// htdemucs is robust to sub-sample artifacts at 44.1 kHz.
    Eigen::MatrixXf toModelMatrix (const float* in,
                                   long long numFrames,
                                   int channels,
                                   int sampleRate)
    {
        const int targetRate = kModelSampleRate;
        const auto pickL = [](int) { return 0; };
        const auto pickR = [](int c) { return c == 1 ? 0 : 1; };

        if (sampleRate == targetRate)
        {
            Eigen::MatrixXf m (2, numFrames);
            for (long long i = 0; i < numFrames; ++i)
            {
                m (0, i) = in[i * channels + pickL (channels)];
                m (1, i) = in[i * channels + pickR (channels)];
            }
            return m;
        }

        const double ratio = (double) targetRate / (double) sampleRate;
        const long long outLen = (long long) std::ceil ((double) numFrames * ratio);
        Eigen::MatrixXf m (2, outLen);
        for (long long i = 0; i < outLen; ++i)
        {
            const double srcPos = (double) i / ratio;
            const long long i0  = (long long) std::floor (srcPos);
            const long long i1  = std::min (i0 + 1, numFrames - 1);
            const double frac   = srcPos - (double) i0;

            const float l0 = in[i0 * channels + pickL (channels)];
            const float l1 = in[i1 * channels + pickL (channels)];
            const float r0 = in[i0 * channels + pickR (channels)];
            const float r1 = in[i1 * channels + pickR (channels)];
            m (0, i) = (float) (l0 + (l1 - l0) * frac);
            m (1, i) = (float) (r0 + (r1 - r0) * frac);
        }
        return m;
    }

    /// Inverse of the above — take one stem (2xN at model rate) back to
    /// the host's interleaved buffer at the host's sample rate.
    std::vector<float> stemToInterleaved (const Eigen::Tensor<float, 3, Eigen::RowMajor>& tensor,
                                          int sourceIdx,
                                          long long targetFrames,
                                          int targetChannels,
                                          int targetSampleRate)
    {
        const int srcRate  = kModelSampleRate;
        const int srcChans = (int) tensor.dimension (1);
        const long long srcLen = (long long) tensor.dimension (2);

        std::vector<float> out ((size_t) (targetFrames * targetChannels), 0.f);

        if (srcRate == targetSampleRate && srcChans == targetChannels)
        {
            for (long long i = 0; i < targetFrames && i < srcLen; ++i)
                for (int c = 0; c < targetChannels; ++c)
                    out[(size_t) (i * targetChannels + c)] = tensor (sourceIdx, c, i);
            return out;
        }

        const double ratio = (double) targetSampleRate / (double) srcRate;
        for (long long i = 0; i < targetFrames; ++i)
        {
            const double srcPos = (double) i / ratio;
            const long long i0  = std::min ((long long) std::floor (srcPos), srcLen - 1);
            const long long i1  = std::min (i0 + 1, srcLen - 1);
            const double frac   = srcPos - (double) i0;

            for (int c = 0; c < targetChannels; ++c)
            {
                const int sCh   = std::min (c, srcChans - 1);
                const float v0  = tensor (sourceIdx, sCh, i0);
                const float v1  = tensor (sourceIdx, sCh, i1);
                out[(size_t) (i * targetChannels + c)] = (float) (v0 + (v1 - v0) * frac);
            }
        }
        return out;
    }

    /// Stem name lookup matching Demucs's training-time source order.
    const char* defaultStemName (int idx, int total)
    {
        static const char* k4[] = { "drums", "bass", "other", "vocals" };
        static const char* k6[] = { "drums", "bass", "other", "vocals", "guitar", "piano" };
        if (total == 6 && idx >= 0 && idx < 6) return k6[idx];
        if (idx >= 0 && idx < 4)               return k4[idx];
        return "stem";
    }
}

// ============================================================================
// Impl — owns the ORT session via demucs.onnx's wrapper struct.
//
// CRITICAL: every Ort::* type is held via std::unique_ptr so that the
// OnnxBackend constructor doesn't touch ORT at all. The host (e.g. FL
// Studio) calls our plugin's constructor while the VST3 DLL is still
// settling — if Ort::Env or Ort::SessionOptions threw at that point,
// the exception unwinds through DllMain / TLS callbacks and the host
// reports STATUS_FATAL_USER_CALLBACK_EXCEPTION (0xc000041d).
//
// All ORT objects are allocated only in loadModel() — i.e. lazily, once
// the user actually drops a file in.
// ============================================================================
struct OnnxBackend::Impl
{
    std::unique_ptr<demucsonnx::demucs_model> model;
    std::unique_ptr<Ort::SessionOptions>      session_options;
};

OnnxBackend::OnnxBackend()
    : impl (std::make_unique<Impl>())   // trivially constructible (null ptrs)
{
    // INTENTIONALLY NO ORT CALLS HERE. See the Impl comment above.
}

OnnxBackend::~OnnxBackend() = default;

bool OnnxBackend::isLoaded() const noexcept
{
    return impl && impl->model && impl->model->sess && impl->model->nb_sources > 0;
}

std::string OnnxBackend::activeBackendName() const
{
    switch (active)
    {
        case Backend::DirectML: return "GPU (DirectML)";
        case Backend::Cpu:      return "CPU";
        case Backend::Auto:     return "(auto)";
    }
    return "?";
}

bool OnnxBackend::loadModel (const std::string& modelFile,
                             int numSourcesIn,
                             Backend preferred)
{
    lastError.clear();
    sources = numSourcesIn;

    // FIRST: pre-resolve onnxruntime.dll from our plugin bundle directory
    // (idempotent thanks to call_once). Any host that didn't already load
    // ORT gets OUR version; if some other plugin already loaded a
    // different version, Windows will reuse that handle — but at least
    // the API surface is consistent for our session.
    ensureOnnxRuntimeLoaded();

    // Lazy-allocate the Ort wrappers ON THIS THREAD, ONLY when actually
    // requested. The constructor of OnnxBackend deliberately doesn't
    // construct these. Catch every exception so we never let one escape
    // a context that the host can't handle.
    try
    {
        if (! impl->model)
            impl->model = std::make_unique<demucsonnx::demucs_model>();
        impl->session_options = std::make_unique<Ort::SessionOptions>();
    }
    catch (const std::exception& e)
    {
        lastError = std::string ("ORT init failed (DLL load problem?): ") + e.what();
        return false;
    }

    // Read the .onnx bytes upfront — same path both EPs use.
    std::vector<char> modelBytes;
    if (! readFileBytes (modelFile, modelBytes, lastError))
        return false;

    auto& options = *impl->session_options;
    options.SetIntraOpNumThreads (0);    // 0 = ORT defaults
    options.SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);

    auto tryDirectML = [&]() -> bool
    {
        // The DML provider factory throws on failure (e.g. no D3D12 device);
        // we swallow the exception and fall back to CPU.
        try
        {
            // Adapter index 0 = system default GPU. Most users have one.
            OrtSessionOptionsAppendExecutionProvider_DML (options, 0);
            return true;
        }
        catch (const Ort::Exception& e)
        {
            lastError = std::string ("DirectML init failed: ") + e.what();
            return false;
        }
        catch (const std::exception& e)
        {
            lastError = std::string ("DirectML init failed: ") + e.what();
            return false;
        }
    };

    bool dmlOK = false;
    if (preferred == Backend::DirectML || preferred == Backend::Auto)
        dmlOK = tryDirectML();

    if (dmlOK)
    {
        active = Backend::DirectML;
        // DML EP works best with single-threaded ORT scheduling — let the
        // GPU saturate via its own command queue.
        options.SetExecutionMode (ORT_SEQUENTIAL);
        options.DisableMemPattern();
    }
    else
    {
        // Fresh options for the CPU path — clean slate so DML residue
        // (sequential mode etc.) doesn't carry over.
        impl->session_options = std::make_unique<Ort::SessionOptions>();
        impl->session_options->SetGraphOptimizationLevel (GraphOptimizationLevel::ORT_ENABLE_ALL);
        active = Backend::Cpu;
        lastError.clear();   // not actually an error if user wanted Auto
    }

    if (! demucsonnx::load_model (modelBytes, *impl->model, *impl->session_options))
    {
        lastError = "demucs.onnx::load_model() rejected the .onnx file "
                    "(corrupt, wrong opset, or shape mismatch).";
        return false;
    }

    // Capture nb_sources from the caller; demucs.onnx doesn't infer it
    // from the model graph reliably (output shape varies by variant).
    impl->model->nb_sources = sources;
    return true;
}

SplitResult OnnxBackend::split (const float* audio,
                                long long numFrames,
                                int numChannels,
                                int sampleRate,
                                ProgressCallback onProgress,
                                const std::atomic<bool>& cancel)
{
    SplitResult result;
    if (! isLoaded())        { result.errorMessage = "No model loaded.";   return result; }
    if (audio == nullptr || numFrames == 0 || numChannels < 1)
        { result.errorMessage = "Empty input audio.";  return result; }

    const auto t0 = std::chrono::steady_clock::now();

    if (onProgress) onProgress ({ 0.02f, "preparing", 0, 0 });

    auto inputMatrix = toModelMatrix (audio, numFrames, numChannels, sampleRate);
    if (cancel.load()) { result.errorMessage = "Cancelled."; return result; }

    if (onProgress) onProgress ({ 0.05f, "processing", 0, 0 });

    // Bridge demucs.onnx's (float, string) progress callback to our richer
    // ProgressReport struct, plus cancel-via-exception for early exit.
    auto bridge = [&onProgress, &cancel] (float frac, const std::string& msg)
    {
        if (cancel.load())
            throw std::runtime_error ("__stemmerizer_cancel__");
        if (onProgress)
        {
            const float clamped = std::max (0.f, std::min (1.f, frac));
            onProgress ({ 0.05f + 0.90f * clamped, msg, 0, 0 });
        }
    };

    // demucs.onnx returns Tensor3dXf which is RowMajor — match that.
    Eigen::Tensor<float, 3, Eigen::RowMajor> outTensor;
    try
    {
        outTensor = demucsonnx::demucs_inference (*impl->model, inputMatrix, bridge);
    }
    catch (const std::runtime_error& e)
    {
        if (std::string (e.what()) == "__stemmerizer_cancel__")
        {
            result.errorMessage = "Cancelled.";
            return result;
        }
        result.errorMessage = std::string ("Inference failed: ") + e.what();
        return result;
    }
    catch (const Ort::Exception& e)
    {
        result.errorMessage = std::string ("ORT exception: ") + e.what();
        return result;
    }
    catch (const std::exception& e)
    {
        result.errorMessage = std::string ("Inference failed: ") + e.what();
        return result;
    }

    if (onProgress) onProgress ({ 0.95f, "writing", 0, 0 });

    const int N = (int) outTensor.dimension (0);
    if (N != sources)
    {
        result.errorMessage = "Model returned " + std::to_string (N) +
                              " stems, expected " + std::to_string (sources);
        return result;
    }

    const int outChannels = numChannels >= 2 ? 2 : 1;
    result.stems.resize ((size_t) N);
    for (int s = 0; s < N; ++s)
    {
        StemBuffer& sb = result.stems[(size_t) s];
        sb.name        = defaultStemName (s, sources);
        sb.numChannels = outChannels;
        sb.sampleRate  = sampleRate;
        sb.interleaved = stemToInterleaved (outTensor, s, numFrames, outChannels, sampleRate);
    }

    const auto t1 = std::chrono::steady_clock::now();
    result.processingSeconds = std::chrono::duration<double> (t1 - t0).count();
    result.success           = true;

    if (onProgress) onProgress ({ 1.f, "done", 0, 0 });
    return result;
}

} // namespace stemmerizer::dsp
