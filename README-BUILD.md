# Build & Sign — Stemmerizer

End-to-end instructions for going from a clean source checkout to a signed,
shippable Windows installer.

## 0. Prerequisites

| Tool                         | Version           | Get it                                          |
|------------------------------|-------------------|--------------------------------------------------|
| Visual Studio (Desktop C++)  | 2022 or 2026      | https://visualstudio.microsoft.com/              |
| CMake                        | ≥ 3.22            | https://cmake.org/download/                      |
| Python                       | 3.10 or newer     | https://www.python.org/downloads/                |
| Git                          | recent            | https://git-scm.com/download/win                 |
| Inno Setup                   | 6.2 or newer      | https://jrsoftware.org/isinfo.php                |
| EV code-signing certificate  | active            | SSL.com, Sectigo, DigiCert                       |

You also need accepted developer accounts at:

- **JUCE** — https://juce.com/get-juce/ → Starter tier (free, ≤ $20k revenue/yr).
- **Steinberg** — https://www.steinberg.net/developers/ → accept the VST 3 SDK terms.

Both are free, both require accepting a click-through agreement once.

## 1. Get the weights

```powershell
python scripts\fetch_weights.py --all
```

This downloads ~1.2 GB of PyTorch checkpoints from Meta's CDN and converts
them into ~270 MB of ggml files in `resources\weights\`. First run takes
~10 minutes; the conversion is deterministic, so subsequent runs are cached.

## 2. Configure & build

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_BUILD_TYPE=Release

cmake --build build --config Release --parallel
```

CMake will fetch JUCE 8 and demucs.cpp on first run.

Outputs land in `build\Stemmerizer_artefacts\Release\`:

```
Standalone\Stemmerizer.exe
VST3\Stemmerizer.vst3\Contents\x86_64-win\Stemmerizer.vst3
```

The VST3 is also auto-copied into `%CommonProgramFiles%\VST3\` for local
testing (JUCE's `COPY_PLUGIN_AFTER_BUILD TRUE`).

## 3. Smoke test in a DAW

The plugin has not been DAW-tested in this repo. **Do this before
distributing anything.** Test at minimum:

- [ ] Plugin scans cleanly in Reaper, Live, FL, Cubase.
- [ ] Editor renders correctly at 100%, 150%, and 200% display scaling.
- [ ] Drag-drop a 3-minute WAV → split completes → 4 stems appear in the
      output folder.
- [ ] Cancel mid-job — UI returns to idle, partial files cleaned up.
- [ ] Switch model variant (4-stem fast → 4-stem HQ → 6-stem) — model
      reloads without crashing.
- [ ] Plugin survives DAW project save/load (no editor state lost).
- [ ] CPU usage during inference is reasonable on a typical 8-core machine
      (target: ≤ 60% of total CPU, completes 3-min file in ≤ 3 min).

## 4. Sign the binaries

Without a valid Authenticode signature, Windows SmartScreen will flag the
installer on every download. EV certs ship in two flavours:

- **Hardware token** (USB or HSM) — required since June 2023 for EV certs.
- **Software cert** (.pfx file) — only valid for OV (non-EV) certs now;
  these still trigger SmartScreen warnings until you build reputation.

### Sign each plugin binary

```powershell
$cert = "<thumbprint or /f path-to-pfx /p password>"
$ts   = "http://timestamp.digicert.com"

# Plugin
signtool sign /tr $ts /td sha256 /fd sha256 /a $cert ^
  "build\Stemmerizer_artefacts\Release\VST3\Stemmerizer.vst3\Contents\x86_64-win\Stemmerizer.vst3"

# Standalone
signtool sign /tr $ts /td sha256 /fd sha256 /a $cert ^
  "build\Stemmerizer_artefacts\Release\Standalone\Stemmerizer.exe"
```

### Sign the installer (built and signed in one step)

Inno Setup picks up `signtool` via the `SignTool` directive in
`installer\Stemmerizer.iss`. Set:

```powershell
$env:SIGNTOOL = "signtool sign /tr $ts /td sha256 /fd sha256 /a `$f"
```

Then build:

```powershell
cd installer
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" Stemmerizer.iss
```

Output: `installer\output\Stemmerizer-1.0.0-Windows-x64-Setup.exe`.

## 5. Verify

- Right-click the installer → Properties → **Digital Signatures** tab → cert
  is present, shown as "OK".
- On a clean Windows VM, run the installer end-to-end:
  - SmartScreen should NOT block it (warning page only if reputation is low).
  - Plugin loads in a freshly-installed DAW.
  - Uninstaller cleanly removes the VST3 from `%CommonProgramFiles%\VST3\`.

## 6. Distribute

- Upload the signed installer to your store (Gumroad / Lemon Squeezy /
  your own site). Hash the binary (SHA-256) and publish the hash on your
  download page so users can verify integrity.
- Notarization is **macOS-only**. Windows Authenticode signature is
  sufficient for SmartScreen + most enterprise allow-lists.

## Troubleshooting

| Symptom                                                       | Likely cause                                              |
|---------------------------------------------------------------|-----------------------------------------------------------|
| "Could not find Inter-Regular.ttf"                            | Drop font files into `resources/fonts/`                   |
| `convert-pth-to-ggml.py` errors with `No module named demucs` | The script auto-installs `demucs` via pip; check Python   |
| Plugin loads but stays black                                  | OpenMP DLL missing — install VC++ Redistributable         |
| Inno Setup: "redist\\VC_redist.x64.exe not found"             | Drop the file there or remove that `[Files]` entry        |
| "Weights not found" alert at first split                      | Run `fetch_weights.py` and re-run installer build         |
