# handoff.md — current state of CNA

This file describes the repository as it is now and is kept true. Until 2026-10-11 it was `NEXT.md`
and also carried the dated session log of 2026-07 to 2026-10; that log is in Git history
(`git show 8b5564f1e:NEXT.md`).

## 0. CNA/cna-demos repair campaign handoff — 2026-10-24

This is the current continuation point for a fresh-context agent. It is not a claim that the
overall campaign is complete. Push only on the owner's word, Sharp Runtime before CNA before
cna-demos; the 2026-10-24 work is published.

Repositories and branches:

- CNA: this repository, branch `next`, published through the commit recording this (2026-10-24,
  pushed on the owner's word together with Sharp Runtime and cna-demos).
- cna-demos: `../cna-demos`, branch `next`, published through the same push (`dd0f284` and the
  commit recording it). Its `docs/CNA_ISSUES.md` is the permanent historical inventory and its
  assertions remain the oracle.
- Sharp Runtime: `../sharp-runtime`, branch `next`, published through `f990d074` (XmlSerializer
  property members, which CNA `bbed8c61e` needs). Do not make an incidental dependency edit.
- Museum and cna-engine inventories are consumer-owned. Do not edit them from CNA; their agents
  move pins/findings after published CNA commits are available.

Campaign accounting from the repair matrix `CNA_DEMOS_REPAIR_MATRIX.json` (deleted on 2026-10-29
once every row had its final disposition; `git show 9c2b3e2d4:CNA_DEMOS_REPAIR_MATRIX.json`):
**156 historical IDs, 156 final dispositions**: 119 `FIXED_VALIDATED`, 23
`DOCUMENTATION_CORRECTED`, 8 `ALREADY_FIXED`, 3 `FIXED_PLATFORM_VALIDATION_PENDING` (003
SpriteBatch Textures[0], 101 video URI resolution, 152 StorageContainer FileShare: each needs a
  native Windows/XNA run this machine cannot do; §8.1 says what to run), one `DEMO_EXPECTATION_BUG`,
one `DUPLICATE_ROOT_CAUSE` and one `UNSUPPORTED_CONTRACT_CORRECTED`. The three graphics findings
outside the historical count were resolved too:
`DC-R1` `FIXED_VALIDATED` and `DC-R2` `UNSUPPORTED_CONTRACT_CORRECTED` on the owner's decisions,
and `DC-R3` `FIXED_VALIDATED`. No demo carries a `knownIssue` marker any more.

### Latest accepted work

- Historical C API generation-audit retirement (2026-11-01):
  the 402-line `docs/c-api/GENERATION_AUDIT.md` snapshot from `CBIND-AUDIT-001` was removed from
  live documentation. Its October 12 counts, ABI version, 123-test inventory, Wine/DXVK results and
  retired DIRECTX11 limitation described one completed campaign and had drifted from the current
  3,213-route surface; later edits had also blurred the meaning of a fixed audit record. No active
  obligation was lost: the current maintenance contract is `docs/c-api/GENERATED_ROUTES.md`, handle
  generation exhaustion is in `docs/c-api/HANDLES.md`, and the original report and fixes remain
  recoverable with `git show 3ee6ae3c8:docs/c-api/GENERATION_AUDIT.md`. Generation, ABI, export,
  documentation-count and release gates remain green after the removal.
- C API generated-routes maintainer-guide reconciliation (2026-11-01):
  `docs/c-api/GENERATED_ROUTES.md` now matches the generator's actual conversion and test
  boundaries. It corrects the claim that every `result` parameter is passed unconverted (only its
  text inputs are; structures and booleans still follow conversion and `raw` rules), distinguishes
  dedicated from shared semantic helpers, and qualifies required/optional outputs and
  feature-disabled stubs. The workflow no longer claims that `CApiRouteTestCoverage` enforces a
  hand-written semantic test: regenerated `GeneratedRoutesSmoke.c` names the route automatically,
  so that gate is a name census and the family test remains required evidence. The current census
  is 3,213 declarations: 3,205 generated routes from 56 specs into 56 includes plus the generated
  contract test, and exactly the seven error queries plus `cna_get_abi_version` remain manual.
  Generated drift, five generator regressions, route-name coverage, Boolean drift, declared exports
  and the release gate pass; the generated strict-C contract test passes through the private SDLGPU
  runner.
- C API game-runtime and components contract reconciliation (2026-11-01):
  `docs/c-api/GAME_COMPONENTS.md` now describes the complete current game, component, window,
  graphics-device-manager and borrowed-content-manager boundary instead of the earlier
  implementation narrative. It corrects four obsolete claims: every explicit component disposal
  remains observable, duplicate collection add/insert is rejected, nonempty colonless launch
  arguments are retained with an empty value, and XNA's integer window token is distinct from the
  structured native-window accessor. The page now records blocking, one-frame and host-driven run
  shapes, foreign-thread dispatch, callback borrowing, service constraints, mutable device settings
  and manager retention. Public Doxygen clarifies callback-borrowed game routes, repeated explicit
  initialization and duplicate insertion, and the manager-destroy documentation is attached to its
  declaration. All 12 selected game, component, window, manager, ABI and generated-route tests pass
  through the private SDLGPU runner; all nine coverage, generation, limitation, export and release
  gates pass.
- C API gamer-services contract reconciliation (2026-11-01):
  `docs/c-api/GAMER_SERVICES.md` is now a current C-boundary contract instead of a chronological
  implementation narrative. It separates deterministic no-service strict-C evidence from the
  manual configured-service client, records owned versus borrowed handles, distinguishes waiting
  callback routes from Guide's genuinely deferred keyboard and message-box workflows, and keeps
  service limitations in the native limitations page. The page and public Doxygen now describe a
  gamer tag as a fixed `uint64_t` token rather than a pointer-sized value, correct the obsolete claim
  that no supported runtime has a friend service, and state that `PropertyDictionary` recognizes
  nine kinds but exposes setters for eight because streams are inspection-only. Historical avatar
  extension removal moved out of the live contract. All 13 selected gamer, Guide, achievement,
  leaderboard, avatar, ABI and boundary C API tests pass through the private SDLGPU runner; all nine
  coverage, generation, limitation, export and release gates pass.
- C API shared-byte fuzzing reconciliation (2026-11-01):
  `docs/c-api/FUZZING.md` now states the exact shared adapter boundary instead of claiming that one
  string-view target covers every caller-controlled byte parser in the expanded C API. It records
  the deterministic oracle's actual enumeration and sampling split, distinguishes the native
  test-build object check from a manual libFuzzer campaign, and replaces the broken historical
  Clang command with a verified build that includes the platform headers, current Sharp Runtime
  paths and core exception RTTI. Findings and the mutable corpus now stay inside the selected
  stable build tree.
  The generated release gate narrows its matching safety claim to UTF-8, pointer/count and
  element-size adapters and explicitly leaves subsystem parsers their own obligations. The fuzz
  object and UTF-8 oracle build, `CApi_Utf8Oracle` passes through the private SDLGPU runner, a
  10,000-run ASan/libFuzzer check finds no disagreement, and the release/documentation gates pass.
- C API error-contract reconciliation (2026-11-01):
  `docs/c-api/ERRORS.md` is now a current result, category, exception-firewall and diagnostic
  contract instead of the original ABI snapshot. It records all fixed-width category values and
  the range-category exception, exact non-overwriting size/copy semantics, the successful
  foreign-thread-dispatch reset, canonical exception type/parameter metadata, network-join and
  sensor-error supplemental payloads, and first-failure persistence for lifecycle callbacks.
  The consumer rules now distinguish plain-C control flow from binding-only exception metadata and
  message text. Focused error, boundary, content-exception, network, sensor, host-driven callback
  and generated-route regressions pass through the private SDLGPU runner; route generation,
  route-test coverage, coverage, limitations and export-count gates remain current.
- C API effects contract reconciliation (2026-11-01):
  `docs/c-api/EFFECTS.md` is now a current reflection, lifetime, construction, stock-effect and
  material-extension contract instead of an implementation-era narrative with eight repeated test
  transcripts. It corrects the false claim that an invalid handle clears the current technique,
  documents all seven PBR texture slots and the later material properties, records that only
  SOFTWARE currently applies ColorMatrixEffect through SpriteBatch, and adds ShaderEffect compile
  diagnostics plus the tightly packed vec3/mat4 array rules. Public compiled-effect Doxygen now
  names the WEBGPU, SOFTWARE and DIRECTX9 opt-ins as well as FNA3D, SDLGPU and EasyGL. Generated
  coverage evidence no longer claims unverified HEADLESS, OPENGLES3 and sanitizer runs, and its
  stale five-slot clone statement is corrected to seven. All 13 selected targets build; all 11
  effect, generated-route, graphics-extension and ABI C tests pass through the private SDLGPU
  runner; route generation, route-test coverage, coverage, limitations and export-count gates pass.
- C API devices and sensors contract reconciliation (2026-11-01):
  `docs/c-api/DEVICES.md` now separates the always-built sensor/vibration surface from the optional
  `CNA_DEVICES` host services, names the two still-planned accelerometer keyboard-emulation members
  and records the correct host identity: Android/iOS are `DEVICE`, while every desktop and browser
  target is `EMULATOR`. Sensor disposal is no longer overgeneralized: accelerometer/gyroscope have
  the C layer's blanket post-disposal guard, while compass/motion retain their canonical per-route
  behavior. The page, public sensor Doxygen, strict-C test comments and generated coverage evidence
  now agree that `DevicesSmoke.c` invokes the real camera/tray constructors only as boundary probes;
  deterministic behavior remains on test backends, and physical camera behavior is not qualified.
  All six focused SDLGPU C API tests pass through the private runner with the current
  `CNA_DEVICES=OFF` tree; route generation, route-test coverage, limitations and the full coverage
  inventory are current.
- C API core contract reconciliation (2026-11-01):
  `docs/c-api/CORE.md` now covers the current three-layer foundation (`abi.h`, `core.h` and
  `core_ext.h`) instead of stopping at the original logger/platform/renderer-classification slice.
  It records ABI primitives and shared values, per-thread diagnostics plus canonical exception
  metadata, process-wide logger threshold/sink behavior, platform facts, sparse renderer identities,
  compile-time versus selected versus active renderer state, opt-in fallback history, assembly title
  and the non-exhaustive `_ext` naming convention. The public log-sink Doxygen and
  `CALLBACKS_AND_THREADING.md` now state synchronous emitting-thread delivery, serialization,
  non-reentrancy and the context lifetime through replacement/reset; the C API landing-page map
  now links the core contract. All ten focused SDLGPU C API tests pass through the private runner,
  and the renderer-identity, runtime-discipline, coverage, limitations and export-count gates pass.
- C API content contract reconciliation (2026-11-01):
  `docs/c-api/CONTENT.md` now describes the complete current manager surface rather than the former
  three-loader slice: all nine typed/foreign producers, their independent versus cache-disposed
  ownership, game-manager borrowing/copying, roots and manifests, low-level ContentReader and
  process-wide type-reader behavior, caller XNB/CNB/CNJ loaders, reflective readers, object
  dictionaries and Model tags. The remaining absent typed manager routes are Song, Texture3D,
  Curve and standalone AnimationClip. The generated coverage mapping and `LIMITATIONS.md` now name
  that same boundary. `content.h` no longer calls native `ResourceContentManager` a placeholder:
  its two-argument native form is implemented, while the C factory must use the no-resource form
  because `System::Resources::ResourceManager` has no C representation, so C-created resource
  managers still return IO on load. All six focused SDLGPU C API tests pass through the private
  runner: ContentSmoke, ContentReaderSmoke, ContentModelEffectSmoke, ContentExceptionSmoke,
  VideoSmoke and RuntimeGameSmoke; both generated coverage checks are current.
- C API CNB contract reconciliation (2026-11-01):
  `docs/c-api/CNB.md` is now a current ownership, authoring, loading and error contract instead of
  the chronological `CBIND-106`–`CBIND-111` implementation narrative. It records the active read-
  limit consumers, current Texture2D BC1/BC2/BC3-plus-RGBA8 representation boundary, Model schema-1
  C data graph including cameras, schema-2 runtime-load versus C authoring boundary, SoundEffect
  schema 2 and the wider WAV conversion rules. The page now distinguishes the reserved, schemaless
  Effect identity from implemented asset schemas and states that a registered custom CNB loader is
  reachable through `cna_content_manager_load_foreign_ext`; the former direct-invocation-only claim
  was also corrected supportively in `docs/c-api/CONTENT.md` and the public `content.h` Doxygen
  contract. A stale WAV-test comment was aligned with the implementation. All seven focused CNB and
  Content strict-C tests pass through the private SDLGPU runner, as do six native regressions for
  custom manager dispatch, Model schema separation/loading, wide PCM conversion, format-spec scope
  and compressed texture fallback.
- C API audio contract reconciliation (2026-11-01):
  `docs/c-api/AUDIO.md` is now one current subsystem contract rather than an initial PCM slice with
  later SoundEffect, streaming, capture, 3D and XACT appendices. It distinguishes four direct
  SoundEffect constructions from the non-caching content-manager acquisition path; records the
  complete parent/child destruction order, instance behaviour, any-thread fire-and-forget exception
  and opt-in dispatch requirement for producer-thread streaming; and makes callback delivery and the
  shared audio registration lifetime explicit. Microphone short reads, 3D mode latching and the
  nearest-listener stereo approximation are reconciled with the headers and tests. The XACT section
  now states the content-version-46 settings boundary, indexed renderer details, variable domains,
  owned versus fire-and-forget cues and the actual resident/streaming-wave-bank evidence. Its
  limitations section records the absent deterministic positive capture fixture, the known
  `Microphone::Start` failure-state defect, missing playback-device-loss recovery and the lack of a
  positive streaming-XWB fixture instead of presenting those paths as fully qualified. Seven
  focused SDLGPU C API tests pass through the private runner: AudioSmoke, AudioSoundEffectSmoke,
  ContentSmoke, AudioStreamingSmoke, Audio3DSmoke, XactSmoke and AudioUnavailableSmoke.
- C API callback, threading and shutdown reconciliation (2026-11-01):
  `docs/c-api/CALLBACKS_AND_THREADING.md` is now a current cross-cutting contract rather than an
  initial-ABI snapshot. It distinguishes lifecycle failure callbacks from observing event handlers
  and synchronous contexts from retained registrations; records the owner-thread default, the two
  any-thread SoundEffect play routes and the opt-in blocking foreign-thread service queue; names the
  game-driving and dispatch routes that are never queued; and documents explicit pumping, error
  transfer and shutdown cancellation. The callback-scope section now includes frame hooks,
  components and activated/deactivated handlers, both graphics-device loan windows and the complete
  non-reentrant driving set. Shutdown now separates blocking owned children from game/window event
  registrations that deliberately survive disposal. The directly contradictory thread paragraph
  and obsolete initial-slice boundary were removed from `docs/c-api/AUDIO.md`. Seven focused SDLGPU
  C API regressions pass through the private runner:
  lifecycle, host-driven run, component device borrow, foreign-thread calls, owner-thread refusal,
  any-thread SoundEffect play and stress/registration teardown.
- C API landing-page reconciliation (2026-11-01):
  `docs/c-api/README.md` is now a current entry point rather than a hand-maintained inventory of
  the initial ABI 0.1.0 slice. It records ABI 0.52.0, 3,213 exports, 60 C99/C++11-compatible public
  headers and the generated 8,134-symbol disposition (7,033 implemented, 15 partial, 633 planned,
  453 not applicable), with the experimental verdict honestly **Not ready** while planned rows
  remain. The old C17/HEADLESS vertical-slice narrative and 60-name header list are gone; renderer
  evidence now distinguishes the deterministic HEADLESS refusal control from real-renderer proof,
  the native-window interop descriptor is acknowledged as the narrow exception to the no-native-
  object boundary, and a compact document map replaces the closing wall of prose. The landing-page
  ABI, header count, coverage tuple and verdict are now checked by `CApiReleaseGate`; its CI job has
  a negative stale-README test, and `CApiDocExportCounts` now watches the README export count too.
  Six focused coverage/compatibility/limitations/export/release/ABI CTests pass, all relative links
  resolve, and the existing generated reports remain unchanged and current.
- Installed C API consumer contract reconciliation (2026-11-01):
  `docs/c-api/CONSUMING.md` now matches the package actually emitted by `CNACApi`. The installed
  consumer gate is scoped to native ELF and Mach-O trees rather than every build; Windows has
  header-only evidence here and Emscripten uses its separate Wasm artifact. The guide no longer
  calls the callback the only legal graphics-device loan window, assumes every package carries a
  static archive, calls the renderer-independent example headless, or repeats a stale full-install
  size. Its non-CMake ELF command now supplies an executable RUNPATH: the former `-L`-only command
  linked but could not find `libcna_c_api.so` at startup. Vendored SDL and wgpu-native are separated
  from system-selected FFmpeg, curl, Opus, zstd and shaderc dependencies, and the static target's
  resolved/partly absolute external closure is explicitly qualified to a producer-compatible
  machine. The documented export-map paths now point to `modules/c-api/cmake/`. The standalone
  example requests the `CApi` package component explicitly, and the installed-consumer script no
  longer embeds an obsolete 113 MB measurement. A fresh SDLGPU static archive exposes all 3,213 C
  routes, the package installs, and shared plus static `hello_cna` both build and run through the
  private consumer test; export, ABI-baseline and prose-count gates pass with them.
- C API ABI/versioning contract reconciliation (2026-11-01):
  `docs/c-api/ABI_VERSIONING.md` now separates its historical release ledger from the current
  contract, records the current 98 generated `CNA_Bool` routes, and describes the actual `0.x`
  review rule and native gate scopes. Most importantly, it no longer presents appending a field to
  an existing versioned structure as a compatible evolution path: the implementation requires the
  exact structure version and at least the library's compiled size, so its larger-tail tolerance is
  one-way and an older smaller caller cannot use a newer enlarged layout. Compatible growth must
  use reserved storage or a new supported structure/version/route. The baseline regression suite
  now proves that an appended field is also a layout break, and the generator rejects a current ABI
  identity in the release note that disagrees with `abi.h`; the CI path filter includes both files.
  C and C++ header walls now pin the promised IEEE-754 binary32/binary64 representations. The
  header-only and full-library baselines agree on ABI 0.52.0, 196 structs, 296 scalar types, 1,478
  integer constants, 11 floating constants, 14 string constants, 141 colours and 3,213 exports;
  all 12 generator regressions and all seven focused C API CTests pass, the latter through the
  private SDLGPU runner.
- C API WebAssembly artifact reconciliation (2026-11-01):
  `docs/c-api/WASM_ARTIFACT.md` is now a current consumer contract rather than an eleven-part
  snapshot tied to the deleted `fixcnats.md`. It removes the stale qualified ABI 0.21.0, lists all
  layouts actually pinned by `module_entry.c`, distinguishes the WEBGL2 browser probe from WebGPU
  renderer evidence, and records that the native ELF/Mach-O manifest generator is not a Wasm
  provenance tool. The generator now rejects a `.wasm` instead of falsely reporting Linux/x86_64
  with zero exports. The Node smoke reads `abi.h`, compares the exact packed runtime version and
  decodes its 16-bit major correctly. Emscripten configurations no longer enter the native C API
  smoke block and fail on `Threads`, so the three Wasm tests can be registered; the browser probe
  explicitly skips non-WEBGL2 renderers rather than claiming WebGPU qualification. The WEBGL2 C
  ABI artifact rebuilt with all 3,213 routes, and link, Node and real Chrome/Playwright probes all
  pass through the private runner. A WEBGPU+C-API configure confirms the explicit browser skip,
  but its fresh module build remains unqualified because vendored ENet misdetects `socklen_t` under
  this Emscripten tree and fails on a conflicting typedef; no WebGPU C ABI pass is claimed.
- C API header-compatibility audit (2026-11-01): `docs/c-api/COMPATIBILITY.md` now describes the
  source-level contract its generator and CI actually enforce. The unconsumed four-row runtime
  table is gone: no tool read it, the current HEADLESS/SOFTWARE/ASan trees do not build the C API,
  and the ASan preset is OPENGLES3 rather than the table's SOFTWARE, so its claim that every C
  smoke ran in every row was false. Runtime, renderer, sanitizer and platform qualification are
  now explicitly separate evidence. Schema 2 validates unique toolchains, language/standard
  agreement, roles, notes and outside-matrix boundaries before rendering. A missing Windows C++
  target row was added together with its CI package, bringing the measured matrix from 23 cells
  over seven toolchains to 26/26 passing cells over eight toolchains and 60 public headers. The
  shared limitations page now records the same honest boundary and ties the compiled-effect gap
  to the C smoke's lack of valid bytecode rather than to obsolete renderer configurations; the
  generated release gate remains **Not ready** solely because 633 symbols are still planned. The
  five compatibility/limitations/export/release CTests pass, as does `CApi_EffectSmoke` under the
  private SDLGPU runner. Reconfiguration also exposed the earlier `SDL_GPU` parser regression as
  an unclassified SDL reference; `CoreExtSmoke.c` now has one narrowly bounded containment entry,
  and the non-production SDL audit passes at 85 files/1,089 references.
- Generated C API limitations audit (2026-11-01): `docs/c-api/LIMITATIONS.md` is current against
  the 8,134-symbol inventory: 15 partially mapped symbols in nine approved groups and 453 symbols
  with no C form in 89 classified groups. Its declared non-symbol limitations now record the
  foreign-thread dispatch and any-thread `SoundEffect` exceptions instead of calling every handle
  use unconditionally thread-affine; remove stale dated GNU-unique-symbol and renderer-count
  snapshots; scope the compiled-effect gap to the C smoke suite; and acknowledge the
  separately qualified Emscripten C ABI instead of saying the web target has never been exercised.
  The limitations generator also renders singular symbol counts correctly. Its shared
  outside-the-header-matrix boundary was reconciled by the following compatibility audit. The four
  registered header/limitations/export-count/release-gate checks pass, and the renderer-identity
  and release document gates remain green with the experimental verdict correctly **Not ready**.
- Experimental C ABI release-gate audit (2026-11-01): the generated
  `docs/c-api/RELEASE_GATE.md`, its ten declared criteria and every measured source are current.
  Nine criteria are met; coverage closure is correctly not met while 633 public symbols remain
  planned, so the experimental ABI remains **Not ready**. The generator's public help no longer
  claims that two owner-decision criteria exist when the current declaration has none, and its
  verdict now renders the singular correctly as "1 criterion is unmet." The document was
  regenerated only through `check_release_gate.py --write`; `--check`, `--run` and the registered
  `CApiReleaseGate` CTest all pass without changing the decision or reclassifying a criterion.
- Generated C API coverage audit (2026-11-01): the mandatory
  `generate_coverage_inventory.py --check` gate found `docs/c-api/COVERAGE.md` stale after recent
  public-header documentation changes. Regeneration changes only the full-matrix SHA-256; its 466
  headers and 8,134 symbols remain 7,033 implemented, 15 partial, 633 planned and 453 not
  applicable, with the same six open planned owners. The full inventory, total module-scope
  function, case-insensitive Internal/Detail exclusions, owner approvals and generated summary
  were reviewed without reclassifying any row. `CApiCoverageMatrix`, `CApiLimitations` and
  `CApiReleaseGate` pass; the last validates that the generated release verdict is correctly still
  **Not ready** because all 633 planned symbols remain real open work.
- C renderer/capability contract reconciliation (2026-11-01):
  `docs/c-api/RENDERERS_AND_CAPABILITIES.md` is rewritten from its initial-slice description to the
  current C surface. It now distinguishes compile-time default, selected, active and per-device
  identities; documents both callback and pre-run borrowed-device windows; separates the legacy
  capability flags from the four-state feature, known limit and per-format mask profile; explains
  sparse renderer/feature/limit identity spaces; and covers explicit-state/effect SpriteBatch
  begin plus full and windowed backbuffer readback. The unrelated audio/touch paragraph and the
  obsolete claim that state/effect handles were future work are gone. Public C header comments now
  identify the compile-time default and active device renderer precisely, and the misplaced
  `get_current_type` Doxygen block is attached to its declaration. `CoreExtSmoke` now pins former
  `SDL_GPU` alongside retired `VULKAN` as unrecognized by the C name predicate. The four relevant
  C smoke targets build and pass on the private display; the ABI-header baseline and all three
  renderer registry/discipline/combination checks pass.
- Removed-renderer archive reconciliation (2026-11-01): `docs/removed-renderers.md` remains the
  historical record for all 41 retired identities, but now separates its current permanent-value
  contract from dated coverage and validation evidence. It clarifies that retired constants are
  unpublished while their numbers stay reserved, distinguishes the refused former `SDL_GPU`
  spelling from the retired `SDL_RENDERER` identity, and states the different retired-name
  behaviour of selection routes and the C `try_parse` predicate. Actual commit dates replace the
  campaign dates in five `Removed` rows, the missing `RRC-012` and `RRC-018` removal hashes are
  explicit, and the stale post-BLEND2D surface-presenter claim now records `SOFTWARE`'s next-day
  `TCR-2` successor. The identity gate now parses the document's complete reserved-value table and
  rejects any missing, duplicated, renumbered or resurrected entry; the C ABI smoke pins a retired
  name as unrecognized by `try_parse`. Its target builds, and all 69 identity/configuration tests,
  seven private-display runtime diagnostics, the private-display C ABI smoke and the ABI-header
  baseline pass.
- Renderer-registry documentation reconciliation (2026-11-01): the former informal overview is
  replaced by the exact ten-identity/eight-family contract across C++, the stable gapped C ABI,
  CMake selection and the generated runtime registry. It now records every identity's category,
  maturity and implementation family, distinguishes SDL/FNA3D/WebGPU drivers from CNA renderers,
  covers retired-value semantics, platform/build gates, native and browser WebGPU dependencies,
  default and multi-renderer selection, and the difference between compile-time availability and
  successful device creation. The identity checker now mechanically guards this page's public and
  family counts; the README's misplaced capability reference and the old handoff heading were
  corrected. `CnaTests` and the SDLGPU C ABI smoke target build; all three renderer-registry
  scripts pass, as do 74 configure/retirement/default tests, 22 private-display C++ registry and
  classification tests, and the private-display C ABI core-extension smoke test.
- Detailed renderer-capability-profile documentation reconciliation (2026-11-01): the original
  implementation/task narrative is replaced by the current device-scoped C++ and C ABI contract.
  The page now distinguishes the legacy summary from the cached detailed snapshot, documents all
  four support states (including that `Restricted` is false for bool convenience queries), the
  eight current limits and retired ordinal gaps, the two per-format masks and their three common
  classified usages, exact cache invalidation points, diagnostic-report schema and buffer rules,
  and the different invalid-identity behavior of C++ and C. `CnaTests`, the SDLGPU C API smoke
  target and the multi-renderer test target build. On private displays, all seven
  `RendererCapabilityProfileTest` cases, `CApi_GraphicsDeviceSmoke` and the four LOD/PBR,
  multisample-mask and render-target-readback `CrossRendererContractTest` cases pass.
- WebGPU design-notes retirement (2026-11-01): the 68-line `docs/webgpu-design-notes.md` duplicate
  is deleted after its five topics were checked against the implementation and the reconciled
  renderer contract. It incorrectly left the implemented transient-buffer pool as future work,
  attributed WebGPU's edge-expansion wireframe to EasyGL, and called callback pumping the only
  native/browser difference. Wireframe, callback/Asyncify behavior, vertex-color mapping and
  platform ownership already live in `docs/webgpu-renderer.md`; the only useful extra rationale,
  why FIFO queue ordering makes transient-buffer reuse safe without an explicit fence, was folded
  into that page. Both incoming links were removed and the documentation inventory now covers the
  remaining 177 tracked pages.
- WebGPU renderer-document reconciliation (2026-11-01): the 1,662-line `WEBGPU-1`..`209` campaign
  diary is replaced by a 389-line current native/browser contract with the required precise parity
  statement and an explicit limitations section. It removes contradictory historical claims about
  opaque-white classic null textures, unimplemented render-target mips and semantic layouts,
  Texture2D-only block compression and an all-clear limitations list. The page now delegates moving
  inventories, distinguishes live adapter capabilities from fixed support, records the two known
  presentation defects, surface requirement, device-loss boundary, MRT/cube, base-instance,
  split-stream, format, sample-mask, LOD-bias and custom SpriteBatch limits, and documents both
  legacy and reflected WGSL plus opt-in compiled effects. All local links and the renderer-identity
  gate pass; 187 WebGPU-specific targets rebuild, and the private native run passes all 190 tests
  in the `WebGPU` label (188 executed passes and two declared conformance skips). The browser
  canvas-lifetime harness was not rerun because this host has Node 20.19.2 while the driver requires
  Node 22+ with global `WebSocket`; the previously recorded headless-Chrome evidence is unchanged.
- Documentation reconciliation inventory (2026-11-01): root `documents.md` now lists all 177
  tracked `docs/**/*.md` files and distinguishes completed standalone reconciliation from pending
  work and files changed only as supporting indexes/ledgers. It records the two partially duplicate
  BasicEffect and AlphaTestEffect passes, explains the other high-touch files, and excludes six
  historical pages retired on 2026-10-31 and the WebGPU design notes retired on 2026-11-01. The
  inventory matches `git ls-files` exactly: every
  tracked Markdown document appears once, no extra entry exists and every link resolves.
- `AlphaTestEffect` support-document cleanup (2026-11-01): the Phase 43 Tasks 371-379 diary and
  EasyGL-only support table are replaced by the current renderer-neutral contract. The page now
  defines the public defaults, eight-way shader selector, exact half-byte comparison boundaries,
  texture/material/vertex alpha ordering, opaque-black null sampling, view-space fog, semantic
  inputs, discard/depth rule, renderer evidence boundary and owned-texture extension. Closed tasks
  remain only as a compact Git-history map, while moving test totals are replaced by stable fixture
  names. The renderer-identity gate passes, and all 66 selected API, null-texture, EasyGL, WebGPU,
  SDLGPU and Software comparison/parity/fog checks pass on private displays.
- `BasicEffect` support-document cleanup (2026-11-01): the Phase 42 Tasks 361-370 diary and
  EasyGL-only support table are replaced by the current renderer-neutral contract. The page now
  defines the public defaults, 32-variant selector, material/lighting and fog equations, semantic
  vertex inputs, measured D3D9 vertex-output saturation, null-texture behavior, renderer evidence
  boundary and explicitly separate CNA shadow/punctual-light extensions. Closed tasks remain only
  as a compact Git-history map, while moving test totals and registrations are replaced by their
  stable fixture names. The renderer-identity gate passes, and all 75 selected API, null-texture,
  saturation, EasyGL, WebGPU and SDLGPU parity/conformance checks pass on private displays.
- XNA/FNA migration-guide reconciliation (2026-11-01): the graphics Tasks 486-489 snapshot and
  its fixed-item/member checklists are replaced by a current porting workflow tied to the generated
  runtime-member report, live renderer capabilities, content/effect contracts and this file's sole
  known-defect ledger. The guide no longer calls the fully implemented XNB `ModelReader` partial,
  labels live render-target `Texture2D::GetData` a CPU-cache read, recommends one GL identity for
  every 3D port, presents Task 933's expected post-`SpriteBatch` rasterizer state as a command-order
  defect, or sends GPU tests to caller-selected Xvfb/`DISPLAY`. It now distinguishes `CNAEXT` from
  `CNA_CNAEXT`, lists the exact compiled-effect build options, describes compile-time and runtime
  renderer selection, and routes all GPU/window work through the private runner. All 20 local
  links resolve, the renderer-identity gate passes, and six focused ModelReader, maturity,
  capability-profile and EasyGL SpriteBatch/3D-order tests pass on the private display.
- Runtime renderer-selection documentation reconciliation (2026-11-01): the former
  `RTR-P0`–`P14` campaign/current-contract mixture now describes the canonical 10 identities over
  eight implementation families, generated descriptor registry, selection precedence,
  post-success latch, opt-in reported fallback, window-kind ownership, platform combination rule
  and browser boundary. Historical LLGL transcripts, 2026-08 binary-size measurements and obsolete
  Emscripten zlib/`RandomAccess.cpp` workarounds were removed. The public
  `GraphicsRendererSelection` Doxygen now matches the implemented post-success latch, and the live
  EasyGL multi-profile MSAA defect is explicit instead of the page claiming that nothing remains
  open. All three renderer consistency gates pass, the focused `CnaTests` executable builds, all
  five configure-time registry/default tests pass, and the private selection/fallback/registry/
  window run has no failure across 78 registered tests: 73 pass and five platform-window cases are
  declared skips.
- Shared parity-fixture documentation reconciliation (2026-11-01): the former 13-fixture
  `WEBGPU-207` snapshot now documents the canonical 32-entry rendered corpus and the separate
  32-entry conformance-program corpus driven by `ParityFixtures.cmake`, including their actual
  family registrations, primary versus full-frame oracles, reviewed policy types and extension
  workflow. All examples now use the private GPU runner. Both frame scripts inherit its display,
  refuse an absent display and validate that the reference is desktop `OPENGL33` rather than the
  capability-different default `OPENGLES3`; the WebGPU page delegates the moving inventory to the
  shared guide. Registered OPENGLES3, OPENGL33, WebGPU and SDLGPU parity suites complete 32/32,
  32/32, 32/32 and 34/34 without failure. The freshly built OPENGL33 reference also passes both
  complete OPENGL33↔WebGPU and OPENGL33↔SDLGPU corpora, with every fixture satisfying its own
  oracle and its reviewed whole-frame policy.
- FNA3D renderer-document reconciliation (2026-10-31): the mixed `FNA3D-0`..`63` campaign
  narrative is replaced by the current Experimental portable-family contract. The page now owns
  the pinned FNA3D/MojoShader forks, offline source verification, runtime OpenGL/Direct3D 11/
  SDL_GPU selection, mandatory pre-window preparation, real compiled XNA Effect route, live
  driver-dependent capabilities, resource/lifecycle rules, explicit refusals and Linux/macOS/
  Windows qualification boundary. Closed device-creation incidents and fixed validation totals
  were removed; the 2026-08-11 image comparison is explicitly historical and its rerun now uses
  the private GPU runner. The complete Release FNA3D configuration builds at the verified fork
  pins. Its private OpenGL label completes 66 registered tests with no failure: 64 execute and two
  source-`ShaderEffect` cases are declared inapplicable skips.
- Software renderer-document reconciliation (2026-10-31): the 1,227-line `SOFTWARE-1`..`485`
  campaign diary, fixed suite totals and live-desktop/Xvfb instructions are replaced by the
  current Experimental CPU-rasterizer contract. The page preserves the six numbered architecture
  decisions cited by source, records the live rendering/resource/MSAA/query surface and bounded
  allocation policy, and distinguishes a retained-but-unexecuted `ShaderEffect` placeholder from
  opt-in executed compiled XNA effects. It also documents the optional platform surface presenter
  instead of calling every `Present()` a no-op, routes GPU comparison through the private runner,
  and delegates task evidence to the parity ledger and Git history.
  The focused Software and diagnostics targets build. After relinking six stale shared-library
  model-test executables, the full Software label completes with 175 passes, 21 declared
  capability/profile skips and no failure.
- `WEBGL2` renderer and Emscripten platform-document reconciliation (2026-10-31): the GLB/Task 459
  snapshots are replaced by two current, non-overlapping contracts. The renderer page now owns the
  supported-class identity, runtime EasyGL profile, exact WebGL 2/GLSL ES contract, profile-specific
  swizzle/base-vertex/wireframe/occlusion/MSAA behavior, live format/capability queries and browser
  qualification. The platform page owns final-link policy, canvas/display behavior, blocking versus
  host-driven game loops, pthread/OFFSCREEN_FRAMEBUFFER and renderer-lease interaction,
  WasmFS/IDBFS persistence, asynchronous context recovery and the measured Chrome-centric evidence
  boundary. It removes the obsolete `SurfaceFormat::Color`-only claim and no longer presents the
  build/link-only multi-renderer CI job as the whole browser evidence. The stable
  `cmake-build-wasm-webgl2` C API artifact rebuilds and links; its contract reports exact WebGL 2
  with Asyncify off, the Node module smoke finds all 3,215 exports, and the real Chrome browser
  probe passes all 14 ABI, Game lifecycle, WEBGL2 context, clear/present and stock-effect MRT
  readback checks. Renderer identity and combination consistency gates also pass.
- `OPENGL33` renderer-document reconciliation (2026-10-31): the old GLB campaign snapshot and its
  fixed suite counts, closed failures and Xvfb instructions are replaced by the current production-
  class desktop-core profile contract. The page now distinguishes runtime `GlProfile` selection
  from the build default, desktop GLSL/stock-shader/compiled-effect dialects, native base-vertex,
  point-size, wireframe and exact-query behavior, live capability boundaries, Linux/macOS evidence
  and the missing current real-Windows qualification. Stale RTR-P11 comments in the descriptor and
  CMake profile/combination rules were corrected. The audit also found the multi-profile
  `RasterizerState.MultiSampleAntiAlias` defect now recorded in §5.1 and qualified in the
  RasterizerState page instead of hiding it behind the single-renderer claim. A new dedicated
  `cmake-build-opengl33` Debug configuration builds its focused graphics/renderer and nine live
  EasyGL targets; 100 selected checks on a real Mesa 4.6 core context produce 87 passes and 13
  intentional profile/fallback skips, with no failure.
- `GraphicsDeviceManager` support-document reconciliation (2026-10-31): the Task 230 snapshot now
  describes the current Game-owned-device lifecycle and real `GraphicsDevice::Reset` pipeline.
  Runtime MSAA, color/depth presentation formats, swap interval and viewport changes are no longer
  mislabeled as stored-only preferences; applied renderer values, event forwarding, physical versus
  logical resize behavior, CNAEXT presentation modes and the native C API boundary are explicit.
  The audit also corrected obsolete reset/MSAA comments and the WebGPU page's former presentation-
  mode default. Targeted runtime/EasyGL and SDLGPU C API targets build; 50 focused manager,
  lifecycle, resize, depth, MSAA, vsync and C API tests pass across EasyGL, Software, WebGPU and
  SDLGPU, and all six selected C API documentation/coverage gates pass after refreshing their
  generated inventory. Two real API gaps found by the audit are now in §5.1 rather than hidden by
  a blanket supported label.
- `RasterizerState` support-document reconciliation (2026-10-31): the EasyGL-only Phase 38
  snapshot is now a current eight-family contract. It preserves the measured Microsoft XNA
  winding and state-freeze rules, records conditional native EasyGL wireframe, WebGPU edge
  expansion and the per-family cull/bias/scissor routes, and separates multisampled target storage
  from `RasterizerState.MultiSampleAntiAlias`. The old claim that the property reaches no renderer
  was false: the shared seam now forwards it, Software implements the complete triangle/line/
  wireframe behavior and desktop OpenGL toggles `GL_MULTISAMPLE`; the remaining no-op families are
  recorded in section 5.2. Stale WebGPU comments that still described wireframe refusal and
  EasyGL line expansion were corrected without changing behavior. The WebGPU target rebuilds; 133
  focused tests across OPENGLES3, WebGPU, SDLGPU, Software, DirectX9, FNA3D and HEADLESS select 116
  executed passes plus 17 intentional capability/profile skips.
- `ShaderEffect`/compiled-effect support-document reconciliation (2026-10-31): the old
  `CNAEXT::ShaderEffect`, six-stock-effect and renderer-unspecified snapshot is now the current
  two-contract guide. It names the real XNA-namespace `CNAEXT` type, the five public stock effects,
  the `CNA_CNAEXT` gate for typed code/packages and all eight renderer-family boundaries for legacy
  acceptance, exact dialect intake, actual shader execution and compiled `.fxb` support. The same
  audit corrected SDLGPU documentation: typed SPIR-V intake does not require runtime `shaderc` and
  can use ShaderCross, while only the legacy GLSL-text route is compiler/build-conditional. Focused
  validation passes 33 tests across EasyGL, WebGPU, SDLGPU, Software, DirectX9, FNA3D and HEADLESS;
  that includes a rebuilt `CNA_CNAEXT=ON` SDLGPU typed-package pixel conformance test. The renderer
  identity consistency check also passes.
- `OcclusionQuery` support-document reconciliation (2026-10-31): the old four-renderer snapshot is
  now a current eight-family contract. It preserves the recovered XNA lifecycle, adds the shared
  overlapping-interval coordinator and signed-`DWORD`/boolean aggregation rules, distinguishes
  exact counts, visibility-only results, HEADLESS validation and explicit refusal, and records the
  present DirectX9/FNA3D evidence boundaries. The stale WebGPU first-render-pass limitation is
  removed from all three capability pages: later contiguous runs, including a run after `Clear`,
  use additional query-set slots and contribute to the logical result. Focused validation passes
  all 152 selected tests across EasyGL, WebGPU, SDLGPU, Software, DirectX9, FNA3D and HEADLESS.
- DirectX9 source-`ShaderEffect` capability reporting (2026-10-31): the renderer's existing real
  HLSL path now reports `ShaderDialectEXT::Hlsl`, accepts HLSL vertex and fragment payloads and
  answers that source execution is real. Consequently `RendererCapabilityProfile` publishes
  `ShaderEffectSourceExecution` and `ShaderDialectHlsl` as supported instead of contradicting the
  rendered pixels. The focused MinGW target builds, and both `DirectX9_EffectRenderer` (which now
  pins all public answers before its uniform-driven pixel checks) and
  `DirectX9_SpriteBatch_CustomEffect` pass under Wine+DXVK in the private runner. The DirectX9
  `CnaGraphicsTests` target also builds, and its three focused capability/profile regressions pass.
- Graphics renderer feature-matrix reconciliation (2026-10-31): the old EasyGL/D3D9 comparison,
  separate Software companion and historical glTF campaign tables are replaced by one current
  entry point covering all six rasterizing families plus HEADLESS/STUB. The new matrix separates
  baseline support from build/profile/device/driver-dependent features, preserves the special
  `Present` boundary, links each conditional claim to its focused source of truth and no longer
  presents closed task lists or dated test counts as current capability evidence. Focused
  validation selected 159 capability/profile/contract tests across the multi-renderer, SDLGPU,
  WebGPU, Software, FNA3D and Headless configurations: 147 passed and 12 inapplicable renderer
  branches skipped as intended; the canonical renderer-identity check also passes.
- Vertex-format support-document reconciliation (2026-10-31): the old EasyGL-only stride table is
  now a cross-renderer contract that separates declaration validity, native format mapping and
  shader semantic consumption. It records all 12 formats across the six rasterizing renderer
  families, all 13 usage semantics, Reach/HiDef and device limits, multi-stream collision remapping
  and the compiled-effect boundary; the false `Depth`/`Fog`/`PointSize` backlog is gone. Focused
  validation passes 196 shared, EasyGL, Software, SDLGPU and FNA3D tests; the SDLGPU, FNA3D and
  multi-renderer focused test targets build successfully.
- Viewport/display/adapter support-document reconciliation (2026-10-31): the Phase 40 page no
  longer says Emscripten and Android have never run CNA. It records the real Chrome WEBGL2/WEBGPU
  and Android-emulator OPENGLES3 boundary without claiming dedicated adapter-value coverage, and
  incorporates the later adapter video pin/rebinding/thread-safety work, XNA's strict 1.6
  widescreen threshold, `XNA-MISSING-010` device-selection flags and renderer-aware capability
  probes. Focused validation passes 112 API, subsystem-lifecycle, platform and resize tests across
  the default and WebGPU builds; two renderer/environment-specific branches are expected-skipped.
- Sampler-state support-document follow-up reconciliation (2026-10-31): the Phase 35 page no
  longer calls mip-aware `Point`/`Linear` a deliberate level-zero deviation, and now records
  REMED-GFX-175's corrected nine-value filter table. The renderer inventory includes Software and
  DirectX9 instead of a stale catch-all no-op row, the EasyGL summary covers later LOD/W, lifetime
  and `SpriteBatch` work, and the closed historical list is separated from the two genuine current
  limitations. All 14 focused EasyGL sampler, mip, anisotropy, AddressW and publication regressions
  pass in the private GPU runner.
- SkinnedEffect support-document reconciliation (2026-10-31): Tasks 893–895 are no longer
  presented as open, the pre-modular test path and `NEXT.md` references are gone, and the former
  "more accurate" per-pixel-only behavior is correctly recorded as the Task 1102b XNA mismatch.
  The page now includes the later fog, world-normal and `Vector4` bone-index fixes and points to
  the current EasyGL/SDLGPU cross-family corpus. Focused private-runner validation passes all
  95 API, EasyGL and SDLGPU tests.
- EnvironmentMapEffect support-document reconciliation (2026-10-31): Tasks 890 and 891 are no
  longer described as open, the old pre-modular test path and `NEXT.md` reference are gone, and
  the page now records the later fog, per-vertex Fresnel and amount-saturation fixes. Its matrix
  points to the current EasyGL/SDLGPU regressions, including Task 1112's correction of the former
  per-fragment Fresnel deviation. Focused private-runner validation passes all 57 property,
  EasyGL and SDLGPU tests.
- SpriteFont support-document reconciliation (2026-10-31): the obsolete combined-`SpriteEffects`
  limitation is closed. The page now reflects `REMED-GFX-003`'s composable flag operators,
  four-entry `DrawString` mirror tables, defensive low-bit mask and focused combined-flip/operator
  regressions; the support matrix no longer reports value 3 as unrepresentable. Focused validation
  passes 7/7 tests.
- GraphicsDevice support-document cleanup (2026-10-31): the obsolete manual FNA surface audit was
  retired after its enduring `Clear` behavior decisions moved into the XNA-over-FNA ledger and its
  `Present` capability boundary moved into the current renderer matrix. Stale Software and parity
  text now reflects `XNA-MISSING-011`: the overload exists, shared validation/clipping is active,
  Software honors source regions, HEADLESS accepts non-observable requests, and other non-default
  combinations fail explicitly. The generated runtime census was rerun unchanged at 3,627/3,627;
  focused HEADLESS/Software/EasyGL private-runner validation passes all 35 executed tests with 7
  expected renderer-gated skips.
- Graphics-resource documentation cleanup (2026-10-31): the obsolete Task 211 FNA audit was
  retired after its valid metadata and lifetime contract moved into the current
  `graphics-resource-lifetime.md`. That document now matches measured XNA disposal-event ordering,
  exception-safe deregistration and the implemented cross-device move/tracking transfer; the
  XNA-over-FNA decision has a permanent ledger entry. The focused private-GPU lifecycle slice
  passes 27/27 tests.
- DualTextureEffect support-document reconciliation (2026-10-31): Task 889 is no longer described
  as an open EasyGL `VertexColorEnabled` defect; the support matrix and migration guide now point
  to its implemented colored-shader path and discriminating EasyGL/SDLGPU/WebGPU coverage. The
  null-sampler section now reflects the measured opaque-black rule and its shared indexed and
  non-indexed regression across all active rasterizing renderers, replacing the obsolete claim
  that renderer-family reconciliation was still pending. Focused private-GPU validation passes
  21/21 tests across EasyGL, SDLGPU and WebGPU.
- Surface-format support-document reconciliation (2026-10-31): the EasyGL page now separates XNA
  profile/resource restrictions, renderer classification, public transfer fidelity, physical GL
  storage and renderability. It records the completed classic Texture2D/cube/volume and DXT work,
  the runtime-probed normalized/float render-target matrix, honest refusal of FNA extension
  formats, and the surviving `Alpha8`, texture-norm16 and WebGL swizzle limitations; the obsolete
  T176 implementation backlog was removed. The focused private-GPU validation passes all 54
  format, sampling, render-target and refusal regressions.
- Render-target support-document reconciliation (2026-10-31): the Phase 39 page now reflects the
  implemented closure of Tasks 877, 880 and 881, recorded cube-face binding/disposal, exact EasyGL
  depth formats, real viewport state and the shared one/four-target profile ceilings. The old
  `EasyGL_MRT_TwoAttachments` failure is correctly classified as a discarded test premise; its
  multi-output replacement and the measured `SOFTWARE-220/221/222` validation rules are current.
- Graphics-state support-document reconciliation (2026-10-31): the depth/stencil, sampler and
  rasterizer support pages now agree with the implemented closure of Tasks 866, 871 and 872.
  Named presets, stencil clears (including ordering/write-mask restoration), and independent
  `ReferenceStencil` updates point to their current implementation and discriminating regressions;
  Task 869's surviving literal-identity difference points to the accepted `SOFTWARE-198` mapping.
- Depth-occlusion audit documentation cleanup (2026-10-31): the resolved Task 955 investigation
  was retired after the `GraphicsDevice` constructor-default contract and its active EasyGL/SDLGPU
  regression moved to `docs/depthstencilstate-support.md`. Task 956's separate SpriteBatch blend
  leak remains fixed and independently documented/tested. The old SimpleAnimation conversion and
  retired-renderer comparisons are historical only; the full investigation and screenshots remain
  in Git history.
- Culling-audit documentation cleanup (2026-10-31): the resolved Task 954 investigation was
  retired after its measured real-XNA winding contract and active regression inventory moved to
  `docs/rasterizerstate-support.md`. All oracle, test and follow-up references now point to the
  live contract. The old SimpleAnimation index-buffer repair is superseded by `cna-samples`'s
  official-XNB re-port, and the incidental Bgfx subrange finding left no current renderer defect;
  the full investigation and screenshots remain in Git history.
- Graphics-extension documentation cleanup (2026-10-31): the closed `MOD-RETIRE-1` pre-deletion
  scope audit was removed after its surviving API inventory was checked against
  `modules/graphics-ext`, its ABI consequence against the 0.30.0 record, and its historical
  measurements against Git. The only still-useful future constraint is now kept in
  `docs/cnaext-engine-layer.md`: any lightweight `DebugGizmos` replacement should build on
  `DebugDraw` and use generic geometry instead of restoring dependencies on the retired engine
  layer.
- glTF PBR inventory and retained-record reconciliation (2026-10-31): source inventories now
  distinguish HEADLESS's ownership-only validation from the six renderers that consume PBR maps,
  and track DirectX9's current rigid/colour stride gate. This closes the three source-contract
  failures exposed by the XNB-61b full content run without recasting that focused repair as a new
  full-suite checkpoint (`85b1d0c26`). The retained center-collapse verdict and API-change review
  now reflect the completed topology, rigid-animation, material-state, L6/L7, specular-map and
  sampler work; the reusable campaign retrospective remains unchanged (`b8e05abf8`).
- Cube/volume documentation reconciliation (2026-10-31): the feature matrix and EasyGL resource
  guide now describe the implemented mip, exact-format transfer and shader-sampling contracts
  instead of the completed Tasks 862/863 as future work. EasyGL Texture3D context-loss recovery
  remains the separate §5.1 defect (`07bf17725`).
- Compiled-effect documentation cleanup (2026-10-31): the obsolete pre-implementation FX bytecode
  plan was removed after its still-current architecture, format, dependency and renderer material
  was verified in `docs/shader-effect-vs-fx-bytecode.md` and `docs/fx-compiled-effects.md`
  (`7c2a0a317`).
- Graphics capability fail-closed hardening (2026-10-31): unknown legacy capability identities
  now return unsupported instead of inheriting permissive renderer defaults, and partially known
  format-usage masks cannot pass a support query. EasyGL, FNA3D, HEADLESS and SOFTWARE explicitly
  answer only their recognized legacy capabilities. Direct and cross-renderer regressions cover
  unknown identities, unknown format bits and stable answers across renderer reconstruction
  (`ab11ea1e4`).
- WebGPU binding-cost audit follow-up (2026-10-31): the fixture now unbinds its sampled texture
  before rewriting it, as XNA requires, without dropping the renderer-owned keep-alive for queued
  deferred draws. This closes the former `WebGPU_BindingCost` entry in §5.1; the renderer correctly
  refused the test's bound-resource `SetData` (`366819a1f`).
- `XNB-61b` compressed-manifest and container hardening (2026-10-31): typed runtime loads,
  type-erased external references, the canonical importer and the CNAEXT content manifest now use
  one bounded XNB container decoder for uncompressed, LZX and MonoGame LZ4 bodies. The manifest
  inventories and aggregates readers from compressed files, type-erased references load LZ4, and
  filesystem/custom-stream loads consistently enforce the 64 MiB file ceiling. Regressions cover
  compressed/uncompressed body equality, both manifest codecs and usage counts, malformed scans,
  LZ4 external-reference loading and pre-allocation refusal of oversized filesystem and virtual
  streams. Both affected test binaries build; the broad private-GPU XNB filter passes 492/501
  with nine renderer-capability skips and no failures. The full 2,099-test content target exposed
  only three unrelated PBR source-inventory failures; `85b1d0c26` subsequently closed those
  focused failures, but the complete content target was not re-run as part of that repair.
- Software renderer audit reconciliation (`SW-01`…`SW-07`, 2026-10-31): the July audit's clipping,
  viewport/scissor, depth/stencil, draw-range, instancing and public blending findings were already
  closed by the current renderer and its shared contracts. The remaining renderer-boundary
  hardening replaces overflow-prone `x + width` and signed byte-count arithmetic for uncompressed
  cube uploads/readbacks with subtraction bounds and checked `size_t` plans. A direct regression
  proves extreme coordinates, widths and short buffers are refused atomically. The complete
  `cmake-build-software` tree builds, and all 196 Software-labeled CTests complete with 175 PASS,
  21 declared capability/profile SKIP and 0 FAIL without a display server.
- SDLGPU audit reconciliation (`SGPU-06`/`SGPU-07`/`SGPU-08`, 2026-10-31): the lazy
  logical-backbuffer proxy already closes the old swapchain-readback gap, so SDLGPU now participates
  in the shared point-list, indexed/non-indexed range and ordinary binding-offset backbuffer
  matrices instead of retaining stale exclusions. The Uint32-above-65,535 regression now asserts
  its exact backbuffer pixel on SDLGPU, while renderer-specific render-target fixtures remain as an
  independent oracle. The renderer guide records the proxy cost model, the intentional CNJ/native
  CNB Effect contract boundary and the exact limits of Windows/Apple/mobile qualification. A Debug
  build of `CnaTests` and the affected standalone fixtures succeeded; the focused private-GPU run
  passed all 80 applicable tests (11 intentional Software-only or unmeasured-instancing skips), and
  the complete private-GPU `SdlGpu_*` registration set passed 392/392 with five declared contract
  skips and no graphics-profile deaths.
- Maintainer study workflow (2026-10-31): `docs/maintainer-study-guide.md` replaces two personal
  August study-plan drafts with a rolling thirteen-week method grounded in current sources of
  truth, `loc.sh`, generated-code boundaries, the current module/renderer set and the private GPU
  runner. It keeps a ten-hour core study track with an optional ten-hour production track, leaves
  personal notes outside the product repository, and deliberately introduces no new backlog,
  release calendar or AI line/file quota. `docs/README.md` now routes maintainers to it.
- Emscripten `Game` loop parity (2026-10-30): native `Tick()`, application-style Asyncify
  `Run()` and host-driven `RunFrameEXT()` now use one timing/update/draw state machine. Browser
  scheduling disables only native fixed-step waiting; it retains the high-resolution clock,
  `GameTime` ordering, the 500 ms cap, slow-frame history, reset, draw suppression, exit, disposal,
  renderer-context and exception behavior. The separate static `Game*`, copied `GameTime` and
  floating-point millisecond accumulator are gone. Eleven deterministic non-blocking tick
  regressions plus the existing lifecycle/clock/reset set pass 22/22; the clean WEBGL2 C ABI build
  and real Chrome probe pass 14/14, including a second sequential fixed-step game; and the WebGPU
  application demo completes 30 `Run()`/Asyncify frames with clean teardown. The full
  `CnaRuntimeTests` target passes 255/258 with the three environment-classified skips.
- Renderer identity normalization (2026-10-30): CNA's public renderer key is now consistently
  `SDLGPU`, including CMake selection, compile definitions, runtime names, presets, CI and the C
  ABI. The former `SDL_GPU` selector, feature options and C constant are deliberately refused
  rather than retained as aliases; the C ABI is therefore 0.51.0, while the renderer constant's
  numeric value remains 31. Actual SDL API identifiers such as `SDL_GPUDevice` and the
  `SDL_GPU_DRIVER` hint are unchanged. The migration guide and changelog record the source-level
  replacements. A follow-up preserves SDL's literal `SDL_GPU*` API prefix and Metal diagnostic in
  the two explanatory texts that the rename had overreached, and directly tests both refused old
  feature-option spellings. The full SDLGPU tree and the eight-renderer `CnaTests` target build;
  21 focused
  standalone identity/ABI/selector tests, 20 multi-renderer identity/registry tests and four
  SDLGPU smoke/parity tests on the private GPU display pass.
- `BENCH-001` (2026-10-27; CNA commit `421a38e4f`, consumer commit `ee012a8`): the external
  `cna-benchmark` now
  builds against the current CNA tree and runs all seven scenes on SDLGPU and OPENGLES3. Its
  streaming scene unbinds the sampled texture before the next upload. CNA reuses an unshared
  single-level texture renderer for full `SetData` while preserving copy isolation for shared
  renderers. The Texture2D slice passes 55/55, benchmark CTest 10/10, and both 300-frame GPU runs
  complete on the private display.
- `cna-rts` consumer intake (RTS-001/RTS-002, 2026-10-27; CNA commit `3b43bd868`, consumer commit
  `5b5dc24`): its September findings on
  render-target PNG/JPEG saves, EasyGL shader-dialect reporting and raw Color texture uploads are
  already answered by the current CNA code. The EasyGL render-target effect-source C1 fixture now
  exercises a real custom 3D shader, its `uRtFlipV` row correction, and ordinary-texture versus
  render-target pixels (4/4 focused checks; full registered fixture passed). The consumer's first
  100k draw exposed its default Reach profile, whose 65,535-instance limit matches Microsoft XNA
  4.0; `cna-rts` now selects HiDef and saves screenshots directly from its RenderTarget2D. Its
  private-GPU 10k/100k/1M sweep completes with one draw call per scale, and the 10k/100k/1M
  deterministic worker-count checks match.
- The final cna-demos handoff audit (2026-10-24) reconciled the repair matrix's third
  platform-pending row: ISSUE-152 now uses the same
  `RESOLVED_PENDING_PLATFORM_VALIDATION` status and structured `PENDING_NATIVE_WINDOWS`
  requirement as ISSUE-003 and ISSUE-101. No product source changed. A fresh cna-demos validation
  against this CNA tree passed all 732 enabled CTests (687 PASS, 45 declared SKIP; three manual
  tests disabled), and the complete 719-ID sweeps were SOFTWARE 671/0/45/3 and OPENGLES3
  709/0/7/3. The only remaining campaign actions are the three native-Windows checks (§8.1) and
  the consumer-owned museum reruns described under "Exact next task".
- Consumer intake, museum `docs/CNA_FINDINGS.md` Open section (2026-10-24): every entry was checked
  against HEAD; the museum reran them all on 2026-10-25 against `9d49828f4` and moved each one it
  verified to its "Fixed in CNA" section (see "Exact next task"). Fixed here: MouseState/GamePadState text as XNA's IL
  formats it (`0ef875619`), six Pipeline Workshop divergences 3/4/5/10/11/12 (`889a31be4`), TaskItem
  equality (`48a5c75eb`), the unsigned ENet fragment bitmaps (`03b92269c`), a readable refused
  vertex type (`a80534927`), the WAV/devices.h and content-task link documentation (`acc2ee55b`,
  `626de40e6`), and on the owner's decisions `RendererFeature::RenderTargetReadback` (36,
  `e357bc4c4`), PresentationParameters with XNA's defaults (0x0, full screen; CNAEXT
  `CreateWindowedEXT`, `e460e0ed6`) and XNA's TextureImporter/TextureProcessor answering an
  XNA-typed `BuildAndLoadAsset<TextureContent, TextureContent>` (`671d8478e`). Found on the way:
  `17cd5d3c3` (EasyGL drew a colourless PBR record black since `bb6a2f44e`) and `b747faca2` (the
  input gate's filter picked up a media test). Then the three items that needed design: XNA's
  `XmlSerializer` text for Color, Rectangle and Point (`bbed8c61e`, Sharp Runtime `f990d074` adds
  property members and document-order reads; oracle `../cna-demos/docs/repair-evidence/content/
  XmlColorRectangleOracle.cs`), and generated nested names chosen against the whole build:
  `98aa655f2` shares one asset's names with the contexts `Convert` creates, and `419723275`
  resolves two nodes that generated one name for different assets in node order,
  rebuilding the later node with the name reserved, whatever the worker count and under
  incremental builds. The pending-pan TSan race was already fixed by
  `594d7d328`. Checkpoint after all of it: full cmake-build-multi CTest 12,542 of 12,563 (the 21
  others are the multi-tree compiled-effect and audio-driver artefacts, which pass with their
  renderer, and the faults fixed above); demos tree rebuilt from CNA `17cd5d3c3` plus the texture
  route, 719-ID sweeps SOFTWARE 670/1/45/3 and OPENGLES3 708/1/7/3 with the one failure a demo check
  corrected in demos `8855ae3` and then passing, and serial CTest 687 PASS/45 SKIP/0 FAIL/3 disabled
  (838 s); reports `../cna-demos/build/linux-full/reports/repair-museum-closure-*`.
- handoff §5.1's one defect, `SpriteSortMode::Immediate` batched on `SDLGPU` and `WEBGPU`: an
  upload to a texture a queued draw samples is now ordered after that draw (SDLGPU submits the
  queued work first, WEBGPU writes Texture2D into a fresh texture), and
  `EasyGL_SpriteBatch_SortModeSemantics` leg B asserts XNA's red/blue on SDLGPU, WEBGPU,
  OPENGLES3, OPENGL33, SOFTWARE and FNA3D. WEBGPU's TextureCube and Texture3D, found by reading,
  rename the same way since the commit after it, and `*_Conformance_texture_setdata_after_draw`
  checks a Texture2D and a TextureCube changed between two draws on every pixel renderer.
- DC-R1 (`51fb39d5a`, owner: implement): SDLGPU binds a RenderTargetCube face in any slot of a
  target set. DC-R2 (`c7f6c68e5`, owner: all-or-none exact, refuse others): SDLGPU applies a
  MultiSampleMask keeping every sample or none and refuses any other, and
  `RendererFeature::MultiSampleMask` (35, C ABI 0.50.0) reports each renderer's mask support,
  checked against a 4x probe. `cube_mrt_binding` passes M1-M4 on SDLGPU. Serial cna-demos CTest
  after both (demos tree rebuilt from CNA `c7f6c68e5`): 732 enabled, 687 PASS/45 SKIP/0 FAIL/3
  disabled (870.97 s), the batch 9 outcome; reports
  `../cna-demos/build/linux-full/reports/repair-dcr-closure-ctest.*`.
- DC-R3 (`9ef292993`): `cna_shader_effect_create` copies a whole SPIR-V module byte for byte
  instead of refusing it as non-UTF-8 text, so a C caller can give SDLGPU (dialect SPIRV) a custom
  effect it runs. Found when `c_api.graphics.shader_effect_state`, which had used a GLSL
  placeholder there, was given a SPIR-V pair (demos `77444d7`); it passes 17/17 on SDLGPU.
- Batch 9 closed (`FIXED_VALIDATED`, 2026-10-24): 005 (a vertex holding CNA's 16-byte Color is a
  compile error in the raw user draws), 007 (SpriteBatch refuses a ColorMatrixEffect where the
  renderer does not apply it), 008 (no Prop.hpp macros from CNA's public headers), 067 (C colour
  space names), 066 (owner: SetBlendEnabled installs non-premultiplied blending on every
  renderer), 022 (owner: a glTF's extracted textures are always Texture2D children, CNB and
  XNB), 031 (STEP hold keys and CUBICSPLINE samples survive linear clip playback), 023 (glTF
  cameras in the Model CNJ and an optional MCAM CNB chunk) and 006 (RendererFeature
  SamplerLevelOfDetailBias, PbrEffectShading, SkinnedPbrEffectShading, each checked against
  every renderer's pixels; this fixed FNA3D's SpriteBatch dropping the batch's mip state and
  corrected headers that called SOFTWARE's PBR a fallback). `30afa280b` bumps the glTF Model
  pipeline component versions so existing content builds recompile. C ABI 0.50.0 (published 2026-10-24)
  carries the camera routes and feature constants; 3,212 exports.
- galaxy-eggbert UB-003 (`f8e2b8cc6`): the avatar converter writes XNA's clockwise front faces;
  galaxy-eggbert re-verified it.
- Batch 8 (081, 091-096, 098, 104, 114) and ISSUE-010/020/029/062 closed earlier on 2026-10-23/24.

The latest complete cross-subsystem checkpoint (2026-10-24, CNA `96577db26`): `cmake-build-multi`
fully rebuilt, full CTest 12,553 of 12,569 passed in 927 s; the 16 others are the 15 SDLGPU/FNA3D
compiled-effect entries and `CApi_AudioUnavailableSmoke`, and pass when rerun with
`CNA_GRAPHICS_RENDERER=SDLGPU` (62/62), `=FNA3D` (37/37) and without `SDL_AUDIO_DRIVER=dummy` (1/1).
Demos tree rebuilt from the same CNA: 719-ID sweeps SOFTWARE 671 PASS/0 FAIL/45 SKIP/3 NOTRUN and
OPENGLES3 709/0/7/3, against the consumer intake checkpoint only
`game.graphics_device_manager.preparing_device_settings` FAIL-to-PASS (the demos `8855ae3`
correction); serial CTest 732 enabled, 687 PASS/45 SKIP/**0 FAIL**/3 disabled (791.75 s). Reports:
`../cna-demos/build/linux-full/reports/repair-final-checkpoint-*` and
`cmake-build-multi/pp-validate/final-full-ctest.log`. Before it, the consumer intake checkpoint
under "Latest accepted work", and batch 9 (2026-10-24, demos
tree rebuilt from CNA `30afa280b`): full 719-ID SOFTWARE 669 PASS/2 FAIL/45 SKIP/3 NOTRUN, OPENGLES3
707 PASS/2 FAIL/7 SKIP/3 NOTRUN; the two failures on each were `content.model.pipeline_contract` and
`content.xnb.output_pipeline` pinning the old identities, corrected in demos `dcc3811` and passing
on both renderers. Against batch 8 only FAIL-to-PASS and SKIP-to-PASS rows changed otherwise. Serial
CTest: 735 registered/732 enabled, 687 PASS/45 SKIP/**0 FAIL**/3 disabled (954.26 s). Reports:
`../cna-demos/build/linux-full/reports/repair-batch9-closure-{full,ctest}*`.

Native suites must run through the private runner with `SDL_AUDIO_DRIVER=dummy` (and from the
source directory for Content/Media): without it SDL's ALSA device reports "Host is down" and the
SoundEffect/video-audio tests fail or abort for environmental reasons. In `cmake-build-multi`
the SDLGPU/FNA3D compiled-effect tests need `CNA_GRAPHICS_RENDERER=SDLGPU`/`FNA3D`. A change to
what a content importer, processor or writer produces must bump its identity version, or
incremental builds keep stale outputs.

### Exact next task

The museum Pipeline Workshop items are all answered: 1 (build-wide nested names, `98aa655f2` and
`419723275`), 9 (`bbed8c61e` on Sharp Runtime `f990d074`, measured against
genuine XNA, demos `9598b43`) and 16 (owner: the documentation half is enough; the task sources
stay in `modules/content-pipeline`). What remains needs a host this machine does not have: the
three Windows/XNA platform validations (003, 101, 152; §8.1). The museum's reruns of
2026-10-25 against `9d49828f4` are in `../museum-simulator/docs/CNA_FINDINGS.md`: every finding it
can run here is verified fixed or documented, the ThreadSanitizer races and ENet's UBSan shift
included (a `-fsanitize=thread,undefined` museum tree: no report), and so is the Android emulator's
Float16 readback (`53292f147`: the half-float target reads 2.0). None stays open there.
The museum repair log `CNA_MUSEUM_REPAIR_HANDOFF.md` was removed then; it is in Git history
(`git show 9d49828f4:CNA_MUSEUM_REPAIR_HANDOFF.md`).
Triaged, not a defect: in `cmake-build-multi`, `cna_c_api_effect_smoke` run with
`CNA_GRAPHICS_RENDERER=WEBGPU` fails its stage 3 because a WEBGPU effect on the WEBGPU-76 WGSL
contract has no cube sampler and refuses one with `NotSupportedException`
(`WebGPUDescriptorEffect.cpp` `BindTextureCube`), which the C route reports as
`CNA_RESULT_NOT_SUPPORTED`; the test, registered under the tree's HEADLESS default, assumes cube
binding. `effects.h` now documents that refusal on the three texture-binding routes. STUB fails
the same stage creating textures, an expected STUB artefact.

Consumer-only work still matters. cna-engine's `upstream-issues.md` reports every finding CNA
answered resolved at its pin `cb377fa04`: UP-001, UP-002, UP-006, UP-018, UP-019 and UP-020 (the
CNA-side intake log, `CNA_ENGINE_REPAIR_HANDOFF.md`, was deleted on 2026-10-29 and is in Git
history). The museum has rerun its findings (above); the
consumers also need to know that `new PresentationParameters()` is now XNA's 0x0 full-screen value,
so a device built from it directly must state a size. Do not claim UBSan/TSan/hardware/service
validation when its required tree or hardware is absent.

Build discipline remains: existing build directories only, shared
`CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv`, at most 8 jobs and preferably one compiler under
the established 4 GiB high / 6 GiB max / no-swap scope. All GPU/UI tests go through
`tools/platform/run_gpu_tests_private.sh`. Never weaken a demo/test oracle, and update the
permanent issue history and handoff after every accepted batch.

**§5 is the authoritative current defect inventory; the three fixes still awaiting a native
Windows run are §8.1.** The repair matrix that recorded every historical cna-demos finding is in
Git history (above). Capability documents describe supported behavior, tests are executable
evidence, and Git history preserves measurement history. Every implementation plan has been retired
and `plans/` removed (2026-10-29); `docs/task-id-index.md` says where each one's content went. Do
not let an issue disappear from §5 or §8.

Ready to pick up next, when the owner asks: the three native-Windows validations in §8.1.

Repository state last reconciled: **2026-10-31** (`next`, `b8e05abf8`), after the graphics
capability, PBR inventory, cube/volume documentation and retained glTF record reconciliations. The
preceding content checkpoint completed compressed manifest inventory, shared bounded container
decoding and type-erased LZ4 loading. The last plans were retired on 2026-10-29 (`225e88b09`). The
campaign accounting above dates from
2026-10-24 (`96577db26`), and the test runs in §0 and §2 carry their own dates. The last full test run is the
checkpoint identified above; do not upgrade later focused evidence into a full claim.

## 1. Project summary

CNA is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`) on a
CNA-owned platform abstraction (SDL3 by default) and a curated set of graphics renderers. FNA's
source is the day-to-day behavioural reference; where FNA and XNA disagree, XNA wins (`CLAUDE.md`).

The project is leaving its AI-driven feature-expansion phase for long-term maintenance by a human
C++ developer. The renderer set, the platform layer and the content formats are deliberately kept as
they are: no new renderers, asset formats, graphics engines or subsystems. Work from here is fixing
§5, keeping behaviour faithful to XNA, and keeping validation deterministic.

## 2. Current status

**Renderers.** 10 public identities over 8 implementation families — `OPENGLES3`, `OPENGL33`,
`WEBGL2`, `WEBGPU`, `HEADLESS`, `SOFTWARE`, `STUB`, `DIRECTX9`, `SDLGPU`, `FNA3D`; the three GL
identities share the internal EasyGL implementation. `SDLGPU` is CNA's modern native path: SDL
owns its Vulkan, Direct3D 12 and Metal backends. The canonical list is
`cmake/RendererIdentities.cmake` (held to the C++ enum and the C ABI by
`scripts/check_renderer_identities.py`); the table with gates and factories is
[`docs/renderer-registry.md`](docs/renderer-registry.md). 41 identities are retired — `VULKAN`,
`DIRECTX11`, `METAL` and `SDL_RENDERER` on 2026-10-13 — and refused by name at configure time and at
run time; their reserved ABI values are in [`docs/removed-renderers.md`](docs/removed-renderers.md).
Do not add or restore a renderer without an explicit owner decision.

**Graphics layer.** Every renderer implements one contract, `IGraphicsRenderer`, for XNA graphics
plus CNA's graphics extensions (SPIR-V/GLSL/WGSL `ShaderEffect` with vertex/fragment
`ShaderPackageEXT`, PBR and skinned PBR, image-based lighting, shadow reception, float render
targets, instancing, compiled effects, `Texture3D`/`TextureCube`). There is no
second, general-purpose GPU layer underneath: the compute-shader, storage-buffer/image,
texture-array, memory-barrier, indirect-draw and GPU-timer abstractions were retired end to end on
2026-10-13 (MRT; C ABI 0.47.0, `docs/c-api/ABI_VERSIONING.md`), including their common interfaces,
backend implementations, capability/limit identities, C API routes and tests.
`modules/graphics-ext/` remains (`docs/cnaext-engine-layer.md`).

**Platforms.** `CNA_PLATFORM` is `SDL3` (default; Windows, X11, Wayland, macOS, iOS, Android and the
browser are all reached through SDL3's drivers), `HEADLESS`, or POSIX-only `TERMINAL`. CNA's own
`WIN32`/`X11`/`WAYLAND` backends were retired on 2026-10-06 and are refused by name.
`CNA_AUDIO_PLATFORM` is `SDL3`, `NULL` or `ALSA`. Evidence differs per target:

| Target | What is actually verified |
|---|---|
| Linux x86_64 | Everything; this is where the suites run. |
| Windows | MinGW-w64 cross builds run under Wine: `DIRECTX9` through DXVK, `SDLGPU`'s Direct3D 12 backend through vkd3d-proton, `SOFTWARE`. `windows-msvc-ci.yml` now automatically builds and tests an SDL-free native-MSVC contract and an SDLGPU Direct3D 12 smoke set; it was added on 2026-10-30 and no successful run is recorded yet. The deeper `DIRECTX9` workflow remains manual. No real-Windows hardware validation is current (§5.2). |
| Web (Emscripten) | `WEBGL2` and `WEBGPU` builds run in Chrome (`docs/web-emscripten-graphics-limitations.md`). |
| Android | `OPENGLES3`, the default there, on an x86_64 emulator; `SDLGPU` on Android is not qualified (`docs/android-graphics-limitations.md`). |
| macOS / iOS | macOS CI builds CNA with `SOFTWARE` and runs the portable platform suites and a bundle smoke app, and (gating, since RRC-051, not run yet) builds `SDLGPU` and runs its smoke set on SDL's Metal backend; iOS is a `SOFTWARE` simulator smoke app, with non-gating `SDLGPU` legs. `SDLGPU` is the default on both but is not re-qualified there (§5.2, `docs/apple-platforms.md`). |

**XNA API surface.** Every public type and every documented member of the Microsoft XNA 4.0 runtime
reference is represented; the generated census is
[`docs/xna-4-runtime-member-coverage.md`](docs/xna-4-runtime-member-coverage.md)
(`python3 tools/audit_xna_runtime_surface.py --write-reports`). Representation is not behaviour:
behavioural fidelity is held by each module's tests.

**Bindings.** C and C# are the maintained bindings. CNA.NET (`libcna/cna-dotnet` `develop`) admits
exactly C ABI 0.47.0 and pins one CNA `next` commit (`CNA_UPSTREAM_COMMIT` in its
`.github/workflows/build.yml`); its CI builds and loads the native library from that commit. CNA
`next` is ABI 0.50.0: 0.48.0 (the 2026-10-13 renderer retirement) removed four renderer constants,
0.49.0 (2026-10-17) renamed the renderer classification typedefs, constants and eight routes
from `graphics_backend` to `graphics_renderer`, and 0.50.0 (2026-10-23, galaxy-eggbert UB-002)
added the two `cna_skinned_model_ext_{get,set}_part_diffuse_color_at` routes. CNA.NET names none
of them and must admit 0.50.0 before it moves its pin. CNA `main`/`develop` are the v0.1.0 release (ABI 0.46.0), which CNA.NET
refuses. The archived bindings in `cna-lab` stay pinned to older ABIs and are not maintained.

**Dependencies.** FNA3D and MojoShader are CNA's forks, `libcna/FNA3D` and `libcna/mojoshader`,
pinned at `10f650b9d0f6` / `03b686fe1a33` on their `cna` branches (`docs/fna3d-renderer.md`). Both
carry the fixes for the 2026-10-26 dependency audit (reports in
`/rv/tmp/cna-dep-migration-codex-audit-2026-10-26/reports/`) and for the independent review of those
fixes the same day (`/rv/tmp/cna-audit-remediation-codex-review-2026-10-26/raw/`, passes R1-R4,
R5-R8 and R9-R10 in three rounds;
each fix names its finding in a `CNA-Audit-Finding:` trailer, CNA's own in its commit subject) and
regression suites of their own:
`tests/cna` in each, built with `-DMOJOSHADER_CNA_TESTS=ON` / `-DFNA3D_CNA_TESTS=ON` (FNA3D's need a
display: run them through the private runner). SDL is `86296ac8` with five CNA patches
(`cmake/ThirdPartySDL.cmake`), ENet upstream master `5a9c537`.

**Content.** XNB readers (including LZX), CNA's `.cnb`/`.cnj`, direct glTF 2.0 loading, the
`cna-content` build tool and the XNA Content Pipeline façade: `docs/content-pipeline.md`,
`docs/xna-content-pipeline-parity-report.md`, `docs/gltf-limitations.md`. No further formats are
planned.

**Tests.** GoogleTest under CTest; `ctest -N` in a build tree gives its count. At the FNA3D pin
`10f650b9d0f6` (2026-10-26, end of the dependency-audit remediation and its review),
`cmake-build-multi`'s `CnaTests` with the audit's graphics filter plus `XnaMultisamplePixelCenter*`
and `GltfRendererPbrFallbackPolicy*` ran 1,683 tests with no failure on each of `OPENGLES3`,
`OPENGL33`, `WEBGPU`, `SOFTWARE`, `SDLGPU` (Vulkan) and `FNA3D` with its OpenGL and its SDLGPU
driver (the audit had measured 6 failures on `SDLGPU` and 15 to 25 on each of the others, all
fixed or test defects); its network
filter 574/574 and the platform suites 1,553 on X11 and 1,553 on Wayland, none failed. The family
example suites of the renderers B2-04 changed, each in its own default tree (Release; `FNA3D` at
`10f650b9d0f6`, the others at `aa3139087e69` with MojoShader `bbee0a5fab1f`): `FNA3D`
63/63; `SDLGPU` 65 selected (MSAA, render-target, cube, readback, effect and
`SdlGpu_TextureDefragment`), every pixel check passes, two debug-layer assertions need a Debug tree;
`WEBGPU` 41/41, `SOFTWARE` 34/34; `render_target_own_msaa` and `cube_mrt_binding` pass on all five
families. Fork
suites: MojoShader `tests/cna` 16/16 normal and under clang ASan+UBSan+LSan with no check excluded,
with its 14 SDL_GPU device cases on RADV; FNA3D `tests/cna` 21 (27 with `TRACING_SUPPORT`) under
ASan+UBSan on OpenGL, OpenGL ES 3 and SDLGPU, and its eight executables on D3D11 under Wine with
DXVK 3.1.1. Cross builds at `10f650b9d0f6`: `cmake-build-mingw` under Wine with DXVK 3.1.1
ran `DirectX9CompiledEffect*` 31/32 (one declared skip) and `XnaMultisamplePixelCenter*` /
`RenderTargetReadbackLayout*` 5/6 (one declared skip) on `DIRECTX9`; the Emscripten
`cna_webgpu_compiled_effect_page` passed 21/21 in headless Chrome; Android was not rebuilt at this
pin (at `aa3139087e69` Android x86_64, NDK 29, built SDL with its patches and `cna_platform`, not
run). On 2026-10-31 under Wine with DXVK 2.6.0, the two DirectX9 presentation-reset regressions
and the focused live-swap-chain VSync test passed through the private GPU runner; optional
`PresentInterval::Two` now falls back to one retrace for a windowed chain, and unavailable
exclusive fullscreen falls back to a windowed D3D9 chain without losing the XNA fullscreen
request. The focused `ShadowTargetOrientation/2` PBR pixel test also passed after rigid
`PbrEffect` began accepting the compatible 68-byte declaration with an unused skinning suffix.
The `D3D9_XNA_Diff` gate then passed 40/40 active scenes, including a new real-XNA-generated
render-target-sampled-as-texture reference; its separate manifest gate reports exact pairing for
40 active and 7 auxiliary scene/reference pairs.
Latest full Debug runs, 2026-10-17 at the end of the handoff 5.1 stabilization pass (branch `stabilization`, worktree
`cna-stab`), through the private runner on RADV: `cmake-build-multi` (`OPENGLES3` default with
`OPENGL33`, `WEBGPU`, `SDLGPU`, `FNA3D`, `SOFTWARE`, `HEADLESS`, `STUB`; C API) 11,433 tests, 1
failed (`EasyGL_Conformance_render_target_own_msaa`, §5.3), 195 skipped;
`cmake-build-sdlgpu` 11,284, none failed, 256 skipped; `cmake-build-webgpu` 10,964, none failed, 256
skipped; `cmake-build-software` 10,982, none failed, 101 skipped; `cmake-build-fna3d` (its OpenGL
driver) 10,806, 2 failed, 400 skipped -- `ContentPipelineCliTest.ContentBuildGraphReportsALongCycleDeterministically`
and `Sdl3InputTest.MouseReadsThePointerBesideTheWindowAsXnaDoes`, which pass alone (three of three);
the second reads the pointer of the private display that parallel tests also move, and failed the
same way once in an earlier SOFTWARE run. Under Wine, `cmake-build-mingw` (Release, `DIRECTX9` with
DXVK 2.6.0): `CnaGraphicsTests.exe` in eight shards 2,688/2,688, and the `DIRECTX9` ctest suite
59/59 without `DirectX9_CompiledEffects` (§5.3); with DXVK 3.1.1 the whole suite 60/60, twice.
`cmake-build-headless` was not re-run. Every run excluded `XnaPipelineGenuineRuntime` and
`XnaDifferentialBuildTest` (Wine-hosted XNA builds), which §7 says to run outside the private
runtime. CI: `general-tests-ci.yml` runs the unfiltered suite for
`OPENGLES3` and fails on a dirty checkout; the other workflows are subsystem- or platform-specific.

## 3. Where current information lives

| Question | Source of truth |
|---|---|
| Is X broken right now? | §5 of this file |
| Which renderers exist, how are they selected? | `docs/renderer-registry.md`, `docs/runtime-renderer-selection.md` |
| Does renderer R support feature F? | `docs/graphics-renderer-feature-matrix.md`, `docs/renderer-capability-profiles.md`, each renderer's own doc |
| Compiled XNA effects per renderer | `docs/fx-compiled-effects.md` §10 |
| Platform contract | `docs/platform-abstraction.md` |
| API-surface coverage | `docs/xna-4-runtime-member-coverage.md` (generated) |
| C API generation maintenance model | `docs/c-api/GENERATED_ROUTES.md` |
| Deliberate deviations from FNA/XNA | `CHECKLIST.md`, `docs/xna-fna-behavior-decisions.md` |
| GamerServices / Net / Avatar boundary | `docs/gamer-services-known-limitations.md` |
| Why is a subsystem in (or out of) an executable? Link boundaries, size options, footprint gates | `docs/link-size-architecture.md` |
| Build rules (ccache, build directories, display isolation) | `CLAUDE.md` / `AGENTS.md` |
| Index of the other documents | `docs/README.md`; what a task ID belonged to: `docs/task-id-index.md` |

## 4. Working rules for this phase

- Fix defects, do not add surface. A fix comes with a focused regression test.
- Never run tests on the live desktop: use `tools/platform/run_gpu_tests_private.sh`.
- A test may write only to its build tree or a temporary directory. `scripts/check_clean_checkout.sh`
  enforces that a validation run leaves tracked files unchanged.
- Build into the existing `cmake-build-<variant>/` trees with ccache; do not create one-off build
  directories (`CLAUDE.md`, "Build locations & caching").

## 5. Known bugs and limitations

The single authoritative list. The stabilization pass of 2026-10-16/17 fixed or removed every
reproduced CNA defect that was on it; an entry says when it was last measured.

### 5.1 Defects

- `GraphicsDeviceManager::setPreferredBackBufferWidthProperty` and
  `setPreferredBackBufferHeightProperty` accept zero and negative values. FNA rejects `value <= 0`
  with `ArgumentOutOfRangeException("value", "BackBufferWidth and BackBufferHeight must be greater
  than zero.")`, and no measured XNA disagreement exists. Found by the 2026-10-31
  `docs/gdm-coverage.md` reconciliation; positive-value round trips are tested, invalid values are
  not.
- A direct `Dispose()` of the `GraphicsDevice` managed by `GraphicsDeviceManager` does not forward
  `GraphicsDevice::Disposing` to `GraphicsDeviceManager::DeviceDisposing`. `CreateDevice()` installs
  a device `Disposing` subscription only to release the renderer-context lease, whereas FNA wires
  the same device event to `OnDeviceDisposing`; manager-initiated disposal does raise the manager
  event once. Found by the 2026-10-31 `docs/gdm-coverage.md` reconciliation; the direct-device path
  has no regression test.
- `Microphone::Start` reports `MicrophoneState::Started` even when the capture device failed to
  open: `modules/audio/src/Xna/Microphone.cpp` swallows the failure (`catch (...)`, following FNA)
  and sets the state unconditionally. Microsoft XNA's `Microphone.Start` hands the native start's
  error code to `Helpers.ThrowExceptionFromErrorCode`, so a failed start throws there, and XNA
  wins. Found 2026-10-28 by reading XNA's decompiled source (former AUD-13-001); not yet pinned by
  a test.
- `WEBGPU` fails two shared presentation programs it is not registered for, which EasyGL and
  `SDLGPU` pass (`cmake-build-multi` run with `CNA_GRAPHICS_RENDERER=WEBGPU`, 2026-10-28):
  `presentation_mode_contract_test.cpp` under Letterbox and Overscan, where a sprite covering the
  40x40 logical canvas after a mid-frame `SetPresentationMode` lands unscaled (40x40 at the
  letterbox origin instead of filling 96x96), and `spritebatch_presentation_test.cpp`'s two
  Deferred batches under different custom viewports, which both read black. Not investigated.
- On Android, the landscape remap `Accelerometer`, `Gyroscope` and `Motion` apply by default
  (`Detail::ConvertAndroidPortraitToXnaLandscape`, a CNAEXT convenience that
  `SetAndroidLandscapeRemapEnabled(false)` turns off) is, for both landscape rotations, a
  reflection (`diag(1,-1,1)` / `diag(-1,1,1)`, determinant -1) rather than a rotation: a real 90°
  turn of the device exchanges X and Y. Windows Phone itself never remaps; it reports the
  device-fixed frame. Found by derivation on 2026-07-17 (devices plan `MOT2-001`, recorded in
  `AndroidMotionMath.hpp`) after `ACCEL-008` had kept the remap on 2026-07-07; never checked on a
  phone.
- Three `SpriteBatch::Begin` overloads take `BlendState` by value (`SpriteBatch.hpp`: the
  two-argument one, one of the two five-argument ones and the six-argument one; the other
  five-argument overload takes `const BlendState*`), where XNA's `BlendState` is a class passed
  by reference.
  Every such call copies the state twice, each copy allocating its own state on the heap. Found
  by the Inspector's first end-to-end run (`AUD-DIAG-INSP-0004`, 2026-09-18) and left open
  because the fix changes a public signature.
- EasyGL (`OPENGLES3`, `OPENGL33`, `WEBGL2`) disables blending whenever colour and alpha both use
  `Blend::One` / `Blend::Zero`, without looking at the blend functions
  (`EasyGLRenderer::ApplyBlendState`). With `BlendFunction::ReverseSubtract` XNA evaluates
  `0 - source`, but EasyGL copies the source; the same shortcut also skips `Max` and `Min`, which
  ignore the factors (not measured). Found by SDLGPU's blend-equation oracle (`SDLGPU-58`,
  formerly `EASYGL-PARITY-1`, 2026-09-10); SDLGPU evaluates the equation correctly, and EasyGL
  has no regression leg for it yet.
- EasyGL's `ApplyRasterizerMultiSampleState` is still selected by the build default's
  `CNA_GL_PROFILE_OPENGL33` definition instead of the active runtime `GlProfile`. A normal
  single-renderer `OPENGL33` build toggles `GL_MULTISAMPLE` correctly, but the current
  `OPENGLES3`-default `cmake-build-multi` silently leaves it unchanged when `OPENGL33` is selected;
  a default-`OPENGL33` native multi-profile build would issue that desktop-only state change for a
  selected `OPENGLES3` device. Found by the 2026-10-31 `docs/opengl33-renderer.md` reconciliation;
  no runtime-profile regression covers it yet.
- In browser fullscreen the drawable a GL renderer letterboxes into is not the rectangle the browser
  presents. Measured 2026-09-08 with SAMPLE-071 (Yacht, 480x800 back buffer) on a headless Chrome
  with an 800x600 screen: the canvas reported 800x600 while its CSS box was 800x544, and the game
  covered about 71 % of its logical area in both axes, uniformly scaled, its bottom and right
  outside the visible frame. `GlPresentationSurfaceState` computes the letterbox correctly from what
  it is given (`EasyGLSurfaceState.LetterboxKeepsTheLogicalSizeAtTheBackbufferAndCentresTheRect`); the
  wrong input is the drawable from `Sdl3Window::GetPixelSize()`'s web path
  (`webDrawableOverride_`, which already special-cases leaving fullscreen). Not re-measured since;
  not reproducible natively here, where X11 fullscreen has no window manager to resize the window.
- An EasyGL `Texture3D` does not survive a GL context loss (WebGL 2, Android): unlike every other
  EasyGL texture, buffer, render target and query, `EasyGLTexture3DRenderer` is not an
  `easygl::RecoverableResource`, so it is never registered for restoration and keeps a dead handle.
  `TextureCube` gained recovery on 2026-09-04. Found by reading; no context-loss test covers a
  volume texture.

### 5.2 Limitations

- `RasterizerState.MultiSampleAntiAlias` is honored by `SOFTWARE` and by a single/default-profile
  desktop `OPENGL33` build only; the multi-profile selection defect is in §5.1.
  `OPENGLES3`/`WEBGL2` have no equivalent native switch, while `WEBGPU`, `SDLGPU`, `DIRECTX9` and
  `FNA3D` currently inherit the renderer seam's no-op. This is separate from multisampled target
  storage: those families may still allocate and resolve MSAA targets. Microsoft XNA demonstrably
  writes D3D9 render state 161 for `false`; its native-D3D9 pixel effect has not been measured here
  because DXVK reports the state unhandled (`docs/rasterizerstate-support.md`).
- `SDLGPU` applies `BlendState.MultiSampleMask` on a multisampled target only when it keeps every
  sample or none: the pinned SDL reserves `SDL_GPUMultisampleState::sample_mask`/`enable_mask`, so
  a mask of none drops the draw and any partial mask throws `System::NotSupportedException`
  (DC-R2, owner decision, `c7f6c68e5`). `RendererFeature::MultiSampleMask` reports each renderer's
  answer; `ColorWriteChannels` is honoured.

- Native `SurfaceFormat::Alpha8` render-target storage is unavailable on every renderer:
  `GraphicsDevice::SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Alpha8)` is false, and a
  render target preferring Alpha8 falls back to `Color` as XNA's preferred-format rule allows
  (`GraphicsCapabilityFloatRenderTargetTests.cpp`). Alpha is kept; the one-byte-per-pixel storage
  is not (`Color` is four). How the Xbox 360 allocated an Alpha8 target was never captured.
- `FNA3D` refuses a `PbrEffect` or `SkinnedPbrEffect` draw whose vertex declaration has no
  `TANGENT0` with `System::NotSupportedException` (`FNA3D-MISSING-STOCK-INPUT`,
  `modules/renderers/fna3d/src/Fna3dDraw.cpp`) wherever FNA3D picks its SDLGPU or Direct3D 11
  driver, which cannot build a pipeline with a missing input; its OpenGL driver and every other
  renderer supply the GL neutral tangent (`91612a0a3`). Measured by the museum's `rv-shading` on
  RADV, 2026-10-25.
- Online sessions and invitations are native-only; a browser build refuses them explicitly
  (`docs/browser-network-readiness.md`, `docs/gamer-services-known-limitations.md` §26).
- `DIRECTX9` is validated under Wine (+DXVK) only. `SDLGPU`'s Direct3D 12 backend is validated
  under Wine (+vkd3d-proton); an automatic hosted native-MSVC smoke workflow was added on
  2026-10-30, but no successful run is recorded yet and hosted smoke is not real-hardware
  qualification. The C API's pure-C suites
  run under Wine from the MinGW-w64 build; `MediaLibrarySmoke` reports a skip on Windows, whose
  known folders cannot be pointed at its fixture. The MinGW-w64 test binaries also skip on
  Windows what it cannot provide: a Python interpreter where none can be started, `/dev/full`,
  aging a directory through `utime`, symbolic links, a renderer without 3D, and the ordering guard
  for tests that need an uninitialized `GamerServicesDispatcher`.
- The installed C++ SDK (`CNA_INSTALL_CXX`, `docs/installed-cpp-sdk.md`) is validated on native
  Linux only, shared and static; the static SDK was validated with the C API off, so a mixed static
  C/C++ adapter is unproven. The MinGW-w64 and WebAssembly/WebGPU trees only configure and export
  its package: no relocated Windows SDK or installed WebAssembly SDK has been run.
- The content and content-pipeline *tests* are not Unicode-clean: about 1,150 `.string()` calls,
  most narrowing a fixture path before handing it to CNA, so under a non-ASCII `TEMP` on Windows
  they throw in the test body (Windows portability WINPORT-F5, measured on native Windows
  2026-09-16). The library is not implicated; its path contract is
  `docs/filesystem-path-model.md`.
- **`SDLGPU` is the default renderer on Windows, macOS and iOS but is not qualified there** since
  the 2026-10-13 retirement of `DIRECTX11` and `METAL` (`docs/removed-renderers.md`). Evidence that
  exists: the full tree passed with `SDLGPU` (Metal backend) on a Mac mini M4 in the AM4 campaign,
  before the retirement; Direct3D 12 runs under Wine. The automatic hosted native-MSVC Direct3D 12
  smoke gate was added on 2026-10-30, but its first successful run is still pending. Not done:
  native Windows hardware qualification, a re-run on Apple hardware, and any iOS build (`SDLGPU`
  has never been built for iOS; CI tries it
  as a non-gating leg). Known gaps against the retired native backends, each to be implemented or
  capability-gated in that campaign: the legacy `ShaderEffect` GLSL-text route (`CustomEffects`)
  is compiled out on Windows, Apple and the browser because no runtime GLSL compiler is present;
  with `CNA_CNAEXT=ON`, explicitly typed SPIR-V remains available on a live SDLGPU device and uses
  ShaderCross for D3D12/Metal. Occlusion queries are unavailable (SDL_gpu 3.5 has none).
  `RendererFeature::BaseInstanceDrawing` is implemented by
  `SDLGPU` since RRC-043 (its stock, instanced-matrix and compiled-effect routes expand per-instance
  streams on the host from the first instance); no other survivor claims it.
- iOS has simulator smoke evidence only, with `SOFTWARE`.
- `Microsoft::Devices` sensors and vibration have never run on a phone: the Android axis, compass
  and attitude mappings are derived and host-tested only. The device checks still open are listed
  at the top of `docs/devices-hardware-checklist.md`.
- `WEBGPU` is supported (promoted from experimental on 2026-10-17) and verified on Linux x86_64
  and in headless Chrome only; its Windows, macOS and linux-aarch64 wgpu-native packages are
  unverified (`WEBGPU-1`, `docs/webgpu-renderer.md`).
- `SOFTWARE` is a bounded CPU renderer, and `HEADLESS` and `STUB` make no pixel claim
  (`docs/renderer-registry.md`, "Categories and maturity").
- Per-subsystem capability boundaries, by design rather than defect, are in the documents listed
  in §3.
- In a `CNA_SHARED_LIBRARY=ON` tree, `CMAKE_LINK_DEPENDS_NO_SHARED` (`CMakeLists.txt`) keeps the
  executables from relinking when `libcna.so` changes. After a commit that changes a class layout
  or a vtable, the executables linked before it run against a library they were not linked for
  (the loader warns `Symbol ... has different size in shared object`) and fail for no reason of
  their own; delete and relink them before reading the suite. Seen on 2026-10-13 in
  `cmake-build-multi` after the link-boundary work's virtual-function additions: 237 bogus
  failures from 1,245 stale executables.
- **Pixel-centre bias.** Every renderer moves 3D triangles 63/128 of a pixel, not 1/2, to emulate
  Direct3D 9's integer pixel centres (2026-10-26 audit B2-07). The 1/128 keeps a fill edge that
  falls exactly on a host sample on XNA's side; it is the value DXVK itself applies, and the XNA 4.0
  runs that settled pixel centres on this machine go through DXVK. Interpolated attributes are
  therefore evaluated 1/128 pixel off D3D9's centre. Native Direct3D 9 was not available to
  measure whether it differs.
- **Pixel centres of lines and points.** The XNA correction above moves triangles only, in every
  FNA3D driver; lines and points are drawn at the host's half-integer centres. The MSAA measurement
  that extended the correction to multisampled targets (B2-04) drew triangles, so nothing measured
  says what XNA does for lines and points there.
- **MojoShader SPIR-V gaps** (FNA3D's SDLGPU driver, CNA's SDLGPU and WEBGPU compiled effects):
  ps_1_4 `BEM` and `TEXDEPTH` refuse by name ("unimplemented in spirv profile"); GLSL implements
  both.
- **MojoShader FOG and other vertex semantics** (after the review's fixes): FOG in any lane, alone
  or packed, translates in every profile, and SPIR-V's vertex and pixel FOG interfaces match in
  type; GLSL ES and Metal place FOG lanes positionally, so a vertex/pixel pair declaring FOG in
  different lanes behaves differently there than in the other profiles (not measured on Direct3D 9).
  A vs_3_0 PSIZE alone in a lane other than x is still narrowed to x. HLSL refuses FOG, SAMPLE and
  TESSFACTOR vertex inputs (FNA3D's D3D11 table also names Sample `SV_SampleIndex`, invalid as a
  vertex input), and the D3D11 glue's integer-format rewrite looks for `PSIZE0` while HLSL emits
  `PSIZE`. The ARB1 profile still names oFog/oPts FOG1/PSIZE2 and binds no pixel FOG input (no CNA
  path uses ARB1). Vulkan pipeline creation and GL program linking of FOG pairs were not run;
  spirv-val and interface-type comparison were.
- **FNA3D OpenGL ES 3 readback** reads RGBA8, `ColorSrgbEXT` (stored as `GL_SRGB8_ALPHA8` since the
  review; the unsized format FNA3D used before was refused by ES 3 at upload, so such a texture
  never had storage), the float formats, and any other format only where the implementation's own
  read layout is the format's; it refuses the rest by name. On Mesa 25.0.7 (radeonsi) Rgba1010102,
  Rgba64, ByteEXT and UShortEXT read, Rg32 and `ColorBgraEXT` are refused for their layout.
  Desktop GL reads every uncompressed format.
- **FNA3D pixel-store state**: readbacks and uploads save and restore every pack/unpack value they
  set, so another user of the context keeps its own; an upload therefore makes six
  `glGetIntegerv` calls, each a synchronization point under Mesa's glthread.
- **FNA3D tracing with several devices** shares one trace session among overlapping devices
  without telling them apart: the second device's creation is not recorded and its calls replay on
  the first device's state (2026-10-26 review R9-02), and its SwapBuffers with its own window
  override still trips the tracer's assertion. Trace one device at a time.
- **MojoShader's D3D11 glue** (FNA3D's D3D11 driver; found in the review's third round, not
  fixed): a ps_3_0 register packing semantics that the vertex shader writes to separate registers
  (FOG in `o1.x`, TEXCOORD0 in `o2.yzw`, read together from `v0`) produces HLSL naming a member
  that does not exist and fails to compile -- also before the review (R9-01); and the integer
  input-layout rewrite finds a semantic by substring, so `DEPTH1` can match an earlier `DEPTH10`
  and swap their integer/float declarations (R9-03). CNA's committed effects contain neither.
- **SDL_shadercross initialization is the application's** (MojoShader's SDL_GPU glue): the glue
  calls neither `SDL_ShaderCross_Init()` nor `_Quit()`, which act on the whole process without
  reference counting, so DXBC is offered only after the application initialized ShaderCross
  (`mojoshader.h`); FNA3D's SDLGPU driver forwards DXBC to SDL when it is offered. CNA's SDLGPU
  renderer initializes ShaderCross; a standalone FNA3D build with `MOJOSHADER_STATIC_SPIRVCROSS`
  must. FNA3D's test of that selector links a stand-in static package (archive shape, C++ runtime),
  not the real one: the only static SDL_shadercross package on this machine was built without DXC
  and its package config requires DirectXShaderCompiler anyway.
- **A shared SDL prebuilt can change under a running build** when a tree with another manifest
  rebuilds it (2026-10-26 review R3-01/R4-01; the watched stamp protects only the next build). Give
  such a tree its own `CNA_SDL_PREBUILT_ROOT` (`docs/development-environment.md`).
- **Audio output has no device-change or device-loss handling of CNA's own.** The SDL3 backend
  opens `SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK`, so a change of the system default is whatever SDL3
  does with a default-device stream; the ALSA backend does not reopen a PCM that disappears.
  Neither case has been measured (former AUD-04-010/011; capture devices do report a lost device).
- **EasyGL re-sends per-draw state the previous draw already set** (performance only; no picture
  is wrong). GLB-41 removed the sampler, viewport and polygon-mode traffic; two costs remain.
  GLB-42: the stock-program semantic remap re-specifies a vertex buffer's VAO before each draw and
  restores the buffer's own layout after it (`ConfigureDeclarationForStockProgramEXT` /
  `RestoreDeclarationLayoutEXT`) -- in cna-car-simulator's `WEBGL2` scene (1,156 draws, 2026-09-19)
  about 23,000 `disableVertexAttribArray` and 23,000 `vertexAttribDivisor` calls a frame plus the
  matching enable/pointer/VAO binds; caching the remapped layout needs every path that relies on the
  restored one (custom and compiled effects) audited first. GLB-43 (cna-street CNA-F19,
  2026-09-30): `BindDrawParams` uploads every uniform the stock program declares and rebinds every
  texture unit on every draw, about 9 µs a draw on either GL renderer, because easy-gl's
  `Program::set_uniform` and `Texture::active_bind` keep no record. Proposed: a per-program record
  of the last value at each uniform location (sampler-unit uniforms set once at link) and a per-unit
  bound-texture record, dropped on context loss, texture deletion and relink as GLB-41's records
  are, with real-context tests like `EasyGLRedundantStateTests.cpp`.
