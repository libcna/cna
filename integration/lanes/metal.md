# Lane card — `metal` · **INTEGRATED 2026-08-09**

| Field | Value |
|---|---|
| Logical lane | `metal` |
| Original refs | `refs/heads/feature/metal` and `refs/remotes/origin/feature/metal` — both unchanged at **`48928d113cb864f78d754256d2d559d914d4f1a7`** |
| Archive tag | **`archive/preintegration/metal-20260804`** · annotated object `43f6eab8d40c6006265cd4e19223cdd3d68c1fc3` · peels to `48928d11` · sole Metal archive · GPG-good · unchanged |
| Fork/base | **`ac3aaaeb2a5ba27dbd9e22e782c7041e6e40947c`** |
| Historical commits / files | **99 / 59** · 240 commit/path events · 59 unique/net paths · linear · zero merges · 99/99 Robert-authored/committed and maintainer-PGP signed |
| History map | Integrated tree `docs/metal-history-map.tsv` · **99 rows = 88 retained replays + 11 explained omissions** |
| Replay head | **`b69449e2fcd0438bb2f5efd170441785fe424c36`** — 88 retained commits, 217 path events, 55 unique/net paths, replayed chronologically |
| Adaptation head | **`e2ffe7290ddf5aab5c211b1fc2c00f0e09bd42f1`** — 94 signed commits: 88 replay + 6 post-audit fix/documentation commits; 296 path events, 76 unique / 72 net paths |
| Integration merge/head | **`012b158eb8246ce267887acbd4fc7a2468d89e52`** · signed `--no-ff` · parents `4ac696c7` / `e2ffe729` · tree `31200b608cd2a4c8ccd0f7cb9d6325540cec9458` equals adaptation |
| Batch 6 checkpoint | **COMPLETE** · signed annotated `integration/checkpoint-batch6-20260809` · object `8d347c933a3da3c39f22711e40e80cf7a29c4682` · peels to integration merge · GPG-good · local only, not pushed |
| Historical inspection | Ref-only; no original `feature/metal` worktree was created or checked out for adaptation |
| Adaptation worktree | `/rv/data/development/github.com/openeggbert/cnaintegration-metal` — clean at handoff |
| Subsystem | Direct native Apple Metal through Objective-C++ and runtime-compiled MSL |
| Supported target | **macOS only**; integration accepted for source continuity, with adapted Apple compile/runtime external |
| Public identity | **41st** CNA backend: `METAL`; no renderer fallback |
| Shared production delta | Registration/build union plus a Metal-guarded high-pixel-density window flag in current `GraphicsDevice.cpp`; no common backend-interface, capability-enum, or shared Texture2D production change |
| Conflict class | **MEDIUM** — 10 sequencer stops; 15 reconstructed file-conflict events across 11 unique paths; 76 exact and 12 semantically adapted replay pairs; 11 mapped omissions; current contracts win |
| Final status | **READY / INTEGRATED** — no known defect remains reachable through the conservative supported contract; adapted native validation is an explicit external confidence boundary |

## 1. Source-continuity decision and evidence boundary

This host is Debian and has neither Apple's framework headers nor a Metal device. It cannot compile
`MetalGraphicsBackend.mm`, link Metal/QuartzCore/Foundation, compile runtime MSL, or execute native
resource and pixel paths. The owner explicitly authorized integration under a **source-continuity**
criterion: current C++ contracts and portable policy logic must be green, every historically known-
wrong supported path must be repaired or disabled, and the absence of adapted Apple evidence must
be stated rather than converted into a pass.

The immutable historical evidence is narrower:

- latest production-changing commit: **`e0f42426836ce9f2d4823d50732850877020aef1`**;
- final historical head: `48928d113` (four later commits changed only `NEXT.md`/`plans/plan_metal.md`
  handoff prose);
- GitHub Actions run **`29814126178`**, `macos-14`, Xcode 15.4, Metal API/GPU validation enabled;
- native build passed; CTest **143 total / 136 passed / 7 failed**;
- six failures returned only the most recent clear colour: `Metal_PbrEffect_Golden`,
  `Metal_SkinnedPbrEffect_Golden`, `Metal_DrawUserPrimitives_VPC`,
  `Metal_SpriteBatch_CustomEffect`, `Metal_MultipleRenderTargets`, and
  `Metal_Backbuffer_MSAA`;
- `Metal_RenderTarget2D_MSAA` separately applied a device-clamped sample count of four but rendered
  a binary, non-antialiased edge;
