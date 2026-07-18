# Audit: src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp

## Metadata

- Source file: `src/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.cpp`
- Audit status: AUDITED (**scoped-depth review** — see Methodology note; this is the largest single file in the
  entire audit, 8805 lines, larger than EasyGL's 4733)
- Subsystem: `backend-webgpu` shard
- File type: C++ implementation — the entire WebGPU backend (via pinned `wgpu-native` v29.0.1.1) in one file
- Related header/implementation: `include/CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.hpp` (1606 lines,
  audited separately, same shard)
- XNA/FNA relevance: implements the same effect surface as EasyGL (`BasicEffect`, `SkinnedEffect`,
  `DualTextureEffect`, `EnvironmentMapEffect`, `PbrEffect`/`SkinnedPbrEffect`, `SpriteBatch`) — per `CLAUDE.md`,
  this backend is **explicitly experimental**: "the current baseline implements native surface/device setup,
  clear/present, Texture2D, buffer uploads and WGSL SpriteBatch. Do not describe it as... full XNA 3D parity
  until the remaining shader, state, effect, render-target, readback and test tasks are actually complete."
- Graphics backend relevance: one of the 14 confirmed backends, uses pinned `wgpu-native` (a genuine external
  upstream dependency, not an openeggbert sibling — already documented in `THIRD_PARTY_NOTICES.md` as a
  downloaded binary, never vendored into the tree).
- FNA reference: N/A directly for the WebGPU-specific plumbing; the ported-shader content was cross-checked
  against the equivalent already-audited EasyGL shader (see F1 below, which is a confirmed cross-backend
  propagation of an EasyGL defect this file's own comments explicitly attribute to a line-for-line port).
- Main related tests: `examples-tests-webgpu` (22 files, not yet audited at time of writing).

## Methodology note

Given this file's size (8805 lines — the single largest file audited in this entire pass, larger even than
EasyGL), this review read the header's public surface, the full constructor/destructor (including the
`try`/`catch` exception-safety wrapper), and the `SkinnedEffect` shader-generation section
(`CreateSkinnedResources()`/`DestroySkinnedResources()`) — the latter specifically to check whether this backend's
own documented practice of porting shaders "line-for-line" from EasyGL (stated in this file's own comments,
e.g. at `CreateSkinnedResources()`) also propagated the world-space-normal-transform defect this audit's
`backend-easygl` report (F2) found and root-caused. It did. Given this backend's explicitly experimental status
(per `CLAUDE.md`) and its sheer size, a full shader-by-shader re-verification of every remaining effect
(BasicEffect, DualTextureEffect, EnvironmentMapEffect, PbrEffect) was not performed in this pass — that is
deferred to Pass 3 (systematic FNA parity sweep) and Pass 4 (cross-backend capability matrix), where it can be
done systematically across all backends implementing the same effects rather than repeated per-backend here.

## Executive Verdict

