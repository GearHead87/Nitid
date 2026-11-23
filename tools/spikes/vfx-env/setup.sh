#!/usr/bin/env bash
# Shared Python environment for the V1–V5 VFX SDK tests.
# Creates third_party/vfx-venv (Python 3.12 via uv) and installs NVIDIA's sample
# requirements (nvidia-vfx, torch+cu128, av, numpy). Needs scripts/fetch-sdks.sh first.
set -euo pipefail

root=$(cd "$(dirname "$0")/../../.." && pwd)
samples="$root/third_party/nvidia-vfx-python-samples"
venv="$root/third_party/vfx-venv"

[ -f "$samples/requirements.txt" ] || { echo "run scripts/fetch-sdks.sh first"; exit 1; }

[ -x "$venv/bin/python" ] || uv venv --python 3.12 "$venv"
uv pip install --python "$venv/bin/python" --index-strategy unsafe-best-match \
  -r "$samples/requirements.txt"
uv pip check --python "$venv/bin/python"

"$venv/bin/python" - <<'EOF'
import importlib.metadata as md, torch
print("python ok")
for p in ("nvidia-vfx", "torch", "av", "numpy"):
    print(f"{p:12s} {md.version(p)}")
print("torch cuda:", torch.cuda.is_available(), torch.version.cuda,
      torch.cuda.get_device_name(0) if torch.cuda.is_available() else "-")
import nvvfx as vfx
print("nvvfx import ok:", [n for n in dir(vfx) if not n.startswith("_")][:20])
EOF
