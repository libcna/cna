# Audit: examples/vulkan_vertex_format_test.cpp

## Metadata

- Source file: `examples/vulkan_vertex_format_test.cpp` (354 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-vulkan` shard — Vulkan vertex-input-attribute mapping test
  (`VulkanVertexFormatHelper` + per-stride pixel readback)
- File type: standalone `Game`-subclass executable, CTest-registered
  (`cna_vulkan_test(cna_test_vulkan_vertex_format …)` /
  `cna_register_backend_test(NAME Vulkan_VertexFormat_AllStrides …)`,
  `cmake/Tests/VulkanTests.cmake:427-429`).
- XNA/FNA relevance: direct — `VertexElementFormat`, `VertexElementUsage`, `VertexDeclaration`,
  `VertexElement`, `VertexPositionColor`/`VertexPositionTexture`/`VertexPositionColorTexture`/
  `VertexPositionNormalTexture`.
- Related production code: `include/CNA/Internal/Backends/Vulkan/VulkanVertexFormatHelper.hpp`
  (`VertexElementFormatToVk`/`VertexElementFormatSize`), `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp`
  (`VulkanVertexBufferBackend` class, `include/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp:336-361`;
  `DrawPrimitivesEx()` lines 7356-7424 and its stride-dispatch logic; `GetOrCreatePipelineFogColored3D`
  lines 4797-4912, `GetOrCreatePipelineFogTex3D` lines 4914+, `GetOrCreatePipelineLitTextured3DVertexLit`
  lines 4578-4680 — all hand-written, per-stride `VkVertexInputAttributeDescription` arrays).
  `include/Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp` (`vertexDeclaration_` member, line 323;
  `GetBackend()` line 267).
- git corroboration: `f0246d9f` "feat(Tasks 231-240,248,327-328): buffer API audit, Vulkan tests,
  EasyGL/metagl compat fix" (authored 2026-06-27), matching this file's own "Task 248" header attribution.

## Purpose

Two-part test. Part 1 (`testMappingTable()`) asserts `VertexElementFormatToVk`/`VertexElementFormatSize`
return the expected `VkFormat`/byte-size for all 12 `VertexElementFormat` enum values. Part 2 (`testStrideNN`,
`NN` ∈ {16,20,24,32}) builds a custom `VertexDeclaration`+`VertexBuffer` for each of the four canonical XNA
vertex layouts (`VertexPositionColor`, `VertexPositionTexture`, `VertexPositionColorTexture`,
`VertexPositionNormalTexture`), draws a full-screen quad via `BasicEffect`, and reads back the center pixel
to prove the position/color/uv/normal attributes are bound to the shader inputs the test expects.

## Executive Verdict

**Needs attention** — Part 2's four pixel-readback sub-tests are genuine, well-designed, and this audit
independently confirmed (by reading the actual hardcoded `VkVertexInputAttributeDescription` arrays for
strides 16/20/24/32) that they currently exercise real, correctly-wired production pipelines. Part 1,
however, is materially weaker than its own header comment claims: `VertexElementFormatToVk`/
`VertexElementFormatSize` are **not called by any production pipeline-creation code** anywhere in this
~9000-line backend — the actual `VkVertexInputAttributeDescription` arrays are all separately hand-written
per pipeline function — so for 7 of the 12 tested `VertexElementFormat` values (`Single`, `Short2`, `Short4`,
`NormalizedShort2`, `NormalizedShort4`, `HalfVector2`, `HalfVector4`) there is no production Vulkan pipeline
anywhere in this backend that uses the corresponding `VkFormat` at all, meaning `testMappingTable()` for
those 7 values checks the helper function only against itself (see F1).

## Checklist Results

### API / XNA / FNA parity — PASS
`VertexElement(offset, format, usage, usageIndex)` constructor calls (lines 182-184, 217-219, 254-257,
293-296) and `VertexDeclaration(stride, {elements...})` (lines 181, 216, 253, 292) match the real XNA
`VertexElement`/`VertexDeclaration` API shapes. `dev.SetVertexBuffer(&vb)`/`dev.DrawPrimitives(...)`
(lines 192-193, etc.) match `GraphicsDevice.SetVertexBuffer`/`DrawPrimitives`.

### Behavioral correctness — PASS for Part 2, WEAK for Part 1 (see F1)
- Independently traced `GetOrCreatePipelineFogColored3D` (stride 16, lines 4813-4816):
  `attrs[0] = {0,0,VK_FORMAT_R32G32B32_SFLOAT,0}` (position), `attrs[1] = {1,0,VK_FORMAT_R8G8B8A8_UNORM,12}`
  (color) — matches `GpuVPC{float x,y,z; uint8_t r,g,b,a;}` (test lines 57, 175-184) exactly, and matches
  `VertexElementFormatToVk(Vector3)`/`(Color)` in the helper table.
- Independently traced `GetOrCreatePipelineFogTex3D` (stride 20/24 branch, lines 4939-4953): stride 20 ⇒
  `{0,VK_FORMAT_R32G32B32_SFLOAT,0}`+`{1,VK_FORMAT_R32G32_SFLOAT,12}` (position+uv); stride 24 ⇒ adds
  `{1,VK_FORMAT_R8G8B8A8_UNORM,12}` (color) and moves uv to offset 16 — matches `GpuVPT`/`GpuVPCT` structs
  (lines 58-59, 211-238, 246-277) and the corresponding `VertexDeclaration`s exactly.
- Independently traced `GetOrCreatePipelineLitTextured3DVertexLit` (stride 32, lines 4595-4599):
  `{0,VK_FORMAT_R32G32B32_SFLOAT,0}` (pos), `{1,VK_FORMAT_R32G32B32_SFLOAT,12}` (normal),
  `{2,VK_FORMAT_R32G32_SFLOAT,24}` (uv) — matches `GpuVPNT` (line 60, 285-298) exactly. Also traced
  `lit_textured3d.frag.glsl`'s `if (pc.lightingEnabled > 0.5) ... else color = fragTint * tex;` (lines 44-77)
  — since `testStride32` never calls `EnableDefaultLighting()`/sets `LightingEnabled`, `BasicEffect`'s real
  XNA default (`LightingEnabled=false`) routes through the `else` branch, so the magenta result the test
  expects genuinely comes from `fragTint(white) * tex(magenta)`, not from an untested lit path — confirmed,
  not assumed.
- `DrawPrimitives` dispatch (`DrawPrimitivesEx`, lines 7356-7424) selects the pipeline **purely from
  `vb.GetStride()`** (line 7363: `const std::size_t stride = vb.GetStride() > 0 ? vb.GetStride() : 20;`),
  which is correct and sufficient for these 4 canonical layouts, but see F1/F2 for what this means for the
  claimed generality of Part 1.
- `RasterizerState::CullNone` (line 326) is the same recurring "Task 896" winding workaround seen elsewhere
  in this batch, applied once for all four stride sub-tests via shared `Initialize()` device state.

### Logic — PASS for Part 2's arithmetic; see F1 for Part 1
`colorClose(got, want, tol=30)` (lines 118-123) is appropriately tight for distinguishing the four target
colors (red/green/blue/magenta) used across the sub-tests.

### Architecture — MEDIUM concern (see F1)
`VulkanVertexBufferBackend` (`VulkanGraphicsBackend.hpp:336-361`) stores only a raw byte `stride_`
(`GetStride()`), never the originating `VertexElement`/`VertexDeclaration` list. `VertexBuffer` itself
*does* retain the full `VertexDeclaration` (`vertexDeclaration_`,
`VertexBuffer.hpp:323`/`getVertexDeclarationProperty()`), but `GraphicsDevice::DrawPrimitives()` only ever
forwards `currentVertexBuffer_->GetBackend()` (a bare `IVertexBufferBackend&`) to the backend — the
declaration's actual per-element formats/usages/offsets never reach `VulkanGraphicsBackend` at all. Pipeline
selection in `DrawPrimitivesEx` is consequently **stride-value-keyed**, with each hardcoded
`VkVertexInputAttributeDescription` array baking in an *assumed* layout for that stride (e.g., "stride 20
always means `float3 pos + float2 uv`"). A custom, non-canonical `VertexDeclaration` that happens to match
one of these byte strides by coincidence (e.g., a particle-system vertex using two `Vector2` fields, no
position, also totaling 20 bytes) would be silently bound as if it were `VertexPositionTexture`, with no
detection or error. This is a real architectural limitation of the current Vulkan backend, not a defect
introduced by this test — but it directly limits what this test's own header claim ("proving the vertex
attribute at location=0/1/2 are wired to the correct shader inputs") can actually mean: it is proven true
only for these four specific canonical layouts, not for `VertexDeclaration`-driven binding in general.

### Testing — WEAK for Part 1 (see F1), PASS for Part 2
Part 2 is a strong, independently-confirmed test of 4 real production pipelines. Part 1 asserts a
standalone helper's self-consistency for 12 values, of which only 5 (`Vector2`,`Vector3`,`Vector4`,`Color`,
`Byte4`) have any actual production-pipeline `VkVertexInputAttributeDescription` counterpart anywhere in
this backend to potentially drift from; the other 7 have zero live production consumer to check against.

### Cross-file consistency — PASS with one shared observation
`fx.VertexColorEnabled = true;` (line 190) is the same public-field (not `getX`/`setX`) `BasicEffect`
property surface flagged as an informational cross-file note in this batch's
`vulkan_texture_srgb_test.cpp.audit.md` (F2 there) — not repeated as a separate finding here, but noted for
completeness.

## Detailed Findings

### F1 — `testMappingTable()` validates `VertexElementFormatToVk`/`VertexElementFormatSize` against themselves for 7 of 12 formats; no production Vulkan pipeline in this backend uses the corresponding `VkFormat` at all for those 7

- Severity: MEDIUM
- Confidence: HIGH (confirmed by exhaustive grep of every `VkFormat` literal in the ~9000-line backend
  file, and by reading `VulkanVertexBufferBackend`'s member list directly)
- Category: test-coverage / tautological-test / architecture
- Location/symbol: `testMappingTable()` (lines 127-167); `VertexElementFormatToVk`/`VertexElementFormatSize`
  (`VulkanVertexFormatHelper.hpp:20-60`)
- Evidence: Grepped every `VK_FORMAT_R8G8B8A8_UINT` (Byte4), `VK_FORMAT_R32G32B32A32_SFLOAT` (Vector4),
  `VK_FORMAT_R32G32B32_SFLOAT` (Vector3), `VK_FORMAT_R32G32_SFLOAT` (Vector2), `VK_FORMAT_R8G8B8A8_UNORM`
  (Color) occurrence across `VulkanGraphicsBackend.cpp` and found real production
  `VkVertexInputAttributeDescription` entries for all five (e.g. `aBoneIndices` using `R8G8B8A8_UINT` at
  lines 5218/5348/5830; `aBoneWeights`/`aTangent`/sprite-color/instance-matrix rows using
  `R32G32B32A32_SFLOAT` at lines 2370/5217/5580/5954-5957). By contrast, `VK_FORMAT_R32_SFLOAT` (Single),
  `VK_FORMAT_R16G16_SINT`/`R16G16B16A16_SINT` (Short2/Short4), `VK_FORMAT_R16G16_SNORM`/
  `R16G16B16A16_SNORM` (NormalizedShort2/4), and `VK_FORMAT_R16G16_SFLOAT`/`R16G16B16A16_SFLOAT`
  (HalfVector2/4) do not appear **anywhere else** in the entire file — the only place these `VkFormat`
  values are ever produced is inside `VertexElementFormatToVk` itself. Additionally confirmed
  `VulkanVertexBufferBackend` (`VulkanGraphicsBackend.hpp:336-361`) stores only a raw `stride_`, never a
  `VertexElement`/`VertexDeclaration`, and that `VertexBuffer`'s own stored `vertexDeclaration_`
  (`VertexBuffer.hpp:323`) is never forwarded past `GetBackend()` to the Vulkan backend — confirming there
  is currently no code path in this backend, generic or otherwise, that could ever consume a `Short2`/
  `HalfVector2`/etc.-typed vertex element even if one were declared.
- Why it matters: the file's own header claims "Also validates the VulkanVertexFormatHelper mapping table
  for all 12 VertexElementFormat values," implying this is a meaningful contract check. For 7 of those 12
  values, the check can only ever fail if someone edits the switch statement in
  `VertexElementFormatToVk`/`VertexElementFormatSize` to disagree with itself — it provides zero protection
  against `VulkanVertexFormatHelper.hpp`'s own doc comment's stated goal ("These mappings must match the
  hardcoded `VkVertexInputAttributeDescription` arrays in `VulkanGraphicsBackend` pipeline creation
  functions") for any format not already used by one of the ~17 hand-written pipeline functions. A future
  attempt to add real `Short2`/`HalfVector2`-based vertex support to this backend (e.g. compressed UVs)
  could pick a `VkFormat` inconsistent with this table, and this test would still report 100% pass, because
  it never touches the actual new code.
- FNA/XNA comparison: N/A — this is a CNA-internal test-design/architecture question, not an XNA behavior
  question (all 12 `VertexElementFormat` values themselves are real XNA enum members and are represented
  correctly by the table's *own* logic).
- Related files: `include/CNA/Internal/Backends/Vulkan/VulkanVertexFormatHelper.hpp`,
  `include/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.hpp:336-361`,
  `include/Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp:267,323`.
- Suggested future action (not implemented by this audit): either (a) extend Part 2 with at least one
  pixel-readback sub-test that actually exercises one of the currently-unused formats through a real custom
  effect/pipeline, or (b) explicitly document in this file's own header that Part 1 is a standalone-contract
  check for future implementers rather than a live cross-check against current pipeline code, so the
  distinction this audit had to independently establish is visible to a future reader without re-deriving it.

## Cross-File Observations

- The `VertexColorEnabled` public-field `BasicEffect` API surface (line 190) is the same one flagged as an
  informational cross-file note (F2) in this batch's `vulkan_texture_srgb_test.cpp.audit.md`; not
  duplicated as a separate finding here.
- The `RasterizerState::CullNone` "Task 896" comment (line 324-326) recurs identically across this file,
  `vulkan_texture_srgb_test.cpp`, and `vulkan_texture_mip_filter_effect_test.cpp` in this same batch.
- This file predates (2026-06-27) the AMD/RADV blank-frame-flake retry-loop pattern established in
  `vulkan_scissor_test.cpp` (2026-06-29); unlike the other files in this batch that omit the retry loop
  despite being authored *after* that pattern existed, this file's omission is chronologically explainable
  — noted here as context, not repeated as its own MEDIUM finding, since the underlying risk (a spurious
  all-black readback on a real documented driver flake) is nonetheless still live for this file's
  `Initialize()`-time `GetBackBufferData` calls today.
- Unlike this file's sibling in the `bgfx` shard (`examples/bgfx_vertex_format_test.cpp`), which per its own
  grep hit only calls `VertexElementFormatSize` (not a Vk-specific mapping function), this Vulkan file's
  Part 1 additionally checks a `VkFormat` table — a slightly larger, but per F1 not proportionally more
  meaningfully cross-checked, surface.

## Missing or Weak Tests

- See F1 — 7 of 12 `VertexElementFormat` values in `testMappingTable()` have no live pipeline cross-check.
- No sub-test exercises a stride that does *not* correspond to one of the backend's known canonical
  layouts (e.g., a deliberately "wrong" 20-byte custom declaration using two `Vector2` fields instead of
  `Vector3`+`Vector2`) to demonstrate the stride-only dispatch limitation described in the Architecture
  section — such a test would presumably currently render incorrectly/silently, which is itself worth
  knowing but is not this file's job to prove without a scope change.

## Positive Findings

- Part 2's four pixel-readback sub-tests are genuinely strong: each was independently cross-checked against
  the real, hand-written `VkVertexInputAttributeDescription` arrays for that exact stride in
  `VulkanGraphicsBackend.cpp`, and all four were confirmed to match the test's own `Gpu*` structs exactly,
  including subtle offset details (e.g. stride-24's UV at byte offset 16, after a 4-byte color field at
  offset 12).
- The `testStride32` "no lighting" pixel-color reasoning was independently verified against the actual
  `lit_textured3d.frag.glsl` shader logic (not just assumed from the comment), confirming the magenta result
  really does come from the untinted-texture branch, not an accidentally-still-lit path.
- Static asserts on the four `Gpu*` struct sizes (lines 62-65) are a good, cheap guard against silent
  struct-padding drift on this or a future compiler/ABI.

## Final Assessment

Part 2 of this file is a rigorous, independently-confirmed test of real Vulkan pipeline vertex-attribute
wiring for the four canonical XNA vertex layouts. Part 1, despite its header's framing as validating "the
VulkanVertexFormatHelper mapping table," is materially weaker than advertised: for the majority of tested
`VertexElementFormat` values there is currently no production code path in this backend that could ever
diverge from (or even reach) the table being checked, because per-`VertexElement` format information is
discarded before reaching the Vulkan backend and pipeline selection is keyed purely on raw byte stride.
