# Weights directory

This directory holds the Demucs v4 weights converted to ggml format
(`.gguf` files) that demucs.cpp loads at runtime.

The files are NOT committed — they are large (~80–320 MB depending on model)
and license-clean to redistribute, but version control is the wrong place for
them.

## Generate them

From the repo root:

```bash
python scripts/fetch_weights.py            # default: htdemucs (4-stem fast)
python scripts/fetch_weights.py --all      # all three model variants
```

The first run downloads the original PyTorch checkpoints from
`dl.fbaipublicfiles.com` (Meta's public mirror) and converts them to ggml using
the converter script vendored from `third_party/demucs.cpp`.

## What ends up here

- `htdemucs.gguf`     — 4-stem (vocals/drums/bass/other), fast
- `htdemucs_ft.gguf`  — 4-stem fine-tuned, higher quality, ~4× slower
- `htdemucs_6s.gguf`  — 6-stem (adds guitar, piano)

## Licensing

All three models are released by Meta under the MIT license (same as the
Demucs codebase). See `legal/LICENSE-AUDIT.md` § 3 for the full
commercial-use analysis.
