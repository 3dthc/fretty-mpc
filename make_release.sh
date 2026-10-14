#!/usr/bin/env bash
# One command from source to an installable MPC zip:
#   ./make_release.sh [version]        (default 0.1.0)
# Builds the plugin (vst/build.sh, Docker), then packages it with mpc-vst-plugins' release tool into
# dist/Archtop-<version>-mpc-armv7.zip, with the samples (engine/archtop.smp) next to the .so.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MPC_VST="${MPC_VST:-$HERE/../mpc-vst-plugins}"
VERSION="${1:-0.1.0}"
B="$HERE/vst/build"

"$HERE/vst/build.sh"

[ -f "$B/archtop.so" ] || { echo "build failed: no $B/archtop.so" >&2; exit 1; }
[ -f "$HERE/engine/archtop.smp" ] || { echo "missing $HERE/engine/archtop.smp" >&2; exit 1; }

python3 "$MPC_VST/tools/release.py" \
  --so "$B/archtop.so" \
  --skin "$B/skin/3dthc - VST - Archtop" \
  --entry "$B/pluginlist-entry.xml" \
  --version "$VERSION" \
  --extra "$HERE/engine:engine" \
  --about "Sampled archtop guitar: pickup + mic blend, six-string playing model (Karoryfer Shinyguitar samples, CC0)." \
  -o "$HERE/dist"

echo
echo "Release zip:"
ls -la "$HERE/dist/"*.zip
