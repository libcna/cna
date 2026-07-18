# Audit: examples/vulkan_shader_effect_test.cpp

## Metadata

- Source file: `examples/vulkan_shader_effect_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — `ShaderEffect` with pre-compiled SPIR-V, Vulkan
  backend
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_shader_effect …)` / `cna_register_backend_test(NAME
  Vulkan_ShaderEffect_SpirV …)`, `cmake/Tests/VulkanTests.cmake:35-38`, Task 119).
- XNA/FNA relevance: **N/A for the effect class itself** — `ShaderEffect` is explicitly a `NOXNA`
  CNA extension (`include/Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp:25`, `NOXNA class
  ShaderEffect : public Effect, public IEffectMatrices`), not part of the XNA 4.0 API surface.
  `SpriteBatch::Begin`'s 6-argument overload (sortMode/blendState/samplerState/depthStencilState/
  rasterizerState/effect) it uses **is** XNA-facing and was checked for parity.
- FNA reference: `Graphics/SpriteBatch.cs` (`Begin(SpriteSortMode, BlendState, SamplerState,
  DepthStencilState, RasterizerState, Effect)` overload — 6 positional parameters, same order).
- Related production code: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`VulkanEffectBackend::CompileProgram()` lines 2317-2432, `SetUniformVec4`/`Vec3`/`Vec2` lines
  2461-2477, sprite pipeline selection at `RecordCommandBuffer()` lines 6283-6322),
  `src/Microsoft/Xna/Framework/Graphics/ShaderEffect.cpp`.

## Purpose

Exercises `ShaderEffect`'s pre-compiled-SPIR-V path (as opposed to the GLSL-source-compiled path
other `ShaderEffect` tests likely use): embeds two `uint32_t[]` SPIR-V binary blobs (a vertex
shader mapping pixel coords to NDC + pass-through UV/colour, and a fragment shader computing
`texture * vColor * pc.uColor`) directly as byte arrays in the `.cpp` file, constructs
`ShaderEffect fx(device, vertSpv, fragSpv)` from them (via `std::string` used purely as a
byte-container, not text), sets a red tint via `SetUniformVec4("uColor", 1,0,0,1)`, and renders a
white 1×1 texture through `SpriteBatch::Begin(..., &fx)` onto a green background. Asserts the
sprite's centre reads red (white × red tint) and a background corner reads green.

## Executive Verdict

**Healthy** — every specific numeric/structural claim in the file's own header comment (push-
constant byte offsets, SPIR-V blob sizes) was independently re-verified against the actual byte
arrays and the current `VulkanEffectBackend` implementation and found accurate.

## Checklist Results

