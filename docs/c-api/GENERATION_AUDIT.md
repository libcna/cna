# C API generation audit — 2026-10-12

Task **CBIND-AUDIT-001**. Audited `c_binding`, starting at `16fde50cd`, against the
pre-generation base `2fda5389d`. The starting working tree was clean. This is an audit of the
native C API; existing managed tests were used as consumers, without changing any binding.

## Verdict

**YES WITH MINOR FIXES.** The architecture is suitable for a human C++ maintainer. Keep the
committed-source model and the seven simple route kinds. It replaces repetitive boundary code
with ordinary compiled C++ without introducing a runtime interpreter, a generator framework,
or a new public dispatch mechanism. The fixes below address specific safety and maintenance
gaps; they do not change the public headers, layouts, names or ABI version.

## Runtime and repository model

The complete path is `spec/*.toml` → `tools/c-api/generate_routes.py` → `src/generated/*.inc`
→ `#include` in `src/CnaCApi<Family>.cpp` → the compiler → native/WASM code.
All 56 generated includes and `GeneratedRoutesSmoke.c` are version-controlled. Their banners
name the generator and source spec and say not to edit them. Ordinary compilation consumes
these artifacts; it does not regenerate routes or inspect specifications.

There is no C API runtime TOML parser, Python embedding/link dependency, specification-file
access, or string-expression interpreter. Repository searches, CMake source/install rules,
native dynamic dependencies, representative generated functions, and Clang disassembly of
`cna_vector2_add` agree. The ordinary exception barrier, output checks, handle lookups and C++
calls still have their usual cost. The optional foreign-thread service queue is existing C++
behavior, not generation-time routing. Generation itself adds no runtime parsing or dispatch
overhead; this is not a claim that every wrapper compiles to identical instructions.

The authoritative/editable layers are:

| Layer | Authority and maintenance |
|---|---|
| Public `include/CNA/C/*.h` | Public ABI and documented behavior; manually maintained. |
| `spec/*.toml` | Route mechanics and simple expressions; manually maintained. |
| `CnaCApi<Family>.cpp` | Semantic helpers, ownership, callbacks and family-specific validation. |
| Detail/Marshal/Output support | Shared error, handle, value and buffer contracts. |
| `generate_routes.py` | Deterministic derivation rules; change with regression tests. |
| Generated includes and contract test | Derived, committed artifacts; regenerate and review, never edit independently. |

The `CNACApi` installation contains compiled libraries, public headers, native dependencies
selected for packaging, and CMake import/configuration files. It does not install route specs,
the generator, or a Python runtime. Installed C consumers compile and execute through the
existing install-consumer test. The pre-existing consolidated-static-archive packaging script
and Emscripten/export-list tooling use Python **at build time**. This is distinct from route
generation and does not make Python a consumer runtime dependency.

## Generator and specifications

The generator was read in full. It uses standard-library TOML parsing, explicit route-shape
validation, a small header-signature reader and direct string emission. There is no `eval`,
template engine, external Python dependency or hidden runtime registry. File enumeration is
sorted, TOML declaration order is preserved, set-derived diagnostics/stand-ins are sorted,
output is UTF-8 with LF, and no timestamps, random values or absolute machine paths are emitted.
The header reader deliberately supports this repository's declaration conventions, not arbitrary
C syntax; the compiler and ABI probe remain necessary checks.

Recovery tests remove all generated includes in an isolated source copy, regenerate, compare
every byte with the repository, generate again under a different Python hash seed, and run
`--check`. Deliberate output drift and an orphan include both fail the check; regeneration repairs
them. The actual checkout's drift check and clean-checkout guard also pass. No AI is needed to
recover artifacts. The regression suite now runs in CTest and the existing generation workflow.

All 56 specifications were parsed and inventoried, with expressions inspected for length,
control flow and ownership. The longest expression is a 779-character, multiline graphics-adapter
info aggregate: explicit field assignments, not control flow. The other large aggregates are
similarly readable value construction and were left alone. No single-line expression exceeds
300 characters. One ownership expression, `cna_media_queue_add`, was moved to `MediaQueueAdd`:
the helper keeps the copied song in a `unique_ptr` until the native queue successfully adopts it.
The earlier render-target constructor cleanup was already present and was preserved.

The format is still reasonably declarative, with explicitly permitted C++ expressions. It should
not grow loops, ownership transactions or a second programming language. The remaining small
lambdas adapt shared C++ copy/enumeration helpers. A joystick Point conversion now uses `ToC`
instead of reconstructing the same fields in a TOML lambda.

## Boundary, handles, marshaling and buffers

