# Audit: examples/sdlgpu_texturecube_test.cpp

## Metadata

- Source file: `examples/sdlgpu_texturecube_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — plain, non-render-target `TextureCube` proof for the
  SDL_GPU backend (plans/plan_sdlgpu.md SDLGPU-51)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_sdlgpu_test(cna_test_sdlgpu_texturecube …)` / `cna_register_backend_test(NAME
  SdlGpu_TextureCube …)`, `cmake/Tests/SdlGpuTests.cmake:65-68`, `TIMEOUT 60`).
- XNA/FNA relevance: direct — `TextureCube` constructor, `CubeMapFace` enum,
  `TextureCube.SetData<T>(face, data, elementCount)`/`(face, level, rect, data, startIndex,
  elementCount)`/`GetData` overload set.
- FNA reference: `Graphics/TextureCube.cs`, `Graphics/CubeMapFace.cs` (not separately re-read this
  batch; enum ordering cross-checked against CNA's own `CubeMapFace.hpp`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/TextureCube.cpp` (`SetData`/
  `GetData`, lines 125-212), `src/CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.cpp`
  (`SdlGpuTextureCubeBackend` constructor/`SetData`/`GetData`, lines 4082-4238).

## Purpose

Three-check byte-exact round-trip proof for a real `SDL_GPU_TEXTURETYPE_CUBE` texture
(sampler-only, never a render target): (A) each of the 6 faces uploaded with a distinct solid color
reads back byte-exact, **and** face 0 is re-verified last (after all 5 other faces were written) to
prove sequential per-face `SetData()` calls accumulate rather than orphaning earlier faces' writes.
(B) mipMap "generated case": a uniform-color full level-0 upload on one face auto-generates level 1
for that face. (C) mipMap "authored mip data" case: explicit data written directly to level 1 of a
face is not clobbered by the level-0 auto-generation that already ran, and that face's level 0
remains intact afterward.

## Executive Verdict

**Needs attention.** Checks A/C are strong, content-discriminating, and their underlying backend
code paths were independently verified correct — the specific `cycle=false` orphan-write regression
this file's header comment cites as SDLGPU-40's finding is genuinely still fixed, not stale. Check A
in particular is a real regression guard for the exact scenario a naive per-face cube upload
implementation gets wrong. However, tracing the mip-regeneration trigger this file's Check B/C
depend on surfaced a real, untested cross-face correctness/performance issue in the production
backend (F1: a full-level-0 upload on *any one* face regenerates mip chains for *all six* faces,
including faces whose own level-0 data may not yet be valid) — not exercised by this file since
every check here happens to write its touched face's full level 0 before that face's own mip check,
masking the issue exactly the way this project's own cross-cutting findings describe for other
recurring bugs.

## Checklist Results

### API / XNA / FNA parity

`TextureCube(dev, 4, false, SurfaceFormat::Color)`/`(dev, 8, true, SurfaceFormat::Color)` (lines 92,
118, 132) matches FNA's `TextureCube(GraphicsDevice, int size, bool mipMap, SurfaceFormat)`
constructor exactly. `tex.SetData(face, data, count)` (line 97) matches the 3-arg FNA overload;
`tex.SetData(face, level, rect, data, startIndex, count)` (lines 120, 134, 137) matches the 6-arg
FNA overload with `rect=nullptr` correctly meaning "the whole level" — confirmed against
`TextureCube.cpp:144-170`, whose `mipDim(size_, level)` (line 158) correctly derives `levelSize=4`
for `level=1` of an 8-sized cube, matching the test's own `4*4` mip buffers. `kFaces[6]` (lines
64-68) is ordered `PositiveX, NegativeX, PositiveY, NegativeY, PositiveZ, NegativeZ` — confirmed this
matches `CubeMapFace.hpp`'s own enum declaration order exactly, and matches
`SdlGpuTextureCubeBackend::SetData`'s `destination.layer = static_cast<Uint32>(face)` (line 4152)
treating the XNA face-enum ordinal directly as the cube's array-layer index — a correct, direct
mapping with no reordering needed.

### Behavioral correctness

