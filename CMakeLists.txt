cmake_minimum_required(VERSION 3.22)

# demucs.cpp ships a CMakeLists with cmake_minimum_required pinned below CMake
# 3.5 (which modern CMake rejects). Setting this policy variable BEFORE we
# pull demucs.cpp in via FetchContent makes its old CMakeLists configure
# under modern policies. Has no effect on JUCE (its CMake is current).
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)

project(Stemmerizer
    VERSION 1.0.0
    DESCRIPTION "AI stem-splitting plugin (vocals / drums / bass / other / +guitar / +piano)"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---- Branding ------------------------------------------------------------
set(STEMMERIZER_COMPANY        "NVA Audio")
set(STEMMERIZER_COMPANY_DOMAIN "com.nvaaudio")
set(STEMMERIZER_PRODUCT_NAME   "Stemmerizer")
set(STEMMERIZER_PLUGIN_CODE    "Stmz")
set(STEMMERIZER_MFR_CODE       "Nvaa")
set(STEMMERIZER_BUNDLE_ID      "${STEMMERIZER_COMPANY_DOMAIN}.${STEMMERIZER_PRODUCT_NAME}")

# ---- Build options -------------------------------------------------------
option(STEMMERIZER_BUILD_STANDALONE "Build a standalone Windows .exe alongside the plugin" ON)
option(STEMMERIZER_BUILD_VST3       "Build VST3 plugin"                                    ON)
option(STEMMERIZER_BUILD_AU         "Build Audio Unit plugin (macOS only)"                 OFF)
option(STEMMERIZER_ENABLE_OPENMP    "Enable OpenMP for demucs.cpp inference"               ON)

# ---- Dependencies --------------------------------------------------------
include(FetchContent)

# JUCE 8
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(juce)

# ---- Inference backend: ONNX Runtime + DirectML --------------------------
# We use ONNX Runtime with the DirectML execution provider for GPU
# acceleration. DirectML covers every Windows GPU vendor (NVIDIA / AMD /
# Intel iGPU / ARM) via Direct3D 12 — no per-vendor build matrix, no CUDA
# install required by end users. The CPU EP is the automatic fallback on
# machines without a usable GPU.
#
# Prebuilt binaries live at third_party/onnxruntime/ort-pkg/ (downloaded
# once from the official Microsoft.ML.OnnxRuntime.DirectML NuGet package).
set(ORT_ROOT      "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/ort-pkg")
set(ORT_INCLUDE   "${ORT_ROOT}/build/native/include")
set(ORT_LIB       "${ORT_ROOT}/runtimes/win-x64/native/onnxruntime.lib")
set(ORT_DLL       "${ORT_ROOT}/runtimes/win-x64/native/onnxruntime.dll")

if(NOT EXISTS "${ORT_LIB}")
    message(FATAL_ERROR
        "ONNX Runtime DirectML not found at ${ORT_LIB}. "
        "Run: scripts/fetch_onnxruntime.ps1 (or see README-BUILD.md).")
endif()

# Stage onnxruntime.dll into the build's bin dir AT CONFIGURE TIME so
# juce_vst3_helper.exe (which runs from there during the VST3 POST_BUILD
# step) can resolve it. Without this, the helper picks up a STALE
# onnxruntime.dll from somewhere else on the system (Python, VS, Edge…),
# detects the API-version mismatch ("requested API version [20], built
# with [17]"), and crashes the build.
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Release")
file(COPY "${ORT_DLL}" DESTINATION "${CMAKE_BINARY_DIR}/Release")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/Debug")
file(COPY "${ORT_DLL}" DESTINATION "${CMAKE_BINARY_DIR}/Debug")

add_library(onnxruntime SHARED IMPORTED GLOBAL)
set_target_properties(onnxruntime PROPERTIES
    IMPORTED_LOCATION "${ORT_DLL}"
    IMPORTED_IMPLIB   "${ORT_LIB}"
    INTERFACE_INCLUDE_DIRECTORIES "${ORT_INCLUDE}")

# ---- demucs.onnx vendor: STFT + segmentation helpers (no neural code) ---
# We vendor a small slice of sevagh/demucs.onnx because its STFT pipeline
# and segmentation/overlap-add logic exactly match the htdemucs reference
# implementation. We do NOT use its build system (the upstream CMakeLists
# is Linux-only and embeds ONNX models as .ort.c blobs). We compile the
# bits we need ourselves and link them into Stemmerizer.
FetchContent_Declare(
    demucs_onnx
    GIT_REPOSITORY    https://github.com/sevagh/demucs.onnx.git
    GIT_TAG           main
    GIT_SHALLOW       TRUE
    GIT_SUBMODULES    "vendor/eigen"
    GIT_SUBMODULES_RECURSE FALSE)
FetchContent_GetProperties(demucs_onnx)
if(NOT demucs_onnx_POPULATED)
    FetchContent_Populate(demucs_onnx)
endif()
set(DEMUCS_ONNX_SRC_DIR   "${demucs_onnx_SOURCE_DIR}/src")
set(DEMUCS_ONNX_EIGEN_DIR "${demucs_onnx_SOURCE_DIR}/vendor/eigen")

# Compile only the helper TUs (dsp.cpp = STFT/ISTFT; model_apply.cpp =
# segmentation+overlap-add). model_inference.cpp wraps ORT — we provide
# our own equivalent because we need DirectML EP registration which is
# not in the upstream.
add_library(demucs.onnx.helpers STATIC
    "${DEMUCS_ONNX_SRC_DIR}/dsp.cpp"
    "${DEMUCS_ONNX_SRC_DIR}/model_apply.cpp"
    "${DEMUCS_ONNX_SRC_DIR}/model_inference.cpp")
target_include_directories(demucs.onnx.helpers PUBLIC
    "${DEMUCS_ONNX_SRC_DIR}"
    "${DEMUCS_ONNX_EIGEN_DIR}"
    "${ORT_INCLUDE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/onnxruntime/shim"   # long-path compat
)
target_link_libraries(demucs.onnx.helpers PUBLIC onnxruntime)
set_target_properties(demucs.onnx.helpers PROPERTIES
    CXX_STANDARD          20
    POSITION_INDEPENDENT_CODE ON
    UNITY_BUILD           OFF)
if(MSVC)
    target_compile_options(demucs.onnx.helpers PRIVATE
        /W0 /bigobj /EHsc /Zc:__cplusplus /utf-8
        $<$<CONFIG:Release>:/O2 /Oi /fp:fast /DNDEBUG>)
    target_compile_definitions(demucs.onnx.helpers PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _USE_MATH_DEFINES
        NOMINMAX)
endif()

# ---- The plugin target ---------------------------------------------------
juce_add_plugin(Stemmerizer
    PRODUCT_NAME                  "${STEMMERIZER_PRODUCT_NAME}"
    COMPANY_NAME                  "${STEMMERIZER_COMPANY}"
    BUNDLE_ID                     "${STEMMERIZER_BUNDLE_ID}"
    PLUGIN_MANUFACTURER_CODE      "${STEMMERIZER_MFR_CODE}"
    PLUGIN_CODE                   "${STEMMERIZER_PLUGIN_CODE}"
    FORMATS                       VST3 Standalone
    VST3_CATEGORIES               "Fx" "Tools"
    IS_SYNTH                      FALSE
    NEEDS_MIDI_INPUT              FALSE
    NEEDS_MIDI_OUTPUT             FALSE
    IS_MIDI_EFFECT                FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS   TRUE
    COPY_PLUGIN_AFTER_BUILD       TRUE
    DESCRIPTION                   "${PROJECT_DESCRIPTION}"
)

# ---- Source files --------------------------------------------------------
target_sources(Stemmerizer PRIVATE
    src/PluginProcessor.cpp
    src/PluginProcessor.h
    src/PluginEditor.cpp
    src/PluginEditor.h

    # ---- DSP core ----
    src/dsp/StemSplitter.cpp
    src/dsp/StemSplitter.h
    src/dsp/OnnxBackend.cpp          # NEW: ONNX Runtime + DirectML inference
    src/dsp/OnnxBackend.h
    src/dsp/AudioFileIO.cpp
    src/dsp/AudioFileIO.h
    src/dsp/JobQueue.cpp
    src/dsp/JobQueue.h
    src/dsp/StemMixState.cpp
    src/dsp/StemMixState.h
    src/dsp/StemSession.cpp
    src/dsp/StemSession.h
    src/dsp/Transport.cpp
    src/dsp/Transport.h
    src/dsp/MixRenderer.cpp
    src/dsp/MixRenderer.h
    src/dsp/DragExporter.cpp
    src/dsp/DragExporter.h
    src/dsp/MusicAnalysis.cpp
    src/dsp/MusicAnalysis.h

    # ---- UI ----
    src/ui/Theme.h
    src/ui/Theme.cpp
    src/ui/IconButton.h
    src/ui/IconButton.cpp
    src/ui/DropZone.h
    src/ui/DropZone.cpp
    src/ui/JobList.h
    src/ui/JobList.cpp
    src/ui/ProgressBar.h
    src/ui/ProgressBar.cpp
    src/ui/TransportBar.h
    src/ui/TransportBar.cpp
    src/ui/LoopRegionView.h
    src/ui/LoopRegionView.cpp
    src/ui/WaveformStrip.h
    src/ui/WaveformStrip.cpp
    src/ui/StemRow.h
    src/ui/StemRow.cpp
    src/ui/StemMixerPanel.h
    src/ui/StemMixerPanel.cpp
)

target_include_directories(Stemmerizer PRIVATE
    src
    third_party
)

# ---- Embedded resources (icons, fonts, default UI assets) ---------------
# Optional: any .ttf in resources/fonts/ + the logo, if present. Missing
# fonts are non-fatal — the UI falls back to system fonts (see Theme.cpp).
file(GLOB STEMMERIZER_FONT_FILES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/*.ttf"
     "${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/*.otf")
set(STEMMERIZER_BINARY_SOURCES ${STEMMERIZER_FONT_FILES})
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/images/logo.svg")
    list(APPEND STEMMERIZER_BINARY_SOURCES
         "${CMAKE_CURRENT_SOURCE_DIR}/resources/images/logo.svg")
endif()

if(STEMMERIZER_BINARY_SOURCES)
    juce_add_binary_data(StemmerizerData SOURCES ${STEMMERIZER_BINARY_SOURCES})
    set(STEMMERIZER_HAS_BINARY_DATA TRUE)
else()
    message(STATUS "Stemmerizer: no embedded resources found (this is OK; UI will use system fonts).")
    set(STEMMERIZER_HAS_BINARY_DATA FALSE)
endif()

# ---- Compile defines -----------------------------------------------------
target_compile_definitions(Stemmerizer PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0       # Starter tier: removed in JUCE 8
    JUCE_REPORT_APP_USAGE=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    STEMMERIZER_VERSION_STRING="${PROJECT_VERSION}"
    # ONNX Runtime + DirectML headers transitively pull in <windows.h>, which
    # defines min/max as macros and breaks any std::min / std::max in our
    # codebase. NOMINMAX neutralizes that.
    NOMINMAX
    _USE_MATH_DEFINES
)

# ---- Link libraries ------------------------------------------------------
target_link_libraries(Stemmerizer
    PRIVATE
        demucs.onnx.helpers  # STFT / segmentation helpers
        onnxruntime          # imported shared lib (DirectML EP inside)
                             # — delay-loaded; see OnnxBackend.cpp for the
                             # explicit LoadLibraryEx that locates the DLL
                             # next to the VST3 bundle / standalone exe.
                             # (also brings vendored Eigen via PUBLIC includes)
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_audio_formats
        juce::juce_dsp
        juce::juce_gui_basics
        juce::juce_gui_extra
        juce::juce_graphics
        juce::juce_events
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

if(STEMMERIZER_HAS_BINARY_DATA)
    target_link_libraries(Stemmerizer PRIVATE StemmerizerData)
    target_compile_definitions(Stemmerizer PRIVATE STEMMERIZER_HAS_BINARY_DATA=1)
endif()

if(STEMMERIZER_ENABLE_OPENMP AND OpenMP_CXX_FOUND)
    target_link_libraries(Stemmerizer PRIVATE OpenMP::OpenMP_CXX)
    target_compile_definitions(Stemmerizer PRIVATE STEMMERIZER_HAVE_OPENMP=1)
endif()

# ---- ONNX Runtime DLL deployment ----------------------------------------
# The plugin DLL/exe needs onnxruntime.dll at runtime. We copy it next to
# every built artefact. DirectML.dll is a Windows system component
# (10 1709+ / all 11) so we don't ship it.
#
# IMPORTANT: copy as PRE_LINK (not POST_BUILD). Reason: JUCE's
# COPY_PLUGIN_AFTER_BUILD chains juce_vst3_helper.exe in the POST_BUILD
# step — that helper LOADS our VST3 to extract its module info, which in
# turn constructs StemmerizerProcessor -> StemSplitter -> OnnxBackend,
# which needs onnxruntime.dll on disk. POST_BUILD ordering put our copy
# AFTER the helper ran, so it crashed (0xC0000005). PRE_LINK guarantees
# the DLL is in place before the helper runs.
foreach(target_kind Standalone VST3)
    set(stem "${CMAKE_CURRENT_BINARY_DIR}/Stemmerizer_artefacts/$<CONFIG>/${target_kind}")
    if(target_kind STREQUAL "VST3")
        set(dst "${stem}/Stemmerizer.vst3/Contents/x86_64-win")
    else()
        set(dst "${stem}")
    endif()
    add_custom_command(TARGET Stemmerizer_${target_kind} PRE_LINK
        COMMAND ${CMAKE_COMMAND} -E make_directory "${dst}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ORT_DLL}" "${dst}/"
        VERBATIM)
endforeach()

# ---- MSVC tweaks ---------------------------------------------------------
if(MSVC)
    target_compile_options(Stemmerizer PRIVATE
        /W4
        /MP                 # multi-process compile
        /Zc:__cplusplus
        /permissive-
        /utf-8
    )
    # Eigen + Demucs benefit from these in release
    target_compile_options(Stemmerizer PRIVATE
        $<$<CONFIG:Release>:/O2 /Oi /Ob2 /GL /fp:fast>
    )
    target_link_options(Stemmerizer PRIVATE
        $<$<CONFIG:Release>:/LTCG>
        # Delay-load onnxruntime.dll. Without this, FL Studio / juce_vst3_helper
        # try to resolve the import at module-load time, fail to find the DLL
        # in their own directory, and report "This plugin failed to load."
        # With delay-loading, the OS only tries to resolve onnxruntime.dll on
        # the first call into it — by which time OnnxBackend has explicitly
        # LoadLibraryEx'd it from the VST3 bundle directory.
        /DELAYLOAD:onnxruntime.dll
    )
    # Provides __delayLoadHelper2 referenced by /DELAYLOAD.
    target_link_libraries(Stemmerizer PRIVATE delayimp.lib)
endif()
