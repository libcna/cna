# Audit: examples/sdlgpu_texture3d_test.cpp

## Metadata

- Source file: `examples/sdlgpu_texture3d_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-sdlgpu` shard — `Texture3D` + mipmap proof for the SDL_GPU backend
  (plans/plan_sdlgpu.md SDLGPU-40/41)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_sdlgpu_test(cna_test_sdlgpu_texture3d …)` / `cna_register_backend_test(NAME
  SdlGpu_Texture3D …)`, `cmake/Tests/SdlGpuTests.cmake:60-63`, `TIMEOUT 60`).
- XNA/FNA relevance: direct — `Texture3D` constructor, `Texture3D.SetData<T>(level, left, top,
  right, bottom, front, back, data, startIndex, elementCount)`/`GetData` overload set.
- FNA reference: `Graphics/Texture3D.cs`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/Texture3D.cpp` (`SetData`/
  `SetDataPointerEXT`/`GetData`, lines 106-193), `src/CNA/Internal/Backends/SdlGpu/
  SdlGpuGraphicsBackend.cpp` (`SdlGpuTexture3DBackend` constructor/`SetData`/`GetData`, lines
  3910-4078).

## Purpose

Four-check byte-exact round-trip proof for a real `SDL_GPU_TEXTURETYPE_3D` texture (sampler-only,
never a render target): (A) a deliberately off-center 2x2x1 sub-volume at offset (1,1,z) within a
4x4x2 volume, one distinct solid color per Z slice, round-trips byte-exact — genuinely exercises the
X/Y/Z offset math and per-slice pitch, not just a trivial (0,0,0) case. (B) A full-volume
upload/readback round-trips byte-exact. (C) A uniform-color full level-0 upload with `mipMap=true`
auto-generates level 1 ("generated case"), which reads back the same color. (D) Explicit authored
data written directly to level 1 is not clobbered by the level-0 auto-generation that already ran,
and level 0 itself remains intact afterward — the file's own header comment states this caught a
real bug (`SDL_UploadToGPUTexture`'s `cycle=true` silently orphaning an earlier partial write once a
second write followed it, fixed by `cycle=false`).

## Executive Verdict

**Healthy.** All four checks map onto real, independently-verified backend code paths; the
bug-fix narrative in the header comment (the `cycle=false` fix) was checked against current source
and is genuinely still in effect, not stale. One LOW-severity, pre-existing-pattern gap (F1: the
uniform-color mip check cannot distinguish real box-filter downsampling from a degenerate filter),
matching an identical, already-documented finding for this same shard's
`sdlgpu_rendertargetcube_test.cpp`.

## Checklist Results

### API / XNA / FNA parity