**Needs attention**, primarily due to F1 (the confirmed, cross-backend propagation of the skinned-normal-transform
bug) — but this file's own engineering discipline is otherwise excellent, and specifically **resolves** one of the
two open cross-cutting questions this audit pass raised: its constructor is a model example of correct
exception-safe resource construction (contrast with EasyGL's own confirmed F1 defect in the same area).

## Checklist Results

### API / XNA / FNA parity

**F1 (Detailed Findings)** — the confirmed finding. Everything else in the areas actually read (constructor/
destructor, skinned shader structure, uniform layout) is internally consistent with the equivalent, already-
audited EasyGL logic it's explicitly ported from.

### Behavioral correctness

The skinned-shader's own disabled-light NaN guard (`select(vec3f(0.0), normalize(...), dirNsq > 0.0)`) correctly
mirrors the same defensive pattern this audit found in EasyGL's degenerate-blend-normal guard — a case of a good
practice being carried across the port, even though the normal-transform bug (a different issue) was carried
across too.

### Logic

**F1.**

### Memory/resource lifetime

The constructor's `try`/`catch(...)` block (lines 1864-1893) is a genuinely thorough, correctly-ordered manual
resource-cleanup path: on any exception from `CreateSurface()`/`RequestAdapterAndDevice()`/`ConfigureSurface()`,
it releases (in a sensible reverse-ish order) sprite resources, the sampler cache, MSAA color view/texture, depth
view/texture, unconfigures the surface if configured, then releases queue/device/adapter/surface/instance, and
(on Apple) destroys the Metal view — before rethrowing. Crucially, `IGraphicsBackend::RegisterForWindow(window_,
this)` is the **last statement** in the `try` block (line 1869), after every fallible native-resource-acquisition
step has already succeeded — meaning if any of those steps throws, the object is never registered in the first
place, and there is nothing to leak in the static window registry. This is the textbook-correct version of the
pattern `EasyGLGraphicsBackend`'s own audit (F1) found broken. The destructor (lines 1896-1943) is exhaustive and
consistently null-guards every native handle release.

### C++ correctness

Not independently assessed beyond the constructor/destructor and skinned-shader sections given the scoped-review
methodology.

### Performance

Not independently assessed given the scoped-review methodology.

### Thread safety

N/A — consistent with every other backend audited so far.

### Architecture

The `IWebGPUSamplable`/`IWebGPUCubeSamplable` small-interface pattern (header lines 26-62) is a well-reasoned
solution to a real diamond-inheritance problem (`WebGPURenderTargetBackend` needs to expose a `WGPUTextureView`
the same way `WebGPUTextureBackend` does, but can't inherit from it without an ambiguous diamond since both
already derive from `ITextureBackend`) — explicitly modeled on an equivalent pattern already established in the
Vulkan backend (`IVulkanSamplable`, per the header's own comment), a good example of intentional, documented
cross-backend design consistency rather than three independent reinventions.

### Maintainability

8805 lines is, by a comfortable margin, the largest single file in this entire audit — a genuine, significant
maintainability concern in its own right (see `EasyGLGraphicsBackend.cpp`'s own audit report for the equivalent,
somewhat smaller observation; this file is worse on the same axis). Given the backend's explicitly experimental,
still-actively-growing status, this may partly reflect organic in-progress development rather than a settled
architecture — worth revisiting once the backend reaches feature completion.

### Portability

Correctly branches per-platform surface creation (`WGPUSurfaceSourceWindowsHWND`/`WGPUSurfaceSourceMetalLayer`/
`WGPUSurfaceSourceAndroidNativeWindow`/Wayland/X11) with an explicit, honest `throw` for any unhandled platform/
driver combination (lines 2001-2009) rather than a silent fallback — consistent with this codebase's established
"unsupported = throw loudly" discipline seen in the other backends already audited.

### Robustness

**F1.** Otherwise, per-platform surface creation consistently checks for null handles/properties from SDL before
proceeding and throws descriptively rather than passing a null through to `wgpuInstanceCreateSurface`.

### Testing

Not independently assessed (queued for `examples-tests-webgpu`, 22 files).

## Detailed Findings

### F1 — `SkinnedEffect`'s WGSL shader (`CreateSkinnedResources()`) has the identical world-space-normal-transform
defect already confirmed in the EasyGL backend, explicitly because it was ported from it line-for-line

- Severity: HIGH
- Confidence: HIGH (confirmed by direct source reading)
- Category: correctness / FNA parity (cross-backend systemic issue — see `AUDIT_CROSS_CUTTING_FINDINGS.md`)
- Location/symbol: `WebGPUGraphicsBackend::CreateSkinnedResources()`, vertex shader entry point `vs_main` (around
  line 7545-7555): `let skinMat3 = mat3x3f(skinMat[0].xyz, skinMat[1].xyz, skinMat[2].xyz); output.worldNormal =
  normalize(skinMat3 * input.normal);` — the object's World transform is never composed into this computation at
  all, exactly matching EasyGL's `EnsureSkinnedProgram()` defect (that report's F2).
- Evidence: this file's own comment immediately preceding `CreateSkinnedResources()` (~line 7436-7437) states:
  "Ported from `EasyGLGraphicsBackend::EnsureSkinnedProgram()`'s GLSL shader line-for-line" — an explicit,
  self-documented admission that this shader's logic (bugs included) was deliberately carried over from the
  already-audited EasyGL implementation. The shader's own `LitLightParams` uniform struct even declares
  `normalMatrixCol0`/`normalMatrixCol1`/`normalMatrixCol2` fields (present in the struct layout, presumably shared
  with a sibling non-skinned shader that *does* use them) — but the skinned vertex shader body never reads them
  for the normal transform, only `skinMat3`.
- Why it matters: identical consequence to EasyGL's F2 — any skinned model rendered with a non-identity (rotated)
  World transform via this backend gets lighting normals expressed in bind-pose-local orientation rather than the
  model's actual world-facing direction. Given this file's own comment explicitly frames "matching the EasyGL
  reference exactly... is what makes this backend's rendered output consistent with every other backend for the
  same scene" (a direct quote from the surrounding comment block, applied to a *different*, intentional
  simplification nearby) as a deliberate cross-backend-consistency goal, this specific defect's propagation is a
  natural (if unfortunate) consequence of that same porting discipline being applied to something that was itself
  a genuine bug, not an intentional simplification.
