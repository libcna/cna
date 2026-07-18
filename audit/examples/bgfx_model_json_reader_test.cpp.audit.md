# Audit: examples/bgfx_model_json_reader_test.cpp

## Metadata

- Source file: `examples/bgfx_model_json_reader_test.cpp` (234 lines)
- Audit status: AUDITED — **runtime-verified** (built and executed in this sandbox; see
  "Runtime Verification" below, an exception to this shard's usual static-only depth because the
  file's own header made a specific, checkable, high-value claim about current pass/fail status)
- Subsystem: `examples-tests-bgfx` shard — `Content.Load<Model>()` vertex-stride/decoding pixel
  test (Task 927), CTest-registered as `Bgfx_ModelJsonReader_Quad`
  (`cmake/Tests/BgfxTests.cmake:847-850`, no `WILL_FAIL`/skip property set).
- XNA/FNA relevance: direct — `ContentManager.Load<Model>()`, `ModelMeshPart`/`VertexBuffer`
  decoding from a content descriptor, `Model::Draw` → `Effect`-bound indexed draw dispatch.
- FNA reference: N/A for the `.cnj` descriptor format itself (a CNA-only content pipeline, not
  XNA's binary `.xnb`), but the rendered-output expectation (`BasicEffect`'s default
  `DiffuseColor` = white, `VertexColorEnabled=false` for `VertexPositionNormalTexture`) is a real
  XNA/FNA `BasicEffect` semantic.
- Related production code: `src/Microsoft/Xna/Framework/Content/ContentManager.cpp`
  (`ModelTypeReader::Read()`, `BuildVertexBufferFromRawBytes()` lines 1620-1689),
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`
  (`DrawIndexedPrimitivesEx()` lines 2864-2986, `DrawIndexedColoredPrimitives()` lines 2319-...).

## Purpose

Verifies `Content.Load<Model>()` correctly decodes a real `.cnj` (CNA content JSON) descriptor +
binary vertex/index sidecar files into a working `Model` on Bgfx: writes a synthetic stride-32
(`VertexPositionNormalTexture`) quad's raw vertex/index bytes plus a `.cnj` descriptor to a temp
directory at `Initialize()` time, then loads and draws it via `Content.Load<Model>()` +
`Model::Draw()`, sampling one pixel inside the quad (expected White, `BasicEffect`'s default
diffuse) and one outside (expected the Blue clear colour). This is a Bgfx-specific restructuring
(per-check `RunCheck()` passes, Task-406 workaround) of `examples/easygl_model_json_reader_test.cpp`
(Task 927), which itself documents fixing a real bug: `ModelTypeReader::Read()` used to compare
the descriptor's declared `vertexStride` against `sizeof(...)` of CNA's own vtable-inflated vertex
structs directly, then `reinterpret_cast` the tightly-packed file bytes as an array of those
inflated structs — silently reading every vertex's fields from the wrong byte offset.

## Executive Verdict

**Needs attention** — the file's own header comment describes a second, *separate*,
"KNOWN, PRE-EXISTING… Bgfx limitation" (the "centre" check reading black instead of White, caused
by `BgfxGraphicsBackend` allegedly never overriding `DrawIndexedPrimitivesEx`) and frames it as
still-current, with the test's own stated exit-code contract explicitly anticipating a FAIL on
this specific check. **This audit built the actual CTest-registered binary from the current
working tree and ran it under Xvfb**: it reports **2/2 PASS, exit code 0** — the centre pixel
reads `(255,255,255)` White, not black. The claimed limitation is stale: `BgfxGraphicsBackend::
DrawIndexedPrimitivesEx` **is** overridden today (added by `feat(Task 948): add
BgfxGraphicsBackend::DrawIndexedPrimitivesEx override`, commit `d3a2d0a0`/`fb245962`, same day as
this test file's own authoring commit but ~3.5 hours later) and correctly threads full
`GpuDrawParams` (including `diffuseColor`) through indexed Effect-bound draws. The Task 927
vertex-stride fix this file's *primary* assertion depends on was independently confirmed correct
in the current source. No functional defect exists in either the test or the production code it
exercises — only the file's own header narrative is out of date.

## Checklist Results

### API / XNA / FNA parity
`getContentProperty().Load<Model>("quad")` (line 122), `getContentProperty().
setRootDirectoryProperty(root.string())` (line 189) match FNA's `ContentManager.Load<T>`/
`RootDirectory` surface in name and shape (the `.cnj` descriptor format itself is a documented
`NOXNA` CNA extension, not a claim of XNB parity).

### Behavioral correctness
**Task 927 vertex-stride fix** — independently confirmed correct by reading
`BuildVertexBufferFromRawBytes()` (`ContentManager.cpp` lines 1620-1689): for `stride == 32` (this
test's exact case), each vertex is built field-by-field via `memcpy`-based `readVec3`/`readVec2`
helpers at the correct byte offsets (`readVec3(o)`, `readVec3(o+12)`, `readVec2(o+24)`) into a
properly-constructed `VertexPositionNormalTexture`, **not** a `reinterpret_cast` of the raw file
bytes onto CNA's own (vtable-inflated) struct layout — exactly the fix the file's header claims,
confirmed present and correct in the current tree, independent of the runtime check below.

**"Centre reads black" claim — confirmed STALE, not current behavior.** See "Runtime
Verification" below for the direct build-and-run confirmation; the underlying reason is
`BgfxGraphicsBackend::DrawIndexedPrimitivesEx` (`BgfxGraphicsBackend.cpp` lines 2864-2986) is a
real, fully-wired `override` (confirmed present in `BgfxGraphicsBackend.hpp` line 638, `override`
keyword and matching signature to `IGraphicsBackend`'s virtual) that threads
`params.diffuseColor`/texture/fog through indexed draws exactly like `DrawPrimitivesEx`'s own
long-established non-indexed path — **not** falling back to `DrawIndexedColoredPrimitives` (the
function that *would* produce the observed black, since it unconditionally forces
`vertexColorEn3DUnif_=1`, reading a non-existent `a_color0` attribute on this colour-less
`VertexPositionNormalTexture` mesh, confirmed at `BgfxGraphicsBackend.cpp` lines 2319-2356).

### Logic
`RunCheck`'s per-iteration full `Clear()`+`Load<Model>()`+`Draw()`+read cycle (lines 104-133)
correctly applies this shard's Task-406 workaround; the file deliberately omits the usual
"break on first non-background pixel" early-exit (explained in its own comment, lines 108-113,
since Blue-vs-White-vs-Black are all mutually distinguishable here) — a reasonable,
well-justified deviation from the shard's more common idiom.

### C++ correctness
`WriteFile`/`AppendFloat`/`AppendUint16` (lines 66-84) are straightforward binary-fixture-writing
helpers. **Building this file with the project's real compiler/flags surfaced a genuine GCC
`-Wstringop-overflow=` warning** at `AppendUint16`'s `out.insert(out.end(), bytes, bytes + 2)`
call (line 83) when inlined into `Initialize()`'s `for (std::uint16_t i : {0,1,2,0,2,3})
AppendUint16(idxBytes, i);` loop (line 168-169) — see F2.

### Robustness
`IsWhite`/`IsBackground` (lines 95-102) use asymmetric thresholds appropriate to their respective
target colours (`>=200` per channel for White, `<=30` R/G with `>=200` B for the Blue background)
— correctly distinguishes both expected outcomes from each other and from the "wrong colour"
black failure mode this file's header specifically anticipates.

### Testing
Two checks (centre=White, outside=Blue) are the right pair to distinguish "vertex position/size
wrong" (quad renders somewhere else or at the wrong scale, both checks could shift) from "vertex
colour/shading wrong" (centre reads something other than White while outside stays correctly
Blue) — and this audit's runtime run confirms both currently PASS.

## Detailed Findings

### F1 — File header's "KNOWN, PRE-EXISTING… Bgfx limitation" (centre check expected to fail) is stale; the underlying bug (Task 948) was fixed the same day, and the test currently passes 2/2 at runtime

- Severity: MEDIUM
- Confidence: HIGH (runtime-verified: built the actual CTest-registered target from the current
  working tree and executed it)
- Category: stale-comment / test-documentation-accuracy
- Location/symbol: file header comment, lines 16-38, specifically: *"the 'centre' check below
  currently reads (0,0,0) -- black -- instead of the expected White… tracked as a new, separately-
  scoped Task 948… not fixed here"* and *"Exit code 0 = both PASS, 1 = either FAIL (the 'centre'
  check is EXPECTED to currently fail on Bgfx specifically…)"*
- Evidence:
  1. `git log --reverse -- examples/bgfx_model_json_reader_test.cpp` shows this file was
     authored in commit `02469860` (`fix(Task 927)…`, 2026-07-10 14:03:03), and has since only
     received two purely mechanical renames (`.cnb`→migration `2ec13064`, `.cnj` rename
     `19effe28`) — neither diff touches the header comment's narrative text (confirmed via
     `git show` on both commits).
  2. `git log --oneline --all -S "DrawIndexedPrimitivesEx" -- include/CNA/Internal/Backends/
     Bgfx/BgfxGraphicsBackend.hpp` shows `feat(Task 948): add BgfxGraphicsBackend::
     DrawIndexedPrimitivesEx override` (`d3a2d0a0`/`fb245962`), dated 2026-07-10 17:30:54 — the
     same calendar day as this test's authoring, ~3.5 hours later.
  3. Read `BgfxGraphicsBackend.cpp` lines 2864-2986: `DrawIndexedPrimitivesEx` is a complete,
     `override`-keyword-declared implementation that mirrors `DrawPrimitivesEx`'s full
     `GpuDrawParams` dispatch (diffuse colour, texture, dual-texture, alpha test, fog), not a
     stub and not falling back to `DrawIndexedColoredPrimitives`.
  4. **Directly built and ran the exact registered CTest binary** from the current working tree
     (`cmake -DCNA_GRAPHICS_BACKEND=BGFX`, target `cna_test_bgfx_model_json_reader`, run under
     Xvfb with `SDL_VIDEODRIVER=x11`):
     ```
     [PASS] centre (inside quad): (255,255,255) expected White
     [PASS] outside quad bounds: (0,0,255) expected Blue background
     === 2/2 PASS ===
     EXIT CODE: 0
     ```
     — directly contradicting the header's claim that centre currently reads black and that
     exit code 1 (one check failing) is the expected/tracked outcome.
- Why it matters: as currently written, the file's header actively misleads a future maintainer
  into believing (a) there is a known, unfixed, tracked Bgfx rendering gap (Task 948) affecting
  this exact code path, when Task 948 has been complete since the same day this file was
  authored, and (b) that a FAIL result from this specific CTest is an accepted, expected outcome
  rather than a regression signal — which could cause a *real* future regression in this path
  (e.g. someone breaking `DrawIndexedPrimitivesEx`'s `GpuDrawParams` dispatch again) to be
  wrongly dismissed as "the known Task 948 gap" instead of investigated as new breakage, exactly
  backwards from the intended safety net a CTest is supposed to provide.
- FNA/XNA comparison: N/A — this is a documentation-accuracy issue in a test's own header, not an
  XNA/FNA behavior question; the underlying `BasicEffect` diffuse-colour behavior this test
  verifies (a colour-less mesh rendering at `DiffuseColor`'s default, White) is correct XNA
  semantics and is what is now actually observed.
- Related files: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp`/`.hpp` (Task 948's
  actual fix, outside this batch's direct scope but load-bearing for this finding);
  `examples/easygl_model_json_reader_test.cpp`/`examples/vulkan_model_json_reader_test.cpp` per
  the header's own claim of being "independently verified correct… both 2/2 PASS" on those
  backends — this audit did not re-verify those two files' current pass status, only Bgfx's.
- Suggested future action (not implemented by this audit — this is an audit-only pass): remove or
  rewrite the "KNOWN, PRE-EXISTING… Bgfx limitation"/Task 948 paragraph (lines 16-38) to reflect
  that Task 948 is complete and this check now passes; update the exit-code contract comment
  (line 36-38) to state plainly "Exit code 0 = both PASS, 1 = either FAIL" without the "centre is
  EXPECTED to currently fail" caveat.

### F2 — GCC `-Wstringop-overflow=` warning on `AppendUint16`'s `vector::insert` call (observed during this audit's real build; likely a compiler false positive, not independently proven benign)

- Severity: LOW
- Confidence: MEDIUM (the warning was genuinely produced by a real build of this exact file with
  this project's actual compiler/settings; GCC's `-Wstringop-overflow` is well-known to produce
  false positives on inlined small fixed-size-array `vector::insert` calls, but this audit did not
  construct an independent proof — e.g. ASan/UBSan run — that no real overflow occurs here)
- Category: C++ correctness / build-warning-hygiene
- Location/symbol: `AppendUint16()` (lines 79-84), called from `Initialize()`'s index-writing loop
  (line 168-169)
- Evidence: building `cna_test_bgfx_model_json_reader` in Release configuration produced:
  ```
  warning: writing 2 bytes into a region of size 0 [-Wstringop-overflow=]
    inlined from 'void {anonymous}::AppendUint16(std::vector<unsigned char>&, uint16_t)' at
    .../examples/bgfx_model_json_reader_test.cpp:83:19
  ```
  with GCC's diagnostic attributing the "region of size 0" to the `bytes[2]` local array as seen
  through several levels of `std::vector::insert`/`std::copy`/`std::uninitialized_copy` inlining.
- Why it matters: `bytes` is a genuine 2-element stack array and the insert range
  (`bytes, bytes+2`) is correct — this reads as GCC's static analysis losing track of the array's
  real size through the inlining chain (a known class of `-Wstringop-overflow` false positive on
  `vector::insert` with pointer-pair iterators into a small local array), not an actual
  out-of-bounds write; this audit did not, however, run ASan on this specific binary to
  independently confirm no real overflow occurs, so this is reported as an open, low-confidence
  observation rather than a settled false-positive dismissal.
- FNA/XNA comparison: N/A.
- Related files: none — self-contained to this file's two small serialization helpers.
- Suggested future action (not implemented by this audit): if this warning is otherwise silenced
  project-wide, no action needed; otherwise, consider `std::array<std::uint8_t,2>` +
  `out.insert(out.end(), arr.begin(), arr.end())` or an ASan run to settle whether this is a true
  false positive.

## Cross-File Observations

- This is the one file in this batch where an actively-maintained "known limitation" narrative in
  a test's own header turned out to be stale rather than current — a different failure mode from
  the sibling `easygl_basiceffect_specular_test.cpp` example precedent (where a numeric constant
  was stale but the surrounding narrative was self-aware about it); here the surrounding narrative
  itself is what needs correcting, and the test's actual behavior (2/2 pass) is already right.
- Reinforces this audit's general finding (per the task's own instruction) that "known bug"/
  "currently broken" claims in comments must be independently re-verified against current code and
  git history, not trusted at face value — this file is a concrete, runtime-confirmed instance of
  exactly that risk materializing in this shard.

## Missing or Weak Tests

None beyond F1's documentation-accuracy issue — the test's actual assertions are sound and, per
this audit's runtime run, both currently pass for the right reason (Task 927's stride fix and
Task 948's `GpuDrawParams` dispatch fix both independently confirmed present and correct).

## Positive Findings

- The Task 927 vertex-stride fix this test's primary assertion depends on was independently
  confirmed correct via direct inspection of `BuildVertexBufferFromRawBytes()`'s stride-32 branch.
- This audit's runtime build-and-run (the exercise that surfaced F1) also positively confirms the
  full `Content.Load<Model>()` → `ModelTypeReader::Read()` → `BuildVertexBufferFromRawBytes()` →
  `Model::Draw()` → `BgfxGraphicsBackend::DrawIndexedPrimitivesEx()` pipeline genuinely works
  end-to-end on Bgfx today, which is a stronger, more current statement about this subsystem's
  health than the file's own (stale) header comment gives it credit for.

## Final Assessment

The test's actual assertions are correct and, per a real build-and-run performed by this audit,
both currently pass (2/2, exit code 0) — a materially better result than the file's own header
comment claims. The one substantive issue (F1, MEDIUM) is that the header's "known limitation"/
Task 948 narrative is stale and should be corrected so a future regression in this exact code path
is not wrongly dismissed as still-expected behavior. A minor, unresolved compiler-warning
observation (F2, LOW) was also recorded from the real build.

## Runtime Verification

Performed as part of this audit (exception to this shard's usual static-only depth, justified by
the file's own header making a specific, high-value, checkable claim about current pass/fail
status):

```
$ cmake -S . -B <scratch-build> -DCNA_GRAPHICS_BACKEND=BGFX -DCMAKE_BUILD_TYPE=Release
$ cmake --build <scratch-build> --target cna_test_bgfx_model_json_reader -j16
$ SDL_VIDEODRIVER=x11 DISPLAY=:77 <scratch-build>/cna_test_bgfx_model_json_reader
[WindowDebug] after SDL_CreateWindow: flags=0x622 borderless=false fullscreen=false
BGFX backend requested renderer: OpenGL, active renderer: OpenGL 2.1
[PASS] centre (inside quad): (255,255,255) expected White
[PASS] outside quad bounds: (0,0,255) expected Blue background
=== 2/2 PASS ===
EXIT CODE: 0
```

(Xvfb `:77`, software Mesa/llvmpipe OpenGL 2.1 — same class of sandbox environment this shard's
other files describe elsewhere.) `third_party/SDL`, `third_party/SDL_image`,
`third_party/SDL_mixer`, and `vendor/googletest` git submodules were initialized non-recursively
as a build prerequisite (matching this project's own documented convention); no tracked source
file under audit was modified to perform this verification.
