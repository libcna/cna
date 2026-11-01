# Shared byte-boundary fuzzing for the CNA C API

This page covers the C adapter's shared text, pointer/count and element-size validation. It does
not claim that one target fuzzes every parser reachable through the current C API. Compiled
effects, CNB/CNJ documents, images, DDS, WAV and other subsystem formats accept caller-controlled
bytes too; their semantic parsing and corpus evidence belong to those subsystems.

## Boundary under test

The shared boundary lives in
[`CnaCApiDetail.cpp`](../../modules/c-api/src/CnaCApiDetail.cpp):

| Input shape | Adapter functions | Contract measured here |
|---|---|---|
| Counted UTF-8 text | `ReadText` -> `CopyStringView` -> `ValidateStringView` | Pointer/length consistency, shortest-form UTF-8, Unicode range, surrogate rejection and the per-route embedded-NUL policy. |
| Pointer plus count | `ValidateBuffer` | A null pointer is valid only for a zero count. |
| Element count plus element size | `CheckedElementByteCount` | Pointer/count validation and overflow-safe conversion to `size_t`. |

Generated and handwritten routes normally enter the string path through `ReadText`, which records
a route-specific diagnostic around the lower-level verdict. The tests here call the lower-level
functions directly so millions of cases measure the shared decision rather than repeatedly
measuring an unrelated public route.

Handles, versioned structure semantics, object lifetime and subsystem decoding are outside this
target. Raw buffers whose contents are later parsed still receive the generic pointer/count check,
but that check says nothing about whether the bytes form a valid asset or protocol message.

## Deterministic CTest oracle

`cna_c_api_utf8_oracle_test` is registered as `CApi_Utf8Oracle` in every native test-enabled
configuration. It needs no fuzzing toolchain and terminates, so it is the automated gate. It checks:

- every one- and two-byte sequence under both embedded-NUL policies;
- every three-byte sequence under the policy that permits embedded NUL;
- a structured four-byte boundary matrix under both policies;
- two million reproducible generated strings, one to sixteen bytes long;
- agreement between validation and copying, including byte-exact accepted output;
- null-pointer/zero-length rules; and
- an edge-value matrix for count-times-element-size arithmetic.

The registered timeout is 900 seconds to leave headroom for sanitizer and slower configurations.
The test source is
[`Utf8OracleTest.cpp`](../../modules/c-api/tests/cpp/Utf8OracleTest.cpp).

The test-enabled native build also compiles
[`StringViewFuzz.cpp`](../../modules/c-api/tests/fuzz/StringViewFuzz.cpp) as the object-library
target `cna_c_api_fuzz_targets`. That compile uses the configured compiler and strict warning wall,
which prevents the manual harness from silently rotting. It does not add libFuzzer instrumentation,
link an executable or run a campaign.

## Independent oracle

Both drivers compare results with
[`CApiFuzzOracle.hpp`](../../modules/c-api/tests/support/CApiFuzzOracle.hpp); they are not merely
crash tests.

- The production UTF-8 validator decides from byte ranges. The oracle decodes each code point and
  then checks shortest form, the surrogate interval and the Unicode maximum.
- The production size check detects multiplication overflow by division. The oracle constructs the
  complete 128-bit product from 32-bit limbs and inspects its high and low halves.

These deliberately different algorithms are less likely to share the same defect. As a test of
the test, inverting the oracle's surrogate rule makes both drivers reject their comparison at
`ED A0 80`, the UTF-8 spelling of the first surrogate code point.

## Building the libFuzzer executable

The libFuzzer driver is manual: it requires Clang and has no natural completion point. Start from a
native test-enabled CMake tree and the same sibling `sharp-runtime` checkout used by that tree. The
following Linux command was verified with the repository's SDLGPU build; select another existing
native build directory by changing `CNA_FUZZ_BUILD`.