- `Metal_Capabilities` passed only its then-eight boolean assertions; it did not prove those
  rendering paths.

The adaptation changes interfaces, lifetime, transfer policy, registration, and supported behavior,
so that run is **historical original-tree evidence only**. There is no fresh adapted Objective-C++
compile, native static link, MSL compile, Metal validation, pixel, Retina, frame-pacing, physical-
display, Intel/Apple-Silicon comparison, or iOS/tvOS result. The current workflow is the route to
that evidence; no integrating session pushed or ran it.

## 2. Provenance, replay, and history

The archive tag existed and verified before adaptation. Local and remote original refs still equal
its peeled target. `docs/metal-history-map.tsv` accounts all 99 historical commits:

| Disposition | Count | Meaning |
|---|---:|---|
| Replayed | 87 | Chronological signed replay preserving author, author date, subject, and technical intent |
| Replayed, then adapted out | 1 | The historical shared-test include commit was recreated for provenance; its obsolete unrelated hunks were removed in the adaptation layer |
| Omitted diagnostic | 5 | Temporary pixel, draw-call, capture, or sample-count instrumentation; durable findings remain in later retained records |
| Omitted superseded | 2 | One FFmpeg link-directory attempt superseded by `IMPORTED_TARGET`; one build-directory prose change already present in stronger form |
| Omitted already integrated | 2 | XACT copy guard and SDL harness linking already satisfied by the current base |
| Omitted session handoff | 2 | `NEXT.md`/plan-only session handoffs with no durable production or test change |

Replay range-diff is **99 rows = 76 `=` + 12 `!` + 11 `<`**. The twelve non-equal pairs are
accounted current-context adaptations: backend-registration unions; current compile-definition and
README lists; current platform and static-link rules; current FFmpeg/test context; removal or
folding of temporary diagnostics; and retained historical plan conclusions around custom effects,
MSAA, and readback. No unexplained patch loss remains. The history map, rather than range-diff's
similarity pairing, is authoritative for which original commit each omission is accounted by.

The adaptation reflog proves exactly **10 sequencer conflict stops** among the 88 retained replays,
at adapted commits `03774b0e`, `4486f08c`, `1d51c88d`, `3dcf7b91`, `ba72c6a0`, `3fe5b05f`,
`b68437c3`, `caf7c1a6`, `bf95fde8`, and `dec956cc`; the other 78 cherry-picks were clean.
Because Git does not retain resolved index stages, deterministic original-parent / adapted-parent /
original-commit tree reconstruction is the evidence source. It yields **15 file-conflict events
across 11 unique paths**, with per-stop counts **5, 1, 1, 2, 1, 1, 1, 1, 1, 1**:

- `CMakeLists.txt`, `cmake/BackendLibraries.cmake`, `cmake/BackendSelection.cmake`,
  `cmake/CnaLibrary.cmake`, and `cmake/Tests/MetalTests.cmake`;
- `include/CNA/GraphicsBackendType.hpp`,
  `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`, and
  `src/CNA/Internal/Backends/Metal/MetalGraphicsBackend.mm`;
- `tests/Microsoft/Xna/Framework/GraphicsBackendCompileDefinitionTests.cpp`, `README.md`, and
  `plans/plan_metal.md`.

The twelve `!` pairs must not be called twelve conflicts: two (`85701cdba -> 53010bcfa` and
`5be69beb2 -> 9a0c0378e`) applied cleanly but differ in current context. Range-diff also
heuristically pairs diagnostic `c14df78d2` with retained `3fe5b05f` while leaving `10c8b77e4`
unmatched; the curated TSV instead records `c14df78d2` omitted/accounted by `10c8b77e4`, and maps
that retained original to `3fe5b05f`. Thus the 11 `<` count agrees with the omission count, but the
TSV—not similarity pairing—is authoritative for row identities. All 88 recreated rows have zero
author-name, author-email, author-date, or subject mismatch from their mapped originals, and there
was no chronological reorder.

Six post-audit commits follow the replay:

| Pair | Commits | Scope |
|---|---|---|
| Initial adaptation | `48f6b46f` / `4ccc1d56` | `METAL-258`–`-262`, current interfaces, conservative support, CI/provenance record |
| Stabilization | `d3e25ddf` / `64087202` | `METAL-263`–`-280`, policy/oracle coverage and truthful documentation |
| Retained-resource lifetime | `fbfe0756` / `e2ffe729` | `METAL-281`, owner-health and post-owner backend-handle boundary |