`Texture3D(dev, 4, 4, 2, false, SurfaceFormat::Color)` (line 84) matches FNA's `Texture3D(
GraphicsDevice, int width, int height, int depth, bool mipMap, SurfaceFormat)` constructor exactly
in argument order/count. `tex.SetData(0, left, top, right, bottom, front, back, data, startIndex,
elementCount)` (lines 87-88, 114, 126, 129) matches the 10-argument FNA overload exactly — confirmed
against `Texture3D.cpp:118-137`, which computes `right-left`/`bottom-top`/`back-front` before
forwarding to the backend, matching FNA's own `SetData<T>` (`right - left, bottom - top, back -
front` passed to `FNA3D_SetTextureData3D`, `Texture3D.cs:130-132`) field-for-field. Note: neither
FNA's own `Texture3D.SetData<T>`/`GetData<T>` nor CNA's `Texture3D.cpp` (lines 129-130, 180-181)
validate the requested box against the actual per-level dimensions (only `left<right`,
`top<bottom`, `front<back` ordering is checked) — confirmed this is intentional FNA parity, not a
CNA-introduced gap (`Texture3D.cs:252-257` has the identical, narrower check).

### Behavioral correctness

- Check A: `SetData(0, 1, 1, 3, 3, 0, 1, redSlice, 0, 4)`/`(…, 1, 2, greenSlice, 0, 4)` (lines 87-88)
  → `Texture3D::SetDataPointerEXT` (lines 139-148) computes `x=left=1, y=top=1, z=front,
  w=right-left=2, h=bottom-top=2, depth=back-front=1` and forwards to
  `SdlGpuTexture3DBackend::SetData(level, x, y, z, w, h, depth, data, dataLength)` — traced this
  exactly matches the backend's own parameter order (`SdlGpuGraphicsBackend.cpp:3947`). The backend
  builds `SDL_GPUTextureRegion{.x=1,.y=1,.z=0 or 1,.w=2,.h=2,.d=1}` (lines 3987-3992) — genuinely
  exercises non-zero X/Y/Z offsets and a 2-slice Z range, not a degenerate (0,0,0) case.
- Check B: full-volume `tex.SetData(full.data(), 32)` (line 103) → the 3-arg overload
  (`Texture3D.cpp:111-116`) delegates to the 10-arg overload covering `(0,0,0,width,height,0,depth)`
  — confirmed this is the full 4x4x2=32-element volume, matching `full.size()`.
- Check C: `tex.SetData(0, 0, 0, 8, 8, 0, 2, full.data(), 0, 128)` (line 114) on an 8x8x2,
  `mipMap=true` texture — `SdlGpuTexture3DBackend::SetData`'s `isFullLevel0Upload` guard
  (`level==0 && x==0 && y==0 && z==0 && w==width_ && h==height_ && depth==depth_`, lines 4006-4007)
  evaluates true for exactly this call, triggering `SDL_GenerateMipmapsForGPUTexture(cmd, texture_)`
  (line 4009) — genuinely the "generated case" the header comment claims, not a hardcoded/always-on
  regen.
- Check D: the *same* full-level-0 Orange upload (triggering the identical auto-regen as Check C),
  followed by a **separate** `SetData(1, 0, 0, 4, 4, 0, 1, authoredMip, 0, 16)` call explicitly
  targeting level 1 — since `level==0` is false for this second call, `isFullLevel0Upload` is false,
  so the auto-regen is **not** re-triggered (confirmed this would have clobbered the just-written
  Magenta level-1 data had it re-fired) — genuinely proves the explicit level-1 write survives
  untouched by any subsequent auto-regen path, and the final level-0 readback (line 138) confirms
  the Orange level-0 data is unaffected by the later, separate level-1 write.
- The `cycle=false` fix the header comment describes (lines 18-21) is genuinely present and current:
  `SDL_UploadToGPUTexture(copyPass, &source, &destination, false)` (line 4000), with the adjacent
  comment explaining exactly the orphan-write bug this audit's own reasoning (multiple independent
  sub-volume/per-level `SetData` calls must all land on the same underlying GPU resource) confirms
  is the correct rationale — not a stale claim contradicted by later code, unlike several other
  backends' comments this audit series has flagged elsewhere.

### Logic

`AllExact()` (lines 58-65) compares all four channels exactly (no tolerance) — appropriate for a
"byte-exact" claim, unlike the `±N` tolerance helpers used by lit/shaded pixel tests elsewhere in
this project (there is no lighting/blending in play here, so exact equality is the correct bar, not
an over-strict one).

### C++ correctness

`SolidColors(int count, const Color& c)` (lines 53-56) constructs a `std::vector<Color>` via the
fill constructor — straightforward, no aliasing/lifetime concerns; each check's vectors are
function-scope locals with clear, non-overlapping lifetimes.

### Memory/resource lifetime

Each check constructs its own `Texture3D` in a nested scope (lines 83-97, 100-107, 111-119,
123-140) — `Texture3D`'s destructor (`Dispose(true)` via the `Texture` base, not re-traced here as
out of this batch's scope) releases the backend's `SDL_GPUTexture` before the next check's texture
is constructed, avoiding unnecessary concurrent GPU texture pressure across checks.

### Performance

Four separate `SDL_GPUTransferBuffer` create/map/unmap/release cycles per `SetData`/`GetData` call
(traced in `SdlGpuTexture3DBackend::SetData`/`GetData`, lines 3956-4077) — appropriate for a
correctness-proof test; not a production hot-path concern for this file.

### Robustness

`SdlGpuTexture3DBackend::SetData`/`GetData` (lines 3950-3951, 4022-4023) early-return on
`w<=0||h<=0||depth<=0` and throw `std::out_of_range` if `dataLength` is too small for the requested
region (lines 3953-3954, 4025-4026) — this file's checks never exercise either edge (all requested
regions are valid and appropriately sized), so this is confirmed-present robustness in the
production code, not exercised coverage in this specific test file.

### Testing

Strong, genuine byte-exact proof for both sub-volume-offset and full-volume paths, and a real
two-call sequential-write proof for the mip-orphaning regression this file's header comment cites.
One coverage gap for the mip-generation check specifically — see F1.

## Detailed Findings

### F1 — Check C's uniform-color mip-generation oracle cannot distinguish genuine box-filter downsampling from a degenerate/incorrect filter (though it would catch "mip generation skipped entirely")

- Severity: LOW
- Confidence: MEDIUM (the ambiguity for *filter correctness* is real and unavoidable with a uniform
  source color; less certain whether "mip regeneration never ran at all" would reliably produce a
  visibly-different result, since `SDL_CreateGPUTexture`'s initial level-1 memory content is
  undefined/implementation-dependent rather than guaranteed non-Orange)
- Category: test-coverage
- Location/symbol: Check C (lines 111-119), `tex.GetData(1, 0, 0, 4, 4, 0, 1, gotMip.data(), 0,
  16)` (line 117)
- Evidence: Check C fills the entire 8x8x2 level-0 volume with one solid color (`Color::Orange`)
  and asserts level 1 reads back the *same* color. A uniform source downsamples to itself under any
  correct box filter, any degenerate "just copy the corner texel" filter, or (in a hypothetical
  regression) a mip level that was never actually regenerated but happened to contain leftover/
  zero-initialized memory that coincidentally is not distinguished from Orange by this specific
  check's tolerance-free `AllExact` comparison only if that leftover memory happens to equal Orange
  (unlikely, but the check's *design* does not rule out any of these possibilities structurally —
  only Check D's *content-distinguishing* Magenta-vs-Orange pair does).
- Why it matters: a regression that broke mip generation specifically for *non-uniform* content
  (the only case that actually matters for real 3D-texture sampling at a distance, e.g. volumetric
  fog/light lookup tables) would not be caught by this check, even though the header comment
  presents it as proof mip generation "works." This is the identical pattern this same shard's
  `sdlgpu_rendertargetcube_test.cpp.audit.md` already documents as its own F1 (LOW) for that file's
  analogous uniform-color mip check.
- FNA/XNA comparison: N/A — test-design question; FNA itself has no cross-mip content-fidelity
  conformance test for `Texture3D` either.
- Related files: `sdlgpu_texturecube_test.cpp` (this same batch's Check B has the identical
  pattern — see that file's own report), `sdlgpu_rendertargetcube_test.cpp` (established precedent
  for this exact finding shape in this shard).
- Suggested future action (not implemented by this audit): use a non-uniform per-texel pattern
  (e.g. alternating colors per 2x2x1 block within the uploaded region) at level 0 and assert the
  expected *averaged* value at level 1, which only passes if real box-filter downsampling executed.

## Cross-File Observations

- Unlike `sdlgpu_texturecube_test.cpp`'s analogous mip-regeneration path (see that file's own F1),
  `Texture3D` has only a single volume (no separate per-face resources), so there is no equivalent
  "regenerating one volume's mips can be triggered by/interact with another volume's stale content"
  cross-contamination risk here — the single-resource design makes this file's mip-regen trigger
  condition (`isFullLevel0Upload`) unconditionally safe once it fires, unlike the cube case.
- Confirmed via `git log` that the `cycle=false` fix this file's header comment describes as
  "fixed by passing cycle=false" is not a stale claim: `SDL_UploadToGPUTexture(...,false)`
  (line 4000) is the current, active code, and the surrounding comment's stated rationale (multiple
  independent sub-volume/per-level `SetData` calls landing on the same resource) matches this file's
  own Check A/D structure exactly — a good example of a header comment whose bug-fix narrative is
  genuinely still accurate, contrasting with several other backends' stale "known bug" comments this
  audit series has flagged elsewhere.
- `Texture3D.cpp`'s lack of level-dimension bounds validation (noted under API/FNA parity above) is
  confirmed to be intentional FNA parity, not a gap introduced by this file's test scope or by CNA's
  own implementation — not raised as a Detailed Finding since matching FNA's own (lenient) behavior
  here is the explicitly correct choice per this project's stated fidelity policy.

## Missing or Weak Tests

- See F1 — the mip-generation check uses a content-blind uniform-color oracle.
- No check exercises an invalid/out-of-range box (e.g. `right > width` for a given level, or a
  negative offset) to confirm `Texture3D::SetData`'s/`GetData`'s existing ordering-only validation
  (`left>=right`, etc.) actually throws as expected — reasonable to omit given this file's explicit
  scope is the byte-exact happy path, but a gap nonetheless relative to this project's own stated
  testing checklist ("boundary, invalid-argument" coverage).

## Positive Findings

- Check A's off-center, per-slice-distinct-color sub-volume is a genuinely strong discriminator for
  X/Y/Z offset and per-slice-pitch math — not a lazy (0,0,0)-only proof.
- Check D's Magenta-vs-Orange content distinction (rather than reusing the same color for both
  levels) is a real, content-discriminating proof that the authored mip write survives the earlier
  auto-generation and that level 0 remains untouched by the later write — genuinely stronger than
  Check C's own uniform-color mip check (see F1).
- The header comment's bug-fix narrative (`cycle=false` fix for the orphan-write bug) was
  independently verified accurate against current production source, not merely trusted.

## Final Assessment

A strong, accurate byte-exact `Texture3D` proof with a verified-current bug-fix narrative. Only gap
found is the mip-generation check's content-blind oracle (F1, LOW), which mirrors an
already-documented pattern elsewhere in this same shard rather than introducing a new concern.
