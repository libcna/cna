# Audit: examples/bgfx_vertex_format_test.cpp

## Metadata

- Source file: `examples/bgfx_vertex_format_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — `BgfxVertexFormatHelper` mapping-table + `VertexBuffer` smoke test
- File type: standalone `Game`-subclass executable, CTest-registered (`cna_bgfx_test(cna_test_bgfx_vertex_format …)`
  / `cna_register_backend_test(NAME Bgfx_VertexFormatMapping …)`, `cmake/Tests/BgfxTests.cmake:160-164`); Task 249
  per its own header comment and confirmed by `git log` (`9c585fc0`/`9481b7d3 feat(Task 249): Bgfx vertex layout
  mapping tests — all 12 VEF + 13 VEU values`).
- XNA/FNA relevance: indirect — `VertexElementFormat`/`VertexElementUsage` are XNA-facing enums
  (`Microsoft::Xna::Framework::Graphics`), but the file under test, `BgfxVertexFormatHelper.hpp`, is a pure CNA
  backend-internal helper with no FNA counterpart (bgfx has no XNA analog).
- FNA reference: `FNA/src/Graphics/Vertices/VertexElementFormat.cs`, `VertexElementUsage.cs` (enum definitions),
  `VertexDeclaration.cs` `GetTypeSize()` (byte-size table).
- Related production code: `include/CNA/Internal/Backends/Bgfx/BgfxVertexFormatHelper.hpp` (header-only, no
  `.cpp`), `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`MakeBgfxLayout()` lines 2008-2093,
  `BgfxVertexBufferBackend::SetData()` lines 2109-2127, `CreateVertexBuffer()` lines 2129-2132),
  `src/Microsoft/Xna/Framework/Graphics/VertexBuffer.cpp` (ctor lines 29-38).

## Purpose

Two-phase test: (1) `CheckMappingTable()` — a pure enum-comparison sweep run *before* `Game`/bgfx initialization,
asserting `VertexElementFormatToBgfx()` and `VertexElementFormatSize()` against expected values for all 12
`VertexElementFormat` entries, and `VertexElementUsageToBgfxAttrib()` against all 13 `VertexElementUsage` values
(including the 5 that have no bgfx equivalent and must return the `bgfx::Attrib::Count` sentinel); (2) a
`Game::Draw()`-driven loop (`BgfxVertexFormatTest`) that constructs a `VertexBuffer` at each of the project's four
standard vertex strides (16/20/24/32, matching `VertexPositionColor`/`VertexPositionTexture`/
`VertexPositionColorTexture`/`VertexPositionNormalTexture`) and checks that construction does not throw.

## Executive Verdict

**Needs attention** — phase 1 (`CheckMappingTable`) is a genuine, fully-verified, boilerplate-free test: every one
of its 12+12+18 assertions was independently checked against `BgfxVertexFormatHelper.hpp`'s actual switch
statements and matches exactly. Phase 2, however, is materially weaker than its own header comment and the file's
Task 249 CMake description claim: it never calls `VertexBuffer::SetData`, so it can never exercise
`BgfxVertexBufferBackend::SetData()`'s stride-dependent `MakeBgfxLayout(stride_in_bytes)` re-derivation at all —
every one of the four "stride 16/20/24/32" smoke-test cases actually only ever constructs the Bgfx backend's
*default* stride-16 layout (see F1) and is trivially unfailable. Separately (F2, cross-file, most significant):
the `BgfxVertexFormatHelper.hpp` mapping table this file rigorously validates is not called anywhere in
`BgfxGraphicsBackend.cpp`'s actual vertex-layout construction — `MakeBgfxLayout()` dispatches purely on raw byte
stride via hardcoded `if (stride == N)` cases, never consulting a `VertexElement`'s `VertexElementFormat` or
`VertexElementUsage` at all, so this test's entire subject (the format/usage mapping) has no bearing on how the
Bgfx backend actually lays out a real `VertexBuffer`.

## Checklist Results

### Purpose
Correctly placed under `examples/` as a backend-integration CTest executable; namespace usage (`using namespace
CNA::Internal::Backends::Bgfx;`) is appropriate for a backend-internal helper test. PASS.

### API / XNA / FNA parity
N/A for the helper itself (`CNA` namespace, no XNA surface), but the enums it maps are XNA-facing and were checked
against FNA:
- `VertexElementFormat.hpp` (CNA) declares exactly the same 12 values, in the same order, as
  `FNA/VertexElementFormat.cs` (`Single…HalfVector4`) — no extra/missing members.
