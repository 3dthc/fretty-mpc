#!/usr/bin/env bash
# One command from source to an installable MPC zip:
#   ./make_release.sh [version]        (default 0.4.0)
# Builds the plugin (vst/build.sh, Docker), then packages it with mpc-vst-plugins' release tool into
# dist/Fretty-<version>-mpc-armv7.zip, with the instruments (engine/*.smp) next to the .so.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
MPC_VST="${MPC_VST:-$HERE/../mpc-vst-plugins}"
VERSION="${1:-0.4.0}"
B="$HERE/vst/build"

bash "$HERE/vst/build.sh"

[ -f "$B/fretty.so" ] || { echo "build failed: no $B/fretty.so" >&2; exit 1; }


python3 "$MPC_VST/tools/release.py" \
  --so "$B/fretty.so" \
  --skin "$B/skin/3dthc - VST - Fretty" \
  --entry "$B/pluginlist-entry.xml" \
  --version "$VERSION" \
  --extra "$HERE/engine:engine" \
  --about "Sampled guitars (acoustic archtop, electric), harps, strumstick and violin (bowed and pizzicato) on a six-string playing model, with a synth layer, drive, chorus and space. All samples CC0." \
  -o "$HERE/dist"

echo
echo "Release zip:"
ls -la "$HERE/dist/"*.zip
