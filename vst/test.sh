#!/usr/bin/env bash
# Hosttest (x86, ASan/UBSan) gegen den GEPATCHTEN Wrapper: der Standard-Porttest aus mpc-vst-plugins
# plus test/timing_test.c (Note mit deltaFrames D muss bei Frame D erklingen).
set -euo pipefail
cd "$(dirname "$0")/.."
: "${MPC_VST:=../mpc-vst-plugins}"
SRC="$(cd "$MPC_VST" && pwd)"
PATCHED="$PWD/vst/build/mv-patched-test"
rm -rf "$PATCHED" && mkdir -p "$PATCHED"
(cd "$SRC" && tar --exclude=.git -cf - .) | (cd "$PATCHED" && tar -xf -)
python3 vst/wrap_fix.py "$PATCHED"
MPC_VST="$PATCHED" bash "$PATCHED/tools/test_port.sh" vst/vst.json
# test_port.sh has written vst/build/params.h
CMD="gcc -fsanitize=address,undefined -g -O1 -std=gnu11 -D_DEFAULT_SOURCE -w -Ivst/build -Isrc -Iwrap \
src/kcb_engine.c src/mpc_adapter.c wrap/vst2_wrap.c test/timing_test.c -lm -o vst/build/timing_test \
&& ./vst/build/timing_test"
if command -v gcc >/dev/null; then
  rm -rf wrap && cp -r "$PATCHED/wrapper" wrap && trap 'rm -rf wrap' EXIT
  bash -c "$CMD"
else
  docker run --rm -v "$PWD":/b -v "$PATCHED/wrapper":/b/wrap:ro -w /b gcc:12 bash -c "$CMD"
fi
