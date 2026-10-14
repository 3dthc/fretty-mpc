#!/usr/bin/env bash
# Build Archtop for MPC OS: pack the samples once, then the .so, skin and plugin-list entry.
#   vst/build.sh            (run from anywhere; needs Docker, python3 and python3-numpy)
# Expects a checkout of sd88me/mpc-vst-plugins next to this repo (override with MPC_VST=...).
# The samples come from Karoryfer's Shinyguitar (CC0), pinned to one commit and cloned into third_party/ (not committed:
# ~800 MB). Output: vst/build/archtop.so, vst/build/skin/..., vst/build/engine/archtop.smp.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
MPC_VST="${MPC_VST:-$REPO/../mpc-vst-plugins}"
SHINY="$REPO/third_party/karoryfer.shinyguitar"
SHINY_URL=https://github.com/sfzinstruments/karoryfer.shinyguitar
SHINY_COMMIT=57243cca85277dbcc120ce17c6178032f93c80f3

[ -x "$MPC_VST/tools/build_port.sh" ] || { echo "mpc-vst-plugins not found at $MPC_VST (set MPC_VST=...)" >&2; exit 1; }

if [ ! -f "$HERE/build/engine/archtop.smp" ]; then
  if [ ! -d "$SHINY/.git" ]; then
    mkdir -p "$REPO/third_party"
    git clone "$SHINY_URL" "$SHINY"
  fi
  git -C "$SHINY" checkout -q "$SHINY_COMMIT"
  python3 -I "$REPO/tools/pack_samples.py" "$SHINY" "$HERE/build/engine"
fi

"$MPC_VST/tools/build_port.sh" "$HERE/vst.json"
echo "built: $HERE/build/archtop.so  (+ engine/archtop.smp to ship next to it)"
