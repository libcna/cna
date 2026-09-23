#!/bin/bash
# SPDX-License-Identifier: MS-PL
#
# plans/plan_pre_sdlgpu_closeout.md PSG-0002: run a GoogleTest binary in BOUNDED MEMORY.
#
# Why this exists, stated as the measurement that forced it. A CNA test builds a GraphicsDevice,
# and on some renderers a device is not cheap to have had: the provider keeps per-device state that
# is not returned when the device is destroyed. Measured on this machine, over the same 200 tests
# of CnaGraphicsExtTests from the same binary:
#
#     WEBGPU   peak RSS 1115 MB          VULKAN   peak RSS 167 MB
#
# So the cost is per RENDERER as much as per test, and it accumulates inside one process. That is
# why `CnaTests` -- 10 564 tests, most of them device-creating -- could not be run on WEBGPU at all
# during the WebGPU closeout: every attempt, at four shards and at twelve, was killed by the
# machine's low-memory reaper long before it finished.
#
# The lever that actually works is therefore **how many tests share one process**, not how many
# processes run at once. Both are bounded here, and the first is the one with a safe default:
# 481 tests in one WEBGPU process is killed on a 30 GB machine, 240 is not.
#
#   tools/tests/run_gtest_bounded.sh [options] <gtest-binary> [-- <extra gtest args>]
#
#     --tests-per-shard N   how many test cases share one process   (default 200)
#     --shards N            exact shard count instead of deriving it from --tests-per-shard
#     --max-parallel N      how many shards run at once             (default 1)
#     --filter F            gtest filter, applied before sharding
#     --out DIR             logs, XML and state                     (default a temp directory)
#     --resume              skip shards that already completed in --out
#     --keep-going          run every shard even after one fails    (default: also keeps going)
#     --stop-on-fail        stop launching new shards after a failure
#     --allow-live-display  permit DISPLAY=:0 / WAYLAND_DISPLAY=wayland-0
#
# It is renderer-agnostic: it reads nothing about the renderer and passes the environment through,
# so `CNA_GRAPHICS_RENDERER=VULKAN tools/tests/run_gtest_bounded.sh ...` shards Vulkan.
#
# It does NOT create a display. Compose it with the private compositor, so one compositor serves
# every shard rather than one per shard:
#
#   tools/platform/run_gpu_tests_private.sh --exec \
#       tools/tests/run_gtest_bounded.sh --max-parallel 2 ./cmake-build-webgpu/CnaTests
#
# Exit status is 0 only when every shard exited 0 and none was killed. A shard killed by a signal
# -- which is what the low-memory reaper does -- is reported as KILLED and fails the run, because a
# runner that reported a partial pass as success would be the very defect this replaces.

set -u -o pipefail

TESTS_PER_SHARD=200
SHARDS=""
MAX_PARALLEL=1
FILTER=""
OUT=""
RESUME=0
STOP_ON_FAIL=0
ALLOW_LIVE_DISPLAY=0

usage() { sed -n '3,45p' "$0" >&2; exit 2; }

while [ $# -gt 0 ]; do
    case "$1" in
        --tests-per-shard) TESTS_PER_SHARD="$2"; shift 2 ;;
        --shards)          SHARDS="$2"; shift 2 ;;
        --max-parallel)    MAX_PARALLEL="$2"; shift 2 ;;
        --filter)          FILTER="$2"; shift 2 ;;
        --out)             OUT="$2"; shift 2 ;;
        --resume)          RESUME=1; shift ;;
        --keep-going)      STOP_ON_FAIL=0; shift ;;
        --stop-on-fail)    STOP_ON_FAIL=1; shift ;;
        --allow-live-display) ALLOW_LIVE_DISPLAY=1; shift ;;
        -h|--help)         usage ;;
        --)                shift; break ;;
        -*)                echo "run_gtest_bounded: unknown option $1" >&2; usage ;;
        *)                 break ;;
    esac
done

