# Fuzzing the CNA C API

A C ABI's most exposed surface is the one that reads bytes the caller chose. For this ABI that is a
short list, and every entry on it is behind one of two functions:

| Surface | Function | Reached from |
|---|---|---|
| UTF-8 text | `ValidateStringView`, `CopyStringView` | every route taking a `CNA_StringView` |
| Buffer arithmetic | `ValidateBuffer`, `CheckedElementByteCount` | every route taking an array and a count |

Both are in `modules/c-api/src/CnaCApiDetail.cpp`. Everything else the ABI accepts is a fixed-width
scalar, a versioned struct whose prefix rules are checked field by field, or a handle.

## Two ways of covering them, and why both exist

**The exhaustive sweep — `CApi_Utf8Oracle`, part of the normal test suite.**

Where the input space is small enough to enumerate, sampling it is a waste of a proof.
`tests/cpp/Utf8OracleTest.cpp` runs **every** byte sequence of length one, two and three —
16,843,008 cases, under both embedded-NUL policies — which is the entire space in which a UTF-8
scanner's interesting mistakes live: truncation, overlong forms, surrogates, out-of-range lead
bytes and stray continuations. Four-byte sequences are 4.3 billion, so that sweep becomes
structured rather than exhaustive: every lead byte crossed with the values where a decision
changes. Longer strings come from a seeded generator, so a failure is reproducible rather than
lucky.

It takes well under a second and needs no special toolchain, which is why it is a gate rather than
a campaign.

**The libFuzzer target — `tests/fuzz/StringViewFuzz.cpp`, run by hand.**

Coverage-guided fuzzing finds inputs nobody enumerated, at lengths and shapes the sweep does not
reach. It is not a ctest test for two reasons: it needs Clang, and it does not terminate. It is
compiled by the normal build anyway, as an object library nobody links, so it cannot rot silently
between the sessions when somebody actually goes hunting.

## The oracle

Neither is a crash-only test. Both judge the *answer*, not merely the absence of a signal, against
an independent implementation in `tests/support/CApiFuzzOracle.hpp` — and independent is the
operative word. The implementation decides UTF-8 by matching byte ranges and never forms a code
point; the oracle decodes the code point and applies the Unicode rules to the value. The
implementation asks whether a product *would* overflow by dividing; the oracle forms the whole
128-bit product from 32-bit limbs and looks at it. An oracle that mirrored the implementation's
structure would agree with its mistakes.

Verified to catch a real disagreement, not just to run: inverting the surrogate rule in the oracle
makes the sweep fail on `ED A0 80` — U+D800, the first surrogate — and makes the fuzz target abort
on the same bytes.

## Running the fuzzer

The target is standalone: it needs the C API's detail translation unit and the headers, and no CNA
module libraries at all.

```sh
SR=../sharp-runtime/modules
clang++ -std=c++23 -O1 -g -fsanitize=fuzzer,address \
  -I modules/c-api/include -I modules/c-api/src -I modules/c-api/tests/support \
  -I modules/core/include -I modules/graphics/include -I modules/storage/include \
  -I modules/content/include -I modules/net/include -I modules/gamer-services/include \
  -I modules/devices/include -I modules/audio/include -I modules/math/include \
  -I $SR/core/include -I $SR/runtime/include -I $SR/io/include -I $SR/collections/include \
  -I $SR/globalization/include -I $SR/diagnostics/include -I $SR/text/include \
  -I $SR/threading/include \
  modules/c-api/tests/fuzz/StringViewFuzz.cpp modules/c-api/src/CnaCApiDetail.cpp \
  -o build-probe/string_view_fuzz

mkdir -p build-probe/corpus
ASAN_OPTIONS=detect_leaks=0 ./build-probe/string_view_fuzz -max_len=64 build-probe/corpus
```

Build it into `build-probe/`, the repository's shared probe directory, and delete the binary once
the run's findings are written down — never into a session scratchpad or `/tmp`, per the
repository build rules.

`-runs=N` bounds a run; without it the fuzzer runs until stopped. A finding is written to a
`crash-*` file in the working directory and replayed by passing that file as the only argument.

## Reading a finding

The first byte of the input selects the embedded-NUL policy and the rest is the text, so one corpus
covers both policies. When the target aborts, the reproducer's remaining bytes are the string that
disagreed; the same bytes can be pasted into the sweep as a fixed case, which is the right place
for a regression to live once it is understood.
