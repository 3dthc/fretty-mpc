#!/bin/bash
# tests/program_test.sh [path to mpc-vst-plugins]: builds the engine with the real VST2 wrapper and runs program_test.c
set -e
cd "$(dirname "$0")/.."
MV="${1:-${MPC_VST:-../mpc-vst-plugins}}"
python3 "$MV/tools/gen_vst.py" vst/vst.json --params-h >/dev/null
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"
CF=$(python3 -c "import json;print(' '.join(json.load(open('vst/vst.json')).get('build',{}).get('cflags',[])))" 2>/dev/null || true)
gcc $SAN -std=gnu11 -Ivst/build -I"$MV/wrapper" -Isrc -c src/engine.c -o "$T/e.o"
gcc $SAN -std=gnu11 -Ivst/build -c "$MV/wrapper/vst2_wrap.c" -o "$T/w.o"
gcc $SAN -std=gnu11 -Ivst/build -c tests/program_test.c -o "$T/t.o"
g++ $SAN "$T"/*.o -lm -lpthread -o "$T/program_test"
"$T/program_test"
