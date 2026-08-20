# Lane card — `diligent` (Diligent Engine) · ✅ **INTEGRATED 2026-08-07** · merge `aa9f3fb5` — the thirteenth lane, Batch 3 closes

> **Outcome.** Adapted, validated and merged in one session. Like `sokol` and unlike `wicked`/
> `magnum`, the lane had genuinely been built and run before: a pre-adaptation build at its own fork
> point reproduced its recorded results exactly, so the session started from a proven baseline.
> Interface drift against the head was **one** reference to the removed `GpuDrawParams::instanceVb`.
> Validation found **four defects, all adaptation-owned and all fixed in-lane** — a dropped
> `endif()` that broke configure in every configuration, two orphaned `#endif`s caught at conflict
> resolution, and a wrong expected-check count in the session's own new test. **No lane-owned
> supported-path defect was found.** **Nothing was pushed; no fourteenth lane was begun; `audit/`
> untouched.**
>
> **This is the first CNA backend whose native graphics API is chosen at run time rather than by the
> CMake option.** That does not make it an alias: CNA's implementation genuinely goes through
> DiligentCore, and Diligent's internal Vulkan/OpenGL modes are **not** counted as additional CNA
> backends.

| Field | Value |
|---|---|
| Logical lane | `diligent` |
| Refs | local **and** remote `feature/diligent` — both unchanged |
| Original head | `1ab12b505e40e61101a260ae43d3b9911f219e8a` — unchanged locally and on `origin` |
| Archive tag | `archive/preintegration/diligent-20260804` → `1ab12b50` · unchanged |
| Real fork point | **`1eb22c11`** on `feature/audit` — audit-stacked, the fork point it shares with `sokol` and `llgl` |
| Own commits / files | **65 / 56** |
| Adaptation branch / head | `adapt/diligent` (worktree `cnaintegration-diligent`, retained) |
| Adapted commits | **70** — 65 replayed 1:1 + 5 added by the adaptation |
| History class | **AUTHOR/TRAILER CLEANUP REQUIRED — partial (37/65)**, re-verified at the object level |
| Conflict class | MEDIUM — confirmed |
| Path taken | **ADAPTATION** |

---

## 1. History — re-verified at the object level

Exactly the two classes the inventory's `28/37` predicted.

| Class | Count | Detail |
|---|---|---|
| `Claude <noreply@anthropic.com>` author **and** committer | **37** | SSH-signed by the campaign's known non-maintainer key, each carrying `Co-Authored-By: Claude <model>` **and** `Claude-Session:` |
| `Robert Vokac` author **and** committer | **28** | maintainer-PGP signed, no trailers, no attribution text |

0 merges, 0 WIP/fixup/squash subjects. Every commit was re-authored under policy A2 to
`Robert Vokac <robertvokac@robertvokac.com>` with its **original author date preserved**, both
trailers stripped 37/37, and re-signed with the maintainer key. Post-adaptation the range is
**70/70 maintainer-signed, 70/70 maintainer-authored, zero banned-token hits**.

