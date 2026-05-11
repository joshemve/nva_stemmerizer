# Stemmerizer

AI stem-splitting plugin (VST3 + standalone) — vocals, drums, bass, other,
plus optional guitar and piano via the 6-stem model. Built in C++/JUCE 8 with
the Demucs v4 architecture (via demucs.cpp) doing the inference, on-CPU,
fully offline.

> **State.** v0.1 working set. Compiles a license-clean VST3 + standalone
> from a clean checkout on Windows. Has not yet been tested in a real DAW;
> see "Known limitations of this build" below.

## License model

- **Source code in this repo:** proprietary, NVA Audio (see `LICENSE`).
- **Compiled plugin:** sold to end users under `legal/EULA.md`.
- **Third-party deps:** every one license-cleared for commercial sale —
  see `legal/LICENSE-AUDIT.md` for the per-component breakdown and
  `legal/NOTICE.md` for the user-facing third-party notices that ship in
  the installer.

## Project layout

```
nva_stemmerizer/
├── CMakeLists.txt            ← top-level build
├── LICENSE                   ← proprietary license for the source tree
├── README.md                 ← this file
├── README-BUILD.md           ← step-by-step build & sign instructions
├── src/                      ← C++ source
│   ├── PluginProcessor.{h,cpp}
│   ├── PluginEditor.{h,cpp}
│   ├── dsp/                  ← demucs.cpp wrapper, file I/O, job queue
│   └── ui/                   ← Theme, DropZone, StemMixer, JobList, …
├── resources/
│   ├── fonts/                ← Inter, JetBrains Mono (OFL 1.1)
│   ├── images/               ← logo, installer icon
│   └── weights/              ← ggml model files (downloaded, NOT committed)
├── third_party/              ← demucs.cpp (git submodule or fetched by CMake)
├── scripts/
│   └── fetch_weights.py      ← downloads + converts Demucs v4 → ggml
├── installer/
│   └── Stemmerizer.iss       ← Inno Setup installer script
├── legal/
│   ├── EULA.md               ← end-user license agreement (DRAFT)
│   ├── PRIVACY.md            ← privacy policy (DRAFT)
│   ├── NOTICE.md             ← third-party notices for the installer
│   └── LICENSE-AUDIT.md      ← internal license-compatibility audit
└── docs/                     ← design notes, future architecture
```

## Quick build (Windows)

Prereqs: Visual Studio 2022/2026 with the Desktop C++ workload, CMake 3.22+,
Python 3.10+, Git.

```powershell
# 1. fetch & convert the Demucs weights (one-time, ~10 minutes)
python scripts/fetch_weights.py --all

# 2. configure
cmake -S . -B build -G "Visual Studio 17 2022" -A x64

# 3. build (Release)
cmake --build build --config Release --parallel

# 4. (optional) build the installer
cd installer
"C:\Program Files (x86)\Inno Setup 6\ISCC.exe" Stemmerizer.iss
```

The VST3 will be installed in `%CommonProgramFiles%\VST3\Stemmerizer.vst3`
by the JUCE post-build step (`COPY_PLUGIN_AFTER_BUILD TRUE`).

For full build & code-sign instructions, see `README-BUILD.md`.

## Known limitations of this build

These are the things that are still on you (the human) to finish before
shipping:

1. **Run-test in a real DAW.** Reaper / FL / Cubase / Live. Verify the
   plugin loads, the editor renders correctly at common DPIs (100/125/150/
   200%), and that the offline-process flow (drag file in → wait → stems
   on disk) actually completes.
2. **Code-sign the installer and the plug-in DLL** with an EV cert from
   SSL.com / Sectigo / DigiCert. Without it Windows SmartScreen will
   flag it on every download. Pass the cert file path to Inno Setup via
   the `SIGNTOOL` env var (see `README-BUILD.md`).
3. **Replace placeholders in `legal/EULA.md` and `legal/PRIVACY.md`**
   (entity name, jurisdiction, support email, payment processor, license-
   server domain). Then have a lawyer review.
4. **Register as a Steinberg developer** at
   https://www.steinberg.net/developers/ to legitimize VST3 distribution.
5. **Register a JUCE 8 Starter license** at https://juce.com/get-juce/.
6. **Drop final font files** into `resources/fonts/`. The CMake target
   currently lists Inter + JetBrains Mono filenames; if they're absent the
   build will fail on the binary-data step. Source: https://rsms.me/inter/
   and https://www.jetbrains.com/lp/mono/.
7. **Drop a final logo / installer icon** into `resources/images/`
   (`logo.svg` and `stemmerizer.ico`).

## Roadmap

- v1.0 — what's in this build: 4/6-stem split, drag/drop, batch queue,
  WAV/FLAC export, dark UI, VST3 + standalone.
- v1.1 — per-stem preview waveforms; per-stem level/mute/solo before
  export; A/B compare against the input mix.
- v1.2 — MP3 export (LGPL LAME shipped as a separate optional installer
  component to keep the core binary LGPL-clean).
- v1.3 — macOS (AU + VST3) build via the same CMake target.
- v2.0 — real-time stream-process mode using a smaller, faster model
  trained for low-latency separation (research project).