- `VertexElementUsage.hpp` (CNA) declares the same 13 values as `FNA/VertexElementUsage.cs`.
- The test's `CheckSize()` byte-size table (lines 147-158) was checked field-by-field against
  `FNA/VertexDeclaration.cs GetTypeSize()` (lines 150-180): `Single=4, Vector2=8, Vector3=12, Vector4=16, Color=4,
  Byte4=4, Short2=4, Short4=8, NormalizedShort2=4, NormalizedShort4=8, HalfVector2=4, HalfVector4=8` — every value
  matches exactly.
PASS (helper-table-vs-FNA-byte-size parity confirmed exactly).

### Behavioral correctness
`CheckMappingTable()`'s 12 `CheckFmt(...)` calls (lines 133-144) were each individually diffed against
`BgfxVertexFormatHelper.hpp::VertexElementFormatToBgfx()`'s switch (lines 41-53): `Single/Vector2/Vector3/Vector4`
→ `Float,{1,2,3,4},false,false`; `Color`→`Uint8,4,true,false`; `Byte4`→`Uint8,4,false,true`; `Short2/Short4`→
`Int16,{2,4},false,true`; `NormalizedShort2/NormalizedShort4`→`Int16,{2,4},true,false`; `HalfVector2/HalfVector4`→
`Half,{2,4},false,false` — all 12 rows match the production switch exactly, no transposed/stale value found (in
contrast to the sibling EasyGL specular test's stale constant found in a prior audit batch). The 18
`CheckUsage(...)` calls (lines 161-184) were likewise diffed against `VertexElementUsageToBgfxAttrib()`'s switch
(lines 72-105): `Position/Normal/Tangent/Binormal/BlendIndices/BlendWeight` map 1:1 to
`Position/Normal/Tangent/Bitangent/Indices/Weight`; `Color[0..3]`→`Color0..Color3`; `TextureCoordinate[0..7]`→
`TexCoord0..TexCoord7`; `Depth/Fog/PointSize/Sample/TessellateFactor`→`Count` sentinel — all match. PASS for phase
1's actual correctness-of-assertion.

However, phase 2 (`Game::Draw()` / `UploadAndCheck`) has a genuine defect — see F1.

### Logic
`UploadAndCheck()` (lines 194-222) constructs a `VertexBuffer(dev, decl, vertexCount, BufferUsage::None)` inside a
`try`/`catch`, then unconditionally reports `[PASS]` and explicitly discards its own `data`/`vertexCount`
parameters (`(void)data; (void)vertexCount;`, lines 213-214) without ever calling `VertexBuffer::SetData(...)`.
Its own inline comment (lines 204-210) claims *"data upload is done in the stride-16 case via the concrete type
helper below"* — no such helper exists anywhere else in the 281-line file; this is a stale/incorrect comment
describing code that was never written (or was removed and the comment never updated). See F1.

### C++ correctness
`GpuVPC`/`GpuVPT`/`GpuVPCT`/`GpuVPNT` (lines 46-54) are correctly `static_assert`-verified packed structs
(16/20/24/32 bytes) with no padding surprises. `BgfxAttribInfo`/`bgfx::AttribType::Enum`/`bgfx::Attrib::Enum`
comparisons use plain `==`, safe for these scoped/plain enums. No UB, aliasing, or lifetime issues found in either
phase. PASS.

### Memory/resource lifetime
`VertexBuffer vb(...)` (line 203) is stack-scoped, destroyed at the end of `UploadAndCheck()`'s try block — no
leak. `GraphicsDeviceManager` is owned via `unique_ptr` on the `Game` subclass, standard pattern matching sibling
Bgfx test files. PASS.

### Robustness
The `try`/`catch (const std::exception&)` around `VertexBuffer` construction is reasonable protective structure
for a "must not crash" smoke test in principle, but see F1 — because nothing after construction (`SetData`) is
ever exercised, there is no actual code path in this test that *could* throw for a stride-related reason; the
try/catch's protective value is close to zero in this specific instantiation.

### Testing
Phase 1 (`CheckMappingTable`) is complete, exact, and correctly covers all 12 `VertexElementFormat` and all 13
`VertexElementUsage` values including the 5 "unsupported" sentinel cases — a genuinely strong test of the helper's
internal self-consistency. Phase 2 is the weak half: see F1 (does not actually upload data despite its name/header
claim) and F2 (the mapping table it validates has zero bearing on the actual Bgfx vertex-layout code path for any
vertex whose byte stride is not one of `MakeBgfxLayout()`'s hardcoded special-cased strides — and even for those
that are, the *content* of the layout is picked by stride alone, not by consulting `VertexElementFormat`/
`VertexElementUsage`, so this test cannot detect the mapping table going out of sync with what the backend
actually does for a real custom-format vertex declaration).

## Detailed Findings

### F1 — Phase 2's "VertexBuffer upload" smoke test never uploads data, and always exercises the same hardcoded stride-16 backend layout regardless of the label/stride under test

- Severity: HIGH
- Confidence: HIGH (fully traced: `UploadAndCheck()` → `VertexBuffer` ctor → `GraphicsDevice::backend_->CreateVertexBuffer(vertexCount)` → `BgfxGraphicsBackend::CreateVertexBuffer(int capacity)` (`BgfxGraphicsBackend.cpp:2129-2132`) → `BgfxVertexBufferBackend::BgfxVertexBufferBackend(int capacity)` (line 2095-2102), whose body is exactly `layout = MakeBgfxLayout(16); handle = bgfx::createDynamicVertexBuffer(...)` — **unconditionally 16**, independent of any `decl`/stride argument, because `VertexBuffer`'s own constructor (`VertexBuffer.cpp:29-38`) forwards only `vertexCount` to `CreateVertexBuffer`, never `vertexDeclaration`. `MakeBgfxLayout(stride_in_bytes)` is only re-invoked with the *real* stride inside `BgfxVertexBufferBackend::SetData()` (line 2109-2127) — which this test never calls.)
- Category: test-coverage / correctness-of-test
- Location/symbol: `UploadAndCheck()` (lines 194-222), all four call sites in `Draw()` (lines 236-256)
- Evidence: `UploadAndCheck` takes `data`/`vertexCount`/`stride` parameters purely for display/labeling
  (`std::printf(...stride...label...)`) and then explicitly discards `data`/`vertexCount` with `(void)` casts
  (lines 213-214) without any call resembling `vb.SetData(...)`. Its own comment claims *"data upload is done in
  the stride-16 case via the concrete type helper below"* (line 209) — there is no such helper anywhere in this
  file.
- Why it matters: the four `[PASS] VertexBuffer(stride=N) created` lines this test prints for
  16/20/24/32 are all guaranteed to pass unconditionally — none of them ever reach
  `BgfxVertexBufferBackend::SetData()`'s stride-dependent `MakeBgfxLayout(stride_in_bytes)` call, so the file's own
  top-of-file claim ("The four standard vertex strides (16/20/24/32) are uploaded", line 10) and the CMake
  registration comment ("Task 249: Bgfx vertex layout mapping tests — all 12 VertexElementFormat values", line
  160-161 of `BgfxTests.cmake`) both overstate what phase 2 actually verifies. A regression that broke
  `MakeBgfxLayout(20)`/`MakeBgfxLayout(24)`/`MakeBgfxLayout(32)` (e.g. swapping the `TexCoord0` attribute count, or
  corrupting the stride-24 branch's interleaved Color0/TexCoord0 order) would not be caught by this file at all,
  because none of those code paths are ever reached — every one of the four `UploadAndCheck` calls silently
  constructs the exact same stride-16 `Position+Color0` layout underneath, regardless of the `decl`/label passed
  in.
- FNA/XNA comparison: N/A (CNA-internal backend-wiring gap, not an XNA/FNA behavior question).
- Related files: `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp` (`MakeBgfxLayout`,
  `BgfxVertexBufferBackend::SetData`), `src/Microsoft/Xna/Framework/Graphics/VertexBuffer.cpp`.
- Suggested future action (not implemented by this audit): have `UploadAndCheck` actually call
  `vb.SetDataRaw(data, vertexCount, stride)` (the `NOXNA` raw-stride escape hatch already declared in
  `VertexBuffer.hpp`) so `BgfxVertexBufferBackend::SetData()` is genuinely exercised with each of the four real
  strides, and ideally read back `vb`'s resulting bgfx layout (or add a backend-internal accessor) to assert the
  actual attribute list rather than only "did construction throw."

### F2 — `BgfxVertexFormatHelper.hpp`'s mapping table (the subject of this entire test file) is not used anywhere in the Bgfx backend's real vertex-layout construction

- Severity: HIGH
- Confidence: HIGH (grepped the full `src/`/`include/` tree: `VertexElementFormatToBgfx`,
  `VertexElementUsageToBgfxAttrib`, and `BgfxVertexFormatHelper` appear only in the header itself and in this test
  file — zero references from `BgfxGraphicsBackend.cpp` or any other production `.cpp`)
- Category: architecture / test-coverage
- Location/symbol: `include/CNA/Internal/Backends/Bgfx/BgfxVertexFormatHelper.hpp` (whole file, unused),
  `src/CNA/Internal/Backends/Bgfx/BgfxGraphicsBackend.cpp:2008-2093` (`MakeBgfxLayout(std::size_t stride)`)
- Evidence: `MakeBgfxLayout()` dispatches purely on `if (stride == 52) … else if (stride == 20) … else if (stride
  == 24) … else if (stride == 32) … else if (stride == 48) … else if (stride == 56) … else if (stride == 68) …
  else { /* assume VertexPositionColor: Position float3 + Color0 normalized ubyte4, padded to stride */ }` — every
  branch hand-writes a fixed `bgfx::Attrib`/`AttribType` sequence for a *known, hardcoded* stock XNA vertex struct
  (`VertexPositionTexture`, `VertexPositionColorTexture`, …, skinned/PBR variants), never inspecting
  `VertexDeclaration::GetVertexElements()`/`VertexElementFormat`/`VertexElementUsage` at all. Confirmed this is not
  an oversight specific to construction: `VertexBuffer`'s constructor (`VertexBuffer.cpp:29-38`) stores the
  caller's `VertexDeclaration` in `vertexDeclaration_` but never forwards it past `CreateVertexBuffer(vertexCount)`
  — `IGraphicsBackend::CreateVertexBuffer` takes only a capacity, and no code in the tree (`grep -rn
  "GetVertexElements"` under `src/CNA/Internal/Backends/`) ever reads a real `VertexElement` list to build a GPU
  layout. The same hardcoded-stride pattern independently exists in `VulkanGraphicsBackend.cpp` (its own
  `VulkanVertexFormatHelper.hpp` is equally unreferenced) and is explicitly cross-referenced from `MakeBgfxLayout`
  itself (lines 2014-2019: *"this layout is independently duplicated (magic stride 52) in
  EasyGLGraphicsBackend.cpp's ApplyLayout and VulkanGraphicsBackend.cpp's GetOrCreatePipelineSkinned3D"*) — i.e.
  this is a known, intentional, cross-backend simplification, not a one-off bug in this file, but it means the
  helper this test file exists to validate has no live production consumer.
- Why it matters: any custom `VertexDeclaration` a game defines with an unusual `VertexElementFormat`/
  `VertexElementUsage` combination whose resulting byte stride does not happen to collide with one of
  `MakeBgfxLayout`'s special-cased magic numbers (16/20/24/32/48/52/56/68) silently falls into the final `else`
  branch and is treated as `Position(float3) + Color0(normalized ubyte4)` — i.e. the Bgfx backend will silently
  misinterpret that vertex's real layout (wrong attribute semantics, wrong component types) rather than erroring
  or actually deriving the correct layout from the declaration. `BgfxVertexFormatHelper.hpp`'s careful,
  fully-correct mapping table (confirmed exactly right by this very test) therefore currently has no effect on
  what a game actually sees rendered — it is dead code from the production backend's point of view, and this
  test's 100%-pass result gives no signal about the real per-vertex-declaration GPU behavior a game author would
  observe.
- FNA/XNA comparison: FNA's `VertexBuffer.SetData<T>(T[] data) where T : struct` is fully generic over any
  user-defined vertex struct/`VertexDeclaration`; CNA's `VertexBuffer` (see `VertexBuffer.hpp`) only exposes
  concrete overloads for the built-in stock types (`VertexPositionColor`, `VertexPositionColorTexture`,
  `VertexPositionNormalTexture`, `VertexPositionTexture`, `VertexPositionNormalTextureSkinned`) plus a `NOXNA`
  `SetDataRaw(void*, count, stride)` escape hatch — this Bgfx-side finding is a direct, concrete consequence of
  that broader (pre-existing, project-wide) API gap, not a new discovery about `VertexBuffer` itself.
- Related files: `src/CNA/Internal/Backends/Vulkan/VulkanGraphicsBackend.cpp` (identical pattern, its own
  `VulkanVertexFormatHelper.hpp` is equally unused), `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`ApplyLayout`, same stride-based dispatch per its own cross-referenced comment).
- Suggested future action (not implemented by this audit): either (a) thread `VertexDeclaration`'s actual
  `VertexElement` list down to `CreateVertexBuffer`/`SetData` so `MakeBgfxLayout` can call
  `VertexElementFormatToBgfx`/`VertexElementUsageToBgfxAttrib` per-element for genuinely arbitrary declarations
  (making the helper load-bearing), or (b) if the hardcoded-stock-type-only design is an accepted, permanent
  scope limit, document that explicitly in `BgfxVertexFormatHelper.hpp`'s own header comment so a future reader
  does not assume (as this test file's name implies) that it drives real vertex-layout selection.

## Cross-File Observations

- `VulkanVertexFormatHelper.hpp`'s `VertexElementFormatSize()` is explicitly called out in this file's own
  doc-comment as "Identical to VulkanVertexFormatHelper::VertexElementFormatSize()" (line 111) — confirmed true by
  inspection, but both are equally unused by their respective backends' actual vertex-layout code (F2 applies
  symmetrically to `examples/vulkan_vertex_format_test.cpp`, out of scope for this report but worth flagging for
  whichever shard covers it).
- `examples/vulkan_vertex_format_test.cpp` (sibling file, `examples-tests-vulkan` shard) calls
  `VertexElementFormatSize` directly too (`vulkan_vertex_format_test.cpp:157-158`) — same helper-validated/
  production-unused pattern.
- This is the only Bgfx pixel-independent example test in the shard that runs assertions *before*
  `Game`/bgfx initialization (`CheckMappingTable()` in `main()` before `game.Run()`), which is a reasonable and
  correct structural choice since the mapping table has no GPU dependency.

## Missing or Weak Tests

- See F1: none of the four `UploadAndCheck` calls actually exercise `BgfxVertexBufferBackend::SetData()`'s
  stride-dependent layout re-derivation; a call to `SetData`/`SetDataRaw` per stride is needed to make this a real
  regression guard for `MakeBgfxLayout()`'s 20/24/32/48/52/56/68 branches.
- No test anywhere in this shard exercises a `VertexDeclaration` whose stride does *not* match one of
  `MakeBgfxLayout`'s hardcoded cases, so the silent Position+Color0 misinterpretation fallback (F2) has no
  regression coverage at all across the whole Bgfx test suite, not just this file.

## Positive Findings

- `CheckMappingTable()`'s 12 format checks + 12 size checks + 18 usage checks (42 total assertions) were all
  individually cross-checked against the actual `BgfxVertexFormatHelper.hpp` production switch statements and all
  match exactly — a genuinely thorough, non-boilerplate validation of that helper's internal correctness.
- The byte-size table was independently cross-checked against FNA's `VertexDeclaration.GetTypeSize()` and matches
  exactly for all 12 `VertexElementFormat` values — real XNA/FNA parity verification, not just internal
  self-consistency.
- The four `static_assert(sizeof(...) == N)` checks on the packed GPU vertex structs (lines 51-54) are a good,
  cheap correctness guard against accidental struct-packing regressions.

## Final Assessment

Phase 1 of this file is a strong, fully-verified unit test of `BgfxVertexFormatHelper.hpp`'s mapping tables.
Phase 2 is materially weaker than advertised (F1: never uploads data, always exercises the same hardcoded
stride-16 layout) and, more importantly, this audit found that the entire subject of the file — the
`VertexElementFormat`/`VertexElementUsage` → bgfx mapping — has no live production consumer in the actual Bgfx
vertex-layout path (F2): `MakeBgfxLayout()` dispatches on raw byte stride alone. This is a pre-existing,
cross-backend (Bgfx/Vulkan/EasyGL) architectural simplification, not something introduced by this test file, but
it means a 100%-passing run of this file provides no assurance about how the Bgfx backend actually lays out a
real, arbitrary-format `VertexBuffer` — only that a well-known, separately-maintained hardcoded stride table
happens to cover the project's own stock vertex types correctly.