Generated boundaries use the existing exception barrier and standard error recording. Required
outputs precede Boolean/argument checks; handle outputs are cleared before semantic work.
Feature-disabled routes deliberately report `NOT_SUPPORTED` and clear any provided handle
output, even when ordinary argument validation would fail. Diagnostics are explicitly not a
stable string interface. Multiple-invalid-argument ordering is deterministic but is not a promise
that every helper's semantic checks use one universal order.

`optional` deserves care: some older entries use it to delegate a required-output check to a
shared helper, rather than to accept null. The documentation now says this explicitly. Nine
geometry/device routes had their mandatory checks moved into generated shells together with
removal of the corresponding helper checks. An intermediate geometry test caught the missing
spec update during this cleanup; it was corrected and the generated test gained 16 null-output
assertions. Actual nullable/conditional outputs retain their helper semantics.

Exactly eight public functions remain fully manual, all in `CnaCApi.cpp`: `cna_get_abi_version`
and the seven `cna_error_*` queries. The former returns a version directly; the latter must not
overwrite the error record they read. They are appropriate exceptions, not generation backlog.

The registry validates encoded index, generation and kind, and holds shared ownership during
lookups. It checks thread affinity; parent/borrowed lifetime rules remain in family helpers,
not in a generic release operation. Resource creation/release helpers publish registry entries
without absorbing family-specific bookkeeping. The pre-existing generation increment wrapped
from `UINT32_MAX` to 1, eventually allowing an ancient handle to alias a new object. Exhausted
slots now retire permanently with generation zero, which both lookup and allocation reject.
The boundary arithmetic and ordinary stale/wrong-kind/thread/release behavior are tested.

Marshal conversions are field-by-field for vectors, matrices, quaternion, color, point,
rectangle, bounds and vertex structures. They do not rely on C and C++ struct layout identity.
ABI layout is checked independently. DebugDraw's duplicate Color conversion now uses `ToNative`.

Text copies report UTF-8 byte counts **excluding a terminator**; binary/array copies report their
documented unit. Null destination with nonzero capacity is invalid. Null with zero capacity
reports the size and returns `BUFFER_TOO_SMALL` for nonempty data. Shared copy helpers do not
write a short-buffer prefix. UTF-8 scalar validation rejects overlong encodings, surrogates,
out-of-range scalars, stray continuations and truncation; semantic path/name readers also reject
embedded NUL. The independent UTF-8 oracle, buffer/error tests and family semantic tests exercise
these rules. Error-copy functions deliberately preserve the diagnostic on their own failures.

Five existing `StoreOutput` call sites and some delegated copy helpers still repeat a boundary
check/barrier. They are small shared conveniences, not hundreds of duplicate implementations;
removing every such call was not necessary for this audit. No further conversion consolidation
was identified that justified changing lifetime-sensitive code.

## Compatibility and history

`git diff 2fda5389d -- modules/c-api/include` is empty: the public headers are byte-identical.
The checked-in ABI baseline and ELF/Apple export lists are also unchanged from that base. The
compiled ABI probe agrees on 197 structures, 295 scalar types, 1,463 numeric constants,
14 string constants, 141 color constants and all 3,210 exported functions, including runtime
ABI version **0.46.0**. The declaration/export comparison has no missing or extra names.

History inspection found the expected route migration, support consolidation and test/doc
changes, plus these deliberate fixes:

| Change in the refactor history | Audit conclusion |
|---|---|
| Diagnostic wording | Permitted by `ERRORS.md`; callers branch on result/category. |
| Validation ordering | Deterministic boundary ordering; no public promise of the former inconsistent ordering. |
| Stricter UTF-8 reads | Enforces the existing UTF-8 contract; malformed-name regression retained. |
| GraphicsDeviceManager presentation parameters | Canonical bool/reserved-byte checks now match the shared contract; regression retained. |
| Assembly-title null destination | Shared copy validation prevents a null write; retained. |
| Compiled-out DebugDraw creation | Clears handle as promised; strengthened test starts with a nonzero sentinel. |
| DirectX 11 exit hang | Deliberately process-lifetime adapter-query device avoids late driver-thread teardown; retained. |
| Windows `crypt32` link and portable test fixtures | Necessary platform build/test fixes; no public ABI changes. |

The large diff predominantly moves routes and changes helper indentation/names. No public API
redesign or unrelated subsystem campaign was found. Native MSVC/Apple CI configuration was
inspected, but the renderer workflows are not evidence of a current exhaustive C API campaign.

## Measured metrics

