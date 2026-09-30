#!/usr/bin/env bash
# Baut das Plugin mit den Tools aus mpc-vst-plugins (Docker + QEMU nötig).
# MPC_VST zeigt auf einen Checkout von sd88me/mpc-vst-plugins (in CI automatisch gesetzt).
set -euo pipefail
cd "$(dirname "$0")/.."
: "${MPC_VST:=../mpc-vst-plugins}"
"$MPC_VST/tools/build_port.sh" vst/vst.json
