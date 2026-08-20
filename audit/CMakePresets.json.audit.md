# Audit: CMakePresets.json

## Metadata
- Source file: `CMakePresets.json` (105 lines, repo root)
- Audit status: AUDITED (full read)
- Subsystem: `build-root` shard
- File type: CMake presets configuration (JSON)
- XNA/FNA relevance: N/A — build infrastructure
- Main related tests: the `devices-ubsan` preset is the one `.github/workflows/devices-tests.yml`
  actually uses in CI

## Purpose
Defines 5 named configure presets (`web`, `devices-asan`, `devices-tsan`, `devices-ubsan`, `tests`)
and 5 matching build presets, each a documented recipe for a specific development/verification
workflow (Emscripten web build, 3 sanitizer variants for `Microsoft::Devices` hardening, and a
general native desktop test build).

## Executive Verdict
Correct and thoroughly self-documented. Each preset's `description` field doubles as real
engineering documentation — citing the specific plan/task ID that verified it works
(`plans/plan_devices_phase8.md Task P8-4`), what each sanitizer does and does NOT catch (ASan: "does not
detect data races; use devices-tsan for that"), and — most valuably — the ONE known, disclosed,
out-of-scope finding from the last real run of each sanitizer preset (`devices-tsan`'s description
states it reports exactly one pre-existing, unrelated race in `sharp-runtime`'s `TimeSpan` copy
constructor debug counter, and that this is out of scope for `Microsoft::Devices` work).

## Checklist Results
- `devices-ubsan`'s description states the full Devices-only suite "reports zero issues under this
  preset" — this is a specific, falsifiable claim about a sanitizer run's actual historical outcome,
  not a vague assurance.
- The `tests` preset's description (lines 62) is unusually detailed and operationally important: it
  explicitly warns that running the suite via `ctest` (rather than the `CnaTests` binary directly)
  both spuriously reports unbuilt display-dependent graphics smoke-test executables as failed AND
  races several tests sharing hardcoded `/tmp` fixture paths across processes — and correctly
  attributes this to a `ctest`-invocation artifact, not a real bug in the tests themselves,
  citing `plans/plan_audio.md P9-BUILD-007` for the full rationale. This is exactly the kind of "known
  false-negative source" documentation that prevents a future contributor from chasing a phantom
  test failure.
- Every sanitizer preset correctly sets both `CMAKE_CXX_FLAGS` (`-fsanitize=...
  -fno-omit-frame-pointer -g -O0`/`-O1`) AND the matching `CMAKE_EXE_LINKER_FLAGS`
  (`-fsanitize=...`) — a common real-world mistake is setting only the compile flag and forgetting
  the linker flag, which silently produces an uninstrumented binary; this preset avoids that.
- `devices-tsan` correctly uses `-O1` (not `-O0`) — consistent with ThreadSanitizer's own
  documented recommendation that `-O0` can be prohibitively slow/change timing enough to mask races,
  while `devices-asan`/`devices-ubsan` use `-O0`/`-O1` respectively without needing the same
  consideration to the same degree.

## Detailed Findings
None.

## Cross-File Observations
`devices-ubsan`'s `binaryDir` (`cmake-build-devices-ubsan`) matches exactly the path
`.github/workflows/devices-tests.yml` invokes directly
(`./cmake-build-devices-ubsan/CnaTests --gtest_filter=...`) — cross-verified consistent between the
two files.

## Missing or Weak Tests
Not applicable to a presets configuration file.

## Positive Findings
The sanitizer presets' descriptions documenting known, disclosed, out-of-scope findings from real
historical runs (the `sharp-runtime TimeSpan` race, the zero-issues UBSan claim) are a genuinely
valuable and unusually rigorous documentation practice for a CMake presets file.

## Final Assessment
No findings.