- **Custom-effect lookup and recovery limits** (`IEFFECT-AUDIT-001`, 2026-11-01): EasyGL's
  name setters still query GL on every call; mutable `GetProgram()` permits relinking without a
  detectable executable revision, so a handle/context-keyed cache would be unsafe. Custom effects
  are not registered for context recovery, and direct recompilation does not reset their
  texture-flag upload state. Stock SpriteBatch also queries projection/channel names per flush.
  The measured costs, call sites, WebGPU/D3D9 analysis and deliberately deferred candidates are
  in [`docs/ieffect-renderer-audit.md`](docs/ieffect-renderer-audit.md). These lifecycle limits
  remain open; the separate pre-Apply texture-flag write to another program is regression-fixed.
- **Three audio reports from the 2026-07-17 audit stay unconfirmed, not open:** high-pitched sound
  effects, missing audio files, and distorted or sped-up gameplay audio. They were never
  reproduced against the game they came from, which is not available. The audit narrowed the
  pitch report to a wrong sample rate reaching CNA before the mixer -- the class CBIND-146 fixed
  (`51d9c84cc`, 2026-10-02, an XNA-built sound's content sample rate).
- Three content-pipeline residues against genuine XNA output, none visible in the 7,726-reference
  sample sweep (`tools/xna-sample-sweep/README.md`, *What is left*): a generated `.x` normal can
  carry the opposite sign of a zero (13 of 1,291 probe normals, `XNASWEEP-231`); FBX bone
  transforms keep an ulp-level residue that grows with depth, inside the float tolerance
  (`XNASWEEP-228`..`232`); and `MeshHelper.OptimizeForCache`'s strip termination is empirical
  (9 of 20 probes exact, `XNASWEEP-149`).

### 5.3 External and driver limitations

Behaviour CNA does not own, measured and reported rather than worked around.

- **Conformance programs on drivers and capability boundaries** (`*_Conformance_*`,
  `modules/graphics/examples/conformance/`, measured 2026-10-15/17):
  - `render_target_own_msaa` D (2x) failed on EasyGL in 2026-10-17's run and on SDLGPU and FNA3D
    once B2-04 gave multisampled targets XNA's integer pixel centres: its edge sat at the
    half-integer centre, beyond both 2x samples. The edge now runs through the integer centre and D
    passes on EasyGL, SDLGPU, WEBGPU, SOFTWARE and FNA3D (2026-10-26).
  - FNA3D: `SkinnedEffect.VertexColorEnabled` is ignored outright (B equals A) and saturates before
    the fold; its back buffer keeps a 3, 5 or 7 sample request unrounded while its render targets
    round it down; `GraphicsDeviceManager`'s default `Depth24` request is not applied in
    `graphics_adapter_query`. A `TextureCube` changed between two draws of one frame shows its new
    contents in the earlier draw too (`texture_setdata_after_draw` B, 2026-10-24): FNA3D's SDLGPU
    driver uploads on a command buffer it submits ahead of the frame's draws and cycles only a whole
    single-level 2D texture, so by its source a `Texture3D` or a mipmapped `Texture2D` does the
    same. FNA3D's driver owns those answers.
  - WEBGPU and DIRECTX9: cube faces in a multi-render-target set are refused with
    `System::NotSupportedException` while `MultipleRenderTargets` is true (`cube_mrt_binding` M1-M4
    and the plural cube-face `RenderTargetSemantics` cases skip on that refusal); HEADLESS accepts
    them. A capability boundary. SDLGPU binds them since `51fb39d5a` (DC-R1).
- **LeakSanitizer reports leaks in the pinned SDL** (measured 2026-10-22/23 during the museum
  repairs): about 50 KB in about 910 allocations from initialization to destruction with no CNA code
  linked (lavapipe and RADV), so an SDLGPU sanitizer run does not end at exit 0 under
  LeakSanitizer; CNA suppresses none of it. The MojoShader SPIR-V attribute-fixup leak measured then
  (8,792 bytes in 513 allocations per device), FNA3D's SDLGPU device-array and vertex-binding
  cache leaks, and the dumpspirv and trace-lock leaks are fixed on the forks (2026-10-26; the owner
  authorized fork changes that day). MojoShader's `tests/cna` runs leak-free under
  AddressSanitizer; FNA3D's does on OpenGL and OpenGL ES 3, and on SDLGPU every remaining report
  is SDL's own (X11/xkb and Vulkan ICD initialization reached through device creation).
- **`DIRECTX9` under Wine with DXVK 2.6.0: `DirectX9_CompiledEffects` hangs or aborts in DXVK's
  device teardown; DXVK 3.1.1 does not** (measured 2026-10-17, `cmake-build-mingw`, Release,
  `CNA_DIRECTX9_COMPILED_EFFECTS=ON`). Its 31 gtests were run 30 times on each version in one Wine
  prefix that differed only in the DXVK DLLs: 2.6.0 (Debian's package, `/usr/lib/dxvk`) failed 9 of
  30 -- hangs inside the first test, `CapabilityAndPublicRuntimeContract`, and `std::system_error`
  "Thread not joinable", a string of DXVK's own thread wrapper -- while 3.1.1 passed 30 of 30, and
  with it the whole `DIRECTX9` ctest suite passes 60/60 twice. On 2026-10-15 teardown markers had
  shown every renderer object released and the final `IDirect3DDevice9::Release` never returning.
  CNA was not changed for it. `scripts/run-wine-dxvk9.sh` still defaults to `~/.wine-cna-d3d11`,
  which links the system 2.6.0; `CNA_D3D9_WINEPREFIX=~/deps/wineprefix-cna-dxvk-3.1.1` selects a
  copy with 3.1.1 (machine-local, outside Git); the script's DXVK gate accepts the `v` official
  builds print before the version. `CnaGraphicsTests.exe` is run as eight
  `GTEST_TOTAL_SHARDS` shards because one 2,956-test process under 2.6.0 dies or hangs at a varying
  point.

## 6. Reserved for the human author

- **Licence-header convention.** The tree carries 30 MIT and ~3,150 MS-PL SPDX headers, two
  `storage` source/header pairs with mismatched headers, and ~170 header-less `.hpp`/`.cpp` files
  (mostly examples). Which header CNA-original files carry, and whether `CNA::Internal::Net` stays
  MIT on purpose, is the owner's policy call; a one-line rule next to `CHECKLIST.md`'s header
  section and a lint would follow from it.

## 7. Build, test and environment

```bash
# Configure once per variant (reuse the tree afterwards; see CLAUDE.md for the directory rules).
cmake -S . -B cmake-build-headless -G Ninja -DCMAKE_BUILD_TYPE=Debug \
      -DCNA_PLATFORM=HEADLESS -DCNA_AUDIO_PLATFORM=NULL -DCNA_GRAPHICS_RENDERER=HEADLESS \
      -DCNA_BUILD_TESTS=ON \
      -DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache
cmake --build cmake-build-headless -j8

# Run the suite on a private display and prove the run left the checkout clean.
scripts/check_clean_checkout.sh -- tools/platform/run_gpu_tests_private.sh cmake-build-headless -j8 \
      -E 'XnaPipelineGenuineRuntime|XnaDifferentialBuildTest'

# The six Wine-based XNA interop tests hang inside the private runtime; run them on their own
# (their harness starts its own Xvfb :99).

# Renderer and platform boundary gates.
python3 scripts/check_renderer_identities.py
python3 tools/platform/sdl_inventory.py --check && python3 tools/platform/sdl_classify.py --check
```

- GPU renderers are tested in their own trees (`cmake-build-sdlgpu`, `cmake-build-webgpu`,
  `cmake-build-multi`, …) through the same private runner. Never run several renderers' full suites at the same time: GPU and driver
  contention produces false failures.
- `CMakePresets.json` has the focused unit, sanitizer, web, Apple and multi-renderer presets.
- System packages for a fresh Debian machine: [`docs/development-environment.md`](docs/development-environment.md).

## 8. Near-future tasks

Small, fully specified pieces of work that are ready to pick up. Each one names its evidence, its
plan and the point at which it is done; take one only when the owner asks for it or the current
campaign leaves room.

### 8.1 Three cna-demos fixes awaiting a native Windows run

The CNA/cna-demos repair campaign closed every finding except these three, which are fixed and
validated on Linux but still owe a run on native Windows -- a host this machine does not have.
None of them is a known defect: each is a fix whose Windows execution is unproven. Wine does not
count for any of them. Each one is done when its run passes on native Windows; then delete its
paragraph, and this section with the last one. The evidence files are in `../cna-demos`.

**`CNA-DEMOS-ISSUE-003` -- `SpriteBatch` leaves its texture in `GraphicsDevice::Textures[0]`.**
SpriteBatch kept renderer handles but never published the public texture wrapper. `3b6a2eaf3`
(2026-10-22) publishes `Textures[0]` after each custom pass through lifetime-safe movable
identities, tells a live disposed wrapper from a destroyed C++ one, discards renderer staging of a
failed submission without replaying it, and makes WEBGPU and DIRECTX9 honour `Immediate`. Its 22
regressions pass on SOFTWARE, OPENGLES3 and OPENGL33, with compiled-effect coverage on SDLGPU,
FNA3D and WEBGPU; for DIRECTX9 the five affected translation units only compile in the MinGW
configuration. *To run on native Windows with `DIRECTX9` and
`-DCNA_DIRECTX9_COMPILED_EFFECTS=ON`:* the 22 tests in
`modules/graphics/tests/Microsoft/Xna/Framework/Graphics/` -- `SpriteTextureBindingTests.cpp`
(6), `SpriteTextureBindingLifetimeTests.cpp` (7), `SpriteCompiledTextureBindingTests.cpp` (3),
`SpriteCompiledBindingLifetimeTests.cpp` (5) and `SpriteCompiledRetryPixelTests.cpp` (1) -- and
the cna-demos demos `graphics.texture2d.set_data` and `graphics.texture2d.set_data_rectangle`.
Evidence: `docs/repair-evidence/graphics/SPRITE_BINDING_FINAL_FOCUS.md`.

**`CNA-DEMOS-ISSUE-101` -- video URI resolution and deferred file failures.** URI spellings
bypassed Sharp Runtime's decoding and `TitleLocation` resolution, and a deferred `Play` of a
missing file stopped silently where XNA throws `ArgumentException`. `abbd8a4ea` (2026-10-21, on
Sharp Runtime `c26614e85`) fixed both; the tests are `VideoFilePathBoundaryTests.cpp` and
`VideoMissingFileXnaTests.cpp` in `modules/media/tests/Microsoft/Xna/Framework/Media/Video/`.
*Still unmeasured:* when and how genuine XNA reports an existing file it cannot decode. That needs
a working XNA media reference on native Windows: first a valid WMV as the positive control, then
the undecodable files and the error timing of the public `Play`. Under Wine the valid control
aborted in the unimplemented `mfplat.dll` `MFCreateVideoMediaType`, so no Wine result settles it.
Match CNA to the measurement; nothing was changed on FNA's word. Evidence:
`docs/repair-evidence/media/VIDEO_CODEC_REFERENCE_LIMITATION.md`.

**`CNA-DEMOS-ISSUE-152` -- `StorageContainer` honours the requested `FileShare`.**
`StorageContainer` discarded an explicit `FileShare`, Sharp Runtime had no constructor taking
one, and the shorter overloads used the wrong defaults (XNA opens them exclusively, and `Append`
write-only). `21b4cd64c` (2026-10-23; C contract `169470ee7`; Sharp Runtime `c6984ecef` and
`f51bf9acf`) fixed it with platform-native sharing. The genuine-XNA reference oracle and the Linux
kernel coverage are complete, and Sharp's sharing tests pass 16/16 under Win64 Wine.
*To run on native Windows against these CNA and Sharp Runtime commits or later:* the four sharing
tests of `modules/storage/tests/Microsoft/Xna/Framework/Storage/StorageDeviceTests.cpp`
(`FileSharingIsForwardedToTheNativeStream`, `ShortFileOverloadsAndCreateFileAreExclusive`,
`DeniedSharingDoesNotTruncateStoredData`, `TwoArgumentAppendUsesWriteOnlyAndPreservesThePrefix`)
and the cna-demos demo `storage.storage_container.file_modes`. Evidence:
`docs/repair-evidence/storage/FILE_SHARE.md`.
