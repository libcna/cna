# Audit: tests/Microsoft/Xna/Framework/Media/Video/VideoTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Media/Video/VideoTests.cpp`
- Audit status: AUDITED (full read, 94 lines)
- Subsystem: `tests-xna-media` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Video` (confirmed genuine FNA implementation)
- Main related tests: N/A (this IS a test file); complements `VideoPlayerTests.cpp`

## Purpose
Covers `Video`'s two constructors (raw-file, which probes real file metadata; XNB-sourced 7-arg, which trusts caller-supplied metadata), `FromUriEXT`, `GetTypeName`, and `Video`'s own (no-op-until-attached) `SetAudioTrackEXT`/`SetVideoTrackEXT`.

## Executive Verdict
**PASS.** Correctly distinguishes the two constructors' fundamentally different contracts (probe-and-validate vs. trust-and-defer), matching FNA's own "wait until VideoPlayer tries to load this" design for the XNB path. No findings.

## Checklist Results
- `RawFileConstructorThrowsFileNotFoundExceptionForMissingFile` (MEDIA-44): confirms the raw-file constructor now throws a typed exception instead of silently leaving `width_`/`height_`/`duration_` at zero — a real behavioral upgrade correctly tested.
- `XnbConstructorDoesNotTouchTheFileAtConstructionTime` (line 41): the crucial complementary test — confirms the OTHER constructor does NOT throw for the exact same missing path, since it defers file access to `VideoPlayer`. This pairing (one constructor throws, the sibling doesn't, for the identical bad path) is a strong, deliberate contrast test.
- `RawFileConstructorProbesRealDimensionsAndFps` verifies real, non-placeholder probed values (width/height exact; FPS within a small tolerance; duration strictly positive) against the actual fixture file.
- `XnbConstructorUsesSuppliedMetadataVerbatim` confirms the XNB path uses EXACTLY the caller-supplied values, not re-probed ones — correctly differentiates "trusts metadata" from "ignores metadata."

## Detailed Findings
None.

## Cross-File Observations
- `SetAudioTrackEXTAndSetVideoTrackEXTDoNotThrowOrAffectOtherFields` (MEDIA-86) correctly scopes its assertions to what is OBSERVABLE at the `Video`-alone level (no attached `VideoPlayer`, so the calls only record a preference and forward-when-attached) — explicitly distinguishes this from `VideoPlayer`'s identically-named methods tested in `VideoPlayerTests.cpp`, avoiding a confusing false impression that the two are the same method under test in two files.
- Uses the same `chroma_420.mkv` fixture (160x90 @ 25fps) as `VideoPlayerTests.cpp`, keeping expected dimensions consistent across both files.

## Missing or Weak Tests
- No direct test of `Video`'s equality/`Dispose` semantics (if any exist on this class) — however, nothing in this file's own scope suggests these are missing FEATURES, only that this reviewer cannot confirm from the test file alone whether `Video` exposes `Equals`/`Dispose` as public XNA API surface; if it does, this would be a gap. Flagged as LOW pending a quick header check in a future pass, since this test-file-only review does not have `Video.hpp`'s full API surface in scope.

## Positive Findings
- The construction-throws vs. construction-does-not-throw contrast test (`RawFileConstructorThrowsFileNotFoundExceptionForMissingFile` / `XnbConstructorDoesNotTouchTheFileAtConstructionTime`) is an excellent example of testing a documented API-shape DIFFERENCE between two constructors of the same class, rather than assuming identical behavior.

## Final Assessment
No changes needed for the scope actually covered. Recommend a quick follow-up check of `Video.hpp`'s full public API (out of scope for this test-file-only audit) to confirm no untested public members (e.g. `Dispose`/equality, if present) were missed.