**One deliberate divergence from `sokol`'s message policy, recorded rather than silently taken.**
`sokol` dropped ten per-commit corpus-status paragraphs as stale session reporting. This lane's
equivalent figures (`Full CnaTests regression unchanged (5692 passed, 7 skipped, …)`) are **kept**,
because they are not standalone session-report paragraphs here: they are interleaved mid-sentence
with each commit's own per-suite verification evidence (`All 15 Diligent_* CTest binaries pass
(71 checks total). Full CnaTests regression unchanged (…)`). They are dated measurements the commit
itself recorded, of the same class as the per-suite check counts beside them; they carry no
attribution and no process narration, and the banned-token sweep over the adapted range is clean.
Removing them mechanically would have damaged technical content, and rewording 15 bodies by hand
late in the session was the larger risk. Commit 1's *"CNA's 15th graphics backend"* **was** corrected
to **31st**, which is what it is at this head.

## 2. What the lane is

| Field | Value |
|---|---|
| Public identity | **`DILIGENT`** — `CNA::GraphicsBackendType::Diligent`, name `"DILIGENT"`, the **31st** identity |
| Selector | `-DCNA_GRAPHICS_BACKEND=DILIGENT`; option `CNA_BACKEND_DILIGENT`; define `CNA_BACKEND_DILIGENT` |
| Architecture | **DiligentCore**, itself a portable abstraction over D3D11/D3D12/Vulkan/OpenGL/Metal. CNA therefore sits on two stacked abstraction layers, and this is the **only** CNA backend whose native API is a **runtime** decision |
| Native API selection | `D3D12 → Vulkan → D3D11 → OpenGL`, filtered to the engines DiligentCore actually built. `CNA_DILIGENT_DEVICE` pins one (`d3d12`/`vulkan`/`vk`/`d3d11`/`opengl`/`gl`/`gles`/`auto`); an unrecognised value **throws** rather than falling back, so selection is deterministic |
| Selected here | Configure reports `engines: Vulkan;OpenGL`. Default order picks **Vulkan on `lavapipe`**; the `_OpenGL` CTest variants pin **OpenGL 4.5 compatibility on `llvmpipe`** (Mesa 25.0.7, LLVM 19.1.7), both on Xvfb `:101` with `SDL_VIDEODRIVER=x11` |
| Dependency | **DiligentCore pinned at tag `v2.5.6`**, fetched by `FetchContent` with recursive submodules and built from source. **Nothing vendored, no carried patch**, Apache-2.0. `~/deps/DiligentCore` at exactly `v2.5.6` (410 MB, clean, 8 submodules at their pinned revisions) serves offline builds through CMake's own `FETCHCONTENT_SOURCE_DIR_DILIGENTCORE` — no CNA-specific option was invented |
| Shaders | authored once in **HLSL** and cross-compiled by Diligent — SPIR-V via glslang on Vulkan, GLSL via its own HLSL2GLSL converter on OpenGL. No offline step and no checked-in generated bytecode, so nothing here is hand-edited generated output |
| Tests | **32** registered pixel-proof binaries, each registered twice (`<Name>` and `<Name>_OpenGL`) except `Diligent_DeviceSelectionIntegration`, plus 15 GPU-free `DiligentDeviceSelectionTest` gtest cases and the shared `CnaTests` corpus |

**Diligent is a genuine public backend identity, not an alias.** Its implementation goes through
DiligentCore's own `IRenderDevice`/`IDeviceContext`/`ISwapChain`; that DiligentCore in turn dispatches
onto Vulkan or OpenGL does **not** make this backend an alias of CNA's own `VULKAN` or `EASYGL`
backends, which are separate implementations against those APIs directly. Internal Diligent device
types are **not** counted as additional CNA backends.

## 3. Pre-adaptation baseline — the lane genuinely worked

Like `sokol` and unlike `wicked`/`magnum`, this lane did not need its first-ever build to be the
adapted one. Configured straight from the historical worktree at `1ab12b50`, against the pinned
DiligentCore checkout:

| Gate | Result |
|---|---|
| Configure at the recorded pin | clean — `CNA Diligent: using DiligentCore v2.5.6, engines: Vulkan;OpenGL`, offline route proven |
| Build (31 harnesses + `CNA` + backend) | **0 errors** |
| `ctest -R "^Diligent"` | **61 registered · 53 passed · 7 failed · 1 skipped** (15.6 s) |

The 7 failures are **exactly** the lane's own recorded open set: its six documented OpenGL-only
checks (`Diligent_Instanced`, `Diligent_InstancedStride`, `Diligent_MultiSampleMask`,
`Diligent_ReferenceStencil`, `Diligent_RenderTargetMipGen`, `Diligent_DepthBias`) plus the one
documented Vulkan `Diligent_DepthBias` constant-bias sub-case (6 of its 7 checks pass; only
`DepthBias=-0.128` fails, the known software-rasterizer limitation with two independent pre-existing
precedents, `D9-62` and `Vulkan_DepthBias`). **Everything measured after this point is therefore
attributable to adaptation, not to an unknown starting state.**

## 4. Interface drift — one reference

**Compile probe: exactly one error** across 3 845 lines of backend `.cpp` and 1 658 of `.hpp`,
against the head's drift: `GpuDrawParams::instanceVb` in `DrawInstancedPrimitivesEx`. Nothing else
in the lane failed to compile.

### 4.1 REMED-GFX-201/202 — the stream array

`instanceVb` and the three fields beside it were replaced by `vertexStreams`, one array carrying
every active `VertexBufferBinding` on every route. The instanced route now reads
`FirstInstanceStream(params)`, and two properties the old shape could not carry are honoured rather
than assumed:

- **`InstanceFrequency`** reaches the slot-1 `LayoutElement::InstanceDataStepRate` (a D3D11 step
  rate / a `glVertexAttribDivisor` underneath) **and joins the pipeline cache key** — Diligent
  pipelines are immutable, so two draws differing only in the frequency genuinely need two.
- **Each binding's own `VertexOffset`** becomes its `SetVertexBuffers` byte offset, which is exactly
  FNA3D's D3D11 shape (`offset = VertexOffset * stride` per binding, `BaseVertexLocation` passed
  separately). The multiplier is each stream's **real buffer stride**, not
  `GpuVertexStreamBinding::strideInBytes`, which is 0 for an instance buffer uploaded through
  `SetDataRaw` without a declaration.

`MultiStreamVertexInput` answers **false** and `Instancing` **true**, in an exhaustive **eleven-member
switch with no `default` arm**. Both draw families call `RejectUnsupportedStreamCombination()`
first: this backend selects both its input layout and its shader variant from one buffer's byte
stride and binds exactly one stream of each rate, so a split declaration or a second per-instance
binding is refused outright rather than rendered from a subset.

### 4.2 REMED-GFX-DECL-GUARD — the shared rule, reused

Unlike `sokol`, this backend **does** infer its native layout from the byte stride (`DrawInternal`'s
own `switch (stride)` over 16/20/24/32/48/52/68), which is precisely the mechanism the shared
`RequireFaithfulVertexDeclaration()` models. It is therefore **reused rather than re-derived**.
`SetVertexDeclaration` now remembers the caller's elements instead of consuming only their stride,
and every draw route calls the guard before any pipeline is built or command queued. The ordinary
routes pass `BackendRefusesIt` (their own stride switch already throws outside its table); the
instanced route passes `PositionOnlyFallback`, its slot 0 being Position-only for every stride it
accepts. The rule stays asymmetric — only the bytes the caller declared are checked — so a
position-only declaration still renders.

### 4.3 REMED-GFX-209 — truthful WireFrame, measured

`CNA_BACKEND_DILIGENT` joined `WireFrameTriangleOracle.hpp`'s pixel set, so the report is read from
rendered pixels rather than from source. It answers **`true`**, derived from the live device's own
`DeviceFeatures::WireframeFill` rather than assumed, and `Diligent_FillMode` independently proves a
full-viewport triangle's centre pixel reads back the clear colour under `WireFrame` and the fill
colour again after reverting to `Solid`. It needs no arm of its own in
`WireFrameCapabilityReportIsThisBackendsOwn` — `true` is that file's default arm, and this backend
has no reason to differ from it, unlike EasyGL's known-wrong `false` (`REMED-GFX-219`) or WebGPU's
deterministic refusal (`WEBGPU-115`). Same shape `sokol` took.

### 4.4 Registration union

The **tenth** of the campaign, and the widest so far — 17 conflicted files on the first commit
alone. 30 existing identities kept token-exact, DILIGENT added to `BackendSelection.cmake`
(docstring, `STRINGS`, option, explicit-selection guard, enabled-list, dispatch),
`BackendLibraries.cmake`, `CnaLibrary.cmake` (the `--start-group` archive-cycle set),
`UnitTests.cmake`, `CMakeLists.txt`, `GraphicsBackendType.hpp` (enum + `#elif` + name table),
`GraphicsBackendTypeTests.cpp`'s `ExpectedNameFor()` arm, the compile-definition count,
`GraphicsDevice::getBackendWindowFlags()`, `CLAUDE.md`, `README.md`, **`docs/README.md`** and
**`THIRD_PARTY_NOTICES.md`** — the last two being gaps the lane itself never filled.

