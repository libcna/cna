# CTest execution evidence

`tools/evidence/ctest_evidence.py` is a Python 3 standard-library adapter for one
actual CTest invocation. It reuses `tools/build/build_provenance.py` cache/hash/Git
helpers and CTest's own JSON inventory, JUnit and output log. Existing audit,
provenance and CTest reports remain available; this tool does not replace them.

```bash
python3 tools/evidence/ctest_evidence.py run \
  --build-dir cmake-build-software \
  --output-dir "$EVIDENCE_DIR/ctest-run" \
  -R '^ExecutionEvidenceToolTests$' -L tools \
  --evidence-kind metadata \
  --context 'CPU-only tool checks; no GPU qualification'
python3 tools/evidence/ctest_evidence.py validate "$EVIDENCE_DIR/ctest-run/record.json"
```

Set `EVIDENCE_DIR` to an external evidence directory. The output directory must not exist. Keep it outside the source tree or in an
ignored artifact location. Every invocation retains `record.json`, `configured.json`
(the full `ctest -N --show-only=json-v1` inventory), `selected.json` (the same with
`-R`/`-L`), `ctest.xml`, `ctest.log`, inventory stderr logs and `console.log`.
Commands run sequentially, with the same build/configuration for inventory and
execution. `-C` selects a configuration in a multi-configuration tree. The adapter
does not build targets or change CMake configuration. It inherits the environment;
GPU/window tests still require the repository's private display runner. The example
above selects a CPU-only test.

The small version-1 record contains:

- CNA source path, Git HEAD and dirty flag before/after execution; build path,
  CMake cache SHA-256, generator, build type, configured renderer(s), platform,
  CNA feature options and configured C++ compiler path/ID/version.
- Exact inventory/run commands, `-R`/`-L`/`-C` selection, return codes, UTC start and
  adapter wall time; CTest wall time is retained separately. The full inventory
  retains configured test names, commands and properties (including labels).
- Configured and selected counts, passed, failed, skipped and not-run counts,
  per-test results, skip/failure messages and CTest test output. `executed` counts
  tests with a pass/fail result: `passed + failed`. Skipped and not-run tests never
  contribute to it; `selected = passed + failed + skipped + not_run` for complete
  evidence. These count CTest test entries, not individual Google Test assertions.
- Absolute paths and SHA-256 hashes for both inventories, JUnit and CTest log;
  validation errors and the resulting outcome.

All selected tests are required; there is deliberately no skip allowlist. Outcomes
are evaluated in this order:

| Outcome | Meaning |
| --- | --- |
| `incomplete` | Missing/unreadable/malformed artifacts, inconsistent counts/names/statuses, unfinished invocation, changed source identity/cache during capture, or a missing/truncated log completion summary. |
| `zero-tests-selected` | Complete empty selection, even when CTest exits zero. |
| `fail` | A failed or not-run test, or nonzero CTest exit for a nonempty complete run. Disabled tests count as not-run. |
| `required-skip` | At least one required test skipped, with no failures or not-run tests. A missing device remains a skip with its reported reason, not a test failure or qualification. |
| `pass` | Every selected test passed, the artifacts reconcile, and CTest exited zero. |

Only `pass` exits zero from either `run` or `validate`. Validation reparses the XML
and inventories, reconciles suite counters, selected names, log result rows and
completion summary, checks retained hashes and recomputes the stored result. A
log that is missing or truncated cannot validate as a pass. Unsupported CTest
formats fail closed as `incomplete`; this adapter targets CTest's single
`testsuite` JUnit format (CTest with `--output-junit` support, verified on 3.31.6).

`--evidence-kind metadata` is the conservative default. Source/API audits and this
tool's own tests are metadata checks. Use `runtime` only for a selection that
actually exercises the runtime contract under review. A successful metadata run
is not runtime qualification. Repeat `--context` to retain explicit driver/device,
feature or unavailable-prerequisite notes. The adapter records configured facts
and relevant display/renderer environment overrides; it does not probe hardware
or infer a driver, executable build revision, assertion coverage or platform
qualification. Git HEAD/dirty state describes the source at capture time, not
proof that existing binaries were rebuilt from it. Hashes detect later changes;
they do not authenticate the producer. Keep the record and artifacts together;
if moving them, update their absolute paths before validation.