```sh
CNA_FUZZ_BUILD=cmake-build-sdlgpu
CNA_FUZZ_SHARP=../sharp-runtime
CNA_FUZZ_WORK="$CNA_FUZZ_BUILD/fuzz/c-api-string-view"

cmake --build "$CNA_FUZZ_BUILD" \
  --target cna_c_api_fuzz_targets cna_c_api_utf8_oracle_test \
  --parallel 8

mkdir -p "$CNA_FUZZ_WORK/corpus" "$CNA_FUZZ_WORK/findings"

clang++ -std=c++23 -O1 -g -fno-omit-frame-pointer \
  -fsanitize=fuzzer,address \
  -I modules/c-api/include \
  -I modules/c-api/src \
  -I modules/c-api/tests/support \
  -I modules/core/include \
  -I modules/graphics/include \
  -I modules/storage/include \
  -I modules/content/include \
  -I modules/net/include \
  -I modules/gamer-services/include \
  -I modules/devices/include \
  -I modules/audio/include \
  -I modules/math/include \
  -I modules/platform/include \
  -I "$CNA_FUZZ_SHARP/modules/core/include" \
  -I "$CNA_FUZZ_BUILD/SHARP_RUNTIME/generated/include" \
  -I "$CNA_FUZZ_SHARP/modules/runtime/include" \
  -I "$CNA_FUZZ_SHARP/modules/io/include" \
  -I "$CNA_FUZZ_SHARP/modules/collections/include" \
  -I "$CNA_FUZZ_SHARP/modules/uri/include" \
  -isystem "$CNA_FUZZ_SHARP/vendor" \
  modules/c-api/tests/fuzz/StringViewFuzz.cpp \
  modules/c-api/src/CnaCApiDetail.cpp \
  "$CNA_FUZZ_BUILD/SHARP_RUNTIME/libsharp_runtime_core.a" \
  -o "$CNA_FUZZ_WORK/string_view_fuzz"
```

The Sharp Runtime core archive supplies the out-of-line exception RTTI now required by
`CnaCApiDetail.cpp`; the original source-only command no longer links without it. Keep the
executable, corpus and findings inside the selected stable build tree. They are ignored build
artifacts and must not be committed.

## Running a campaign

```sh
ASAN_OPTIONS=detect_leaks=0 \
  "$CNA_FUZZ_WORK/string_view_fuzz" \
  -max_len=64 \
  -artifact_prefix="$CNA_FUZZ_WORK/findings/" \
  "$CNA_FUZZ_WORK/corpus"
```

Without `-runs=N`, libFuzzer continues until interrupted. Add a bound for a finite check, for
example `-runs=100000`. The corpus directory starts empty and is mutated in place; no seed corpus
is currently committed. `-artifact_prefix` keeps `crash-*`, `timeout-*` and other reproducers out of
the repository root.

The harness interprets input as follows:

- byte zero selects whether embedded NUL is rejected;
- the remaining bytes are the `CNA_StringView` payload; and
- for inputs at least 17 bytes long, bytes 1-8 and 9-16 are also copied into the element count and
  element size used by the arithmetic check.

An accepted string must copy byte-for-byte, including an embedded NUL when the selected policy
allows it. A wrong verdict, changed output or arithmetic disagreement calls `abort`, so libFuzzer
writes a reproducer just as it would for an AddressSanitizer failure.

Replay a finding by passing its path to the same executable. Once understood, add the smallest
fixed regression to `Utf8OracleTest.cpp` or the relevant subsystem test; a transient corpus entry
is not a substitute for a named regression.

## Evidence boundary

- `CApi_Utf8Oracle` is the continuously repeatable gate.
- `cna_c_api_fuzz_targets` proves only that the manual driver still compiles in a native
  test-enabled build.
- A libFuzzer campaign is manual; the repository currently specifies no minimum duration,
  coverage threshold or continuously maintained corpus for it.
- This target does not provide end-to-end fuzz evidence for public route wrappers or subsystem
  parsers. Their tests and fuzz campaigns must make their own, narrower claims.
