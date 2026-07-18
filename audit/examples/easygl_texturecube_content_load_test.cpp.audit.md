# Audit: examples/easygl_texturecube_content_load_test.cpp

## Metadata

- Source file: `examples/easygl_texturecube_content_load_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-easygl` shard — Task 934, `ContentManager::Load<TextureCube>()` end-to-
  end via a real hand-encoded `.dds` cubemap file
- File type: hand-rolled `Game`-subclass executable, CTest-registered as
  `cna_test_easygl_texturecube_content_load` (`cmake/Tests/EasyGLTests.cmake:719-720`).
- XNA/FNA relevance: `ContentManager.Load<TextureCube>`, `TextureCube.GetData`, `CubeMapFace` — real
  XNA 4.0 API.
- Related production code: `TextureCubeTypeReader`
  (`src/Microsoft/Xna/Framework/Content/ContentManager.cpp:459-499`),
  `TextureCube::DDSFromStreamEXT` (`src/Microsoft/Xna/Framework/Graphics/TextureCube.cpp:251-348`).

## Purpose

Confirms a previously-missing capability: hand-builds a minimal, real, valid DXT1-compressed `.dds`
cubemap (6 faces, one 4×4 solid-color block per face), writes it to a temp file, loads it through
`Content.Load<TextureCube>("cube")`, and reads back every face's pixel data to verify it decoded to
the correct, distinct, non-cross-contaminated color per face.

## Executive Verdict

**Healthy.** The hand-encoded DDS header/pixel data was checked byte-for-byte against
`TextureCube::DDSFromStreamEXT`'s actual parsing logic and found correct in every field this audit
could verify (magic, header size, dimensions, cube-face caps bits, FourCC, per-face solid-block
encoding); the `ContentManager` code path it claims was previously missing (`TextureCubeTypeReader`)
is confirmed present and correctly wired today.

## Checklist Results

### Behavioral correctness
The hand-built DDS header (`BuildDxt1Cubemap`, lines 91-131) was cross-checked field-by-field against
`DDSFromStreamEXT`'s parser (`TextureCube.cpp` lines 265-308):
- Magic (`"DDS "`, offset 0), header size `124` (offset 4), `flags` including `DDSD_HEIGHT|
  DDSD_WIDTH` (offset 8) — all present and match the parser's own required-flags check.
- `height=4`, `width=4` (offsets 12/16) — match the parser's read offsets exactly.
- `mipMapCount=1` (offset 28) with `DDSCAPS_MIPMAP` deliberately *not* set in `dwCaps` — the parser
  forces `levels=1` whenever that cap bit is absent (`TextureCube.cpp` line 295-296) regardless of
  the header's own `mipMapCount` field, so this test's choice not to set the mipmap cap bit is the
  behaviorally-relevant one, and it correctly matches "no mip chain" as stated in the header comment.
- Pixel-format block (`dwSize=32`, `DDPF_FOURCC`, FourCC=`"DXT1"`) at the exact offsets
  (`formatSize`@76, `formatFlags`@80, `formatFourCC`@84) the parser reads.
- `dwCaps = DDSCAPS_TEXTURE|DDSCAPS_COMPLEX`, `dwCaps2 = DDSCAPS2_CUBEMAP|0xFC00` (all-faces bit) —
  the parser's `isCube` check (`(caps2 & kDdscaps2Cubemap) == kDdscaps2Cubemap`) only requires the
  cubemap bit itself, so the additional all-faces bits are correct-but-not-strictly-required filler,
  consistent with a real DDS cubemap's actual on-disk convention.
- Six `AppendSolidDxt1Block` calls, one per face, each `color0==color1==target color`, all 16 pixel
  indices `0b00` (always selects `color0`) — correctly sidesteps DXT1's two-mode ambiguity (4-color
  vs. 3-color+alpha interpolation, which only matters when `color0 != color1`) exactly as the file's
  own comment states, and this audit independently confirmed the mode-selection logic in
  `DxtUtil::DecompressDxt1` is irrelevant here since every index is `0`, referencing only `color0`.