Run the regression suite directly or through its `tools`-labelled CTest entry:

```bash
python3 tools/evidence/test_ctest_evidence.py -v
ctest --test-dir cmake-build-software --no-tests=error -R '^ExecutionEvidenceToolTests$' --output-on-failure
```

The small checked-in XML/log fixtures are synthetic CTest-format examples. They
cover pass, failure, required skip, empty selection, unavailable executable,
count disagreement and truncation. Unit tests also exercise CLI exit codes,
missing logs, hash changes, selected-name disagreement and stale-output refusal;
no fixture builds or GPU runs are needed.

CI CTest commands must use `--no-tests=error` (or a test preset with
`execution.noTestsAction` set to `error`). This rejects an empty selection; it does
not reject required skips or prove that every intended test was registered. Use
the evidence adapter above when those distinctions matter. In particular, a
nonempty label selection can still lose one of its expected members, and a
registered aggregate can invoke a Google Test filter that matches no cases.
Compare the inventory and the test output with the required witnesses.

`cmake/UnitTests.cmake` runs the two display-global pointer assertions
`Sdl3InputTest.MouseReadsThePointerBesideTheWindowAsXnaDoes` and
`Sdl3InputTest.RecenteringAHiddenCursorKeepsItsWarpsReal` with `RUN_SERIAL`, as well
as their `CnaPlatformTests` aggregate. Each test warps and then reads the same
pointer; another window test can change that state between the two operations.
A lock on only the two readers would leave other window users free to interfere.
The rest of the suite retains its parallel schedule. This isolates one CTest
invocation; concurrent invocations must still have separate private displays.

After regenerating and building the selected tree, verify the registrations with
`ctest --test-dir cmake-build-software --show-only=json-v1`. For a private-display
parallel check, run:

```bash
tools/platform/run_gpu_tests_private.sh cmake-build-software --no-tests=error \
  -R '^Sdl3InputTest\.(MouseReadsThePointerBesideTheWindowAsXnaDoes|RecenteringAHiddenCursorKeepsItsWarpsReal)$' \
  -j4 --output-on-failure
```

Check the two pointer assertions individually with the same runner and their
exact names as well. A skip is an unavailable prerequisite, not a successful
interference regression. Keep the JUnit/log evidence from the owner's display
run; the scheduling properties alone do not prove runtime stability.

The adapter/count contract landed in `3f8145997`; `ddcc0a594` separates source-route
presence from execution in the [C API guide](c-api/README.md) and pipeline audit. Empty-selection
CI guards and pointer serialization landed in `6112d681b`. For `zero-tests-selected` or CTest's
`No tests were found`, inspect `selected.json` and the configured inventory, enable the required
test group and regenerate the tree, then correct the exact filter. For `incomplete`, retain the
failed capture and retry into a fresh directory after resolving missing/truncated artifacts or
source/cache changes. Editing counters cannot repair execution evidence. Python 3 and CTest
with JSON inventory/JUnit support are required; no third-party Python package is needed.

## Compatibility campaign captures (MTH-CONF-007)