- Check A: the `SetData`→`GetData` round trip for all 6 faces (lines 94-107) traces through
  `TextureCube::SetData(face, data, elementCount)` (`TextureCube.cpp:125-128`) →
  `SetData(face,0,nullptr,data,0,count)` (6-arg overload) → `SdlGpuTextureCubeBackend::SetData(face,
  0, 0, 0, w, h, data, len)` (`SdlGpuGraphicsBackend.cpp:4112`), which builds a
  `SDL_GPUTextureRegion{.layer=face, .x=0,.y=0,.w=4,.h=4,.d=1}` and uploads with **`cycle=false`**
  (line 4161) — confirmed this is the exact fix the header comment (lines 6-7) describes ("re-checks
  that the cycle=true orphan-write bug found there does not reappear for per-face cube uploads");
  re-reading face 0 *last*, after all 5 other faces were separately uploaded (lines 109-112),
  genuinely proves sequential per-face writes accumulate onto the same GPU resource rather than each
  new face's upload silently orphaning the previous faces' data (which is precisely what `cycle=true`
  would have caused, per the `Texture3D` sibling bug this same phase's SDLGPU-40 fixed).
- Check B: `tex.SetData(PositiveX, 0, nullptr, full, 0, 64)` (line 120) on an 8-sized, `mipMap=true`
  cube — traced `SdlGpuTextureCubeBackend::SetData`'s `isFullLevel0Upload` guard (`level==0 && x==0
  && y==0 && w==size_ && h==size_`, line 4169) evaluates true, triggering
  `SDL_GenerateMipmapsForGPUTexture(cmd, texture_)` (line 4171) — genuinely the "generated case," not
  hardcoded. See F1 for what this call actually does to the *other* 5 faces.
- Check C: the same full-level-0 Orange upload on `PositiveY` (triggering the identical auto-regen),
  followed by a **separate** `SetData(PositiveY, 1, nullptr, authoredMip, 0, 16)` call explicitly
  targeting level 1 — `level==0` is false for this second call, so `isFullLevel0Upload` is false and
  the auto-regen does **not** re-fire, confirmed this correctly avoids clobbering the just-written
  Magenta level-1 data. The subsequent level-0 readback (line 143) confirms `PositiveY`'s own level 0
  is unaffected by the later, separate level-1 write.

### Logic

`AllExact()` (lines 55-62) — same tolerance-free exact-channel comparison as the sibling
`Texture3D` file, appropriate given no lighting/blending is in play.

### C++ correctness

No aliasing/lifetime concerns — each check constructs its own `TextureCube` in a nested scope
(lines 91-113, 117-126, 131-146), and all buffers are function-scope `std::vector<Color>` with clear
lifetimes.

### Memory/resource lifetime

Same per-check scoping pattern as the sibling `Texture3D` file — each `TextureCube`'s destructor
releases its `SDL_GPUTexture` before the next check's texture is constructed.

### Performance

See F1 — every per-face full-level-0 `SetData()` call on a `mipMap=true` cube triggers a **whole-
cube** `SDL_GenerateMipmapsForGPUTexture` (all 6 faces' chains), not just the touched face's own
chain (SDL_gpu has no per-layer mip-regen control, per this file's own header comment line 16 and
the backend's own comment at `SdlGpuGraphicsBackend.cpp:4164-4168`). Building a complete mipped cube
map by uploading each of 6 faces separately (a common, realistic workflow — e.g. loading a skybox
from 6 individual images) therefore performs 6 whole-cube mip-regeneration passes instead of the 1
that would be needed if regeneration were deferred until all faces are uploaded — a real, if modest
(6x, not exponential), redundant-GPU-work pattern for this specific real-world usage shape. Flagged
as "potentially significant" per this checklist's performance categories, not "likely significant"
given the absolute cost (mip generation for a handful of small faces) is unlikely to be a bottleneck
in practice.

### Robustness

`SdlGpuTextureCubeBackend::SetData`/`GetData` early-return on `w<=0||h<=0` and throw
`std::out_of_range` for undersized `dataLength` (lines 4115-4116, 4118-4119, 4184-4185, 4187-4188) —
not exercised by this file's checks (all requests are valid-sized), matching the sibling
`Texture3D` file's own coverage shape.

### Testing

Checks A and C are strong and content-discriminating. Check B shares the sibling `Texture3D` file's
uniform-color mip-oracle weakness (F2, LOW — same pattern, not repeated in full detail here). The
more significant gap is F1: no check in this file (or, per this backend's git history, in any other
CTest-registered SdlGpu test) exercises building a mipped cube by uploading multiple *different*
faces' full level 0 in sequence, which is exactly the scenario where the whole-cube regen's
cross-face interaction matters.

## Detailed Findings

### F1 — Every full-level-0 `SetData()` call on a `mipMap=true` `TextureCube` regenerates mip chains for all 6 faces, including faces whose own level-0 content may still be uninitialized or only partially updated — untested by this file and potentially producing permanently-wrong mip content for a realistic multi-face-upload workflow

- Severity: MEDIUM
- Confidence: MEDIUM (the mechanism is confirmed exactly as described by direct code reading; the
  *end-state correctness* argument below is a logical derivation from SDL_gpu's documented
  submission-ordering guarantee, not something this audit could runtime-verify without a GPU/driver
  in this sandbox)
- Category: correctness (edge case) / performance
- Location/symbol: `SdlGpuTextureCubeBackend::SetData` (`SdlGpuGraphicsBackend.cpp:4112-4179`),
  specifically `SDL_GenerateMipmapsForGPUTexture(cmd, texture_)` (line 4171), gated only by
  `isFullLevel0Upload` (line 4169) — which checks `level`/`x`/`y`/`w`/`h` for the *touched* face
  only, with no awareness of which of the other 5 faces have themselves ever received real level-0
  data.
- Evidence: consider a realistic sequence not exercised by this file: construct a `mipMap=true`
  cube, then call `SetData(faceA, fullLevel0, dataA)`. This immediately regenerates mip levels 1..N
  for **all 6 faces** (line 4171 operates on `texture_` as a whole, not per-layer — SDL_gpu has no
  per-layer mip-regen control, confirmed by this file's own header comment line 16 and the adjacent
  backend comment, lines 4164-4168). At this point, faces `B`..`F` have never had their own level-0
  data set, so their level-0 GPU memory is whatever `SDL_CreateGPUTexture` initially allocated
  (unspecified/driver-dependent content) — their newly "regenerated" level 1..N is derived from that
  unspecified content, not from any real image data. If the calling code later calls
  `SetData(faceB, fullLevel0, dataB)`, the *next* whole-cube regen recomputes level 1..N for **all**
  6 faces again — including `faceA`, whose level 0 is unchanged (still `dataA`), so `faceA`'s
  regenerated mips end up correct again (idempotent for faces whose level 0 has already been set for
  good). By induction, as long as the *last* full-level-0 upload in a sequence of per-face uploads
  happens after every other face's own level-0 data is already resident on the GPU, the final state
  ends up correct for all 6 faces — but only incidentally, via O(6×facesTouched) redundant
  regenerations, not because the design intends or guarantees this. Two realistic scenarios break
  this incidental correctness: (1) a game that uploads full level 0 for some faces but only ever
  *partial*-region `SetData` calls for others (e.g. patching a skybox face without ever doing a
  "full" replace) — those faces' `isFullLevel0Upload` never fires from their own upload, so their
  mips are only ever regenerated as a side effect of *another* face's full upload, potentially from
  a *stale* (pre-patch) level-0 snapshot; (2) any usage where the game reads back a face's mip level
  1+ (e.g. for a blurred-reflection LOD sampling scheme) in between two other faces' uploads, when
  that face's chain may currently reflect data from before a real intended final state.
- Why it matters: this is a genuine, if narrow, correctness risk for any game building a full mipped
  `TextureCube` face-by-face (the natural, most common way to load a skybox/environment map from 6
  separate source images) — and separately a real, measurable (though not severe) performance cost:
  `numFacesTouched` whole-cube mip regenerations instead of the 1 that would suffice if regeneration
  were deferred until after the last face's upload. Neither this file nor (per a check of
  `git log`/the CMake test list) any other currently-registered `SdlGpu_*` CTest exercises multiple
  *different* faces each doing a full-level-0 upload on the *same* `mipMap=true` cube, so this
  interaction is currently unverified in either direction by this backend's own test suite.
- FNA/XNA comparison: N/A directly — real XNA/FNA has no explicit "regenerate mips" call for
  `TextureCube` at all (mips are supplied by the content pipeline or left undefined if unsupplied),
  so this is a CNA-internal implementation-strategy risk (the choice to auto-regenerate on any
  full-level-0 `SetData`), not an XNA/FNA parity gap.
- Related files: `sdlgpu_texture3d_test.cpp` does not share this risk (a single-volume texture has
  no equivalent "other unrelated slices'" mip content to corrupt via a shared regen call — see that
  file's own Cross-File Observations); `sdlgpu_rendertargetcube_test.cpp`'s own per-frame
  render-target-cube mip regen (`SdlGpuGraphicsBackend.cpp:734-749`) has a materially different, and
  actually safer, design: it explicitly tracks `usedRenderTargetCubeFacesThisFrame_` and regenerates
  once per frame after all faces used *that frame* have already been rendered to, with the adjacent
  comment explicitly reasoning about this being "harmless for a face untouched this frame (same
  unchanged level-0 data produces an identical result)" — i.e. that code path's authors reasoned
  through this exact issue for the render-target-cube case; the plain `TextureCube::SetData` path
  audited here has no equivalent per-call reasoning or safeguard.
- Suggested future action (not implemented by this audit): either (a) defer mip regeneration for a
  plain `TextureCube` until an explicit point the caller controls (matching the render-target-cube
  path's "regenerate once, after all faces for this unit of work are set" model), or (b) at minimum,
  document this behavior explicitly in `SdlGpuTextureCubeBackend`'s own header/class comment so a
  caller building a cube face-by-face knows to always finish with a full-level-0 upload on every
  face (in any order) before relying on any face's mip chain, and add a CTest that uploads all 6
  faces' full level 0 in sequence, then verifies **every** face's level 1 matches its own uploaded
  color (not just the last face touched, as Check B/C effectively do today).

### F2 — Check B's uniform-color mip-generation oracle shares the same content-blind weakness as the sibling `Texture3D` file's Check C (and this shard's `sdlgpu_rendertargetcube_test.cpp` F1)

- Severity: LOW
- Confidence: MEDIUM
- Category: test-coverage
- Location/symbol: Check B (lines 117-125), `tex.GetData(PositiveX, 1, nullptr, gotMip, 0, 16)`
  (line 123)
- Evidence/why it matters: identical reasoning to `sdlgpu_texture3d_test.cpp`'s own F1 — a uniform
  source color (`Color::Orange`) downsamples to itself under any correct or degenerate filter, so
  this check cannot discriminate genuine box-filter downsampling from a wrong-but-coincidentally-
  matching filter. Not repeated in full detail here to avoid duplicating that file's writeup;
  recorded here since it is a distinct instance in a distinct file.
- FNA/XNA comparison: N/A — test-design question.
- Suggested future action: same as the sibling files — use a non-uniform per-texel pattern at level
  0 and assert the expected averaged value at level 1.

## Cross-File Observations

- F1 is the most significant finding of this 4-file batch — a real, if narrow and currently-masked,
  correctness/performance issue in production code, discovered by tracing the mip-regeneration
  trigger this file's own Check B/C depend on, rather than anything the file's own assertions
  surface directly (both checks happen to only ever touch one face per `TextureCube` instance,
  which is exactly the shape that avoids exposing the cross-face interaction).
- Contrast with `sdlgpu_rendertargetcube_test.cpp`'s own frame-scoped mip-regen design
  (`usedRenderTargetCubeFacesThisFrame_`-gated, regenerate-once-per-frame-after-all-faces-rendered):
  that design was evidently reasoned through by its authors for the exact cross-face timing question
  F1 raises for the plain-`TextureCube` path — worth flagging as a place where one of this backend's
  two cube-texture code paths (`SdlGpuRenderTargetCubeBackend` vs. `SdlGpuTextureCubeBackend`)
  applied more careful design than the other to structurally the same problem.
- Confirmed the `cycle=false` fix this file's header comment (lines 6-7) describes as "re-checks
  that the cycle=true orphan-write bug found there [Texture3D/SDLGPU-40] does not reappear for
  per-face cube uploads" is genuinely current, active code (`SdlGpuGraphicsBackend.cpp:4161`) — not
  a stale claim.

## Missing or Weak Tests

- See F1 — no test exercises multiple different faces each receiving a full-level-0 upload on the
  same `mipMap=true` cube, which is the scenario where the whole-cube regen's cross-face timing
  actually matters.
- See F2 — the mip-generation check uses a content-blind uniform-color oracle (same gap as this
  shard's `sdlgpu_texture3d_test.cpp` and `sdlgpu_rendertargetcube_test.cpp`).
- No check exercises an invalid/out-of-range face value or rectangle — reasonable to omit given this
  file's explicit byte-exact-happy-path scope, but a gap relative to this project's own stated
  boundary/invalid-argument testing checklist.

## Positive Findings

- Check A's face-0-verified-last design is a genuinely strong, content-discriminating regression
  guard for the exact orphan-write bug class SDLGPU-40/51 fixed — not a superficial "upload and
  immediately read back" proof.
- Check C's Magenta-vs-Orange distinct-content design (matching the sibling `Texture3D` file's own
  Check D) correctly discriminates "authored mip data survives" from "auto-generation silently won."
- The `CubeMapFace`→GPU-layer mapping was independently verified correct (a direct ordinal mapping,
  no off-by-one or reversed-face risk).

## Final Assessment

Checks A and C are accurate, well-designed, content-discriminating proofs of real backend behavior.
The batch's most valuable finding (F1) is a genuine, currently-untested cross-face mip-regeneration
correctness/performance risk in the production `SdlGpuTextureCubeBackend::SetData()` path, surfaced
by tracing the exact mechanism this file's own Check B/C rely on rather than by anything this file's
assertions themselves catch — worth a follow-up test and/or a design change (defer whole-cube
regeneration until the caller signals all faces are set) before this backend is used for a
real face-by-face-authored mipped cube map.