- Face write order in `BuildDxt1Cubemap` (+X Red, -X Cyan, +Y Green, -Y Magenta, +Z Blue, -Z Yellow)
  matches `DDSFromStreamEXT`'s own face-iteration loop (`for (int face = 0; face < 6; ++face) ...
  static_cast<CubeMapFace>(face)`, `TextureCube.cpp` lines 319-345), which in turn matches
  `CubeMapFace`'s declaration order (`PositiveX=0, NegativeX=1, PositiveY=2, NegativeY=3, PositiveZ=4,
  NegativeZ=5`) — independently confirmed identical to FNA's own `CubeMapFace.cs` enum ordering.
- RGB565 packing (`ToRgb565`) round-trips exactly for every color used in this test (all channel
  values are either `0` or `255`, which map losslessly through 5/6-bit truncation-then-bit-replication
  RGB565↔RGB888 conversion) — the `tol=40` tolerance in `closeTo` (line 145) is generous but not
  load-bearing for these specific test colors; no precision-loss risk was actually exercised.

### Content pipeline / cross-file verification
Confirmed `TextureCubeTypeReader` is registered (`ContentManager.cpp` line 2784,
`RegisterTypeReader<Graphics::TextureCube>(...)`), maps the `.dds` extension (`GetExtensions()`
returns `{".dds"}`, line 462-465), and its `Read()` delegates straight to
`TextureCube::DDSFromStreamEXT` (line 476) — exactly the code path this test's header comment
describes as newly-added (Task 934, "DEFERRED.md item #14"). This audit did not find evidence this
reader existed before Task 934 (consistent with the comment's own claim); it is fully present and
correctly wired now.

### Robustness
`Initialize()` (which does all the real work, including `Exit()`, lines 153-195) runs entirely inside
`Game::Initialize()` rather than `Draw()` — an unusual but functionally correct choice for a test that
does no rendering of its own (it only exercises `Content.Load`/`GetData`, not a live draw), and
`Draw()` is correctly left as a no-op (line 197).

Temp file handling (lines 156-166): creates a uniquely-named directory under
`std::filesystem::temp_directory_path()` keyed by `reinterpret_cast<uintptr_t>(this)` (the `Game`
instance's own address) to avoid collisions between parallel test runs, writes the DDS bytes, and
loads via `ContentManager`. See Finding F1 for a gap in this area.

## Detailed Findings

No CRITICAL/HIGH findings.

### F1 — Temp directory/file is never cleaned up

- Severity: LOW
- Confidence: HIGH
- Category: robustness / maintainability
- Location/symbol: `Initialize()`, lines 157-166 (`std::filesystem::create_directories(root)`,
  `std::ofstream f(root / "cube.dds", ...)`)
- Evidence: no `std::filesystem::remove_all(root)` (or equivalent) call anywhere in this file, on any
  path (success or otherwise) — a scan of the whole file confirms no cleanup call exists.
- Why it matters: every CTest invocation of this test leaves a `cna_texturecube_content_load_test_
  <pointer-address>/cube.dds` directory behind under the system temp directory permanently. On a
  long-lived CI machine or repeated local test runs this accumulates small leftover files
  indefinitely — not a correctness bug in the test's own assertions, but a minor, avoidable resource
  leak on disk.
- FNA/XNA comparison: N/A (test-infrastructure hygiene, not XNA API behavior).
- Related files: none — self-contained fix would just add a cleanup call.
- Suggested future action (not implemented by this audit): wrap the temp directory in an RAII guard
  or add an explicit `std::filesystem::remove_all(root)` before `Exit()`.

## Cross-File Observations

- This is the only file in this batch that exercises the `Content`/`ContentManager` layer rather than
  going through `TextureCube`'s raw `SetData`/`GetData` or `DDSFromStreamEXT` directly — a genuinely
  different code path than its three sibling `easygl_texturecube_*` files in this same shard, and
  correctly does not duplicate their whole-face/mip/partial-rect coverage.
- `TextureCube` is confirmed move-only (copy constructor deleted per `TextureCube.hpp` line 41) — this
  test correctly receives it by value from `Load<TextureCube>()` (move-constructed), consistent with
  `ContentManager.cpp`'s own comment (line 2934) explaining why `TextureCube` doesn't need the
  `shared_ptr` wrapper some other content types require.

## Missing or Weak Tests

None significant — the test covers the specific previously-missing gap (Content.Load<TextureCube>)
end-to-end for all 6 faces. A DXT3/DXT5 cubemap load isn't covered here, but `DDSFromStreamEXT` itself
(which handles all three FourCCs) is exercised more broadly by other existing tests referenced in this
file's own header comment.

## Positive Findings

- The hand-encoded DDS bytes were verified field-by-field against the actual parser rather than
  assumed correct from the comment — every offset and flag this audit checked lines up.
- Correctly avoids DXT1's two-interpolation-mode ambiguity by using equal color endpoints with
  all-zero indices, and explains why in-line rather than leaving it as an unexplained magic choice.
- Exercises the real gap (a missing content-pipeline reader) rather than re-testing the already-proven
  underlying decoder, avoiding duplicate coverage with sibling tests.

## Final Assessment

A carefully-constructed, low-level-correct test that closes a genuine, previously-missing content-
pipeline gap; its only defect is a minor, non-functional temp-file cleanup omission (F1).