The [bounded family map](fna-reference-harness.md#campaign-evidence-map-mth-conf-001007)
and [lifecycle decisions](xna-fna-behavior-decisions.md#mth-conf-004-lifecycle-inspection)
distinguish **declared**, **tested behaviour**, **platform-qualified** and **unassessed** surface.
Campaign evidence retained outside the repository includes the following dumps, logs, commands,
source/binary hashes and results. Recipes below reproduce the named witnesses from a checkout;
counts describe historical runs, not promised totals at a later revision. No tests ran for this
MTH-CONF-007 documentation reconciliation.

| Capture / integrated commit | Retained evidence / executable reproduction | Source/platform qualification |
|---|---|---|
| CONF-001 / `a31835b10` | Eight CLI outcomes: five former false passes now refuse, three legacy fixture self-comparisons pass; 36 Python cases. Run comparator checks below (`FnaReferenceComparisonToolTests`). | Initial source `24e84090c` plus patch; Linux CPU/tool metadata. Fixture self-comparison is acceptance only; strict provenance introduced later rejects the old goldens. |
| CONF-002 / `aa0a7364e` | Two byte-identical value dumps, three original categories compared at tolerance 0, five runtime reference refusals; final comparator 49 and recipe 5 pass. Run the pinned value recipe and tool checks below. | Initial source `dcac546f3` plus patch; Linux/.NET SDK 8.0.425; FNA `00498b0b81425b14bf8da23e68655215b9b5daa7`, assembly 26.10.0.0 SHA-256 `f973ec1edd32290f19750d6d3d26fba23ab1a813fedd71f1ea026edf03c9647c`. Neither native library loaded. |
| CONF-003 through `ee5c7b4b4` | Two aligned FNA dumps, CNA dump, strict comparison, exact-disposition audit, 30 real-dump checks, math and packing logs. Run the value recipe below and the [bounded edge comparison commands](fna-reference-harness.md#bounded-value-and-exception-edges-mth-conf-003); native suite names are in the family map. | Linux x86_64 Release/SOFTWARE/shared; same FNA pin. Twelve repeatability categories; 1,802 retained fields at this stage; math 1,031/0/2 (missing XML), CPU packing 154/0/0. Shared working-tree builds, not integrated-commit rebuilds. |
| Scalar division / `661925667` | Fresh CNA dump, audit of 1,859 retained fields, independent rational binary32 proof of 57 new pairs, 32 real-dump checks and native logs. Reuse the same value recipe; `VectorScalarDivisionContractTest/*.*` and `Vector3CompoundAssignmentTest.*` are the native witnesses. | Observed HEAD `8b3dcb679` plus patch; Linux Release/SOFTWARE/shared. Reused aligned FNA dump SHA-256 `bd529a90d8c6878e49e47711883b9ec9be826e41739dc913c68019d8a46dc9d7`; no FNA rerun. Math 1,084/0/2, packing 172/0/0, Python 32/0/0. Eleven scalar instances failed before correction. |
| CONF-004 / `3671c7ee0` | Native runtime/content/graphics result JSON, exact commands/exits and source/binary hashes. The HEADLESS recipe below replays the historical selections, including all 26 new cases. | Starting HEAD `077a6981c`, final observed `f79f074ff` plus patch; Linux Debug/HEADLESS platform+renderer/shared, GCC 14.2.0. Runtime 137/0/1, content 188/0/3, graphics 102/0/1: **427 pass / 0 fail / 5 skips**. Skips: window/filesystem/audio/render-target capabilities. |

Pass/fail/skip triples count Google Test cases, not CTest entries. Shared HEAD changed during
implementation; dumps lack automatic CNA producer revision metadata. Retained source/binary
hashes bound those observations; an implementation commit alone cannot prove executable
provenance. Shipped-XNA IL identities/offsets and local regeneration commands are in the
[decision ledger](xna-fna-behavior-decisions.md#mth-conf-004-lifecycle-inspection),
[half notes](../tools/fna-reference/xna-half/README.md) and
[Curve notes](../tools/fna-reference/xna-curve/README.md). No successful original-XNA runtime
execution is claimed.

From the checkout root, set `EVIDENCE_DIR` to a fresh external directory and `FNA_SOURCE_DIR`
to the clean, provisioned external FNA checkout; use the existing configured build trees.
The [harness setup](fna-reference-harness.md#building-and-generating-values) defines prerequisites.
The commands below are reproduction instructions, not additional runs:

```sh
python3 -B scripts/test_compare_fna_reference.py -v
python3 -B tools/fna-reference/test_run_reference.py -v
export FNA_EXPECTED_REVISION=00498b0b81425b14bf8da23e68655215b9b5daa7
python3 -B tools/fna-reference/run-reference.py --output-dir "$EVIDENCE_DIR/fna-values"
cmake --build cmake-build-software --target cna_reference_dump CnaMathTests CnaGraphicsTests --parallel 4
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  cmake-build-software/cna_reference_dump "$EVIDENCE_DIR/cna-values.json"
export FNA_EDGE_REFERENCE="$EVIDENCE_DIR/fna-values/values-1.json"
export CNA_EDGE_CANDIDATE="$EVIDENCE_DIR/cna-values.json"
python3 -B tools/fna-reference/check-value-edges.py "$FNA_EDGE_REFERENCE" "$CNA_EDGE_CANDIDATE"
python3 -B tools/fna-reference/test_value_edges.py -v
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  cmake-build-software/CnaMathTests --gtest_output=json:"$EVIDENCE_DIR/math.json"
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy \
  cmake-build-software/CnaGraphicsTests \
  '--gtest_filter=PackedVectorEdgeReferenceTest.*:XnaHalfConversionTest.*' \
  --gtest_output=json:"$EVIDENCE_DIR/packing-witnesses.json"
```

The last command replays the named packing edge/IL witnesses, not the wider historical
154/172-case selections. Current audit success requires 1,859 exact dispositions with an equal
remainder at tolerance zero. The owning harness also gives the raw comparator command, whose
expected exit is 1; disposition success is not FNA parity.

For the original lifecycle selections (HEADLESS platform and renderer; no window qualification):

```sh
cmake --build cmake-build-headless --target CnaRuntimeTests CnaContentTests CnaGraphicsTests --parallel 4
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy timeout 900 \
  cmake-build-headless/CnaRuntimeTests '--gtest_filter=GameComponentCollectionTest.*:GameComponentEnumerationContractTest.*:GameServiceContainerTest.*:GameTest.*:NonBlockingTickTest.*:ComponentLifecycleCompatibilityTest.*:GameEventSemanticsTest.*:GraphicsDeviceManagerTest.*:GameComponentOrderingTest.*:UpdateAndDraw/ComponentPassOrderingTest.*:GameLifecycleOrderingTest.*:GraphicsDeviceManagerLifecycleContractTest.*:GameDisposalDispatchContractTest.*' \
  --gtest_output=json:"$EVIDENCE_DIR/lifecycle-runtime.json"
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy timeout 900 \
  cmake-build-headless/CnaContentTests '--gtest_filter=*ContentManager*:*Ownership*' \
  --gtest_output=json:"$EVIDENCE_DIR/lifecycle-content.json"
env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy timeout 900 \
  cmake-build-headless/CnaGraphicsTests '--gtest_filter=GraphicsResourceEventOrderTest.*:GraphicsDeviceDisposalHookTest.*:GraphicsDeviceLifecycleTest.*:EffectParameterCollectionTest.*:EffectTechniqueCollectionTest.*:EffectPassCollectionTest.*:Model*Collection*Test.*' \
  --gtest_output=json:"$EVIDENCE_DIR/lifecycle-graphics.json"
```

Integration commit messages additionally report private SOFTWARE graphics (3,115 pass /
83 capability/environment skips, `ee5c7b4b4`), multi runtime (299/0/5), content (2,161/0/0)
and SDLGPU C API (152 CTest entries passed, `3671c7ee0`). Their raw private captures were not
located in this reconciliation: these remain **commit-reported results**, not independently
reconciled platform qualification here. Retain the owner's JUnit/log, cache and driver identities
before relying on them. The separate C adapter one-shot disposal witness can be replayed with
`tools/platform/run_gpu_tests_private.sh cmake-build-sdlgpu --no-tests=error -R '^CApi_GraphicsDeviceManagerSmoke$' --output-on-failure`;
native repeat notifications do not establish that contract. Later CONF-005 captures are mapped below;
remaining inputs retain their [precise next experiments](fna-reference-harness.md#campaign-evidence-map-mth-conf-001007).

### Renderer, C API and browser follow-up (2026-11-03)

This refresh covers changes after `4fa24831c`. **Declared** surface remains the
[member inventory](xna-4-runtime-member-coverage.md). **Tested behaviour** is bounded by
these witnesses; **platform qualification** is limited to the retained Linux runs below.
No new runtime tests or XNA binary executions ran for this documentation refresh.

Campaign evidence outside the repository is retained under `../cnaplan/evidence/`
(relative to the repository root). Paths below are relative to that evidence directory.
Commits identify integrated changes, not verified binary build revisions: these were shared
working-tree runs, and most logs lack an immutable source/cache/binary identity bundle.
The desktop runs use private Weston/Xwayland in `cmake-build-multi`, except the noted
`cmake-build-sdlgpu` runs. “Six renderers” means OPENGLES3, OPENGL33, SDLGPU, WEBGPU,
FNA3D and SOFTWARE; HEADLESS/STUB skips never qualify pixels. `EasyGL_` in the shared
CTest names is a registration prefix, not proof of the selected renderer. Renderer-owned
tests can force their own identity (notably `WebGPU_SpriteDepthStencil`). FNA source references
use the local checkout at `00498b0b81425b14bf8da23e68655215b9b5daa7`; no managed reference
dump was regenerated for these graphics/C API witnesses.

| Tested claim / integrated revision | Oracle | Executable witness (CTest unless stated) | Retained run and limits |
|---|---|---|---|
| Deferred sprites observe SetData before End; Immediate retains submitted pixels; blend/depth and scissor/sampler restoration (`cba2e9e36`) | FNA `src/Graphics/SpriteBatch.cs` queue/flush semantics; hand-derived interior RGBA | `EasyGL_Conformance_state_transition_combinations` | `test007/owner-t007b-*.log`, then `owner-t007d-{OPENGLES3,SDLGPU,WEBGPU}.log`: repaired witness passes on six renderers, HEADLESS/STUB skip. Color/HalfVector4, requested 0/4 samples, Immediate/Deferred are selected combinations, not a Cartesian sweep. Applied format/sample downgrades and custom/compiled-effect subskips are not qualified legs. |
| WebGPU stock sprites capture depth/stencil state and reference per submission (`cba2e9e36`) | Hand-derived occlusion/stencil pixels; FNA Begin state application | `WebGPU_SpriteDepthStencil` | Same `test007/owner-t007d-*.log`: WEBGPU-specific test passes, 72 assertions over None/Depth16/Depth24/Depth24Stencil8 and Immediate/Deferred. Does not establish arbitrary transform/clipping or custom shader behavior. |
| Float sprite positions/sizes survive vertex generation; selected source/origin/flip/alpha pixels (`9036515ca`, strengthened `74a61a0b2`) | FNA float destination path; independent hand-derived RGBA literals added after the original CNA-vs-CNA comparison | `EasyGL_SpriteBatchSubpixelContract` | `conf005/owner-c005b-*.log`: six renderers pass five equality cases; `exit002/owner-fr1b-WEBGPU.log` retains the strengthened pass. The other five strengthened passes are commit-reported in `74a61a0b2`; HEADLESS skip retained, STUB skip commit-reported. Equality alone was not an independent oracle. |
| Completed target draws are readable through RenderTarget2D and Texture2D, across A/B/A, rectangles, offsets, mips and resolve (`9036515ca`) | Hand-authored quadrant texels; FNA GetData/resolve contract | `EasyGL_Conformance_render_target_readback` | `conf005/owner-c005b-*.log`: six renderers pass; HEADLESS/STUB skip. Color/HalfVector4, 0/4 samples, Preserve/DiscardContents. Unsupported/substituted legs skip; DiscardContents is explicitly cleared after rebind, so undefined discarded pixels are not compared. RGBA8 exact, half floats tolerance 0.001; no bound-target readback claim. |
| BlendFactor/write masks, alpha endpoints, sample masks and device overrides (`9036515ca`) | Hand-derived blend equations and n/4 resolve values, at most one UNORM8 LSB tolerance | `EasyGL_Conformance_blend_msaa_boundaries` | `conf005/owner-c005b-*.log`; verbose `owner-c005c-{SDLGPU,FNA3D,WEBGPU}.log` retains 4/8/0 mask subskips respectively. SDLGPU qualifies all/none masks only; FNA3D's SDL GPU driver qualifies no masks (reserved SDL fields). Unsupported 4x legs do not qualify MSAA. See [handoff §5.3](../handoff.md#53-external-and-driver-limitations). |
| Line/point coverage at 0/.25/.5/.75 offsets (`9036515ca`) | Observational renderer readback only; **no XNA oracle** | `EasyGL_Conformance_line_point_centres_witness` | Same six-renderer four-test captures; asserts only that pixels are visible, prints coordinates. PointListEXT is a CNA extension. L35/L36 native XNA/D3D9 centers remain unassessed. |
| FNA3D sprites use logical viewport extent; HDR 2D targets and probed BC1 cubes are admitted (`177b55ecc`) | FNA SpriteBatch.PrepRenderState; pinned FNA3D format mapping/readback probe; literal/parity fixtures | `EasyGL_SpriteBatch_CustomViewport`, `EasyGL_SpriteBatch_ViewportSwitch`, `EasyGL_SpriteBatch_CustomViewportRT`, `EasyGL_Parity_hdr_render_target`, `EasyGL_Parity_compressed_cube`, `Fna3dCompiledEffectTest.CubeStorageClassificationIsIndependentOfRenderTargets` | `conf005/owner-final-sweep-FNA3D.log`: named witnesses pass. Cube targets remain Color-only, BC1 depends on the startup probe, other compressed cubes refuse. Instancing refusal is a capability skip. Logical readback does not qualify physical presentation; the diagnostic `EasyGL_CrossFamily_SpriteBatch_Presentation` fails, and FNA3D's own Presentation registration remains absent. SOFTWARE has no physical layout. |
| Adapter queries follow runtime selection before creation and the active device afterward (`177b55ecc`) | Selected renderer descriptor/device consistency; existing XNA QueryFormat contract, not a new XNA measurement | `GraphicsAdapterPlatformTest.QueriesFollowRuntimeSelectionBeforeDeviceCreation`, `EasyGL_Conformance_graphics_adapter_query` | `conf005/owner-final-sweep-*.log`: named cases pass under OPENGLES3/OPENGL33/SDLGPU/WEBGPU/FNA3D selections. This checks routing and applied values, not universal format/sample support. |
| Sprite transform changes Z; negative depth clips (`74a61a0b2`) | Hand-derived `(x,y,layerDepth,1) * transform * projection`, e.g. .25+.5 fails against .5 | `EasyGL_Conformance_spritebatch_transform_depth` | `exit002/owner-fr1b-WEBGPU.log`: 18 assertions pass; HEADLESS skip retained. Six pixel-renderer passes and STUB skip reported by the commit. WEBGPU/SDLGPU projective matrices with nontrivial clip W remain unqualified. |
| C device/platform ownership, stale loans, membership and exit cleanup (`ade6f921e`) | Independent fixed-seed C ownership model, explicit result/last-error checks; membership additionally agrees with XNA IL and FNA | `CApi_{Network,Resource,Graphics}LifetimeSequence`, `CApi_GraphicsLifetime_{borrow,device-platform,exit,membership,standalone-exit,recreate-exit}-regression`, `CApi_HandleRegistry` | `safe004/owner-safe004-gpu3.log`: 10/0/0 on SDLGPU; full set `owner-safe004-capi-all.log`: 155/0/0. Three campaigns each use 256 sequences × 128 transitions. C handle safety does not establish native device-first child-disposal equivalence or exhaustive lifetime safety. [Membership agreement](xna-fna-behavior-decisions.md#mth-conf-004-lifecycle-inspection). |
| C texture refusals, guarded output windows and cross-device binding (`f942e63ba`) | C ABI contract, independent byte patterns/sentinels and literal clear color; native exception contract remains separate | `CApi_DeviceOutputContracts_{cpu,formats,textures,backbuffer,buffers,affinity}` | `test005/owner-t005f-*.log`: full CApi sets 166/0/0 on each of six runtime selections, 168/0/0 in SDLGPU tree. 14 uncompressed formats × five resource kinds may succeed or return NOT_SUPPORTED with an invalid handle; only Color must succeed. Native profile refusal is NotSupportedException, renderer refusal InvalidOperationException. `TextureFormatContractNative.cpp` is a helper executed by `_cpu`, not a CTest named CApi_TextureFormatContractNative. Native `HiDefFormatConstructionTest.*` / `UnsupportedFormatConstructionTest.*` are separate witnesses, not included in these CApi totals. |
| C subscriptions survive source-first/subscription-first and exit teardown (`96dadb0c0`) | Independent callback counts, stale-handle and atexit cleanup assertions; C ownership contract | `CApi_WindowSubscription_{game-first,subscription-first,exit,exit-reused-slot}`, `CApi_DeviceSubscription_{device-first,device-subscription-first,device-exit}` | `exit002/owner-fr2-sdltree.log`: full SDLGPU 175/0/0; `owner-fr2-OPENGLES3.log`: 173/0/0. WEBGPU initial full run has four window failures; corrected `owner-fr2b-WEBGPU.log` reruns seven: 3/0/4, offscreen window refusal. Full corrected WEBGPU suite was not recaptured here; FNA3D/SOFTWARE 173/0/0 are commit-reported. No ASan qualification. |
| Independent C value/output expectations (`96dadb0c0`) | Literal vectors/text/hash, diagnostic fields, CNB wire prefix, PCM endpoints and computed mip average; existing XNA packing decisions, CNA contracts for extensions | `CApi_PackedValueSmoke`, `CApi_InputValueSmoke`, `CApi_DisplayValueSmoke`, `CApi_ShaderSmoke`, `CApi_CnbModelV2Smoke` | Same `exit002` CApi captures. Shader diagnostic literals run even when shader-package support is disabled; this does not qualify shader execution. 22 vectors span 17 packed formats; diagnostics counts 0/0/4/3; linear/sRGB mip literals and PCM8/PCM16 endpoints. Native parity remains secondary. No new XNA run, physical input/display coverage or general CNB/audio/codec parity. |
| Browser campaign and WebGL2 loss/recovery (`982d75273`) | Literal pixels/state assertions plus fractional-vs-translated rendering comparison; simulated lifecycle, not XNA execution | `cna_browser_campaign_page.html` via `scripts/run_browser_tests.mjs` (**no CTest registration**) | `plat006/owner-p006d-{WEBGL2,WEBGPU}.log`: 11/11 checks each in headless Chrome via the private runner (WEBGL2 launcher requests SwiftShader; WEBGPU uses Vulkan launch flags and logs an unidentified adapter, not hardware qualification). WEBGL2 lost/restored events observed; WEBGPU uses its debug lifecycle seam. Covers preload/ContentManager, deferred upload, sprite depth, resize and restored resources. Browser fractional check remains comparative. Real GPU-process loss, multi-renderer bundle, Firefox, Safari/mobile remain unassessed. |
| EasyGL state-cache A/B preserves the final scene while reducing calls (`b0d1b2c5a`) | Same-scene before/after checksum and separately counted GL calls; **not XNA parity** | `cna_easygl_state_benchmark` (**not registered in CTest**) | `gl004/{metadata.json,commands.json,summary.json,owner-ab-run2.log}`: source HEAD `1d79dd4c3` plus benchmark, GL-002/003 hunks reversed for control; Linux Debug, Radeon 780M/Mesa 25.0.7, OPENGLES3/OPENGL33. BindTexture 544→95 and VertexAttribPointer 2560→256 per frame; checksum equal. Submit medians 2.65→1.84 / 2.63→2.10 ms are indicative on a shared host, not a release/per-platform guarantee. +672 bytes per vertex buffer; no WEBGL2 timing or GL-005 speed claim. |

Counts written as pass/fail/skip above count CTest entries; browser numbers count page checks.
CTest's “100% tests passed” also appears for all-skipped selections. Broad sweeps are not
all-green qualification: `conf005/owner-final-sweep-*.log` selects 992 entries per invocation,
including capability skips; OPENGLES3/OPENGL33 have zero failures, SDLGPU/WEBGPU have two GLSL
ShaderEffect multi-tree failures, FNA3D also fails texture_setdata_after_draw and the physical
Presentation diagnostic. `exit002/owner-fr1-sweep2-WEBGPU.log` selects 712, with the same two
GLSL failures. Selected witness passes do not erase those results.

Companion browser logs retain WEBGPU coverage 9/9 (`plat006/owner-p006-webgpu.log`);
that log also contains an earlier failing campaign-page run, superseded by the
`owner-p006d-WEBGPU.log` pass. They also retain the C ABI probe 14/14 and fullscreen 20/20 after rebuilding (`owner-p006e-capi.log`,
`owner-p006e-fs.log`), and canvas lifetime 16/16 (`owner-p006-canvas.log`). These are
script/page checks, not added desktop CTest entries; the C probe also has the
`CApi_WasmBrowserProbe` registration.

The `1d79dd4c3` boundaries also remain explicit: static C API + C++ SDK mixed linking fails
in both orders (external `plat004/REPORT.md`, opt-in `--probe-static-mixed` diagnostic;
shared mixed consumers pass). `CApi_OutputContracts_code` pins INVALID_STATE when shader
text/byte copy uses the wrong payload form (`test005/owner-t005f-sdltree.log`, `f942e63ba`).
Its CNAEXT-gated registration is absent from the multi-renderer captures; `shader.h` does
not name that refusal result.
These are CNA packaging/C ABI boundaries, not XNA-vs-FNA decisions. See
[handoff §5.2](../handoff.md#52-limitations) for the owning limitations and next work.
