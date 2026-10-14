#!/usr/bin/env bash
# Build Fretty for MPC OS: the .so, skin and plugin-list entry (samples are already packed in engine/).
#   vst/build.sh            (run from anywhere; needs Docker and python3)
# Expects a checkout of sd88me/mpc-vst-plugins next to this repo (override with MPC_VST=...).
# The samples ship packed in engine/fretty.smp (Karoryfer's Shinyguitar, CC0, ~20 MB losslessly compressed). Only if
# that file is missing are they re-packed: Shinyguitar is cloned (pinned commit, ~800 MB) into third_party/ and
# tools/pack_samples.py runs (needs python3-numpy). Output: vst/build/fretty.so, vst/build/skin/...
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
MPC_VST="${MPC_VST:-$REPO/../mpc-vst-plugins}"
SHINY="$REPO/third_party/karoryfer.shinyguitar"
SHINY_URL=https://github.com/sfzinstruments/karoryfer.shinyguitar
SHINY_COMMIT=57243cca85277dbcc120ce17c6178032f93c80f3

[ -f "$MPC_VST/tools/build_port.sh" ] || { echo "mpc-vst-plugins not found at $MPC_VST (set MPC_VST=...)" >&2; exit 1; }

if [ ! -f "$REPO/engine/fretty.smp" ]; then
  if [ ! -d "$SHINY/.git" ]; then
    mkdir -p "$REPO/third_party"
    git clone "$SHINY_URL" "$SHINY"
  fi
  git -C "$SHINY" checkout -q "$SHINY_COMMIT"
  python3 -I "$REPO/tools/pack_samples.py" "$SHINY" "$REPO/engine"
fi

bash "$MPC_VST/tools/build_port.sh" "$HERE/vst.json"
echo "built: $HERE/build/fretty.so  (+ $REPO/engine/ ships next to it)"
