#!/usr/bin/env bash
# Baut das Plugin mit den Tools aus mpc-vst-plugins (Docker + QEMU nötig).
# MPC_VST zeigt auf einen Checkout von sd88me/mpc-vst-plugins (in CI automatisch gesetzt).
# Gebaut wird gegen eine KOPIE davon, deren Wrapper vst/wrap_fix.py samplegenau macht
# (Noten starten an ihrer Position im Block statt am Blockanfang, ohne Zusatzlatenz).
set -euo pipefail
cd "$(dirname "$0")/.."
: "${MPC_VST:=../mpc-vst-plugins}"
SRC="$(cd "$MPC_VST" && pwd)"
PATCHED="$PWD/vst/build/mv-patched"
rm -rf "$PATCHED" && mkdir -p "$PATCHED"
(cd "$SRC" && tar --exclude=.git -cf - .) | (cd "$PATCHED" && tar -xf -)
python3 vst/wrap_fix.py "$PATCHED"
MPC_VST="$PATCHED" bash "$PATCHED/tools/build_port.sh" vst/vst.json