Physical lines include blanks/comments. Hand-written implementation means top-level `.cpp` and
`.hpp` files in `modules/c-api/src`, excluding public headers, tests and generated includes.

| Measure | Before this audit | After fixes |
|---|---:|---:|
| Public `cna_*` functions | 3,210 | 3,210 |
| Generated boundaries | 3,202 (99.7508%) | 3,202 (99.7508%) |
| Generated expression routes, including shared generic helpers | 2,253 | 2,252 |
| Routes with dedicated semantic helpers | 949 | 950 |
| Fully manual functions | 8 | 8 |
| Hand-written implementation LOC | 48,957 | 48,961 |
| Generated C++ include LOC | 51,815 | 51,858 |
| Spec LOC / files | 5,905 / 56 | 5,902 / 56 |
| Route generator LOC | 939 | 956 |
| Generated files from route generator | 57 | 57 |
| Generated C contract-test LOC | 5,256 | 5,272 |
| Generated assertion sites | 4,227 | 4,243 |

The pre-refactor base has **93,642** hand-written implementation lines by the same definition.
The expression/helper classification above matches the campaign's convention, not absence of
all helper calls. The stricter mechanical breakdown is **1,971 non-`result` routes and 1,231
`result` routes**; 281 of those `result` routes call shared helpers rather than dedicated
route-named semantic implementations. Handle probes execute repeatedly, so 4,243 is the number
of assertion sites, not distinct semantic behaviors or total dynamic assertions.

Generated tests are useful boundary probes, not semantic proof. They check null outputs,
version headers, invalid/stale/wrong-kind handles, selected cleared outputs and text pairs.
Some probes permit an earlier argument rejection or disabled feature; they do not prove the
later lookup ran. The independent bool suite and hand-written family tests remain essential.
After adding the missing DebugDraw depth-setting exercise, every public route is also referenced
outside the two generated C tests. That name census still does not prove complete behavioral
coverage.

## Corrective changes

1. Reject ineffective/misspelled `refuse_nul` and malformed group field types with named spec
   diagnostics. Prevent the formatted-copy shortcut from bypassing canonical-bool validation
   on a future bool-taking route. Add generator recovery/drift/schema tests to CTest and CI.
2. Make bool-validation diagnostics allocation-safe inside the `noexcept` function; a regression
   forces allocation failure and requires a normal `INVALID_ARGUMENT` result/error record.
3. Remove redundant nested barriers in DeviceQuery, DeviceCommand, StoreOptionalDistance and
   DebugDraw's WithMap; move required-output checks with their spec exemptions as described above.
4. Retire exhausted handle generations instead of wrapping and resurrecting stale handles.
5. Move media-queue ownership into a named RAII helper; reuse Point/Color marshal conversions.
6. Add the missing ELF export-map `LINK_DEPENDS`, matching the Apple link-dependency practice.
   Generated includes already participate in compiler dependency tracking; spec changes require
   explicit regeneration, not an automatic parallel-build writer.
7. Document runtime neutrality, build/package distinctions, recovery, manual exceptions,
   optional-output delegation and the limits of generated test coverage; correct the manual
   functions' source filename. Strengthen DebugDraw disabled-output and setter tests.
8. Restrict the C API tests' automatic X11 driver selection to native non-Apple Unix targets,
   and remove Linux-only driver settings from Windows, Apple and Emscripten test environments.
   Cross-platform inspection found both inherited and explicit `SDL_VIDEODRIVER=x11` settings;
   Windows SDL cannot use that driver, including when its executable runs through Wine.
9. Keep imported host Python out of the cross-compiling emulator. Do not register the native-only
   ABI measuring script with a cross compiler: it directly executes its temporary binary and does
   not implement emulator support. Cross configurations retain compiled C/C++ layout assertions,
   `CApi_AbiSmoke`, header compatibility and export inspection.
10. Export Emscripten's `requestFullscreen` runtime property so SDL can replace it. Previously an
    assertion-enabled module installed a read-only unexported-method trap there, and creating the
    first SDL window threw a JavaScript TypeError. The real browser probe now passes all 13 checks.
11. Correct the capability smoke test to require the common format-usage bits without forbidding
    additional known bits. DirectX11 explicitly classifies sampling/filtering/transfer usages;
    the former test incorrectly required sampling to be unknown. No renderer behavior changed.

No stubs or dependencies were added. No public headers, signatures, enum values, layouts,
calling conventions or ABI version were changed. The changed exceptional behavior is safer
allocation failure and generation exhaustion; no previously valid ordinary call is removed.

## Validation record

