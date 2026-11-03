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
which prevents the manual harness from silently rotting. It does not add libFuzzer instrumentation.

The same objects also link, with `CnaCApiDetail.cpp` compiled in, into the standalone executable
`cna_c_api_string_view_fuzzer` ([`string_view_main.cpp`](../../tools/fuzz/string_view_main.cpp)).
It needs no Clang: it speaks the seeded protocol of the scheduled long campaign
(`--seeded`, `--emit`, `--replay-one`; see "Scheduled long campaign" in
[`fx-bytecode-fuzzing.md`](../fx-bytecode-fuzzing.md)), mutates with UTF-8 boundary bytes and
64-bit edge values, caps inputs at 64 bytes and reports what the entry point reached (accepted and
rejected text, arithmetic checks, overflow verdicts). A disagreement still aborts in the entry
point. It is not a CTest test either.

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
example `-runs=100000`. The corpus directory is mutated in place, so start it from a copy of the
committed seeds below rather than from the repository directory. `-artifact_prefix` keeps
`crash-*`, `timeout-*` and other reproducers out of the repository root.

### Committed seeds

[`tests/fuzz/corpus/string-view/`](../../modules/c-api/tests/fuzz/corpus/string-view) holds ten
hand-specified inputs (MS-PL, SHA-256 pinned by `tools/safe_inputs_manifest.py`). Byte zero is the
policy byte (odd rejects embedded NUL); the 18-byte seeds carry the element count and size as
little-endian 64-bit values.

| File | Bytes (hex) | Covers |
|---|---|---|
| `empty-permit.bin` | `00` | empty text |
| `ascii-permit.bin` | `00 68656c6c6f` | ASCII |
| `embedded-nul-reject.bin` | `01 610062` | embedded NUL under the rejecting policy |
| `multibyte-permit.bin` | `00 c3a9 e282ac f09f9880` | two-, three- and four-byte sequences |
| `surrogate-permit.bin` | `00 eda080` | first surrogate |
| `overlong-reject.bin` | `01 c080 e08080` | overlong forms |
| `max-code-point-permit.bin` | `00 f48fbfbf f4908080` | U+10FFFF and one past it |
| `count-overflow-permit.bin` | `00` count `ffffffffffffffff` size `02` `78` | product overflow |
| `count-exact-reject.bin` | `01` count `03` size `5555555555555555` `79` | product exactly 2^64-1 |
| `size-zero-permit.bin` | `00` count `07` size `00` `7a` | zero element size |

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
- The weekly long campaign (`.github/workflows/robustness-long-fuzz.yml`) runs
  `cna_c_api_string_view_fuzzer` under ASan and UBSan for a measured budget from the committed
  seeds. It is seeded mutation, not coverage-guided; its summary records attempts and reached
  operations, which are not coverage.
- A libFuzzer campaign is manual; the repository currently specifies no minimum duration or
  coverage threshold for it.
- This target does not provide end-to-end fuzz evidence for public route wrappers or subsystem
  parsers. Their tests and fuzz campaigns must make their own, narrower claims.