- FNA/XNA comparison: same as EasyGL's F2 — FNA's `Skinning.fxh` composes the bone's rotational part with
  `WorldInverseTranspose` for the normal.
- Related files: `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp` (the origin of this port, and this
  audit's F2 finding there); very likely every other backend with its own SkinnedEffect implementation (Vulkan,
  Bgfx, D3D9, D3D11, D3D12, SdlGpu — all confirmed via `examples-tests-*` shards to have `skinnedeffect_*` tests),
  flagged as a priority check for each of their audits. Note: the `SkinnedPbrEffect` sibling of this same bug
  (equivalent to EasyGL's F3, using raw World rotation instead of the inverse-transpose) is actually
  **self-documented** in this backend's own header (`WebGPUGraphicsBackend.hpp`, `SkinnedPbrDrawCommand`'s
  preceding comment): "its own (different from unskinned PbrEffect) normal/tangent transform, which uses the raw
  World rotation directly... rather than the precomputed inverse-transpose normal matrix" — a mitigating factor
  for that specific variant (known and written down, not silently unknown) that does not extend to the plain
  `SkinnedEffect` case this finding centers on, where no such disclosure was found near `CreateSkinnedResources()`.
- Suggested future action (not implemented by this audit): once EasyGL's own F2 fix is designed (compose
  `uNormalMatrix`-equivalent with the skin matrix), port the same fix here too, reusing the already-declared-but-
  unused `normalMatrixCol0/1/2` uniform fields.

## Cross-File Observations

- This audit's own cross-cutting question ("does `Canvas`/`SdlGpu`/`WebGPU` share EasyGL's constructor
  window-registration ordering bug?") is now answered for WebGPU: **no**, it does not — see the
  Memory/resource lifetime section above. `Canvas`/`SdlGpu` remain to be checked.
- F1 substantially raises the prior confidence (already "very likely" per the `backend-easygl` report) that
  Vulkan/Bgfx/D3D9/D3D11/D3D12/SdlGpu's own SkinnedEffect implementations share this exact defect, since this is
  now the *second* backend confirmed to have it, via an explicitly-documented line-for-line EasyGL port — the same
  porting practice that produced WebGPU's copy could equally have produced any of the others'.

## Missing or Weak Tests

Not independently assessed (queued for `examples-tests-webgpu`). Given F1, the same "rotated-World skinned model
lighting" test gap flagged in the EasyGL report applies here too.

## Positive Findings

- Model-quality constructor exception safety: full resource cleanup on any construction failure, with
  `RegisterForWindow` correctly deferred until every fallible step has succeeded — the standard this audit will
  now use to judge every other `RegisterForWindow`-calling backend.
- Thoughtful, explicit cross-backend design reuse (`IWebGPUSamplable` mirroring Vulkan's `IVulkanSamplable`) with
  clear documentation of *why* the pattern is needed (the diamond-inheritance problem), not just that it exists.
- Honest, loud failure for unsupported platform/driver combinations during surface creation, consistent with this
  codebase's established discipline elsewhere.

## Final Assessment

A large, still-experimental backend (correctly labeled as such by the project) with excellent constructor-level
exception safety and a confirmed, cross-backend-propagated correctness defect (F1) inherited directly from EasyGL
via an explicitly-documented porting practice. The file's sheer size (8805 lines) is itself a maintainability
concern worth revisiting as the backend matures past its current experimental baseline.