**Two files were resolved to the head's superseding mechanism rather than the lane's.**
`CnjEffectTests.cpp` and `CnjStockEffectTests.cpp`: the lane gated on
`#if defined(CNA_BACKEND_DILIGENT)`, the head had since replaced that with a runtime
`SupportsCapability(CustomEffects)` gate that covers this backend correctly and generically.
Class **F — obsolete, superseded**. `GraphicsDeviceCapabilityTests.cpp` likewise moved from the
lane's per-assertion `#if` blocks to the head's `kExpect*` constant scheme, so the lane's
capability changes were replayed as edits to a **`#elif defined(CNA_BACKEND_DILIGENT)` arm** whose
values move commit by commit exactly as the originals did (MRT `false`→`true` at `DILIGENT-24`,
OcclusionQuery `false`→`true` at `DILIGENT-41`).

**`SOKOL` was missing from the in-repo `CLAUDE.md` backend list** — a gap in the twelfth lane's own
registration union, found while adding `DILIGENT` to that same line. Because `sokol` is the other
Batch 3 lane and this is Batch 3's own stabilization, it was corrected here rather than deferred.
`MAGNUM`'s absence from `README.md`'s list (Batch 2's gap, recorded by `sokol`) was **not** touched.

## 5. The defects validation found

**5.1 A dropped `endif()` (adaptation-owned).** Folding the DILIGENT branch into
`BackendSelection.cmake`'s enabled-backend list consumed the `CNA_BACKEND_SOKOL` branch's closing
`endif()`, leaving the flow-control statements unbalanced. This fails `cmake` configure in **every**
configuration, not just this one. Caught by the first configure of the adapted branch, fixed
forward in its own commit rather than by rewriting the replayed commit — the replay's 1:1
`range-diff` is evidence and stays intact. Same class as `sokol` §6.1.

**5.2 Two orphaned `#endif`s (adaptation-owned, caught at resolution).** `CnjEffectTests.cpp`/
`CnjStockEffectTests.cpp` and `GraphicsDeviceValidationTests.cpp` each had the lane open an
`#if`/`#else` whose `#endif` merged cleanly *outside* the conflict region, so taking the head's side
would have left the `#endif` orphaned. Caught by an explicit `#if`/`#endif` balance check at every
resolution, before any build. Exactly the failure `sokol` §6.1 hit and paid for.

**5.3 A wrong expected-check count (adaptation-owned).** The new
`Diligent_InstanceBindingOffsets` declared `kChecks = 11` while making 12 assertions, so a run in
which **every** check passed still reported failure. Caught by running it. Corrected to 12.

## 6. Capability table — from code and measurement

| Capability | Answer | Class | Evidence |
|---|---|---|---|
| `ThreeD` | `true` | supported and tested | `Diligent_3D`, every stock-effect suite |
| `DepthStencilBuffer` | `true` | supported and tested | `Diligent_RenderTarget`, `Diligent_DepthBias` |
| `MultiSampleAntiAliasing` | `true` (device-probed) | supported and tested | `Diligent_MSAA`, differential diagonal-edge proof |
| `MultipleRenderTargets` | `true` | supported and tested | `Diligent_MRT` |
| `AnisotropicFiltering` | `true` (device-probed) | supported and tested | `Diligent_Anisotropic` |
| `WireFrame` | `true` (device-probed) | supported and tested | §4.3 — `Diligent_FillMode` + the shared oracle |
| `OcclusionQuery` | `true` (device-probed) | supported and tested | the `Diligent_OcclusionQuery` suite; falls back to binary occlusion where the device lacks precise queries |
| `CustomEffects` | **`false`** | deliberately unsupported | `DILIGENT-42`; the head's runtime gate in `CnjEffectTests`/`CnjStockEffectTests` asserts the honest-invalid result |
| `Texture3D` | `true` | supported and tested | `Texture3DTest` in-corpus; storage + readback (sampling is `DILIGENT-42`) |
| `MultiStreamVertexInput` | **`false`** | deliberately unsupported | §4.1 — refusal asserted by the shared `UnsupportedBackendRejectsMixedStreamInstancingDeterministically` |
| `Instancing` | **`true`** | supported and tested | `Diligent_Instanced`, `Diligent_InstancedStride`, `Diligent_InstanceBindingOffsets` |

Recorded rather than answered by this enum: `RenderTargetCube` MSAA (**deliberately unsupported**,
`DILIGENT-25`), volume-texture **sampling** (**deliberately unsupported**, `DILIGENT-42`),
`SkinnedEffect`'s stride-56 vertex-colour variant (**deliberately unsupported**, `DILIGENT-35`),
`BlendState.MultiSampleMask` under DiligentCore's own OpenGL backend (**blocked externally** — v2.5.6
logs an error and does nothing), D3D11/D3D12 (**not applicable** here — code paths only, Windows).

