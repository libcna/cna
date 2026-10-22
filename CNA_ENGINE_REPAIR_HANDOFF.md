# cna-engine upstream repair intake

Owner-requested intake, 2026-10-22. Read-only source inventory:
`../cna-engine/upstream-issues.md`. Do not edit cna-engine or cna-extended.
Use `cna-engine UP-NNN` in every associated commit message. No push authorized.

Current checkpoint: UP-002 `99b54079e`, UP-018 `a6e731cc2`, UP-006 `a2f0378cf`,
UP-019 `144d9f909`. No active build/test job. Next task: UP-001 installed package.
The chronological notes below retain earlier failures and intermediate checkpoints.

Baseline: CNA `a8800619e1b063e4e7f8bed5cf1b19744362c189`, branch `next`;
reported engine pin `900d0db3b` is an ancestor. Preserve the inherited dirty
content/audio ownership and ISSUE-113 changes. The uncommitted storage-path
regressions from the preceding batch are also retained, not part of this intake.
Their 29-check genuine XNA oracle passed; native red baseline is recorded in
cna-demos/build/linux-full/reports/repair-storage-path-before-v1-*.

| Finding | Current disposition | Evidence / next task |
| --- | --- | --- |
| UP-019 | Fixed in `144d9f909`; DirectX runtime validation pending | Four public-effect producer/receiver regressions pass on SOFTWARE/GLES/GL33/WEBGPU/SDL_GPU/FNA3D, including both FNA3D drivers. DirectX bytecode regenerated and all 26 register tables verified; no Windows runtime claim. |
| UP-018 | ENGINE_EXPECTATION_ERROR verified on pin and next | Default device is Reach; preferred Single substitutes actual Color. Engine caches the requested enum rather than target.getFormatProperty(). HiDef Single retains float precision through Clear, Depth24 and indexed fog draws. No production repair required; see exact follow-up below. |
| UP-006 | ALREADY_FIXED and verified | `470b12a5d` repairs mixed umbrella/static-sidecar ordering (ISSUE-016). New engine-style module-only static library + umbrella consumers pass in both orders, with zero duplicate strong Graphics/Input/Vector3 symbols. |
| UP-001 | Confirmed source gap | No CNA install export/config/version package exists. Design relocatable dependency/target export without breaking source consumers or duplicating the runtime. |
| UP-002 | Documentation corrected | `docs/migration-guide.md`, CNAEXT and C++ language artefacts, distinguishes declaration markers from implicit/synthesized C# semantics and explicitly retains strict compiler diagnostics. No runtime/header/test changes needed. |

Only one heavy job/compiler worker at a time, existing build directories, shared
ccache and private GPU runner. Use established MemoryHigh=4G / MemoryMax=6G /
MemorySwapMax=0 scope. Runtime validation not yet performed for this intake.
Exact next task: run focused float-target and mixed-runtime baseline, then add
the asymmetric shadow producer/receiver regression for UP-019.

UP-002 validation: checked the three input declarations, Vector3 compound
operators and their existing C# synthesis comment, CNAHelper.hpp's macro, and
cmake/Harnesses.cmake's deprecation-as-error gate. This is documentation only;
no strict marker, assertion, budget or runtime contract changed.

UP-018 validation: baseline-v1 24/24 tests on SOFTWARE and OPENGLES3, including
23 existing HDR/format tests. shadow-before-v3 adds rectangle readback and OPENGL33;
all 24 precision/format checks pass on all three. The four additional UP-019 tests
fail only on SOFTWARE (top lit / bottom dark), and pass on GLES/GL33. Build v1/v2
of the new shadow fixture failed to compile (unsupported typed DrawUserPrimitives,
then PBR emissive setter spelling); corrected in v3, no runtime change hidden.
Logs: cna-demos/build/linux-full/reports/repair-engine-*. Peak v1 2293923840 bytes,
v3 925278208, all memory events zero. Dirty Texture2D changes concern ownership,
not format/storage/readback; kept untouched and excluded from commits. Validation
uses the shared worktree, not a clean checkout. No claim about the engine's binary
or a rerun of its calibration; its agent owns revalidation.

UP-006 final-v1: rebuilt public Core/Math/Input/GraphicsCore/Content/Audio/Runtime
static-library consumers beside CNA in both orders. The adapter TU is referenced
without initializing hardware, and real Input/Math/Graphics calls execute. Both
programs start and exit cleanly; nm finds no duplicate strong runtime definitions.
Ten CTests pass (five consumer shapes plus five symbol audits), including the
existing opaque ld.bfd case. No production CMake workaround added: existing
470b12a5d is sufficient for these reproductions. Local commits only.

