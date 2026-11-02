#!/usr/bin/env bash
# GLTF-017 / GLTF-383: the same corpus, the same conformance ladder, two renderers.
#
# The oracle harness is supposed to be renderer-independent below L7: L1 is the container, L2 the
# decoded accessors, L3 the semantic mesh, L4 world geometry and L5 the packed GPU bytes -- none of
# which any renderer contributes to. This wrapper compares two configured test executables so
# each renderer is checked against the same committed goldens before their outcomes are compared.
# A multi-renderer executable can also be wrapped with a distinct runtime selection per input.
#
# What this is not: a claim that two runs produce the same numbers as each other. Each run compares
# its own output against the SAME committed goldens, which is a stronger statement -- two agreeing
# runs could agree on a shared mistake, while both matching a third, committed artifact cannot.
#
# Usage:  scripts/gltf-renderer-parity.sh <build-dir-A> <build-dir-B>
#
# Run it from the repository root: every corpus path in the suite is relative to it.
#
# Exit codes: 0 equivalent successful runs · 1 failed/empty run or differing results · 2 unusable environment.

set -uo pipefail

if [ "$#" -ne 2 ]; then
    echo "usage: $(basename "$0") <build-dir-A> <build-dir-B>" >&2
    exit 2
fi

A="$1"
B="$2"
for dir in "$A" "$B"; do
    if [ ! -x "$dir/CnaTests" ]; then
        echo "gltf-renderer-parity: $dir/CnaTests is missing or not executable." >&2
        echo "  Configure one build per renderer, e.g." >&2
        echo "    cmake -S . -B <dir> -DCNA_GRAPHICS_RENDERER=STUB     -DCNA_BUILD_TESTS=ON" >&2
        echo "    cmake -S . -B <dir> -DCNA_GRAPHICS_RENDERER=HEADLESS -DCNA_BUILD_TESTS=ON" >&2
        exit 2
    fi
done

# L1 through L5 plus the suites that assert the packed bytes directly. L6 is deliberately included
# in the full-suite comparison below but not here: it captures effect parameters, which a renderer
# CAN legitimately differ on once one starts consuming what the others ignore.
LAYERS='GltfConformanceL1*:GltfConformanceL2*:GltfConformanceL3*:GltfConformanceL4*:GltfConformanceL5*:GltfBufferOracle*:GltfStrideAndBuffer*:GltfVertexLayoutTable*:GltfVertexBufferInvariants*'

run() {
    local -a statuses
    # Result lines only, with the per-test timings stripped: a millisecond difference is not a
    # divergence, and leaving it in would make this script fail at random.
    "$1/CnaTests" --gtest_filter="$2" 2>&1 \
        | grep -E '^\[       OK \]|^\[  FAILED  \]|^\[  SKIPPED \]|^\[==========\] [1-9][0-9]* tests' \
        | sed -E 's/ \([0-9]+ ms\)//; s/\([0-9]+ ms total\)/(time elided)/'
    statuses=("${PIPESTATUS[@]}")
    if (( statuses[0] != 0 )); then
        echo "gltf-renderer-parity: $1/CnaTests failed with exit ${statuses[0]} for $2" >&2
        return 1
    fi
    if (( statuses[1] != 0 || statuses[2] != 0 )); then
        echo "gltf-renderer-parity: $1 produced no selected test results, or result normalization failed." >&2
        return 1
    fi
    return 0
}

tmp_a="$(mktemp)"; tmp_b="$(mktemp)"
trap 'rm -f "$tmp_a" "$tmp_b"' EXIT

echo "gltf-renderer-parity: L1-L5 oracle layers"
run "$A" "$LAYERS" > "$tmp_a" || exit 1
run "$B" "$LAYERS" > "$tmp_b" || exit 1
if ! diff -u "$tmp_a" "$tmp_b"; then
    echo "gltf-renderer-parity: the two renderers DISAGREE below L6 -- a renderer has reached a" >&2
    echo "  layer it does not own. Per GLTF-392 that is a shared-importer defect, and it" >&2
    echo "  must not be fixed inside a renderer directory (GLTF-392)." >&2
    exit 1
fi
echo "gltf-renderer-parity: identical ($(grep -c '^\[       OK \]' "$tmp_a") tests)"

echo
echo "gltf-renderer-parity: the whole glTF selection"
# Contains, not starts-with: RuntimeGltfModelTest is part of the Tool rung and was the exact suite
# that a historical `Gltf*` prefix filter silently omitted (GLTF-010/383).
run "$A" '*Gltf*' > "$tmp_a" || exit 1
run "$B" '*Gltf*' > "$tmp_b" || exit 1
if diff -u "$tmp_a" "$tmp_b"; then
    echo "gltf-renderer-parity: identical ($(grep -c '^\[       OK \]' "$tmp_a") tests)"
    exit 0
fi

# A difference in the FULL selection is not automatically a defect: a renderer without a 3D
# pipeline skips the tests that need one, and a skip is a different outcome from a pass rather than
# a wrong one. Those are reported and tolerated; anything else is not.
if diff "$tmp_a" "$tmp_b" | grep -vE '^[<>] \[  SKIPPED \]|^[<>] \[       OK \]|^[0-9,]+[acd][0-9,]+$|^---$' \
     | grep -q .; then
    echo "gltf-renderer-parity: the two renderers disagree on a result other than a skip." >&2
    exit 1
fi
echo "gltf-renderer-parity: the only differences are SKIPPED-vs-OK, i.e. a renderer capability"
echo "  gate (GraphicsCapability::ThreeD) rather than a divergence. Reported, not failed."
exit 0