All window/GPU/browser/Wine runs used the private Weston/Xwayland runner, never the owner's
desktop. After the owner's resource reminder, builds used the shared `/rv/cnaccache`, at most
six compile jobs, and no concurrent builds. Early cache-disabled invocations due to sandbox
permissions were superseded by cache-enabled builds with the appropriate filesystem permission.

| Check/configuration | Result |
|---|---|
| GCC HEADLESS, optional modules off, shared/static | Build PASS; 123/123 C API checks PASS. This run preceded the final handle-exhaustion change, covered below. |
| GCC multi-renderer, optional modules off, shared/static | Final implementation build PASS; 123/123 checks PASS. Ten compiled renderer identities; this is not ten separate exhaustive runs. |
| Fresh Clang Release HEADLESS, CNAEXT/DEVICES on | Build with strict warnings PASS; 123/123 checks PASS, including allocation-failure and handle-generation regressions. |
| Incremental build | PASS; no route regeneration or unnecessary compilation; export-map linker dependency inspected. |
| Generator `--check`, two generations, deliberate drift, orphan, clean recovery | PASS; four regression methods with multiple subcases. |
| Clean-checkout guard running generator regression suite | PASS. |
| Bool generation drift / route coverage / release and documentation gates | PASS. Release-gate test validates the recorded policy, not a claim of complete C++ API coverage. |
| ABI probe / header baseline / ELF declared and actual exports | PASS; no changes against `2fda5389d`. |
| Static package and installed CMake consumer | PASS in native CTest suites. |
| MinGW multi-renderer C API and strict C/C++ headers | Build PASS; PE export names exactly match all 3,210 declarations. |
| Cross-build host gates after CMake fixes | 11/11 PASS. Native ABI measuring script is intentionally not registered here. |
| Wine initial full suite | 119 tests: 100 PASS, one intentional MediaLibrary skip, 18 failures. Twelve were host-tool emulation/configuration errors, one an overly restrictive capability assertion, five DirectX/DXVK timeouts. Corrected and isolated below; not reported as a green full DirectX run. |
| Wine HEADLESS rerun of the six renderer-sensitive failures | 6/6 PASS with the same compiled library; this does not prove DirectX pixels. |
| Wine DirectX11 corrected GraphicsDeviceSmoke | PASS; renderer-specific extra capability bits are valid. |
| Emscripten WEBGL2+WEBGPU module/probe | Build PASS. Node smoke PASS, all 3,210 C API exports plus malloc/free present. Link contract PASS: WebGL2, Asyncify off. |
| Actual Chromium browser C API probe | 14/14 PASS, including callbacks, shared-loop lifecycle/timing, frame counts, BigInt handles, render targets and teardown. |
| Existing .NET integration consumer assembly | All 261 tests covered successfully: 260/260 on multi-renderer with dummy audio, and the DEVICES-dependent dialog test 1/1 on the enabled Clang configuration. No managed source changes. |
| Final whitespace / generated drift / header comparison | PASS. |

The managed suite initially failed with unavailable audio and disabled DEVICES; dummy audio and
the matching enabled configuration resolve those failures. An all-HEADLESS run passed 257/261;
four rendering-dependent checks need the multi-renderer configuration, not a stub pixel backend.
Browser discovery initially needed the already installed Playwright path. These were not counted
as successful runs until rerun correctly. Test command invocations with incorrect relative Node
paths or runner arguments were corrected, not treated as product failures.

NOT RUN: Apple native build (no Apple SDK).

NOT RUN: MSVC native build (no MSVC toolchain). MinGW/Wine is supplemental evidence, not a substitute.

No new microbenchmark suite was created: source/dependency/disassembly inspection establishes the
absence of generation-time parsing and dispatch at runtime.

### Exact native CTest inventory

The following 123 checks all passed in both the final-implementation GCC multi-renderer and
Clang runs. This includes the family semantic tests, not just generated contract checks.

