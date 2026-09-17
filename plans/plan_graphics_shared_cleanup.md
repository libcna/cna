# Graphics shared-correctness and test-harness cleanup

A deliberately small workstream: close the shared defects and test debts the DirectX 11/12 Windows
validation left open (`plans/plan_directx12_parity.md`), before the next large renderer project. No new
renderer, no Vulkan modernization, no new CNAEXT API. Task IDs: `GSC-0001`, `GSC-0002`, … .

Evidence classes are kept apart as in the DX12 plan: DirectX12 runs are **WARP** (`CNA_D3D12_ADAPTER=warp`),
DirectX11 runs are the **VirtualBox SVGA** adapter of `win10_local`, Linux runs are Mesa under Xvfb `:99`.
None of it is physical-GPU evidence.

## Status

| ID | Task | Status |
|---|---|---|
| GSC-0001 | Baseline, reproduction, plan | ✅ |
| GSC-0002 | Two-sided stencil faces on DirectX11/DirectX12 | ✅ |
| GSC-0003 | SkinnedEffect bone normals under non-uniform scale on DirectX11/DirectX12 | ✅ |
| GSC-0004 | Missing-texture semantics: measured XNA rule, stock effects vs glTF policy | ✅ |
| GSC-0005 | Back-buffer depth format contract (`PresentationFormatContract`) | ✅ |
| GSC-0006 | Direct3D debug-layer messages fail tests | ✅ |
| GSC-0007 | `CaseInsensitivePathTest` contract (formerly WINNATIVE-F26) | ✅ |
| GSC-0008 | Audio test fixtures isolated per process (`%TEMP%` race) | ✅ |

Follow-ups found on the way are listed at the end; none of them was fixed here.

---

## GSC-0001 — Baseline

| Fact | Value |
|---|---|
| Baseline | `df722e4a320a4881c41cbac68c270413817e199c` (= `origin/next`, 2026-09-17; the DX12 parity merge) |
| Branch | `graphics-shared-cleanup` |
| Guest trees | `C:\cna\build\full-win32-d3d12-nosdl`, `C:\cna\build\full-win32-d3d11-nosdl` (MSVC 19.44, Ninja, RelWithDebInfo) |
| Linux tree | `cmake-build-multi` (OPENGL33 default; VULKAN, SOFTWARE, HEADLESS compiled in; X11 platform) |

Reproduced on the baseline binaries before any change (DX12, WARP):

| Suite | Result |
|---|---|
| `DirectX12_DepthStencilState_StencilTwoSided` | FAIL — `TwoSidedStencilMode=true` column reads the background `(20,20,20)` |
| `DirectX12_SkinnedEffect_WorldNormal` | FAIL 10/12 — `non-uniform bone scale(1,2,1)`: got 90, expected 212, both lighting modes |
| `DirectX12_PresentationFormatContract` | FAIL — 8 checks demand a fixed Depth24Stencil8 |
| `DirectX12_GraphicsAdapterQueryContract` | FAIL — "device Reset applies the adapter query's fixed depth format" |
| `CaseInsensitivePathTest`, 3 rounds × 12 parallel shards | 2 deliberate failures every round |
| audio suites, same 3 rounds | round 2: `AudioEngineTest.SetGlobalVariableEmptyNameThrowsArgumentNull` failed — the `%TEMP%` race |

---

## GSC-0002 — Two-sided stencil

**Expected (XNA).** `DepthStencilState::Apply` in `Microsoft.Xna.Framework.Graphics.dll` (IL) writes
`StencilFunction/Pass/Fail/DepthBufferFail` to `D3DRS_STENCIL*` and, with `TwoSidedStencilMode`, the
`CounterClockwise*` fields to `D3DRS_CCW_STENCILFAIL/ZFAIL/PASS/FUNC` (`0xBA`–`0xBD`). Direct3D 9 applies the
CCW states to counter-clockwise triangles — the winding `D3DCULL_CCW` (`CullCounterClockwiseFace`) removes.

**Root cause.** Shared by both D3D renderers, in state translation — not stencil-op mapping and not
winding. Both rasterizers set `FrontCounterClockwise = TRUE` and map `CullCounterClockwiseFace` to
`CULL_FRONT` (correct; culling fixtures pass), so counter-clockwise triangles are **FrontFace**; but both the
D3D11 depth-stencil cache and the D3D12 PSO cache wrote the `CounterClockwise*` operations to **BackFace**.
The two faces were swapped. FNA3D's Direct3D 11 driver has the identical wiring (its comment is where the
CNA line came from); CNA follows XNA.

