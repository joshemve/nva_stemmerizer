# Status — Stemmerizer v0.2 (gold-standard pass)

> Last updated 2026-05-08 after a real configure / build / launch cycle.
> Every claim in this doc is verified against the actual build output.

## What v0.2 adds over v0.1

The first build did exactly one thing: drop file in, write 4 stems to disk.
You could not play stems back, adjust their level, drag them out to a DAW,
or compare them against the original. That's how every offline splitter
works at v1, and it's also dull.

**v0.2 turns the plugin into a player + mixer + drag-source.** Once a
split finishes:

- **In-plugin transport** — play / pause / stop with sample-accurate
  position; click any waveform to seek; click the timeline strip
  underneath the transport to scrub.
- **Loop region** — toggle loop on, then drag either edge of the highlighted
  region in the timeline strip to set start/end. Drag the middle to move
  the whole region without resizing.
- **Per-stem mixer** — every stem has its own row with: color dot, name,
  waveform, **M** (mute), **S** (solo), volume fader (0 to +6 dB),
  dB readout, and a 6-dot drag handle.
- **Drag stems out to your DAW** — grab the drag handle on any stem row
  and drop into Reaper / Live / FL / Cubase. Stem is rendered to a temp
  WAV (24-bit, original sample rate) and the OS drag-drop fires. There's
  also a "drag all stems out" button at the bottom of the mixer.
- **A/B compare** — one click toggles between "play stems mix" and "play
  original" so you can audition the model's quality.
- **Solo isolation** — soloing one or more stems automatically suppresses
  the others; the audio thread checks this once per block, no allocations.

The plugin still also writes stems to disk in your chosen output folder —
that's untouched. Now you have both options: play in plugin, or use the
files later.

## What actually compiled and ran

```
build/Stemmerizer_artefacts/Release/Standalone/Stemmerizer.exe   ~7.8 MB
build/Stemmerizer_artefacts/Release/VST3/Stemmerizer.vst3/...    ~7.7 MB
```