All 94 adaptation commits and the merge report `%G? = U`, the project's normal Good-signature state,
with fingerprint `255C 69CC 1D09 CA54 EF0C C9DF FB9C E8E2 0AAD A55F`. The 95-commit final Metal
range is authored and committed by `Robert Vokac <robertvokac@robertvokac.com>`. The prohibited
attribution/trailer sweep is empty. A broader `Claude` text search finds only three factual mentions
of the tracked filename `CLAUDE.md`, not attribution. There are no Git notes on the adapted range.
No `origin/adapt/metal` ref exists.

## 3. Identity, dependency, and native route

The supported route is exactly:

```text
CNA public identity METAL
  -> MetalGraphicsBackend (Objective-C++)
    -> Apple Metal + QuartzCore + Foundation
      -> CAMetalLayer supplied through SDL3's macOS Metal-view glue
```

`GraphicsBackendType::Metal`, public name `METAL`, `CNA_GRAPHICS_BACKEND=METAL`,
`CNA_BACKEND_METAL`, backend target, selector, factory, compile-definition test, and CMake cache
lists are token-exact. The enum count moves **40 -> 41**. The backend does not delegate rendering
to SDL_Renderer, SDL_GPU, EasyGL, LLGL, bgfx, Software, or another CNA identity.

The CMake gate requires `CMAKE_SYSTEM_NAME=Darwin` before Objective-C++ is enabled or `.mm` sources
are collected. iOS and tvOS are rejected, not inferred from generic `CAMetalLayer` source. CNA sets
no explicit minimum macOS deployment target; compatibility below the SDK/deployment defaults of a
future successful build is unclaimed. There is no third-party graphics dependency or carried patch.

## 4. Post-audit adaptation and current draw contract

The lane predates `fc0dd2a2`. Its historical changed `GraphicsDevice.cpp` contained one removed-field
write (`GpuDrawParams::instanceVb`). That block was not restored. Current
`GpuDrawParams::vertexStreams`, combined stride, `vertexStart`, `startIndex`, and `baseVertex`
remain authoritative; the final shared `GraphicsDevice.cpp` delta is only the Metal-guarded window
flag `SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY`.

The supported geometry shape is one valid per-vertex stream whose buffer, slot, stride, and
combined stride agree with the draw argument, plus the documented legacy empty-stream route.
More than one per-vertex stream, any instance stream, or an instance count other than one rejects
before native submission. Every ordinary/direct indexed and non-indexed route applies the shared
declaration-fidelity oracle at draw time; same-stride semantic, offset, format, and duplicate-
semantic mismatches cannot silently reinterpret bytes. FNA-compatible four-component `fogVector`,
current normalized render-target descriptors, `BlendWriteState`, and boolean transfer contracts
replace historical fork-era forms.

Conflict/adaptation classes:

- **A — retain:** genuine direct native Objective-C++/MSL backend, SDL3 macOS Metal-window/layer
  glue, selector/CMake/workflow, helpers, tests, and durable documentation;
- **B — preserve current shared authority:** `GpuDrawParams` stream arrays, deferred captured
  resources, the common declaration oracle and faithful validation, normalized target/resource/
  capability contracts, and the current CMake/test topology; stale shared files were never restored
  wholesale;
- **C — supersede:** stale raw draw/state assumptions, temporary diagnostics, obsolete shared
  FFmpeg/XACT/SDL/handoff fixes, and false-positive historical support for MSAA, MRT, custom
  effects, occlusion queries, and backbuffer readback;
- **D — semantic adaptation:** current signatures and stream/stride/declaration rejection;
  format/range/overflow/transfer layout; transactional manual-retain-release command/frame/raster
  state; neutral bindings and foreign-resource rejection; macOS-only support; and weak-owner
  resource-health lifetime;
- **E — tests:** retain and extend portable helper/oracle/policy coverage; known native-pixel
  failures are unsupported, not green tests; preserve historical 136/143 separately and claim no
  adapted Apple pass;
- **F — build/docs:** keep Objective-C++ Apple-gated, Metal-local high-DPI SDL flags, the archive
  reverse-edge repair, bounded workflow, exact history map, 41-identity union, and conservative
  capability/support prose;
- **G — omit/adapt out:** only the 11 accounted rows above; the replayed `a29644` shared
  `<csignal>` lines are removed in adaptation. No unrelated audit or shared wholesale replacement
  was carried.

## 5. Current capability and deterministic-rejection contract

Every current `GraphicsCapability` is handled explicitly; unknown values return false.