## 7. Instancing, multi-stream, textures and effects

**Instancing / multi-stream.** `Diligent_InstanceBindingOffsets` (new this session) pixel-proves the
three properties on the Vulkan path, each leg built so "consumed" and "ignored" produce **different,
in-bounds** pixels: instance `VertexOffset = 1` over four matrices renders matrices 1..3 and leaves
matrix 0's column at the clear colour; `InstanceFrequency = 2` over four matrices and four instances
lights only the first two columns; geometry `VertexOffset = 4` into an eight-vertex buffer draws the
second quad and not the first. **12/12 on Vulkan.** Its `_OpenGL` variant fails for the identical,
already-root-caused reason as `Diligent_Instanced_OpenGL` and `Diligent_InstancedStride_OpenGL` —
under llvmpipe the per-instance attribute reads as zero for every instance, so all instances draw
stacked at the origin. It joins that existing documented class rather than opening a new one.

**Textures / render targets — bounded inspection, units checked.** `Box` coordinates are texels and
are bounds-checked against each mip level's own extents; `TextureSubResData::Stride`/`DepthStride`
are the **source** row/slice pitches in bytes for a tightly packed caller buffer; mip level and
array slice are passed as explicit subresource selectors. On readback the staging texture is created
at the *region* size with `MipLevels = 1`, so the mapped subresource is unambiguous, and the copy
loop reads the driver's own `mapped.Stride`/`DepthStride` rather than assuming `w * 4` —
**the exact arithmetic class that broke Wicked is handled correctly here**, and `Diligent_Npot`
(a 5×3 texture of 15 distinct colours with a non-row-aligned sub-rectangle read) is a real proof of
it. BGRA→RGBA swizzling is applied only where the granted surface format is blue-first.
`Diligent_Mip` proves per-mip `SetData`/`GetData` round-trips byte-exact through a genuine GPU
staging readback. Wicked was **not** reopened and none of its implementation was copied.

