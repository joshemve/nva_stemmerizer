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
    /// Once-only, thread-safe explicit init of ORT.
    ///
    /// With ORT_API_MANUAL_INIT defined, the C++ header skips its own
    /// static initializer that would otherwise call OrtGetApiBase() at
    /// DLL-load time and (in FL Studio's process, where some other plugin
    /// has often already loaded a different onnxruntime.dll under the
    /// same basename) get back the wrong-version API table — leaving
    /// Ort::Global::api_ NULL and crashing the first Ort::Env ctor with
    /// `Read of 0x18` (offset of CreateEnv in OrtApi).
    ///
    /// Instead we:
    ///   1. Load OUR ORT shared library by its UNIQUE filename
    ///      (stemonnx.dll) and absolute path — no possible collision
    ///      with anyone else's onnxruntime.dll.
    ///   2. GetProcAddress("OrtGetApiBase") on the returned handle.
    ///   3. Call it -> OrtApi* matching OUR runtime's actual ABI.
    ///   4. Ort::InitApi(api) — every subsequent ORT C++ wrapper call
    ///      goes through this api_, regardless of what else is in memory.
    ///
    /// Returns true on success. Stash an error string if not.
    bool ensureOnnxRuntimeLoaded (std::string& errorOut)
    {
        static std::once_flag once;
        static bool   succeeded { false };
        static std::string cachedError;

        std::call_once (once, [&]()
        {
            // 1. Find OUR module path.
            HMODULE selfModule = nullptr;
            if (! ::GetModuleHandleExW (
                    GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(&ensureOnnxRuntimeLoaded),
                    &selfModule))
            {
                cachedError = "Could not resolve own module handle (GetModuleHandleExW failed).";
                return;
            }

            wchar_t selfPathW [MAX_PATH] = {};
            ::GetModuleFileNameW (selfModule, selfPathW, MAX_PATH);
            if (selfPathW[0] == 0)
            {
                cachedError = "Could not resolve own module path (GetModuleFileNameW failed).";
                return;
            }

            std::filesystem::path selfDir = std::filesystem::path (selfPathW).parent_path();

            // Allow developer override.
            wchar_t envBuf [MAX_PATH] = {};
            if (::GetEnvironmentVariableW (L"STEMMERIZER_ORT_DIR", envBuf, MAX_PATH) > 0)
                selfDir = envBuf;

            // 2. Load our unique-named DLL by absolute path. Because the
            //    name is unique to us, Windows MUST physically load this
            //    file from disk — it cannot satisfy the request with some
            //    other already-loaded "onnxruntime.dll".
            const auto fullPath = (selfDir / L"stemonnx.dll").wstring();
            HMODULE h = ::LoadLibraryExW (fullPath.c_str(), nullptr,
                                          LOAD_WITH_ALTERED_SEARCH_PATH);
            if (h == nullptr)
            {
                const auto err = ::GetLastError();
                cachedError = "LoadLibraryExW failed (code "
                              + std::to_string (err)
                              + ") for: " + std::filesystem::path (fullPath).string();
                return;
            }

            // 3. Resolve OrtGetApiBase from the just-loaded module.
            using GetApiBaseFn = const OrtApiBase* (ORT_API_CALL*)();
            auto getApiBase = reinterpret_cast<GetApiBaseFn> (
                ::GetProcAddress (h, "OrtGetApiBase"));
            if (getApiBase == nullptr)
            {
                cachedError = "GetProcAddress('OrtGetApiBase') failed on stemonnx.dll.";
                return;
            }

            const auto* apiBase = getApiBase();
            if (apiBase == nullptr)
            {
                cachedError = "OrtGetApiBase() returned null.";
                return;
            }

            // 4. Ask for OUR header's API version. If the loaded DLL is
            //    too old, this returns null — but with stemonnx.dll we
            //    control the version so this should always succeed.
            const auto* api = apiBase->GetApi (ORT_API_VERSION);
            if (api == nullptr)
            {
                cachedError = "ORT loaded but does not implement API version "
                              + std::to_string (ORT_API_VERSION) +
                              " (this should never happen with a matched stemonnx.dll).";
                return;
            }

            Ort::InitApi (api);
            succeeded = true;
        });

        if (! succeeded) errorOut = cachedError;
        return succeeded;
    }