[ $# -ge 1 ] || usage
BINARY="$1"; shift
EXTRA_ARGS=("$@")

[ -x "$BINARY" ] || { echo "run_gtest_bounded: not an executable: $BINARY" >&2; exit 2; }
BINARY=$(cd "$(dirname "$BINARY")" && pwd)/$(basename "$BINARY")

# The live-desktop rule the whole test tree is built on (cmake/TestDisplayPolicy.cmake, GTI-0001).
# This runner opens no display of its own, so the only way it can reach the owner's desktop is by
# inheriting it -- which is exactly the mistake worth refusing by name.
if [ "$ALLOW_LIVE_DISPLAY" -eq 0 ]; then
    if [ "${DISPLAY:-}" = ":0" ] || [ "${DISPLAY:-}" = ":0.0" ] || \
       [ "${WAYLAND_DISPLAY:-}" = "wayland-0" ]; then
        echo "run_gtest_bounded: refusing -- DISPLAY='${DISPLAY:-}' WAYLAND_DISPLAY='${WAYLAND_DISPLAY:-}'" >&2
        echo "looks like the live desktop. Run this inside tools/platform/run_gpu_tests_private.sh," >&2
        echo "or pass --allow-live-display if a human really is watching on purpose." >&2
        exit 2
    fi
fi

if [ -z "$OUT" ]; then
    OUT=$(mktemp -d "${TMPDIR:-/tmp}/cna-bounded-XXXXXX") || exit 2
fi
mkdir -p "$OUT" || exit 2
OUT=$(cd "$OUT" && pwd)

# ---- enumerate, so the shard count is derived from the real inventory ------------------------
LIST_ARGS=(--gtest_list_tests)
[ -n "$FILTER" ] && LIST_ARGS+=("--gtest_filter=$FILTER")
TOTAL=$("$BINARY" "${LIST_ARGS[@]}" 2>/dev/null \
        | grep -cE '^  [A-Za-z0-9_]' || true)
if [ -z "$TOTAL" ] || [ "$TOTAL" -eq 0 ] 2>/dev/null; then
    echo "run_gtest_bounded: enumerated 0 tests (binary refused to list, or the filter matches nothing)" >&2
    exit 2
fi

if [ -z "$SHARDS" ]; then
    SHARDS=$(( (TOTAL + TESTS_PER_SHARD - 1) / TESTS_PER_SHARD ))
    [ "$SHARDS" -lt 1 ] && SHARDS=1
fi

echo "run_gtest_bounded: $(basename "$BINARY"), $TOTAL tests -> $SHARDS shard(s), $MAX_PARALLEL at a time" >&2
echo "run_gtest_bounded: renderer='${CNA_GRAPHICS_RENDERER:-<build default>}' DISPLAY='${DISPLAY:-}' WAYLAND_DISPLAY='${WAYLAND_DISPLAY:-}'" >&2
echo "run_gtest_bounded: output in $OUT" >&2

# ---- run, never more than MAX_PARALLEL at once -----------------------------------------------
PIDS=()
FAILED_EARLY=0

cleanup() {
    trap - EXIT INT TERM
    for p in "${PIDS[@]:-}"; do kill "$p" 2>/dev/null; done
    wait 2>/dev/null
}
trap 'echo "run_gtest_bounded: interrupted" >&2; cleanup; exit 130' INT TERM
trap cleanup EXIT

run_shard() {
    local i="$1"
    local log="$OUT/shard-$i.log"
    local xml="$OUT/shard-$i.xml"
    # Its own TMPDIR: gtest and the drivers below it write there, and a shared one is how two
    # concurrent shards corrupt each other's scratch files.
    local tmp="$OUT/tmp-$i"
    mkdir -p "$tmp"
    local args=(--gtest_output="xml:$xml")
    [ -n "$FILTER" ] && args+=("--gtest_filter=$FILTER")
    (
        export GTEST_TOTAL_SHARDS="$SHARDS" GTEST_SHARD_INDEX="$i" TMPDIR="$tmp"
        "$BINARY" "${args[@]}" "${EXTRA_ARGS[@]+"${EXTRA_ARGS[@]}"}"
    ) > "$log" 2>&1
    local status=$?
    echo "$status" > "$OUT/shard-$i.status"
    if [ "$status" -ge 128 ]; then
        echo "  shard $i: KILLED (signal $((status - 128))) -- see $log" >&2
    elif [ "$status" -ne 0 ]; then
        echo "  shard $i: exit $status" >&2
    fi
    return $status
}

for i in $(seq 0 $((SHARDS - 1))); do
    if [ "$RESUME" -eq 1 ] && [ -f "$OUT/shard-$i.status" ] && \
       [ "$(cat "$OUT/shard-$i.status")" = "0" ]; then
        echo "  shard $i: already completed, skipped (--resume)" >&2
        continue
    fi
    if [ "$STOP_ON_FAIL" -eq 1 ] && [ "$FAILED_EARLY" -ne 0 ]; then
        echo "  shard $i: not started (--stop-on-fail after a failure)" >&2
        continue
    fi

    # Block until a slot frees. `wait -n` returns when ANY child exits, which is what keeps the
    # process count at MAX_PARALLEL rather than at MAX_PARALLEL-per-batch.
    while [ "$(jobs -rp | wc -l)" -ge "$MAX_PARALLEL" ]; do
        wait -n 2>/dev/null || FAILED_EARLY=1
    done

    run_shard "$i" &
    PIDS+=("$!")
done

# Drain.
for p in "${PIDS[@]:-}"; do
    wait "$p" || FAILED_EARLY=1
done
trap - EXIT INT TERM

# ---- aggregate -------------------------------------------------------------------------------
# The XML is preferred because it is a count rather than a line of prose, and it survives a shard
# whose stdout was truncated. The text summary is the fallback for a shard that died before gtest
# could write its XML -- which is precisely the shard worth not losing.
python3 - "$OUT" "$SHARDS" <<'PY'
import glob, os, re, sys, xml.etree.ElementTree as ET

out, shards = sys.argv[1], int(sys.argv[2])
total = passed = failed = skipped = 0
killed = incomplete = 0
failing_names = []

for i in range(shards):
    xml = os.path.join(out, f"shard-{i}.xml")
    log = os.path.join(out, f"shard-{i}.log")
    st  = os.path.join(out, f"shard-{i}.status")
    status = None
    if os.path.exists(st):
        try: status = int(open(st).read().strip())
        except ValueError: status = None
    if status is None:
        continue
    if status >= 128:
        killed += 1

    counted = False
    if os.path.exists(xml):
        try:
            root = ET.parse(xml).getroot()
            # Counted per TESTCASE, not from the <testsuites> attributes. GoogleTest records a
            # GTEST_SKIP as result="skipped" on the case and puts no `skipped` attribute on the
            # root at all, so trusting the root reports every skip as a pass -- which is how the
            # first version of this aggregator turned 933/0/29 into a confident 962/0/0.
            t = f = sk = 0
            for case in root.iter("testcase"):
                t += 1
                name = f"{case.get('classname')}.{case.get('name')}"
                if case.find("failure") is not None or case.find("error") is not None:
                    f += 1; failing_names.append(name)
                elif case.get("result") == "skipped" or case.find("skipped") is not None \
                        or case.get("status") == "notrun":
                    sk += 1
            total += t; failed += f; skipped += sk; passed += t - f - sk
            counted = True
        except ET.ParseError:
            counted = False          # truncated XML: a killed shard's usual signature
    if not counted and os.path.exists(log):
        text = open(log, errors="ignore").read()
        def n(pat):
            m = re.search(pat, text)
            return int(m.group(1)) if m else 0
        p, f, sk = n(r"\[  PASSED  \] (\d+) test"), n(r"\[  FAILED  \] (\d+) test"), n(r"\[  SKIPPED \] (\d+) test")
        passed += p; failed += f; skipped += sk; total += p + f + sk
        failing_names += re.findall(r"^\[  FAILED  \] (\S+\.\S+)", text, re.M)
        if status != 0:
            incomplete += 1

print()
print(f"run_gtest_bounded: {passed} passed / {failed} failed / {skipped} skipped  "
      f"({total} counted across {shards} shard(s))")
if killed:
    print(f"run_gtest_bounded: {killed} shard(s) KILLED BY A SIGNAL -- the counts above are a "
          f"LOWER BOUND, not a result")
if incomplete:
    print(f"run_gtest_bounded: {incomplete} shard(s) ended without usable XML")
for name in sorted(set(failing_names)):
    print(f"  FAILED  {name}")
PY

if [ "$FAILED_EARLY" -ne 0 ]; then
    echo "run_gtest_bounded: at least one shard did not exit 0" >&2
    exit 1
fi
for i in $(seq 0 $((SHARDS - 1))); do
    [ -f "$OUT/shard-$i.status" ] || continue
    [ "$(cat "$OUT/shard-$i.status")" = "0" ] || exit 1
done
exit 0