**Effects / shaders.** All HLSL, cross-compiled by Diligent at device-creation time; there is no
generated artifact to regenerate and nothing was hand-edited. `Diligent_LightingFidelity`
hand-derives emissive isolation, specular scaled by final alpha, multi-light additive summation and
non-uniform-world normal transforms; `Diligent_Pbr` matches three analytically hand-derived
metallic-roughness values exactly plus `SkinnedPbrEffect` through an identity bone; `Diligent_VertexLit`
proves `PreferPerPixelLighting` true/false render pixel-identically for a flat-normal quad. No new
effect functionality was created merely because DiligentCore could support it.

## 8. Validation at the merged content

| Gate | Result |
|---|---|
| Build | backend + `CNA` + `CnaTests` + all 32 harnesses, **0 errors, 0 new warnings** |
| Runtime identity | Vulkan on **lavapipe** / OpenGL 4.5 compat on **llvmpipe** (Mesa 25.0.7, LLVM 19.1.7), Xvfb `:101` |
| Dedicated suites | **78 registered · 69 passed · 8 failed · 1 skipped** |
| Registration arithmetic | 78 = 63 harness registrations (32 binaries + 31 `_OpenGL`) + 15 `DiligentDeviceSelectionTest` cases. Against the baseline: 61 + 2 (the new test ×2) + 15 (gtest cases the pre-adaptation tree never built) = 78 |
| The 8 failures | **7 are the pre-adaptation baseline's exact set** — zero regressions — and the 8th is the new test's `_OpenGL` variant joining that same documented GL instancing class (§7) |
| **Corpus** (`ctest -j1`, full tree) | **5816 registered · 5800 passed · 8 failed · 7 truthful skips** |
| The 8 corpus failures | **exactly the 8 focused-suite failures above** — there is not one non-Diligent failure in the run. The networking Outcome-C flake did not fire here at all |
| The 7 skips | 4 sensor + `WireFrameIsRefusedDeterministicallyOnThisBackend` (inapplicable: truthfully `true`, this backend renders so it must not refuse) + `Texture3DUnsupportedBackendTest` (inapplicable: genuine Texture3D) + `Diligent_BackbufferReadbackBounds_OpenGL`. The same profile `sokol` recorded, plus this lane's own GL readback skip |
| One `(Not Run)` closed | `StrictXnaApiSurfaceCheck_Compile_Run` was `(Not Run)` because its binary was outside the built target set, not because of any defect. Built and re-run: **passes** |
| **Sanitizers** (`cmake-build-diligent-asan`, address+undefined) | **0 ASan errors · 0 CNA-originating UBSan errors · 0 CNA-owned leaks.** Runtimes proven linked: `ldd` shows `libasan.so.8` + `libubsan.so.1`, 54 `__asan_`/`__ubsan_` symbols in the binary |
| Sanitizer scope and arithmetic | A **representative** subset per the batch brief — 13 harnesses covering 2D, 3D, render targets (2D and cube), MSAA, MRT, mips, NPOT textures, instancing (both suites), occlusion queries, the pipeline-state cache, and device create/destroy/teardown. 63 registrations, 38 `(Not Run)` (those binaries deliberately not built with sanitizers), **25 executed** |
| `detect_leaks=0` control | **23/25 pass.** The only 2 failures are `Diligent_Instanced_OpenGL` and `Diligent_InstanceBindingOffsets_OpenGL` — the identical GL divisor class as the ordinary run, not a sanitizer finding |
| Leak classification | With `detect_leaks=1` every binary exits 23, so 57/63 "fail" on the leak exit code alone. **Every leak block was attributed by its real allocating frame (frame #1, not the ASan interceptor): 1522 resolve to DiligentCore / the lavapipe Vulkan driver / Mesa, and the remaining 477 are STL allocations of DiligentCore's own types (`FramebufferCache`, `ObjectsRegistry`, `VulkanObjectWrapper`, `SamplerDesc`). Zero blocks allocate through CNA's own code.** CNA frames appear only *deeper* in these stacks, as the caller that drove a Diligent allocation — which is ownership by Diligent, not by CNA |
| UBSan classification | All 126 runtime errors are inside third-party code: `BufferVkImpl.hpp/cpp` and `DeviceContextVkImpl.cpp` (misaligned `CtxDynamicData` — an upstream DiligentCore Vulkan alignment issue) and `hlslParseHelper.cpp` (glslang null reference binding). **Zero point at a CNA source path**, verified by grep over the whole run |
| One recorded sanitizer-build deviation | `-fno-sanitize=vptr` is required. UBSan's vptr check needs RTTI, and DiligentCore builds glslang/SPIRV-Tools with `-fno-rtti`, so enabling it produces `undefined reference to typeinfo for glslang::TShader` at link. ASan is unaffected and every other UBSan check remains on; the disabled check could not have applied to `-fno-rtti` code in the first place |
| **EasyGL control** (`cmake-build-diligent-easygl`, built from the adapted sources) | **6190 registered · 296 `(Not Run)` · 5894 executed · 5893 passed · 1 failed · 6 skips**. The 296 are the dedicated EasyGL harnesses that do not compile in this environment — the **control-proven pre-existing** condition `sokol` recorded, unchanged and not in scope here. The 1 failure is `TwoProcessLoopbackTest.HostMigration…`, the known networking **Outcome C** coin flip: **3/3 green** re-run in isolation |
| **Sokol control** (`cmake-build-diligent-sokol`, built from the adapted sources) | **37/37, 0 failures.** Run because this lane's registration union touches shared files `sokol` introduced or modified — `BackendSelection.cmake`, `CnaLibrary.cmake`, `GraphicsDevice::getBackendWindowFlags()`, `WireFrameTriangleOracle.hpp`, `GraphicsBackendTypeTests.cpp` and three shared test fixtures. Sokol is unaffected by every one of them |
| Provenance | original ref and archive tag unchanged; `range-diff` pairs **65/65 in order** — 28 byte-identical (exactly the Robert-authored commits), 37 differing (exactly the Claude-authored ones) — plus the 4 adaptation commits appended; attribution sweep over the adapted range **zero hits**; every signature good; `git diff --check` clean |

## 9. Integration history

| Field | Value |
|---|---|
| Adaptation head | `27f7dcefedf4b5e3183b1ec76ab96f1801c8413b` on `adapt/diligent` |
| Adapted commits | **70** — 65 replayed 1:1, 5 added by the adaptation |
| Merge commit | **`aa9f3fb5`** — signed, `--no-ff`, zero conflicts |
| Merged tree | **byte-identical to `adapt/diligent`** (`git diff HEAD adapt/diligent` empty) |
| Signatures | 71 objects in `37066e45..aa9f3fb5` (70 commits + the merge), **all `U`** — good signature, uncertified key, this project's normal state |
| Attribution sweep | **zero hits** over subjects, bodies, authors and committers |
| `git diff --check` | clean |
| Original ref | `feature/diligent` still `1ab12b50`, locally and on `origin` |
| Archive tag | `archive/preintegration/diligent-20260804` unchanged, still `1ab12b50` |

The five adaptation commits: `fix(integration): complete the DILIGENT registration union`,
`test(REMED-GFX-209): measure the Diligent WireFrame report against the shared oracle`,
`refactor(REMED-GFX-201/202): read the Diligent draw routes from the vertex-stream array`,
`test(REMED-GFX-202): pixel-prove the Diligent instanced route's per-binding offsets and frequency`,
and `docs(plans/plan_diligent.md): record the integration, its obligations and DILIGENT-69`.

## 10. Residuals

Unchanged and not claimed by this lane: `REMED-GFX-221` (LOW), `REMED-CONTENT-007`/`-008`
(HIGH/P1 — this lane touches no `Content/` file), the networking Outcome C flake (it did not fire in
this lane's own corpus at all, and fired once in the EasyGL control where it re-ran **3/3 green** in
isolation), Direct2D OWNER-FROZEN, and `REMED-GFX-219` (EasyGL's known-wrong `WireFrame` report,
deliberately untouched).

Lane-local, all pre-existing plan rows: `DILIGENT-66`'s six open OpenGL-only checks,
`DILIGENT-49`'s constant-`DepthBias` environment limitation (shared with `D9-62` and
`Vulkan_DepthBias`), `DILIGENT-42` (custom `ShaderEffect`, volume-texture sampling),
`DILIGENT-25` (`RenderTargetCube` MSAA), `DILIGENT-35` (stride-56 skinned vertex colour), and
DiligentCore v2.5.6's own unimplemented GL `SampleMask` as a permanent upstream boundary.

Two conditions recorded without a defect claim, neither caused by this lane and neither fixed here:
the **296 `(Not Run)` EasyGL dedicated harnesses** (control-proven pre-existing, first recorded by
`sokol`), and **`MAGNUM`'s absence from `README.md`'s `CNA_GRAPHICS_BACKEND` list** — Batch 2's gap,
which belongs to `magnum`'s record. **`SOKOL`'s absence from the in-repo `CLAUDE.md` backend list
was a Batch 3 lane's own gap and was corrected here** (§4.4).

**New findings: one — `DILIGENT-69`**, recorded on `plans/plan_diligent.md`. It is not a CNA defect: the
session's own new test fails under the OpenGL device type for the identical, already-root-caused
Mesa/llvmpipe per-instance-divisor limitation as `DILIGENT-66`'s two existing GL instancing
failures, and passes 12/12 on Vulkan. It is filed so the new test's GL red is attributable rather
than unexplained, and it closes when `DILIGENT-66` does.