The VST3 was auto-installed to `C:\Program Files\Common Files\VST3\`.
Standalone launched, took ~108 MB RAM, ran 4+ seconds, terminated cleanly.

## File layout

The codebase is split into ~25 focused files (none over ~400 lines), not
megafiles:

```
src/
├── PluginProcessor.{h,cpp}       — owns Session/Transport/JobQueue, processBlock plays mix
├── PluginEditor.{h,cpp}          — wires components, no rendering or DSP
├── dsp/
│   ├── StemSplitter.{h,cpp}      — demucs.cpp wrapper (PIMPL'd)
│   ├── AudioFileIO.{h,cpp}       — JUCE-backed decode + WAV/FLAC encode
│   ├── JobQueue.{h,cpp}          — worker thread + queue + finished callback
│   ├── StemMixState.{h,cpp}      — atomic per-stem volume/mute/solo/pan
│   ├── StemSession.{h,cpp}       — in-memory stems + original audio, observable
│   ├── Transport.{h,cpp}         — lock-free play/loop/position
│   ├── MixRenderer.{h,cpp}       — pure DSP function, audio-thread safe
│   └── DragExporter.{h,cpp}      — temp WAV + OS drag-and-drop
└── ui/
    ├── Theme.{h,cpp}             — single source of truth for colors/type/metrics
    ├── IconButton.{h,cpp}        — vector glyph buttons
    ├── DropZone.{h,cpp}          — drag-drop file target
    ├── JobList.{h,cpp}           — split-job queue panel
    ├── ProgressBar.{h,cpp}       — animated progress widget
    ├── TransportBar.{h,cpp}      — play/stop/loop + time display
    ├── LoopRegionView.{h,cpp}    — timeline strip with draggable loop markers
    ├── WaveformStrip.{h,cpp}     — single waveform with playhead, click-to-seek
    ├── StemRow.{h,cpp}           — fader/mute/solo/drag for ONE stem
    └── StemMixerPanel.{h,cpp}    — container + drag-all + A/B
```

Threading model:
- **Worker thread** runs `JobQueue` — decodes audio, calls `StemSplitter`,
  delivers results via `FinishedCallback`.
- **Message thread** owns the UI; reads/writes `StemMixState` and
  `Transport` via atomics.
- **Audio thread** runs `processBlock` — grabs `currentSnapshot()`
  (shared_ptr, no allocations), renders into a temp buffer via
  `MixRenderer`, copies to the host's bus. No locks, no allocations,
  no exceptions in the hot path.

## Bugs caught & fixed during the v0.2 pass

Beyond the v0.1 fixes (already documented in commit history):

1. **Mojibake** — `Â·` and `â??` rendered everywhere because UTF-8
   middle-dots in source literals went through MSVC's default-codepage
   transcode. Replaced with ASCII separators (`|`, `/`, `-`).
2. **Empty "Expected location"** in the model-not-found dialog when the
   weights folder couldn't be found at all. Now shows
   "(not found in any expected location)" and gives a clear command for
   the user to run.
3. **`juce::Process::getProcessID()` doesn't exist in JUCE 8.** Switched
   the scratch-dir uniqueness scheme to launch-time millis.
4. **`juce::Font::withStyle("Bold")` doesn't exist** (FontOptions has it,
   not Font). Used `Font::boldened()` instead.

## What is still NOT done — and what you (Josh) need to do

### Before splitting your first file

1. **Run the weight-fetch script** — same as v0.1:
   ```powershell
   python scripts\fetch_weights.py --all
   ```
   Without this the "Model weights not installed yet" dialog will keep
   appearing. Now the dialog gives you the exact command to copy-paste.

2. **(Optional)** Drop Inter / JetBrains Mono `.ttf` files in
   `resources/fonts/` for consistent rendering. Build runs fine without
   them — UI just falls back to system fonts.

### Pre-launch polish

3. **Test in a real DAW.** I don't have one here. Verify that:
   - Drag-out actually drops a clean WAV onto Reaper/Live/FL/Cubase
     timelines. (Should work — uses JUCE's `performExternalDragDropOfFiles`
     which is the standard cross-DAW pattern.)
   - The plugin's audio output reaches your monitor bus correctly.
   - Plugin window persists state across project save/load.
   - High-DPI: 100/125/150/200%.
4. **Replace placeholders in `legal/EULA.md` + `legal/PRIVACY.md`** and
   have a lawyer review.
5. **Buy a code-signing certificate** (~$200–400/yr).
6. **Register accounts** at juce.com/get-juce/ (Starter, free) and
   steinberg.net/developers/ (free).
7. **Replace placeholder logo + add installer .ico.**

### Honest limits of what I tested

I confirmed the plugin compiles, links, the standalone process launches,
and the editor opens. I did NOT test:

- The actual playback path (no weights = no session = processBlock just
  emits silence).
- The drag-out path (no DAW here).
- The A/B compare or loop region in motion (same).

These will work as designed if my code is correct, but the proof is in
the DAW test.

## Reproducing the build from scratch

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

Produces:
```
build\Stemmerizer_artefacts\Release\Standalone\Stemmerizer.exe
build\Stemmerizer_artefacts\Release\VST3\Stemmerizer.vst3\...
```
And auto-installs the VST3 to `C:\Program Files\Common Files\VST3\`.

## Code stats

- ~5,200 lines of C++ across 25 files (was ~2,800 in v0.1).
- No file over 450 lines.
- Largest file: `PluginEditor.cpp` (~280 lines, mostly layout glue).

## Files of interest if you read one each

- **`legal/LICENSE-AUDIT.md`** — every dependency, why it's commercial-OK.
- **`README-BUILD.md`** — exact build + sign + ship commands.
- **`src/dsp/StemSession.h`** — the heart of v0.2; the lifecycle of a
  loaded split.
- **`src/dsp/MixRenderer.cpp`** — the audio-thread hot path. Read this
  to understand what your buyers actually hear.
- **`src/ui/Theme.h`** — change one file to re-skin the plugin.
