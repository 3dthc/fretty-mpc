#!/usr/bin/env bash
# Fretty: one-step install, from the downloaded zips to a zip ready for the MPC.
#
#   python3 -m zipfile -e /mnt/c/Users/*/Downloads/fretty-mpc-v0.4.0.zip ~/ && bash ~/fretty-mpc/install.sh
#
# What it does:
#   1. finds the fretty-instruments-*-v<version>.zip files in your Windows Downloads (or ~) and adds their instruments
#      to engine/
#   2. checks Docker is running and that mpc-vst-plugins is next to this folder (clones it if not)
#   3. builds the plugin and packages dist/Fretty-<version>-mpc-armv7.zip
#   4. copies that zip to your Windows Downloads, ready to put on the MPC
# Safe to run again: each step skips what is already done.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
VERSION="0.4.0"
INSTRUMENTS="acoustic electric folkharp concertharp strumstick violin"
MPC_VST="${MPC_VST:-$HERE/../mpc-vst-plugins}"
step() { printf '\n\033[1m== %s\033[0m\n' "$*"; }
fail() { printf '\n\033[31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

# Windows Downloads folders visible from WSL (any user), then the Linux home
DL_DIRS=()
for d in /mnt/c/Users/*/Downloads; do [ -d "$d" ] && DL_DIRS+=("$d"); done
DL_DIRS+=("$HOME" "$HERE/..")

# ---- 1. instruments
step "1/4  Instruments"
missing() { local m=""; for f in $INSTRUMENTS; do [ -f "$HERE/engine/$f.smp" ] || m="$m $f"; done; echo "$m"; }
if [ -n "$(missing)" ]; then
  found=0
  for d in "${DL_DIRS[@]}"; do
    for zip in "$d"/fretty-instruments-*-v"$VERSION".zip; do
      [ -f "$zip" ] || continue
      echo "unpacking $zip"
      tmp="$(mktemp -d)"
      python3 -m zipfile -e "$zip" "$tmp"
      cp "$tmp"/fretty-mpc/engine/*.smp "$HERE/engine/"
      rm -rf "$tmp"
      found=$((found + 1))
    done
    if [ "$found" -gt 0 ]; then break; fi
  done
  [ "$found" -gt 0 ] || fail "no fretty-instruments-*-v$VERSION.zip found in: ${DL_DIRS[*]}
       Download them next to fretty-mpc-v$VERSION.zip and run this again."
fi
m="$(missing)"
[ -z "$m" ] || fail "still missing in engine/:$m
       Download every fretty-instruments-*-v$VERSION.zip into the same folder and run this again."
echo "all 6 instrument files present"

# ---- 2. tools
step "2/4  Checking tools"
command -v python3 >/dev/null || fail "python3 is missing:  sudo apt install python3"
command -v docker  >/dev/null || fail "Docker is not available in WSL. Start Docker Desktop (with WSL integration on) and run this again."
docker info >/dev/null 2>&1   || fail "Docker is not running. Start Docker Desktop, wait until it says running, then run this again."
echo "docker ok"
if [ ! -f "$MPC_VST/tools/build_port.sh" ]; then
  command -v git >/dev/null || fail "mpc-vst-plugins not found at $MPC_VST and git is missing (sudo apt install git)"
  echo "mpc-vst-plugins not found, cloning it to $MPC_VST"
  git clone --depth 1 https://github.com/sd88me/mpc-vst-plugins.git "$MPC_VST"
fi
echo "toolchain: $(cd "$MPC_VST" && pwd)"
export MPC_VST

# ---- 3. build
step "3/4  Building Fretty (the first build can take a few minutes)"
rm -f "$HERE"/dist/Fretty-*-mpc-armv7.zip
bash "$HERE/make_release.sh" "$VERSION"
out="$HERE/dist/Fretty-$VERSION-mpc-armv7.zip"
[ -f "$out" ] || fail "build finished without $out"

# ---- 4. hand it over
step "4/4  Done"
copied=""
for d in /mnt/c/Users/*/Downloads; do
  [ -d "$d" ] && [ -w "$d" ] && cp "$out" "$d/" && { copied="$d/$(basename "$out")"; break; }
done
echo "MPC install zip: $out"
if [ -n "$copied" ]; then
  echo "also copied to Windows: $copied"
  echo "  (in Windows Explorer: Downloads\\$(basename "$out"))"
fi
echo
echo "Install it on the MPC the same way as before, replacing the old Fretty."