| Capability | Reported | Supported boundary |
|---|---:|---|
| `ThreeD` | true | Built-in fixed-layout Metal pipelines; current declaration and stream guards apply |
| `DepthStencilBuffer` | true | Native combined depth/stencil attachments |
| `MultiSampleAntiAliasing` | false | Requests clamp/report zero; single-sample attachments only |
| `MultipleRenderTargets` | false | Zero targets restores backbuffer; one normalized 2D/cube-face target accepted; more rejects |
| `AnisotropicFiltering` | true | Native sampler mapping, clamped to Metal's documented range |
| `WireFrame` | true | `FillMode::WireFrame` maps to `MTLTriangleFillModeLines`; state policy is portable-covered |
| `OcclusionQuery` | false | Creation throws; dormant split-command/slot code is unreachable |
| `CustomEffects` | false | Creation, non-null SpriteBatch effect, and non-null 3D effect backend throw |
| `Texture3D` | true | Color-format native storage and checked transfer/readback contract |
| `MultiStreamVertexInput` | false | More than one per-vertex stream rejects before submission |
| `Instancing` | false | Instance streams/counts reject before submission |
| `StencilBuffer` | true | Native stencil plane and mapped state |
| `AdditiveBlending` | true | Native blend factors/operations; last writer owns complete state |

Backbuffer `ReadBackbuffer` throws `System::NotSupportedException`; it never returns the historical
clear-only pixels as success. Unsupported texture/target formats, malformed shapes, non-default
per-target write masks, multisample coverage masks, sampler maximum mip levels, and sampler LOD
biases reject rather than degrade silently.

## 6. Draws, effects, textures, targets, and lifetime

Built-in Basic, AlphaTest, DualTexture, EnvironmentMap, Skinned, PBR, and SpriteBatch pipeline and
uniform selection remains present behind current stream/declaration/state guards. Pipeline shape is
selected from effect flags and canonical stride, never texture-pointer truthiness. Every nullable
stock sample binds an owned white, flat-normal, or white-cube neutral texture, preventing stale
encoder bindings. Custom `ShaderEffect` scaffolding is retained as historical work but unreachable
through the false/throwing supported contract.

TextureCube/Texture3D and single RenderTarget2D/cube-face transfers validate format, mip, face,
region, arithmetic, and exact length before mutation. Staging rows use a macOS-safe 256-byte
alignment and are de-padded on readback; BGRA target data is swizzled to CNA RGBA while RGBA
cube/volume data is unchanged. Updates allocate replacements, preserve untouched subresources by
checked blit, wait/check the exact command, and swap only after completion. RenderTarget2D full
SetData is real; RenderTargetCube's upload boundary remains explicitly false. Generated RT2D mip
levels become defined only after successful generation.

Requested viewport/scissor state survives Clear, target changes, and encoder recreation. Enabled
scissor is intersected with the current attachment; disabled scissor uses the attachment; an empty
logical intersection installs a legal native placeholder and suppresses draws without affecting
Clear. A transient `nextDrawable=nil` is latched as a one-attempt, non-error backbuffer-frame skip
until Present, while offscreen work remains available.

Objective-C++ remains manual retain/release. The backend owns create/`new*` references once,
retains borrowed objects only across calls, rolls back partial construction, and releases the
drawable/layer/view in bounded order. Command failures use a lifetime-safe latch; consumption
abandons uncommitted encoder/command state before throwing, and synchronous work checks its exact
command.

`METAL-281`'s guarantee is deliberately narrow: independently retained backend handles and their
independently owned native resources keep only a weak owner-health link, reject operations after
owner teardown, and can release native attachments without dereferencing a dead `Impl`. This does
**not** claim a public Texture/GraphicsResource wrapper may safely outlive its `GraphicsDevice`;
the common wrapper retains a raw device pointer and that separate baseline lifetime is outside the
Metal lane. Backend escape is nevertheless real through `Texture2D::GetBackendWeak()` and
`ITextureCubeBackend::shared_from_this()`, which is why the backend-level fix is required.

## 7. Post-audit findings

`plans/plan_metal.md` preserves the full evidence and disposition. Every new ID is explicit here:

| Finding | Severity | Final disposition |
|---|---:|---|
| `METAL-258` clear-only backbuffer readback | High | Supported contract disabled; readback throws |
| `METAL-259` non-antialiasing four-sample RT | High | Supported contract disabled; MSAA reports/clamps zero |
| `METAL-260` unowned cached drawable | High | Retained owner policy implemented and portable-tested; native proof external |
| `METAL-261` no partial-construction rollback | Medium | Nil-safe `Impl` rollback/teardown implemented; native proof external |
| `METAL-262` double-retained default device | Medium | Redundant retain removed; ownership audit recorded; native proof external |
| `METAL-263` stride-only declaration interpretation | High | Draw-time declaration-fidelity oracle on all routes |
| `METAL-264` unchecked/tight/racing cube-volume transfers | High | Checked aligned reallocate/preserve/swap policy; native pixels external |
| `METAL-265` viewport/scissor lost across encoder changes | High | Requested/effective state split and portable transition coverage |
| `METAL-266` overclaimed broken occlusion queries | High | Capability false, factory throws, allocation absent |
| `METAL-267` BGRA target exposed as RGBA | High | Format-aware swizzle/de-padding helper covered |
| `METAL-268` RenderTarget2D SetData no-op | High | Checked RGBA→BGRA replacement upload implemented |
| `METAL-269` null stock textures reused stale slots | High | Owned neutral resources and foreign-resource rejection |
| `METAL-270` nontransactional native allocation/cache/state | High | Scoped rollback, nil/size checks, ownership-through-emplace; native proof external |
| `METAL-271` texture presence selected pipeline | High | Effect flags/canonical stride select shape |
| `METAL-272` ImageData format/shape ignored | High | Color-only exact positive shape accepted; malformed/other formats reject |
| `METAL-273` inert/latched SetBlendEnabled | Medium | Complete last-writer opaque/straight-alpha state |
| `METAL-274` disabled depth still wrote | High | Effective write is enable AND requested-write |
| `METAL-275` unlit uniforms retained lighting | High | Accepted unlit normalization applied |
| `METAL-276` generated RT mips reported undefined | High | Defined-level state follows upload/generation success |
| `METAL-277` transient nil drawable threw/retried | Medium | One-attempt frame skip, offscreen work retained |
| `METAL-278` command/readback ordering hid failure | High | Exact-command checks and common failure abandon/reset |
| `METAL-279` empty scissor submitted illegal zero | High | Legal placeholder plus draw suppression |
| `METAL-280` undeclared backend→CNA archive edge | High | Reverse dependency declared and configure-time asserted |
| `METAL-281` retained backend dereferenced dead owner | High | Weak health token, retry/escape/ownerless cleanup covered; native proof external |

Historical `METAL-257` remains visible but is corrected at the post-audit handoff: its premise that
the Metal window lacked `SDL_WINDOW_HIGH_PIXEL_DENSITY` was false. The Metal selection has requested
that flag since initial replay. This is a correction to historical prose, not a new open finding.

## 8. Validation matrix

| Instrument | Result | What it proves |
|---|---|---|
| HEADLESS `CNA` + portable target build | pass, stable in-repo tree, ccache, `-j4` | Current shared interfaces and portable helper compilation |
| Unique Metal portable tests | **206/206 across 28 suites** | Policy, transfer layout/conversion, state, declaration, lifetime, matrix/uniform/enum logic |
| `ctest -R '^Metal'` | **207/207** | Same 206 cases plus `Metal_PortableHelpers` aggregate registration |
| HEADLESS graph audit | no `MetalGraphicsBackend.mm` reference | Portable pass is not misrepresented as Objective-C++ evidence |
| GNU 14.2 ASan+UBSan helper target | **206/206**, linked `libasan.so.8` + `libubsan.so.1`; zero ASan/LSan/UBSan/runtime-error match in complete direct log | Portable C++ memory/UB boundary only |
| Non-Darwin `METAL` configure | intended fatal macOS-only message; 271,419-byte configure tree; no ObjC++/`.mm` artifact | Truthful host/platform gate |
| OPENGLES/EasyGL principal shared control | **125 total = 124 pass + 1 intentional wireframe-inapplicable skip** | Shared registration/capability/declaration inverse paths |
| EasyGL real RT readback + viewport/scissor | **2/2** | Metal's small shared window/registration union did not regress principal rendering controls |
| LLGL supported-path continuity | **48/48** | Accepted Group G Linux/OpenGL backend remains intact |
| LLGL smoke/RT/viewport-scissor | **3/3** | Focused current-head rendering control |
| Historical macOS run | **136/143**, seven classified failures | Original-tree Apple compile/runtime only |
| Adapted macOS/physical Apple run | **not run** | Explicit external boundary; no pass claimed |