**Change.** `D3D11StateObjectCache.cpp` and `D3D12PipelineStateCache.cpp`: ordinary → BackFace, CCW →
FrontFace when two-sided, both faces ordinary otherwise. Culling untouched.

**Regression test.** `TwoSidedStencilTests.cpp` (renderer-neutral gtest, Software/OpenGL33/OpenGLES3/
DirectX11/DirectX12): both windings drawn in one pass and probed separately, so a swap, an ignored mode and a
both-faces application each give a different named result — per field (pass, fail, depth-buffer fail),
the CCW function alone, `TwoSidedStencilMode=false`, agreement with `CullCounterClockwiseFace`/
`CullClockwiseFace`, and a render target as well as the back buffer.

## GSC-0003 — SkinnedEffect normals under non-uniform scale

**Expected (XNA).** Microsoft's XNA 4.0 stock-effect source (`SkinnedEffect.fx`, "Microsoft XNA Community
Game Platform", carried by FNA) skins the normal directly — `vin.Normal = mul(vin.Normal,
(float3x3)skinning)` — and only the later World transform uses `WorldInverseTranspose`. With bone
`Scale(1,2,1)` and normal `(0,.6,.8)` the lit value is `N·L = .832 → 212`; the inverse transpose gives
`.351 → 90`. EasyGL and Software already follow XNA (SOFTWARE-115) and pass.

**Root cause.** The four shared D3D stock skinned vertex shaders (`skinned3d`, `skinned3d_vertexlit`,
`skinned_colored3d`, `skinned_colored3d_vertexlit`) applied a determinant-signed cofactor (inverse transpose)
to the bone matrix — the glTF non-uniform-joint policy of GLTF-264, adopted by DX-230 one day before
SOFTWARE-115 restored XNA's rule on EasyGL. Math, not constants or permutations: every permutation shared it.

**Change.** A `SkinNormal` helper — the weighted bone 3×3 applied directly, keeping the bind-pose normal
only for a zero-length result — in all four shaders, then the unchanged inverse-transpose World. The CNAEXT
PBR skin (`pbr_skinned3d.vert.hlsl`) keeps its joint inverse transpose. `hlsl_shaders.hpp` regenerated with
`compile_shaders_hlsl.py` (MinGW tool under Wine, `TMPDIR` in `build-probe/`): exactly the 8 skinned
vertex-shader arrays changed; the other 50 are byte-identical, so the toolchain matches the committed one.

**Regression test.** The corpus fixture `SkinnedEffect_WorldNormal` already separates 212 from 90 in both
lighting modes and is registered for both D3D renderers; it is the deterministic check.

## GSC-0004 — Missing textures

**Evidence first.** SOFTWARE-303's "five Microsoft XNA probes" were prose only — no probe or output is in
the repository. A focused measurement was therefore made before production changed: seven scenes in
`tools/xna-oracle/scenes/null-texture/`, rendered by the real XNA 4.0 runtime (`Oracle.cs` under Wine,
DXVK D3D9, HiDef), references checked in at `tools/xna-oracle/reference/null-texture/`. A new scene key
`texturenull` (both oracle sides) asks for `TextureEnabled=true` with no texture object. The harness itself
was first checked: `skinned_quad.scene` reproduced its existing reference with 0/65 536 pixels different.
The runtime is Microsoft's XNA 4.0 assemblies; the Direct3D 9 underneath is DXVK on the host GPU, the same
arrangement every existing reference in `tools/xna-oracle` was captured with. It is not native Windows D3D9.

| Scene (XNA 4.0) | Centre pixel | Reading |
|---|---|---|
| BasicEffect, TextureEnabled, Texture null | `(0,0,0,255)` | opaque black |
| SkinnedEffect, Texture null | `(0,0,0,255)` | opaque black |
| AlphaTestEffect, Texture null | `(0,0,0,255)` | opaque black |
| DualTextureEffect, Texture null / Texture2 null | `(0,0,0,255)` / `(0,0,0,255)` | opaque black |
| EnvironmentMapEffect, Texture null, cube (200,100,50), amount .5 | `(100,50,25,255)` | lerp(black, cube) |
| EnvironmentMapEffect, EnvironmentMap null, white texture, amount .5 | `(128,128,128,255)` | lerp(white, black) |

**Final rule.**

* **Classic stock effects** (BasicEffect with `TextureEnabled`, SkinnedEffect, AlphaTestEffect,
  DualTextureEffect, both EnvironmentMapEffect slots): an unbound texture samples **opaque black**. A
  BasicEffect without `TextureEnabled` never samples.
* **CNAEXT PBR** base colour and maps keep their identity fallbacks (white / flat normal) — not XNA API.
* **glTF import** is policy, not effect semantics: glTF's missing `baseColorTexture` is a white
  multiplier, so the importer binds a real 1×1 opaque-white texture to an untextured glTF `SkinnedEffect`
  (runtime `.gltf/.glb` and compiled `.cnj`/`.cnb` models with the glTF material policy). BasicEffect needs
  nothing (it stays untextured); PBR has its own white.

**Changes.** DirectX11: SkinnedEffect and the BasicEffect branch bind opaque black (were white, GLTF-386);
EnvironmentMapEffect binds opaque-black 2D and cube fallbacks (were null views = transparent black, which
also zeroed alpha). DirectX12: the fallback fill binds opaque black for every classic draw, white only for
the PBR base colour. EasyGL: a draw with `textureEnabled=false` binds the white identity — its lit programs
multiply unit 0 in unconditionally, and since SOFTWARE-303's black fallback **every lit untextured
BasicEffect on EasyGL rendered black** (found by the new test; small, same rule, fixed here). ContentManager:
`ApplyGltfWhiteBaseColorTextureEXT`.

**A third case the fallbacks were hiding.** `AlphaTestEffect` with `AlphaFunction=Always` never selects the
dedicated alpha-test program (both pass weights are non-negative), so it takes the ordinary textured branch —
where both D3D renderers bound white. DX12-0021 had fixed only the real alpha-test program. The same fallback
change fixes it; the mutation check below shows it failing on the baseline code.

**Tests.** `StockEffectNullTextureTests.cpp` (values = the XNA references; each case first draws a real red
texture so a stale binding cannot pass); `GltfUnlitMaterial.AnUntexturedSkinSamplesGltfWhiteRatherThanAnUnboundTexture`;
`XnbModelSourceRouteTest.AnUntexturedGltfSkinBuiltToCnbCarriesGltfWhite`; the DX11 source audit now pins white
to the PBR base colour only.

## GSC-0005 — Back-buffer depth format

**Rule.** The back buffer allocates and reports the **requested** `PresentationParameters.DepthStencilFormat`
(`None` allocates nothing; DXGI has no 24-bit depth-only format, so `Depth24` is a D24S8 resource with
stencil disabled, WINCLOSE-0012). The **default** request is `GraphicsDeviceManager.PreferredDepthStencilFormat`
= `Depth24` (XNA/FNA). `GraphicsAdapter.QueryBackBufferFormat` keeps the depth request (FNA returns it
unchanged; XNA keeps any supported one). The CNAEXT store-only `SetPresentationParameters` records the
request without reallocating — the contract EasyGL's depth-format fixture already relies on.

**Root cause.** DX-213 (2026-09-09) had made both renderers report their then-fixed D24S8 honestly;
WINCLOSE-0012 and DX12-0019 then made the back buffers honour the request, but the adapter hook
`SelectBackBufferDepthStencilFormat` in both descriptors still answered D24S8, and two fixtures plus
`GraphicsAdapterTest.QueryBackBufferFormatAcceptsColor` still demanded it. The adapter query disagreed with
the device — the hook's own documented purpose is to agree.

**Changes.** Both descriptors select the request (None for an ordinal DXGI cannot allocate).
`d3d_presentation_format_contract_test.cpp` checks GDM Depth16, Reset D24S8, Reset Depth16, store-only (public
None, native still D16), Reset None (no depth resource) and single-sample Depth16 against the native
resources. `graphics_adapter_query_contract_test.cpp` checks the default Depth24 and the kept request.

## GSC-0006 — Debug-layer messages fail tests

**Before.** DX12-0004's listener printed each test's messages and never failed a test; DirectX11 had no
info-queue capture and no explicit switch (its layer followed `NDEBUG`).

**Changes.**

* `D3DCommon::D3DDebugLayerLog` — one process log both renderers drain into (Present, device loss,
  teardown), with an observer and a registry of live-device drains.
* DirectX11: `CNA_D3D11_DEBUG_LAYER=1|0` (unset = build default), `ID3D11InfoQueue` with INFO/MESSAGE
  denied at storage, `DrainDebugMessagesEXT`.
* DirectX12: the drain feeds the shared log; `GetProcessDebugMessageTotalsEXT`/`GetRecentProcessDebugMessagesEXT`
  keep their API for the stress program.
* `D3DDebugLayerPolicy.hpp` (pure) + `D3DDebugLayerListener.cpp` (Windows) replace the DX12-only listener. At
  each test end every live device is drained, then `ADD_FAILURE` is recorded while gtest still attributes
  results to that test (XML, shard runner and exit code all see it).

**Verdicts.** CORRUPTION and ERROR: always fatal, cannot be allowlisted. WARNING: fatal unless an allowlist
entry names API + message id + test. INFO/MESSAGE: ignored (also denied at the queues). Unknown severity:
fatal. DirectX12 additionally drops IDs 820/821 (optimized-clear-value performance hints, DX12-0003) at its
queue.

**Allowlist (complete, all WARNING, each scoped to one test).**

| API, id | Name | Test | Why it is valid |
|---|---|---|---|
| D3D12 245 | `CREATEINPUTLAYOUT_TYPE_MISMATCH` | `DrawRouteValidation.EveryVertexElementFormatIsBoundOrRefusedByName` | Byte4 read by a float TEXCOORD input; the layer states the conversion is well defined (DX12 plan round H) |
| D3D11 391 | `CREATEINPUTLAYOUT_TYPE_MISMATCH` | same test | the D3D11 form: Byte4/Short2/Short4 read as float; "not an error, since behavior is well defined" |
| D3D11 408 | `QUERY_BEGIN_ABANDONING_PREVIOUS_RESULTS` | `OcclusionQueryPixelCountPrecisionTest.XnaLifecycleRejectsUnavailableAndInvalidSequences` | the test reads `IsComplete` before the result is ready and calls `Begin` again, which XNA's query state machine permits (SOFTWARE-199); the layer: "valid; but unusual" |

The two D3D11 entries are the only findings of the first DirectX11 debug-layer run ever made (below); both were
read, traced to the test that produced them, and classified before being listed.

**Tests.** `D3DDebugLayerPolicyTests.cpp` (every platform: verdicts, scoping, gate attribution, overflow
counting, and `EXPECT_NONFATAL_FAILURE` on the failure path); `D3DDebugLayerCaptureTests.cpp` (Windows, layer
enabled): a zero-sized texture created on the renderer's own device reaches the log as an ERROR through the
live-queue drain.

## GSC-0007 — `CaseInsensitivePathTest`

**Contract.** `ResolveExistingNativePath`/`ResolveExistingXnaPath` promise a spelling that **opens** the file
XNA content named with any ASCII casing, not the on-disk casing. On a case-insensitive filesystem the request
already opens and is returned unchanged, without a directory scan (both callers — ContentManager and the
platform file system — only open the result).

**Change.** Header documentation states it. The two tests probe the scratch directory's filesystem and assert
the promise on it: the result is `equivalent` to the on-disk file and reads its content, and its spelling is
the on-disk one on a case-sensitive filesystem and the request on a case-insensitive one. Separators are always
normalized. No test expects failure on any platform.

## GSC-0008 — Audio fixtures and parallel shards

**Cause.** Six audio suites wrote XACT fixtures to fixed directories under the system temp directory
(`%TEMP%\cna_audio_engine_test` …); concurrent test processes rewrote them while others read them.

**Change.** `AudioTestScratch.hpp`: one directory per test process (`cna_audio_tests_<pid>_<random>`), created
on first use, removed at exit; every fixed fixture directory in `AudioEngineTests`, `SoundBankTests`,
`WaveBankTests`, `AudioCategoryTests`, `CueTests` and `RendererDetailTests` now lives under it. No production
change.

---

## Regression matrix

All Windows runs: `win10_local`, trees rebuilt incrementally from this branch (**0 MSVC warnings** in either
tree), `win32_ctest_interactive.ps1 -Parallel 3` and `win32_gtest_shards.ps1 -Shards 27 -Parallel 3` from
`C:\src\cna` with a non-empty `-Environment` (both round-H conventions). The guest was power-cycled first and
DirectX11 ran first.

| Suite | Before (DX12 plan rounds H/I) | After |
|---|---|---|
| DX11 corpus (VirtualBox adapter) | 227/264 | **231/264** — fixed StencilTwoSided, SkinnedEffect_WorldNormal, PresentationFormatContract, GraphicsAdapterQueryContract; **no new failure** (set diff) |
| DX11 CnaTests | 8 206: 8 038 passed, 4 failed | **8 227: 8 060 passed, 2 failed, 165 skipped** — the two VirtualBox cube-face driver defects |
| DX11 CnaTests, `CNA_D3D11_DEBUG_LAYER=1` | never run | **8 227: 8 061 passed, 2 failed** (same two), 0 fatal messages, 4 allowlisted warnings |
| DX12 corpus (WARP) | 235/262 | **239/262** — the same four fixed; **no new failure** (set diff against round I) |
| DX12 CnaTests (WARP) | 8 202: 8 033 passed, 2 failed | **8 223: 8 055 passed, 0 failed, 168 skipped** |
| DX12 CnaTests, WARP + `CNA_D3D12_DEBUG_LAYER=1` | reports only | **8 223: 8 056 passed, 0 failed**, 3 allowlisted warnings |
| audio + path suites, 12 parallel shards | 3 rounds: path ×2 every round, audio race in round 2 | **5 rounds: 0 failed** |
| Linux OpenGL33 (EasyGL), graphics subset `*RenderTarget*:*GraphicsDevice*:*Texture*:*SpriteBatch*:*Effect*:*Stencil*:*Draw*` | 2 069 passed, 71 skipped, 3 failed (DX12 plan) | **2 079 passed, 71 skipped, 3 failed** — the same `IndexedDrawDeferredTest` Vulkan-define strip tests |
| Linux OpenGL33, content/glTF/audio subset | — | **2 209 passed, 11 skipped, 0 failed** |
| Linux Software, same graphics subset | — | 2 073 passed, 67 skipped, 13 failed: tests that assume the build's default renderer or a real window when Software is forced by `CNA_GRAPHICS_RENDERER` (`GraphicsRendererSelectionTest`, `GraphicsDeviceSubsystemLifecycleTest`, `GraphicsDeviceWindowDescriptionTest`, `GraphicsDeviceRendererTest`) and five `IndexedDrawDeferredTest` strip/topology tests; no file on their path changed here; not re-run on the baseline binary |
| Linux Software, content/glTF/audio subset | — | **2 209 passed, 11 skipped, 0 failed** |
| Linux Vulkan, glTF/CNB/CNJ/adapter + new suites | — | **1 147 passed, 14 skipped, 0 failed** |
| Linux affected suites (143 tests) on OpenGL33 and Software | — | **142 passed, 1 skipped** each (the skip pre-exists) |
| MinGW cross build of D3D core, DirectX11, DirectX12 | — | success, no warning in a changed file |

**Mutation check (DX12, WARP).** `D3D12PipelineStateCache.cpp` and `DirectX12Renderer.cpp` checked out at the
baseline in the guest and CnaTests relinked: `TwoSidedStencilTest` failed 4 of 5 (the fifth, two-sided mode off,
is the control the swap does not affect) and `StockEffectNullTextureTest` failed 3 of 5 (BasicEffect, SkinnedEffect
and the AlphaTest `Always` case above). Restored, touched and rebuilt: 10/10 pass.

## Follow-ups (not fixed here)

| Item | Class |
|---|---|
| GSC-F1 — EasyGL's per-pixel SkinnedEffect program draws a red-textured, ambient-only quad white on an 8×8 back buffer. Per-vertex EasyGL, Software, DirectX11 and DirectX12 draw it red; measured independent of lights, specular colour, matrices and draw order. `StockEffectNullTextureTest` skips only that combination, naming this row | shared graphics defect (EasyGL) |
| GSC-F3 — the remaining corpus fixtures that fail on both D3D renderers (the DX12 plan's shared list minus the four fixed here) for reasons outside this workstream (Reach-profile fixtures, public-layer validation disagreements, `PresentationModeContract`, `Resource_PresentLifecycle` exit code, …) are unchanged | fixture debt |
| GSC-F5 — `ContentManagerVideoXnbTest.TheObjectReferencedFormLoadsToTheSameValuesAsTheInlineOne` writes and then deletes `tests/assets/media/video/video_xnb_object_fixture.xnb`, which is also a tracked file, so a run from the repository root leaves the checkout dirty | fixture debt |
| GSC-F4 — the two VirtualBox cube-face defects and the WARP MinLOD/mixed-filter limitations (`TextureFilterMipContract`, `DescriptorCapacityContract`) remain; none is CNA | environment limitation / physical-GPU validation |
| GSC-F2 — renderers outside this workstream's matrix still bind white for classic null textures (Vulkan, bgfx and others; their own examples pin white) | future renderer work |
