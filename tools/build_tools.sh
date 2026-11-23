#!/bin/sh
# Build the engine's tools and tests for this computer (cc). Same flags as fm1-base's host build: -ffp-contract=off.
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
FL="-O2 -ffp-contract=off -std=c99 -Wall -Wextra -Wno-unused-function -DPLAT_HOST -Idsp -Ifm1-base/include"
mkdir -p build
SRC="dsp/engine.c dsp/seq.c dsp/mpe.c dsp/tuning.c dsp/fx.c dsp/plate.c dsp/names.c dsp/presets.c tools/host_stub.c"
$CC $FL -o build/gen_masks tools/gen_masks.c -lm
$CC $FL -o build/engine_test tests/engine_test.c $SRC -lm
echo "tools: build/gen_masks build/engine_test"