### API / XNA / FNA parity
`sb_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr, nullptr, &fx)`
(line 229) matches FNA's `SpriteBatch.Begin(SpriteSortMode, BlendState, SamplerState,
DepthStencilState, RasterizerState, Effect)` overload's parameter order exactly (`SpriteBatch.cs`).
`ShaderEffect` itself, being `NOXNA`, is correctly out of scope for XNA parity — confirmed it is
declared with the `NOXNA` marker macro and lives outside the `Microsoft::Xna` behavioral-parity
requirement per `CLAUDE.md`.

### Behavioral correctness
Independently re-derived the header comment's push-constant byte-offset table against the actual
`VulkanEffectBackend` implementation:
- *"[16..79] = mat4 uMatrix"* / *"[80..95] = vec4 uColor"* (comment lines 10-12) — confirmed
  against `SetUniformMat4` (`memcpy(pushConst_ + 4, matrix, 64)`, i.e. float-index 4 = byte offset
  16, for 64 bytes = through byte 79) and `SetUniformVec4`
  (`pushConst_[20]=x;...pushConst_[23]=w;`, i.e. float-index 20 = byte offset 80, 4 floats = byte
  95) — both match exactly.
- The SPIR-V blob sizes (`kTintVertSpv_size=1768`, `kTintFragSpv_size=1216`) were independently
  recomputed by counting the actual `0x…` literals in each array (442 words × 4 = 1768 bytes; 304
  words × 4 = 1216 bytes) — both declared constants are exactly correct, not off-by-a-word or
  truncated, and both blobs begin with the real SPIR-V magic number `0x07230203`.
- `VulkanEffectBackend::CompileProgram()`'s own size-multiple-of-4 guard (`vertSpv.size() % 4 != 0`,
  line 2325) would reject a truncated blob, and since the sizes match exactly, `IsEffectValid()`
  (checked at line 218 before proceeding) is a meaningful, non-vacuous guard for this test.

### Logic
Note: `SetUniformVec4`/`SetUniformVec3`/`SetUniformVec2` all ignore their `name` parameter
entirely and always write to the same fixed push-constant slot (`pushConst_[20..23]`) — confirmed
by reading all three implementations back to back
(`VulkanEffectBackend.cpp:2461-2477`). This is a real API-surface simplification (a "named"
uniform setter that is not actually name-addressed — calling `SetUniformVec4("foo", …)` would
silently collide with a prior `SetUniformVec4("uColor", …)` call), but it is consistent with the
class's own documented design (`ShaderEffect.hpp:150-157`'s `Clone()` doc: *"unlike the stock
effects, a ShaderEffect's own uniforms are set directly by the caller... every other field is left
at its default"*) and does not affect this specific test, which only ever sets one vec4 uniform.
Flagged as an INFO-level API-surface observation, not a defect: `ShaderEffect` is `NOXNA`, so this
single-slot design is a CNA-internal implementation choice, not an XNA compatibility question.

### Robustness
The `IsEffectValid()` check (lines 218-223) fails the test cleanly with a `[FAIL]` message and
`Exit()` if SPIR-V compilation fails, rather than proceeding to draw with an unbound/garbage
pipeline — correct defensive structure for a test whose entire premise depends on successful shader
module creation.

### Performance
N/A at test-file scope — no hot-path or repeated-allocation concerns; this is a single-frame,
single-draw test.

### Testing
Two assertions (`centOk`: centre red; `bgOk`: corner green), each requiring one channel `>=200` and
the other `<=50` — appropriately tight thresholds. The corner check (`Rectangle(1,1,1,1)`) is a
genuine "background, not affected by the tinted sprite" check, not merely a duplicate of the
centre check.

### Cross-file consistency
This is the only file in the current batch to exercise `IEffectBackend`'s
custom-SPIR-V-compilation path directly; its push-constant contract (128 bytes, `vpSize | uMatrix |
uColor | uFloat0`) matches the identical contract documented in `VulkanEffectBackend.cpp`'s own
header comment (lines 2317-2321), confirming the test's understanding of the contract is accurate
and not just internally self-consistent.

## Detailed Findings

None — no CRITICAL/HIGH/MEDIUM findings. Every specific claim in this file's comments was checked
against the actual byte arrays and current backend source and found accurate.

## Missing or Weak Tests

- The custom-effect pipeline this test exercises is always built against `owner_->renderPass_`
  (the non-MSAA backbuffer render pass) unconditionally
  (`VulkanEffectBackend::CompileProgram()`, `pci.renderPass = owner_->renderPass_;`, no MSAA
  variant) — this file's own scenario never enables multisampling, so it cannot surface whether a
  `ShaderEffect`-driven `SpriteBatch` draw would be render-pass-incompatible under an MSAA-enabled
  swapchain (a similar class of bug independently confirmed fixed for the *built-in* 2D pipeline by
  the Task 903 finding referenced in this backend's own comments, at
  `VulkanGraphicsBackend.cpp:6283-6296`, but seemingly never extended to the custom-SPIR-V-effect
  pipeline). This is a coverage gap worth flagging for whoever extends MSAA support further, not a
  defect in this specific test (which never claims to cover MSAA).

## Positive Findings

- Every concrete numeric claim in the header comment (byte offsets, blob sizes) was independently
  re-derived from the actual data and found precisely correct — no stale-comment issues here, in
  contrast to some sibling files in this batch.
- The corner/background assertion is a genuine second data point, not a redundant restatement of
  the centre assertion.
- Clean, fail-fast handling of a shader-compile failure via `IsEffectValid()`.

## Final Assessment

A correct, carefully cross-checked test of `ShaderEffect`'s pre-compiled-SPIR-V path. The one
genuine gap identified (no MSAA-render-pass-compatibility coverage for the custom-effect pipeline)
is a production-backend concern for a future task, not a flaw in this file's own design or
assertions.