#else
    inline bool ensureOnnxRuntimeLoaded (std::string&) { return true; }
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
    // Outer try/catch covers EVERY ORT call inside loadModel — including
    // those reached via demucs.onnx's load_model() which can throw from
    // Ort::Session's ctor on bad opset / shape mismatch / oom. If we let
    // any exception escape, the call chain ends up unwinding through a
    // window-message callback that's effectively `noexcept`, triggering
    // std::terminate -> abort -> FL crash (0xc0000409 / P9=7).
    try
    {
    lastError.clear();
    sources = numSourcesIn;

    // FIRST: bring up ORT with our unique-named DLL. This is the ONLY
    // path that triggers any ORT C++ wrapper machinery. If it fails the
    // user gets a clear error message instead of a crash.
    {
        std::string ortErr;
        if (! ensureOnnxRuntimeLoaded (ortErr))
        {
            lastError = "ONNX Runtime init failed: " + ortErr;
            return false;
        }
    }

    // Lazy-allocate the Ort wrappers. With Ort::InitApi already done above,
    // Ort::Env / Ort::SessionOptions construction calls a known-good api_.
    try
    {
        if (! impl->model)
            impl->model = std::make_unique<demucsonnx::demucs_model>();
        impl->session_options = std::make_unique<Ort::SessionOptions>();
    }
    catch (const std::exception& e)
    {
        lastError = std::string ("ORT init failed (after DLL load): ") + e.what();
        return false;
    }

    // We DON'T read the .onnx into a byte buffer anymore. Demucs v4 ships
    // as a small graph file (htdemucs.onnx) + a separate external-data
    // file (htdemucs.onnx.data, ~168 MB of weights). ORT's bytes-based
    // Session ctor resolves external data relative to the process's
    // CWD — which inside FL is FL's install dir, NOT our model folder,
    // so ORT fails with "file_size: cannot find the file specified:
    // htdemucs.onnx.data". Loading by FILE PATH below makes ORT resolve
    // external data relative to the .onnx file's directory. Same model,
    // correct lookup.
    if (! std::filesystem::exists (modelFile))
    {
        lastError = "Model file not found on disk: " + modelFile;
        return false;
    }

    const std::wstring modelPathW = std::filesystem::path (modelFile).wstring();

    if (! std::filesystem::exists (modelFile))
    {
        lastError = "Model file not found on disk: " + modelFile;
        return false;
    }

    // ---- Session-attempt helper -----------------------------------------
    // Builds fresh model + session_options for the requested backend +
    // optimization profile, then creates the Ort::Session via demucs.onnx's
    // path-based loader. On any Ort::Exception we stash the message and
    // return false so the caller can try the next strategy.
    auto trySession = [&] (Backend backend,
                           GraphOptimizationLevel optLevel,
                           bool disableMemPattern,
                           bool sequentialExec,
                           std::string& err) -> bool
    {
        try
        {
            impl->model           = std::make_unique<demucsonnx::demucs_model>();
            impl->session_options = std::make_unique<Ort::SessionOptions>();
            auto& opt = *impl->session_options;

            opt.SetIntraOpNumThreads (0);
            opt.SetGraphOptimizationLevel (optLevel);
            if (sequentialExec)     opt.SetExecutionMode (ORT_SEQUENTIAL);
            if (disableMemPattern)  opt.DisableMemPattern();

            if (backend == Backend::DirectML)
            {
                const OrtDmlApi* dml = nullptr;
                Ort::ThrowOnError (Ort::GetApi().GetExecutionProviderApi (
                    "DML", ORT_API_VERSION,
                    reinterpret_cast<const void**> (&dml)));
                if (dml == nullptr) { err = "DirectML provider not available."; return false; }
                Ort::ThrowOnError (dml->SessionOptionsAppendExecutionProvider_DML (
                    static_cast<OrtSessionOptions*> (opt), 0));
            }
            // (CPU EP is the default; no registration needed.)

            const bool ok = demucsonnx::load_model_from_path (modelPathW.c_str(),
                                                              *impl->model, opt);
            if (! ok)
                err = "demucs.onnx::load_model rejected the .onnx graph.";
            return ok;
        }
        catch (const Ort::Exception& e) { err = e.what(); return false; }
        catch (const std::exception& e) { err = e.what(); return false; }
    };

    // ---- Backend strategy ladder ----------------------------------------
    // Demucs v4 (Hybrid Transformer) has known op-coverage friction with
    // DirectML. The fix is NOT to disable GPU — it's to disable graph
    // optimization fusions that DML can't handle. With ORT_DISABLE_ALL,
    // ORT hands ops to DML as-emitted by the exporter (without fusing
    // them into composite ops DML rejects). DML then accepts every op
    // because each individual op IS in its kernel registry.
    //
    // Ladder (each rung is a complete session-creation attempt):
    //   1. DML + DISABLE_ALL  -> most permissive, usually works for HT-Demucs
    //   2. DML + BASIC        -> minimal fusion, sometimes needed
    //   3. DML + EXTENDED     -> sometimes the BASIC fusions are the problem
    //   4. CPU + ENABLE_ALL   -> guaranteed fallback (ORT CPU EP supports all ops)
    //
    // First rung to load successfully wins. We stash diagnostics from
    // every failed rung in case the user reports back.
    struct Attempt {
        Backend backend;
        GraphOptimizationLevel opt;
        const char* label;
    };
    const Attempt ladder[] = {
        { Backend::DirectML, ORT_DISABLE_ALL,    "DirectML (no graph opts)"   },
        { Backend::DirectML, ORT_ENABLE_BASIC,   "DirectML (basic opts)"      },
        { Backend::DirectML, ORT_ENABLE_EXTENDED,"DirectML (extended opts)"   },
        { Backend::Cpu,      ORT_ENABLE_ALL,     "CPU (full opts, fallback)"  },
    };

    bool loadOk = false;
    std::string accumulatedErrors;

    for (const auto& a : ladder)
    {
        // Skip DML rungs if caller forced CPU.
        if (preferred == Backend::Cpu && a.backend == Backend::DirectML) continue;

        // Skip CPU rung if caller forced DirectML.
        if (preferred == Backend::DirectML && a.backend == Backend::Cpu) continue;

        std::string err;
        const bool sequential       = (a.backend == Backend::DirectML);
        const bool disableMemPattern = (a.backend == Backend::DirectML);

        if (trySession (a.backend, a.opt, disableMemPattern, sequential, err))
        {
            active = a.backend;
            loadOk = true;
            // Stash a note about which rung worked, so callers / logs can see.
            if (! accumulatedErrors.empty())
                lastError = std::string ("Using ") + a.label
                          + " after previous rungs failed. Earlier errors:\n"
                          + accumulatedErrors;
            else
                lastError.clear();
            break;
        }
        accumulatedErrors += std::string ("  - ") + a.label + ": " + err + "\n";
    }

    if (! loadOk)
    {
        // lastError already contains the accumulated error report from
        // every rung that failed (see the loop above).
        lastError = "Every backend rung failed.\n" + accumulatedErrors;
        return false;
    }

    // Capture nb_sources from the caller; demucs.onnx doesn't infer it
    // from the model graph reliably (output shape varies by variant).
    impl->model->nb_sources = sources;
    return true;
    }  // end outer try
    catch (const Ort::Exception& e)
    {
        lastError = std::string ("ORT exception in loadModel: ") + e.what();
        return false;
    }
    catch (const std::exception& e)
    {
        lastError = std::string ("std::exception in loadModel: ") + e.what();
        return false;
    }
    catch (...)
    {
        lastError = "Unknown exception in loadModel.";
        return false;
    }
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
