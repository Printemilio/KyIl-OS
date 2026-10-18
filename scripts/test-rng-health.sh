#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
sources=(vendor/jitterentropy/src/*.c)
for source in vendor/jitterentropy/arch/*.c; do
    case "$source" in *arch-memory.c|*arch-timer.c) continue;; esac
    sources+=("$source")
done
# The entropy implementation's mandatory compiler policy is preserved.
gcc -O0 -fwrapv -ffreestanding -ffunction-sections -fdata-sections -DJENT_BAREMETAL -Ivendor/jitterentropy -Ivendor/jitterentropy/src \
    "${sources[@]}" tests/rng_jitter_health_test.c -Wl,--gc-sections -o build/rng-health-test
build/rng-health-test
build/rng-health-test oom