```text
CApiCoverageMatrix
CApiCoverageScopeModel
CApiCompatibilityMatrix
CApiHeaderCompatibility
CApiLimitations
CApiDocExportCounts
CApiRouteTestCoverage
CApiBoolContractCurrent
CApiGeneratedRoutesCurrent
CApiRouteGeneratorTests
CApiReleaseGate
CApiAbiHeaderBaseline
CApiAbiBaseline
CApiDeclaredExports
CApi_Exports
CApi_HeaderAudit
CApi_RetiredRuntimeFacade
CApi_KeyIdentities
CApi_GamePadButtonIdentities
CApi_AbiSmoke
CApi_RendererEnvironment_unknown
CApi_RendererEnvironment_absent
CApi_ErrorSmoke
CApi_BoolContractSmoke
CApi_GeneratedRoutesSmoke
CApi_CoreExtSmoke
CApi_MathValuesSmoke
CApi_GraphicsDeviceSmoke
CApi_Draw3DSmoke
CApi_GameDefaultDepthBufferSmoke
CApi_GraphicsExtSmoke
CApi_OwnedGraphicsDeviceSmoke
CApi_ConcurrentDeviceCreationSmoke
CApi_GameSecondaryGraphicsDeviceContext
CApi_TeardownLifetime_explicit
CApi_TeardownLifetime_game-alive
CApi_TeardownLifetime_game-alive-no-frame
CApi_TeardownLifetime_children-alive
CApi_TeardownLifetime_partial
CApi_TeardownLifetime_cycles
CApi_TeardownLifetime_device-alive
CApi_RenderTargetLifetimeSmoke
CApi_StorageSmoke
CApi_ContentReaderSmoke
CApi_NetSmoke
CApi_VectorSmoke
CApi_QuaternionSmoke
CApi_MatrixSmoke
CApi_GeometrySmoke
CApi_CurveSmoke
CApi_ColorSmoke
CApi_CnbSmoke
CApi_CnbDocumentSmoke
CApi_CnbTextureSmoke
CApi_CnbModelSmoke
CApi_CnbSchemaSmoke
CApi_CnbToolingSmoke
CApi_PackedVectorSmoke
CApi_VertexValueSmoke
CApi_VertexResourceSmoke
CApi_VertexBufferSmoke
CApi_IndexBufferSmoke
CApi_EffectAnnotationSmoke
CApi_EffectParameterSmoke
CApi_EffectTechniqueSmoke
CApi_EffectSmoke
CApi_BasicEffectSmoke
CApi_StockEffectSmoke
CApi_SkinnedEffectSmoke
CApi_PbrEffectSmoke
CApi_ModelBoneSmoke
CApi_ModelMeshPartSmoke
CApi_ModelMeshSmoke
CApi_ModelSmoke
CApi_MorphTargetSmoke
CApi_SkinnedModelSmoke
CApi_AnimationPlayerSmoke
CApi_GraphicsResourceSmoke
CApi_TextureSmoke
CApi_TextureVolumeSmoke
CApi_LifecycleSmoke
CApi_ContentSmoke
CApi_GraphicsSurfaceSmoke
CApi_InputSnapshotsSmoke
CApi_HapticsSmoke
CApi_JoystickSmoke
CApi_InputDevicesSmoke
CApi_MediaSmoke
CApi_SensorValuesSmoke
CApi_SensorDeviceSmoke
CApi_AudioStreamingSmoke
CApi_AudioSoundEffectSmoke
CApi_AvatarIdentitiesSmoke
CApi_GuideSmoke
CApi_GamerPropertiesSmoke
CApi_LeaderboardsSmoke
CApi_AvatarsSmoke
CApi_AvatarExitWhileLoadingSmoke
CApi_AchievementsSmoke
CApi_GamersSmoke
CApi_GamerIdentitiesSmoke
CApi_XactSmoke
CApi_StressSmoke
CApi_Audio3DSmoke
CApi_GraphicsDeviceManagerSmoke
CApi_RuntimeGameSmoke
CApi_HostDrivenRunSmoke
CApi_ForeignThreadCallsSmoke
CApi_RuntimeComponentsSmoke
CApi_RuntimeComponentDeviceBorrow
CApi_DevicesSmoke
CApi_SensorEventsSmoke
CApi_ExitWithForeignThreadGameSmoke
CApi_ContentModelEffectSmoke
CApi_VideoSmoke
CApi_MediaPlayerSmoke
CApi_MediaLibrarySmoke
CApi_AudioSmoke
CApi_AudioUnavailableSmoke
CApi_HandleRegistry
CApi_BoundaryDetail
CApi_Utf8Oracle
CApi_InstalledConsumer
```

### Remaining validation limitation

Five Wine/DXVK DirectX11 cases timed out at 120 seconds: Draw3DSmoke, ContentSmoke and
TeardownLifetime children-alive/partial/cycles. A serial partial-teardown rerun also timed out
at 30 seconds. All five pass with HEADLESS using the same binaries; the renderer implementation
and these test bodies were not changed by this audit. The existing adapter-query exit fix was
preserved, but is not proof that every DirectX lifetime path works. Root cause of these residual
driver/lifetime failures is not established, and native Windows validation remains necessary.
This was a real outstanding platform result, not a claimed generation regression or a green
DirectX certification. It lapsed when the `DIRECTX11` renderer was retired on 2026-10-13.
