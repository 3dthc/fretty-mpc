#!/usr/bin/env bash
# Build Fretty for MPC OS: the .so, skin and plugin-list entry (samples are already packed in engine/).
#   vst/build.sh            (run from anywhere; needs Docker and python3)
# Expects a checkout of sd88me/mpc-vst-plugins next to this repo (override with MPC_VST=...).
# The instruments ship packed in engine/*.smp (all CC0; tools/pack_samples.py rebuilds them, see README).
# Output: vst/build/fretty.so, vst/build/skin/...
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
MPC_VST="${MPC_VST:-$REPO/../mpc-vst-plugins}"
[ -f "$MPC_VST/tools/build_port.sh" ] || { echo "mpc-vst-plugins not found at $MPC_VST (set MPC_VST=...)" >&2; exit 1; }

missing=""
for f in acoustic electric folkharp concertharp strumstick; do
  [ -f "$REPO/engine/$f.smp" ] || missing="$missing $f.smp"
done
if [ -n "$missing" ]; then
  echo "missing instrument files in $REPO/engine:$missing" >&2
  echo "(unzip the instruments zip into this folder, or rebuild them: tools/pack_samples.py, see README)" >&2
  exit 1
fi

bash "$MPC_VST/tools/build_port.sh" "$HERE/vst.json"
echo "built: $HERE/build/fretty.so  (+ $REPO/engine/ ships next to it)"
