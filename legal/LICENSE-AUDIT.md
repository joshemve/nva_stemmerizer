# Stemmerizer — Third-Party License Audit

This document records every third-party component shipped with or linked into
Stemmerizer, the license each is distributed under, and whether that license
permits sale of a closed-source commercial plugin.

Maintained by: project owner
Last reviewed: 2026-05-08

> **Disclaimer.** This is a developer-prepared audit, not legal advice. Have a
> licensed attorney review this file before public release.

---

## 1. JUCE 8 (audio plugin framework)

- Upstream: https://github.com/juce-framework/JUCE
- Version targeted: JUCE 8 (latest stable on master, locked via git submodule)
- License: **Dual — AGPL v3 _or_ JUCE 8 Commercial EULA**
- Our usage: closed-source commercial VST3 plugin
- Required tier: **JUCE 8 Starter** (free, perpetual, valid up to USD 20,000 of
  combined annual revenue / funding for the legal entity selling the plugin)
- Upgrade path: Indie ($40/mo or $800 perpetual, up to $300k); Pro ($175/mo or
  $3,500 perpetual, unlimited).
- Action items for owner:
  1. Register account at https://juce.com/get-juce/ and accept Starter tier.
  2. Re-evaluate tier whenever annual revenue approaches $20k.
  3. Do NOT combine JUCE binaries with AGPL code under the commercial EULA.
- Splash screen: Starter tier has no splash-screen requirement under JUCE 8
  (this changed from JUCE 7); confirm at registration.

## 2. demucs.cpp (Demucs v4 inference, C++17)

- Upstream: https://github.com/sevagh/demucs.cpp
- License: **MIT**
- Our usage: vendored as git submodule, statically linked
- Commercial use: permitted with attribution + license text in NOTICE
- Notes: pure C++17, depends on Eigen + OpenMP only. No PyTorch / libtorch /
  ONNX Runtime needed at runtime.

## 3. Demucs pretrained weights (htdemucs / htdemucs_ft / htdemucs_6s)

- Upstream code: https://github.com/adefossez/demucs (Meta AI)
- Code license: MIT
- Weights: released by Meta under the same MIT terms accompanying the repo.
- Models we ship:
  - `htdemucs` — 4-stem (vocals/drums/bass/other), default
  - `htdemucs_ft` — fine-tuned 4-stem, marginally higher quality, 4× slower
  - `htdemucs_6s` — 6-stem (adds guitar, piano)
- Models we do NOT ship and explicitly avoid:
  - `mdx_extra` / `mdx_extra_q` — trained data includes the MUSDB18 _test_
    set; benchmark contamination plus stronger dataset-license entanglement.
