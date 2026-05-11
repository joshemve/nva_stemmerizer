#!/usr/bin/env python3
"""
fetch_weights.py — Run demucs.cpp's official PyTorch-to-ggml converter for
each model variant we ship, and stage the output files in
`resources/weights/` with the filenames our StemSplitter expects.

Why this exists
---------------
demucs.cpp's `scripts/convert-pth-to-ggml.py` does the heavy lifting (it
calls into the upstream `demucs` Python package, downloads PyTorch
checkpoints, and emits ggml binaries). All this wrapper does is:

  1. Make sure third_party/demucs.cpp is checked out.
  2. Make sure the `demucs` Python package is installed (so the converter
     can import it and torch.hub-fetch the .pth weights).
  3. Run the converter once per variant, into a fresh temp dir.
  4. Move the produced .gguf into `resources/weights/<our-canonical-name>`.

Upstream CLI (taken straight from convert-pth-to-ggml.py):

    python convert-pth-to-ggml.py <dest_dir>
                                  [--six-source]   # → htdemucs_6s
                                  [--v3]
                                  [--ft-drums | --ft-bass |
                                   --ft-other | --ft-vocals]   # → htdemucs_ft
    (no flag) → htdemucs (4-stem)

Usage
-----
    python scripts/fetch_weights.py            # downloads + converts htdemucs (default)
    python scripts/fetch_weights.py --all      # all three: htdemucs, htdemucs_ft, htdemucs_6s
    python scripts/fetch_weights.py --models htdemucs htdemucs_6s
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT       = Path(__file__).resolve().parent.parent
WEIGHTS_DIR     = REPO_ROOT / "resources" / "weights"
DEMUCSCPP_DIR   = REPO_ROOT / "third_party" / "demucs.cpp"
DEMUCSCPP_REPO  = "https://github.com/sevagh/demucs.cpp.git"

# What flags to pass to convert-pth-to-ggml.py for each variant we ship,
# and the filename our StemSplitter expects in WEIGHTS_DIR.
VARIANTS = {
    "htdemucs": {
        "flags": [],
        "out":   "htdemucs.gguf",
    },
    # The "fine-tuned" ensemble is four separately fine-tuned 4-source nets.
    # demucs.cpp's convert script emits one ggml per fine-tune, with a
    # well-known filename inside the dest_dir. We concatenate them in the
    # order our model loader expects (drums/bass/other/vocals).
    #
    # NOTE: shipping all four .gguf files for the FT variant means the
    # plugin's "fine-tuned" mode actually runs the whole ensemble — same as
    # upstream Demucs's `htdemucs_ft` model. If we want a single-file
    # surrogate, we'd pick the vocals fine-tune (the highest-impact one)
    # and ship that alone; left for v1.1 to decide.
    "htdemucs_ft": {
        "submodels": [
            ("--ft-drums",  "ggml-model-htdemucs-ft-drums-f16.bin"),
            ("--ft-bass",   "ggml-model-htdemucs-ft-bass-f16.bin"),
            ("--ft-other",  "ggml-model-htdemucs-ft-other-f16.bin"),
            ("--ft-vocals", "ggml-model-htdemucs-ft-vocals-f16.bin"),
        ],
        "out": "htdemucs_ft.gguf",
    },
    "htdemucs_6s": {
        "flags": ["--six-source"],
        "out":   "htdemucs_6s.gguf",
    },
}


def run(cmd: list[str], cwd: Path | None = None) -> None:
    print(f"  $ {' '.join(cmd)}")
    subprocess.check_call(cmd, cwd=cwd)


def ensure_demucscpp() -> None:
    if (DEMUCSCPP_DIR / "scripts" / "convert-pth-to-ggml.py").exists():
        return
    print("Cloning demucs.cpp (one-time)…")
    DEMUCSCPP_DIR.parent.mkdir(parents=True, exist_ok=True)
    if DEMUCSCPP_DIR.exists():
        shutil.rmtree(DEMUCSCPP_DIR)
    run(["git", "clone", "--depth", "1", DEMUCSCPP_REPO, str(DEMUCSCPP_DIR)])


def ensure_demucs_python() -> None:
    """The convert script imports `demucs` — install it via pip if missing."""
    try:
        import demucs  # noqa: F401
        return
    except ImportError:
        pass
    print("Installing PyTorch + demucs (one-time, ~1.5 GB)…")
    run([sys.executable, "-m", "pip", "install", "--upgrade", "demucs", "torch"])


def convert_one(variant: str) -> None:
    spec = VARIANTS[variant]
    converter = DEMUCSCPP_DIR / "scripts" / "convert-pth-to-ggml.py"
    if not converter.exists():
        sys.exit(f"convert script not found: {converter}")

    out_path = WEIGHTS_DIR / spec["out"]
    if out_path.exists() and out_path.stat().st_size > 0:
        print(f"  {spec['out']} already present, skipping (delete to re-run)")
        return

    WEIGHTS_DIR.mkdir(parents=True, exist_ok=True)

    # The htdemucs_ft variant is an ensemble — convert each fine-tune to its
    # own ggml then pack them sequentially into one .gguf. Our StemSplitter
    # just hands the file to demucs.cpp's loader, which expects the upstream
    # ensemble layout.
    if variant == "htdemucs_ft":
        with tempfile.TemporaryDirectory() as td:
            td_path = Path(td)
            chunks: list[Path] = []
            for flag, expected_filename in spec["submodels"]:
                run([sys.executable, str(converter), str(td_path), flag])
                produced = td_path / expected_filename
                if not produced.exists():
                    # The script's filename heuristic may have changed
                    # upstream. Fall back to "newest .bin in td".
                    bins = sorted(td_path.glob("*.bin"),
                                  key=lambda p: p.stat().st_mtime,
                                  reverse=True)
                    if not bins:
                        sys.exit("Converter produced no .bin file")
                    produced = bins[0]
                chunks.append(produced)

            # Concatenate in the order drums / bass / other / vocals.
            with open(out_path, "wb") as out:
                for c in chunks:
                    out.write(c.read_bytes())
        print(f"  -> {out_path}")
        return

    # All other variants → single file.
    with tempfile.TemporaryDirectory() as td:
        td_path = Path(td)
        run([sys.executable, str(converter), str(td_path), *spec["flags"]])
        bins = list(td_path.glob("*.bin")) + list(td_path.glob("*.gguf"))
        if not bins:
            sys.exit("Converter produced no output file")
        # Single largest .bin / .gguf is what we want.
        bins.sort(key=lambda p: p.stat().st_size, reverse=True)
        shutil.move(str(bins[0]), str(out_path))
    print(f"  -> {out_path}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--models", nargs="+", default=["htdemucs"],
                    choices=list(VARIANTS.keys()),
                    help="which model variants to fetch & convert")
    ap.add_argument("--all", action="store_true",
                    help="shortcut for --models htdemucs htdemucs_ft htdemucs_6s")
    args = ap.parse_args()

    selected = list(VARIANTS.keys()) if args.all else args.models

    ensure_demucscpp()
    ensure_demucs_python()

    for v in selected:
        print(f"\n=== {v} ===")
        convert_one(v)

    print(f"\nDone. Weights in:\n  {WEIGHTS_DIR}")
    print("These are bundled by installer/Stemmerizer.iss when you build the installer.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