The native workflow now registers only supported `Metal_Smoke`, current 13-capability assertions,
and the portable aggregate/cases. It deliberately does not treat the six readback-dependent or
broken-MSAA historical tests as supported gates. A future fresh workflow must still prove native
compile/link/MSL/lifetime and the narrowed runtime behavior before the adapted backend gains native
confidence.

## 9. Build, process, and repository safety

All accepted builds used stable directories under the integration worktree, ccache,
`CNA_MAX_VENDORED_BUILD_JOBS=2`, and explicit `-j4` or lower:

| Tree | Purpose | Final size |
|---|---|---:|
| `cmake-build-headless` | Portable build/tests | 524,162,423 bytes (508 MiB) |
| `cmake-build-metal-nonapple` | Deterministic Linux rejection | 271,419 bytes (344 KiB allocated) |
| `cmake-build-opengles` | EasyGL controls | 1,639,609,425 bytes (1.6 GiB) |
| `cmake-build-llgl` | LLGL controls | 2,054,503,530 bytes (2.0 GiB) |

The adaptation worktree's final lane trees were 530 MiB HEADLESS, 1.4 GiB ASan+UBSan, and 340 KiB
for non-Apple rejection. One attempted wider sanitizer build first completed the portable target,
then hit a pre-existing HEADLESS link error in unrelated
`cna_audio_mixer_destroy_active_dynamic_voice_harness` (`CNA::Logger::Warn` unresolved). It is not
acceptance evidence; scope was not widened, and the final helper-only build/run above is green.

Final **shared host-global, not lane-exclusive** ccache snapshot: **29,865** cacheable calls,
**3,926** hits, **25,939** misses, **1,023** uncacheable, **4.7/10.0 GB** used. Runtime controls used
session-owned Xvfb `:193`; PID `3342458`
was stopped afterward and its lock/socket are gone. Foreign pre-existing displays `:101` and `:102`
were untouched. No build/test child remained at handoff.

During the evidence session, all 16 logical CPUs briefly saturated. It was **not** a Metal command:
foreign PID `3091920` ran unbounded `ninja -C cmake-build-compile-software iron_gang --quiet`, with
observed tail `ninja 3147872 -> sh 3150069 -> c++ 3150070 -> collect2 3150071 -> ld 3150072`.
Metal launched no further work during the hold, did not signal or kill that tree, and resumed only
after the host audit found it exited; one unrelated single-thread syntax check remained. Metal
commands used `env -u DISPLAY`; only the later bounded cross-backend runtime controls used the
session-owned Xvfb above. No thermal reading was captured, so temperature is **not measured**.

The integration and adaptation worktrees were clean. `audit/` stayed byte-identical at tree
`168c9b668763b78e63106e27d942a76d2457f41d`. The four protected stash objects remained, in current
newest-to-oldest order: `888c3dcc8fb4fc6949bf3790a1483862328b6033`,
`d3b92226e00deb239c7587592c0c5bfc73078aaf`,
`5623d2202fea60b64eb50afa120745595b75d89b`, and
`8f8b8f55c647eb9e57a14093e4f5e30f55fe4157`. No destructive operation, original-ref rewrite,
tag creation, or push was part of Metal integration. Local tracking evidence—not a live remote
query—left `origin/integration/post-audit-phase1` at `c805fd737f4321568fba378e8d1b8fe5b5270666`
while local integration ended at `012b158e` (222 commits / four first-parent commits ahead).

## 10. Group result and checkpoint handoff

Metal is the twenty-first integrated lane. Group G is **4/4**: Skia, Direct2D, LLGL, and Metal.
The logical inventory is **21/21 integrated, zero pending**, with exactly 21 signed first-parent
lane merges since the phase-1 checkpoint. The full integration range reports 899/899 `%G? = U`.

Technical Batch 6 stabilization is READY under the conservative source-continuity contract. A
fresh adapted macOS workflow remains external and no final campaign/`develop` readiness is inferred.
The authorized clean/signature/invariant retake passed. Signed annotated tag
`integration/checkpoint-batch6-20260809`, exact message `CNA integration Batch 6 checkpoint`, was
created once without force as object `8d347c933a3da3c39f22711e40e80cf7a29c4682`; it peels to
`012b158eb8246ce267887acbd4fc7a2468d89e52` and `git tag -v` reports Good under fingerprint
`255C69CC1D09CA54EF0CC9DFFB9CE8E20AADA55F`. It remains local only and was not pushed. **Batch 6
checkpoint status: COMPLETE.** Full group record: `integration/BATCH_6_STABILIZATION.md`.