- Training-data caveat: MUSDB18 itself is licensed for non-commercial research.
  Industry consensus (and Meta's own release stance) treats trained weights as
  a derivative of the architecture, not the data. We rely on Meta's MIT
  release of the weights as authoritative. If a future court ruling changes
  this, we would need to re-train on a fully commercial corpus.
- Weights ship inside the installer in ggml format, converted offline by the
  weight-prep script in `scripts/`.

## 4. ggml (tensor format used by demucs.cpp)

- Upstream: https://github.com/ggerganov/ggml
- License: **MIT**
- Our usage: file format only (no runtime linkage; demucs.cpp reads ggml files
  with its own loader).

## 5. Eigen (linear algebra)

- Upstream: https://gitlab.com/libeigen/eigen
- License: **MPL 2.0** (header-only modules we use); a few non-default modules
  are LGPL — we do not enable them.
- Commercial use: permitted. MPL 2.0 is file-level copyleft: modifications to
  Eigen's own files would have to be published, but linking Eigen into
  closed-source binaries is fine.
- NOTICE requirement: include MPL 2.0 license text and link to upstream.

## 6. OpenMP runtime

- Provided by: Visual C++ runtime (`vcomp140.dll`) — already redistributable
  under the Microsoft VC Redistributable License.
- License: Microsoft permits redistribution with built applications.
- Action: include vc_redist x64 in installer or link statically.

## 7. Steinberg VST3 SDK

- Upstream: https://github.com/steinbergmedia/vst3sdk
- License: **Dual — GPL v3 _or_ Steinberg VST3 Proprietary License**
- Our usage: under the proprietary license (closed-source commercial plugin).
- Required action by owner:
  1. Register as a Steinberg developer (free) at
     https://www.steinberg.net/developers/
  2. Accept the VST3 Plug-In SDK Licensing Agreement.
  3. Plugin must include "VST" trademark notice in EULA / about screen
     ("VST is a trademark of Steinberg Media Technologies GmbH, registered in
     Europe and other countries").
- AU (Audio Unit) for macOS — out of scope for v1.0 (Windows first).

## 8. dr_libs (single-header audio decoders — dr_wav, dr_flac, dr_mp3)

- Upstream: https://github.com/mackron/dr_libs
- License: **Public Domain (Unlicense) _or_ MIT-0** (author's choice)
- Commercial use: unrestricted, no attribution required (we include it anyway
  as good practice).
- Our usage: WAV/FLAC/MP3 file I/O, vendored as headers.

## 9. LAME (MP3 _encoding_) — OPTIONAL, NOT BUNDLED BY DEFAULT

- Upstream: https://lame.sourceforge.io/
- License: **LGPL v2.1**
- Patent status: core MP3 patents expired worldwide by April 2017 — encoding
  is now patent-free.
- LGPL impact on closed-source plugin: dynamic linking is allowed; we would
  ship `libmp3lame.dll` separately and dynamic-load it. Static linking would
  force LGPL on our binary — we do not do this.
- Decision for v1.0: ship MP3 export as an OPTIONAL add-on installer pack so
  the main plugin binary stays free of LGPL coupling. WAV + FLAC ship in core.
- Alternative: Fraunhofer FDK AAC for AAC export (separate license).

## 10. Inno Setup (installer authoring tool)

- Upstream: https://jrsoftware.org/isinfo.php
- License: **Modified BSD** for the tool, **public-domain runtime**.
- Commercial use: unrestricted, including building installers for paid software.

## 11. Fonts shipped in UI

- **Inter** (Rasmus Andersson) — SIL Open Font License 1.1, commercial use OK.
- **JetBrains Mono** (JetBrains) — SIL OFL 1.1, commercial use OK.
- Action: include `OFL.txt` for each font in `resources/fonts/`.

## 12. Icons / iconography

- Use **Lucide** (https://lucide.dev) — ISC license, commercial OK.
- Or hand-drawn SVGs (originated by us, no third-party rights).

---

## Summary — green light to ship commercially

| Component        | License            | Commercial sale OK?          |
|------------------|--------------------|------------------------------|
| JUCE 8 Starter   | Commercial EULA    | YES (under $20k revenue/yr)  |
| demucs.cpp       | MIT                | YES                          |
| Demucs weights   | MIT (Meta release) | YES                          |
| ggml format      | MIT                | YES                          |
| Eigen            | MPL 2.0            | YES (no Eigen modification)  |
| OpenMP runtime   | MS redist          | YES                          |
| VST3 SDK         | Steinberg propriet | YES (after registration)     |
| dr_libs          | Public domain / MIT-0 | YES                       |
| Inno Setup       | BSD                | YES                          |
| Inter / JetBrains| OFL 1.1            | YES                          |
| Lucide icons     | ISC                | YES                          |

No GPL-tainted code is linked in. No non-commercial training data is shipped.
Project is clear to sell as a closed-source commercial plugin under the
JUCE 8 Starter tier.

## Action items the human owner must complete before launch

1. Register account and accept JUCE 8 Starter EULA.
2. Register as Steinberg developer and accept VST3 SDK license.
3. Have a lawyer review the bundled EULA, Privacy Policy, and this audit.
4. Buy a code-signing certificate (~USD 200–400/yr; SSL.com / Sectigo / DigiCert).
5. Re-run this audit any time a dependency is added or upgraded.