UP-019 in progress: Software's directional shadow projection repaired locally.
Exact full-suite before/after comparison covers3157 tests per SOFTWARE/GLES/GL33:
only the four new Software orientation tests change FAIL->PASS; all other results
unchanged. Residual failures17/4/13 are retained (dirty occlusion-query work and
default-renderer/lifecycle tests); no full-green claim. final-v1 canonical skinned
tangent fixture passes4/4 on WEBGPU and SDL_GPU too. FNA3D/SDL_GPU-Vulkan exposes
the same upside-down shadow on all four effects; investigate its shared HLSL next.
It declares no Single target, so orientation uses Color there (UP-018 remains a
separate strict Single precision test on Software/EasyGL). Prior fixture failures
on tangent-free SkinnedPbrEffect are retained in logs, not claimed as passing.
UP-019 final-v4: build PASS; precision/format/orientation 28/28 each on SOFTWARE,
GLES and GL33; four orientation checks each on WEBGPU, SDL_GPU and FNA3D.
FNA3D OpenGL and SDLGPU/Vulkan each pass 11 public shadow tests and all nine
existing sampling/PCF/cascade/punctual checks. All 34 Fna3dCompiledEffectTest checks
pass; all ten mixed-runtime consumer/symbol CTests pass. Memory peak 467894272
bytes, all memory events zero. Earlier final-v2 was a nonexistent test target;
final-v3 exposed the old internal oracle requesting COLOR0 with colourless
vertices. Its request now explicitly disables vertex colour, matching its data;
every pixel expectation is unchanged. The oracle is now registered for both FNA
drivers in multi-renderer builds too. No failure was suppressed.

Five CNA-owned FNA .fxb files regenerated with documented June 2010 fxc /Op;
fixture hashes/README updated. Stock Microsoft/FNA blobs unchanged. Shared HLSL
also feeds DirectX: all 26 extension shader entries rebuilt with pinned native
d3dcompiler_47; real D3DDisassemble register tables match the checked-in upload
tables exactly (repair-engine-dxregisters-v1.json). Generator now reuses the existing
MinGW build directory and shared ccache instead of compiling in /tmp. No DirectX
runtime pass claimed. Logs: repair-engine-final-v4-*, shaders-v1-*, dxshaders-v1-*.

No active heavy job at this checkpoint. All commits local, no push. UP-001 remains
actionable; packaging must preserve shared/static target identity, include Sharp
Runtime's public requirements, and prove relocation with an external consumer.
The original cna-demos matrix still has 156 historical IDs, 41 without a final
disposition, two additional pending platform validations and DC-R1/DC-R2 open.
Do not confuse this engine intake with completion of that campaign.

## UP-018 exact engine follow-up (2026-10-22)

Owner supplied Clear(Color(128,128,128,255)), Depth24, default device and indexed
fog draw. Added two permanent regressions, without replacing the original one.
The default constructor selects Reach, so Single is a preference substituted with
Color, reported accurately by getFormatProperty(). GetData<float> copies the raw
RGBA bytes, not a numeric red-channel conversion; white 0xffffffff is a NaN.
This reproduces the engine observation, but not its claimed Single storage defect.
ShadowMap.cpp stores `format_ = format` (the requested enum) instead of the selected
target format. The engine agent owns the correction; no engine/extended files edited.

Evidence: repair-engine-single-compare-v1 compiles the same three permanent tests
against the existing cna-engine/build static libraries at clean baseline source
900d0db3b52d76d3115ebda0a0e04a25fefad1a5, with its HEADLESS/SDL=OFF configuration,
and separately against current next d9e35a3dd's multi-renderer shared library with
the real Headless platform installed. Both run SOFTWARE, no display, 3/3 PASS.
The latter still links SDL dependencies; it is not mislabeled as an SDL-free build.
HiDef + Depth24: full and first-texel grey/white Clear readbacks, then indexed
far/near/far fog draws retain nearest depths 0.625 and 0.625+1/4096 at every texel.
The default Reach test explicitly confirms the Color fallback and white NaN bits.
Native single-followup-v1: build PASS, 30/30 precision/format/shadow tests each
SOFTWARE/GLES/GL33, ten mixed-runtime CTests PASS, peak804761600/events0.

Recovered Microsoft XNA RenderTarget2D.CreateRenderTarget queries selectedFormat
through GraphicsAdapter.QueryFormat and constructs with that result, not the
preference. This CNA behavior was repaired by70fe41618 (SOFTWARE-216), which is
already an ancestor of900d0db3b. RenderTarget2D.cpp and the software Clear/target
allocation/readback sources have no committed differences pin->current next.
No new runtime fix SHA is claimed for UP-018. UP-006's470b12a5d is NOT in the engine
pin; the prior mixed-runtime validation applies to local next, not to900d0db3b.
Nothing has been pushed; the engine must revalidate after its pin actually moves.
