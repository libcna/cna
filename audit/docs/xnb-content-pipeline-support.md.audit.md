# Audit: docs/xnb-content-pipeline-support.md

## Metadata
- Source file: `docs/xnb-content-pipeline-support.md` (224 lines)
- Audit status: AUDITED (full read)
- Subsystem: `docs` shard
- File type: Markdown documentation (closes `plans/plan_xnb.md` Phase G, XNB-45)
- Cross-references: `tests-xna-content`/`xna-content` shard audits (confirmed the test suite misses
  `ContentReader::ReadExternalReference<T>()`'s absolute-path-escape HIGH finding; confirmed
  `CnjCustomLoaderTests.cpp` surfaces raw `std::` exceptions in `RegisterCnjLoader<T>()`'s guards);
  `tests-cna-internal` shard audit (confirmed `LzxDecoderFuzzTests.cpp`/`XnbContainerFuzzTests.cpp`
  genuinely adversarial)

## Purpose
Documents CNA's real, binary-`.xnb`-compatible content loader: supported readers, generic
collections, custom-reader registration, `ReflectiveReader<T>` non-support, compression/platform
support matrices, the audio support matrix, and malformed-input hardening history.

## Executive Verdict
An exceptionally rigorous, evidence-grounded document — every claim of support or non-support is
tied to a specific test, task ID, or (notably) a real third-party `.xnb` fixture (`prime31/Nez`,
`openeggbert/speedyblupi.com`) rather than only synthetic test data. No overclaiming found; several
claims are independently corroborated by this audit's own review of the related test shards.

## Checklist Results
- The "Malformed/adversarial-file hardening" section's account of `XnbContainerFuzzTests.cpp`'s
  1500-iteration whole-container mutation fuzzer, and the specific heap-buffer-overflow it found and
  fixed in `VertexBufferReader`/`IndexBufferReader`'s raw-byte-blob reads, is independently
  corroborated by the `tests-cna-internal` shard audit (completed earlier in this session), which
  confirmed this exact fuzzer hard-fails on `std::bad_alloc` specifically — "one of the sharpest
  fuzz-harness design choices found in this audit."
- The doc's claim that `ReflectiveReader<T>` is unsupported "by design" (not a silent gap) is
  precisely and honestly framed — the doc explicitly states what a game author must do instead
  (give the type an explicit `ContentTypeWriter`/reader pair) rather than leaving the limitation as a
  dead end.
- The "Real-world confirmation" callouts (a real `prime31/Nez` custom reader, a real MacOSX-platform
  fixture, a real WebAssembly-platform rejection) are a genuinely stronger verification standard than
  synthetic test data alone — confirming the extension points and platform-acceptance policy against
  content this project did not author itself.
- Cross-checked the doc's claim that `ContentTypeReaderManager::AddTypeCreator()` is "already
  public/`NOXNA`" — consistent with the "Custom readers" code sample shown, which calls it directly
  without any friend/internal-access workaround.

## Detailed Findings

### LOW — Doc does not mention the confirmed `ContentReader::ReadExternalReference<T>()` absolute-path-escape HIGH finding
This document is the natural place a reader would look for `ContentReader`'s known gaps (it
extensively documents `ContentReader`-level hardening elsewhere — `ReadBytesExactOrThrow()`,
`CheckCollectionElementCount()`, `BinaryReader::ReadString()`/`ReadBytes(int)` allocation-bomb
hardening), but never mentions the separately-confirmed HIGH finding that
`ReadExternalReference<T>()` can be made to escape the content root via an absolute path, and that
the current test suite only exercises relative `..`-style escapes, missing the absolute-path variant
entirely (confirmed in the `tests-xna-content` shard audit). This is a real coverage gap in an
otherwise very thorough hardening narrative — not a false claim, since the doc simply doesn't discuss
`ReadExternalReference` at all, but a notable omission given how much other `ContentReader` hardening
IS documented here.

## Cross-File Observations
Strongly corroborates the `tests-cna-internal` shard audit's independent characterization of
`XnbContainerFuzzTests.cpp` as exceptionally rigorous fuzz-harness engineering — this doc's own
account (written from the implementation side, describing what the fuzzer found and how it was
fixed) and the test-shard audit's account (written from the test-quality side, describing the
harness's design choices) are mutually consistent, a genuine convergent confirmation from two
independent audit angles.

## Missing or Weak Tests
N/A for a documentation file — see Detailed Findings for the one real coverage-disclosure gap
identified (`ReadExternalReference` absolute-path-escape).

## Positive Findings
The "Real-world confirmation" pattern (verifying claims against real, independently-produced
third-party `.xnb` content rather than only hand-built fixtures) is one of the strongest verification
practices found anywhere in this audit's documentation review — it substantiates platform-acceptance
and custom-reader-extensibility claims against evidence this project had no ability to shape to fit
its own tests.

## Final Assessment
One LOW finding: this otherwise exceptionally thorough hardening narrative never mentions the
confirmed `ReadExternalReference<T>()` absolute-path-escape gap. No overclaiming found anywhere else;
several specific technical claims independently corroborated by this session's own test-shard audits.
