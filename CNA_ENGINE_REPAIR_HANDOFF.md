# cna-engine upstream repair intake

Owner-requested intake, 2026-10-22. Read-only source inventory:
`../cna-engine/upstream-issues.md`. Do not edit cna-engine or cna-extended.
Use `cna-engine UP-NNN` in every associated commit message. The owner authorized
ongoing pushes of validated commits on 2026-10-22. Use normal fast-forward pushes,
never include another agent's uncommitted work, and recheck the remote first.

Current checkpoint: UP-002 `99b54079e`, UP-018 regression/evidence `a6e731cc2` and
`d49c080d9`, UP-006 regression `a2f0378cf` (runtime repair `470b12a5d`),
UP-019 repair `144d9f909`. UP-018 is closed as NOT_A_CNA_DEFECT after the engine
confirmed and corrected its own format bookkeeping in `8b7610c`.
The related museum C/C++ runtime repair is published as
89fb1493f7ffc89460a3bbe8d92fec4a660c0fe7. Both assembly/logger link orders pass,
14 native CTests pass, and installed/relocated shared/static C clients pass.
Full C demo outcomes remain unchanged (SOFTWARE168PASS/9FAIL; GLES169PASS/8FAIL).
Exact evidence and limitations are in CNA_MUSEUM_REPAIR_HANDOFF.md. No active heavy
job remains. UP-001 now has the installed C++ module package described below;
the earlier C-adapter repair alone did not supply those targets.
The chronological notes below retain earlier failures and intermediate checkpoints.

## UP-001 installed SDK implementation and validation (2026-10-22)

The commit containing this entry implements the missing installed CNA C++ package
(commit subject includes `cna-engine UP-001`). Baseline was published CNA
`9cfe94f2a8cf32609738ec5bca135a27cd08e062`; Sharp Runtime remains published
`1dc8785b35284b5cf50f3aec361703bfaf090b72`, demos remains `1295899bbaa2d657348a115b831f31b928d2df6c`.
UP-001 is FIXED / NATIVE_VALIDATED; consumer agents own their pin movement and
revalidation. Windows/WebAssembly installed-runtime validation remains pending,
not claimed as a native pass. No other intake or historical demos issue is closed
by this packaging change. The full repair campaign remains active.

Completed beyond the historical working checkpoint below:

- `CNA_INSTALL_CXX`, actual module/dependency exports, shared/static relocation,
  `CNA::CNA`, SharpRuntime:: components, narrow Math headers/link closure, and
  consumer-selected C++ runtime all work. C ABI package version/compatibility is
  retained. `docs/installed-cpp-sdk.md` documents production/consumption and limits.
- Explicit CApi-only lookup in C or C++ does not load the C++ SDK/dependencies.
  A later C++ request establishes the shared identity contract. Unknown required
  components, unknown optional components and incompatible versions are tested.
- Missing Sharp Runtime and real missing pkg-config multimedia packages are
  tested in fresh consumers. QUIET now returns NOTFOUND with a dependency name;
  it no longer terminates through pkg_check_modules(REQUIRED). Library discovery
  follows the same rule. The C++ CApi-only and missing-FFmpeg cases were observed
  red against the first-v2 installed prototype before repairing the templates.
- `CnaInstalledCppPackage` is a permanent serial opt-in CTest. It builds with one
  worker, installs separate components, removes the original location by moving
  the prefix, rejects source/build paths and escaping symlinks, and builds/runs
  fresh consumers. No producer options are changed by the gate. Failed artifacts
  and all logs/audits survive; only its own successful generated artifacts are
  cleaned (or retained with --keep-artifacts). Linux runtime uses the private runner.
- Bundled dependency headers/licences are included; native WGPU and cross-JSON
  notices supplement dependency distributions missing licence files. Embedded
  stb/dr_libs licence texts travel as complete source files. This is not a legal
  audit of transitive Rust crates inside an externally supplied WGPU binary.
- The MinGW export graph initially exposed the unregistered D3D9Effect helper;
  the helper now uses the same install/include rules. SDL_shadercross and its
  SPIRV-Cross static closure are exported, and cross JSON copies the isolated
  portable headers rather than publishing a symlink to host /usr/include.
  ALSA was inspected: it uses runtime loading, no libasound link export is needed.

Executed final evidence (existing build directories, shared ccache, one compiler,
4G-high/6G-max/no-swap scopes; no OOM events):

- Shared permanent gate `cmake-build-multi/installed-cpp-probe/run-ocao4uq3`:
  6 runtime/closure tests plus three C/C++ API-consumer executables, all pass;
  four negative configure cases plus the three API configure cases pass.
  Peak2,314,186,752 bytes. `run-rrsetkkw` is the preceding first permanent gate.
- Static permanent gate `run-esc64bgu`: 6/6 runtime/closure tests, four negative
  configure cases pass; C API deliberately OFF, no mixed-static-adapter claim.
  Peak1,793,683,456. Driver's finally restored shared/C API ON and removed the
  temporary explicit PIC cache override. Logs: demos reports
  `repair-cna-install-static-gate-v2-*`.
- Restored shared SDK plus seven module probes and both engine-style mixed
  consumers rebuilt, 42 scheduled steps, no compiler warnings/errors observed.
  Private regression gate **16/16**: new installed SDK, three existing build/link
  configuration gates, seven module consumers, both runtime/symbol link orders,
  and the original CApi_InstalledConsumer. The nested SDK gate `run-l_x_51u_`
  also exercised actual ContentManager/AudioEmitter/LaunchParameters symbols.
  Peak2,105,581,568. Logs: `repair-cna-package-native-final-{build,tests}-v1.log`.
- Final SDK rerun adds escaping-symlink checks and the explicit old-Sharp-install
  capability diagnostic: `repair-cna-package-native-final-sdk-v2.log`.
- MinGW configure/export passes after the helper fix; logs
  `repair-cna-package-mingw-configure-v{1,2}.log` preserve red/green evidence.
  WebAssembly/WebGPU configure/export also passes (`repair-cna-package-wasm-configure-v1.log`).
  No relocated Windows/browser SDK execution is claimed.
- Source-only install-OFF configure passes and the SDK flags are restored;
  `repair-cna-package-optout{-restore,}-v1.log`. Build-performance policy and
  diff whitespace checks pass. No full CNA runtime/demos sweep is newly claimed
  for this CMake-only batch; it does not change runtime behavior or knownIssue oracles.

All reports above are under `cna-demos/build/linux-full/reports/` unless an explicit
CNA build path is given. SDK install flags remain ON intentionally in the native,
MinGW and WebAssembly producer trees; the latter two are configure-only checks.
No engine/extended/museum source or pin was changed. Inherited content ownership,
ISSUE-113, storage and their tests remain dirty and excluded from this commit.
Next: publish this validated isolated package commit, then re-read the museum's
current Open section and continue the highest-priority actionable runtime finding;
the cna-demos campaign still has unfinished findings and is not complete.

## Historical UP-001 working C++ SDK checkpoint (2026-10-22)

Published/local HEAD is `9cfe94f2a8cf32609738ec5bca135a27cd08e062`; the SDK work
described here is NOT committed or published yet. Sharp remains published
`1dc8785b35284b5cf50f3aec361703bfaf090b72`, demos remains `1295899b`.
The previous turn made concrete progress (published the build-policy prerequisite);
this turn implements and executes the actual installed C++ target graph. UP-001
remains OPEN until distribution/gate work below is finished. No unrelated source,
engine, extended, museum or dependency repository was edited.

Implementation now in the worktree:

- Opt-in `CNA_INSTALL_CXX`, `CNACpp` install component, and `cna_cpp_sdk` build target.
  The exact configured Sharp Runtime package is enabled/installed separately as
  `SharpRuntime`. Public module aliases retain their individual archive closures.
- Per-module installed header roots, generated Version.hpp, all configured CNA
  module/helper exports, bundled private ENet/Draco/EasyGL/MetaGL/FNA3D/MojoShader
  archives, SDL headers/packages/runtime, and native WebGPU headers/runtime.
  System FFmpeg/FreeType/Zlib/Curl/Opus/JSON/Zstd/shaderc are located on the consumer.
  Zstd/shaderc now use named imported targets instead of producer absolute paths.
- Existing C ABI package/version remains the entry point (SameMajorVersion, ABI
  version, not a silent switch to runtime 0.1.0). `CNA_RUNTIME_VERSION` reports the
  separate C++ release. C-only installations still need no C++ package.
- Shared imports preserve early DSO injection and both C/C++ link orders. The
  TARGET_FILE genex needed an explicit install-side CNA::Shared spelling; CMake
  remaps the other target references automatically. Custom link features have
  cache scope so find_package inside a function does not lose them.
- C++ runtime selection was mechanically factored from BuildPerformance.cmake into
  CxxRuntimeLinkage.cmake and reused by installed clients. Producer runtime-first
  flags are build-only; installed clients choose their own static/shared C++ runtime.
  The single policy exception was moved with its unchanged read-only flag scanner,
  not broadened. The fetched-source fixture now copies both helper files.

Executed evidence (all in existing build directories, one SDK/consumer compiler,
shared ccache, 4G-high/6G-max/no-swap scopes):

- Full eight-renderer native configuration generates the export successfully.
  SDK build first compiled the previously unbuilt design/pipeline/phone archives
  (101 scheduled steps), then subsequent builds were incremental. No compiler
  warning was observed; the existing Draco CMP0148 configure warning remains.
- `repair-cna-install-first-v1-*`: original red InstalledRuntimeProbe is now green
  against a genuinely relocated prefix: 2/2 C++/C link-order tests, assembly title
  and logger share state, no source/build/home path leaks in installed CMake files.
  Peak2,522,324,992 bytes, all memory/OOM events zero.
- `repair-cna-install-first-v2-*`: 6/6. Adds Math-only runtime and ELF closure checks,
  rejects accidental Graphics/XML header exposure, verifies a requested static
  libstdc++/libgcc stays static, and checks repeated/function-scoped find_package.
  Math has no CNA shared/SDL/WebGPU/FFmpeg/ENet DSO dependency. Peak2,406,453,248,
  memory/OOM events all zero. EasyGL/MetaGL FILE_SET headers now belong to CNACpp.
- `repair-cna-install-static-v1-*`: 6/6 against the fully STATIC CNA SDK (C API OFF),
  including the same Math and C++ runtime checks. This is not a static C/C++ mixed
  adapter validation: the two runtime executables are C++ only in this mode.
  Peak1,735,507,968, all memory/OOM events zero. Producer shared/C API settings were
  restored successfully in the driver's finally block.
- Source linkage gates: CnaCxxRuntimeLinkage, CnaSharedRuntimeLinkage,
  CnaBuildSupportExport 3/3 after refactor. The first fetched-source run failed
  because the fixture copied only BuildPerformance.cmake; the missing helper copy
  was corrected, no assertions changed. Before/after logs are
  `repair-cna-package-linkage-{before,after}-v1.log`. Build policy check passes.

Logs/audit JSON live in `cna-demos/build/linux-full/reports/`. SDK copies remain in
`cna-demos/build-probe/cna-sdk-{first-v1,first-v2,static-v1}-relocated`; original
unrelocated prefixes are absent. Driver: ignored
`cna-demos/build-probe/repair-cna-install.mjs <unique-tag> [static]` (static mode
restores producer flags). No job remains active. `build-consumer` currently points
to static-v1. Producer `cmake-build-multi` is restored to CNA_SHARED_LIBRARY=ON,
CNA_BUILD_C_API=ON, no explicit CMAKE_POSITION_INDEPENDENT_CODE cache entry;
CNA_INSTALL_CXX=ON and SHARP_RUNTIME_INSTALL=ON remain intentionally enabled for
SDK development. The temporary CMAKE_PROJECT_CNA_INCLUDE graph hook was removed.

Exact next work before committing/pushing this batch:

1. Harden package failure/component behavior: QUIET dependency discovery must not
   hard-fail via pkg_check_modules(REQUIRED)/find_library(REQUIRED); explicitly
   CApi-only requests should not require C++ dependencies. Add negative component/
   version and C-only compatibility checks, retain existing ABI/version promises.
2. Complete distribution manifest/licences (especially wgpu-native and compiled
   header-only third-party code); validate installed dependency header presence.
   No exhaustive licence-completeness claim yet. Check optional native dependency
   configurations (ALSA, SDL_shadercross) and cross-configuration limitations; only
   the current Linux SDL3 eight-renderer SDK has been tested so far. The earlier
   Emscripten helper test is NOT a full installed CNA WebAssembly validation.
3. Turn the ignored install driver into a permanent scoped gate; register it only
   when SDK installation is enabled. Strengthen installed runtime smoke as useful;
   test shared/static modes, narrow closure, relocation and source independence.
4. Rebuild restored native consumers; run affected module/mixed-runtime/C API
   install/linkage gates and compare to previous baseline. No new full CNA/demos
   runtime sweep has been performed for this CMake-only work yet. Then document
   consumption, commit only this batch, push normally, and give the engine the SHA.

Owned uncommitted paths: root CMakeLists.txt; cmake/InstallCxx.cmake,
InstallCxxDependencies.cmake, CNACppConfig.cmake.in, CxxRuntimeLinkage.cmake,
BuildPerformance.cmake, SharedRuntimeLibrary.cmake, ThirdPartyENet.cmake,
ThirdPartyFNA3D.cmake, Tests/CxxRuntimeLinkageProbe/CMakeLists.txt,
Tests/InstalledRuntimeProbe/*; modules/CMakeLists.txt and the CMakeLists.txt in
core/content/net/gamer-services/renderers/renderers/common/mojoshader/
renderers/sdl-gpu; modules/c-api/cmake/CNAConfig.cmake.in;
tools/build/check_build_performance_policy.py; this handoff. All other dirty files
remain inherited ownership/content/ISSUE-113/storage work. Do not stage them.

## UP-001 build-support export prerequisite (2026-10-22)

Starting CNA revision: `79364adbd780b5ade3650af2047e193f2f737b6e`;
Sharp remains published `1dc8785b35284b5cf50f3aec361703bfaf090b72`, demos remains
`1295899bbaa2d657348a115b831f31b928d2df6c`. This checkpoint fixes the previously
recorded parent-owned build-policy export blocker. UP-001 itself is STILL OPEN:
the full installed CNA C++ target graph and package/version composition remain next.

`cna_link_private_build_support` now confines policy target references to the build
interface. Installed static/object/header-only targets retain sanitizer runtime
link options and Emscripten exception ABI options directly; header-only code also
retains their compile options. Private archive compile flags stay private, and
producer-only debug/linker/size policy targets do not leak. Nested configuration
expressions containing option lists are retained as expressions, not split apart.

The new permanent `CnaBuildSupportExport` regression first failed generation with
eight unexported-policy-target errors (four each for archive/header-only routes).
After repair it configures, builds, installs and runs source and installed consumers.
The native archive has real UBSan instrumentation; installed clients need no manual
sanitizer flags. Real Emscripten/Node clients also pass, including an exception thrown
from the installed archive and caught by its header-usage consumer. Both platforms
check that producer-only definitions disappear, required flags remain, private
compile options do not propagate, and nested Debug option lists survive. Each has
one source plus two installed runtime checks. The fixture uses warnings as errors,
one compiler and optional ccache, with all command output retained in steps.log.

Validation: actual CNA `cmake-build-multi` configure/generate succeeds with explicit
`SHARP_RUNTIME_INSTALL=ON`, including SDL/platform audits (formerly impossible).
The cache was then restored to OFF, preserving the existing source-consumer policy;
configure/generate also passes there. The known Draco CMP0148 developer warning is
unchanged. Seven real module probes plus both engine-style shared/module link orders
were rebuilt with one compiler and passed 11/11 CTests, including duplicate-symbol
checks; GPU-capable consumers used the private Weston/Xwayland runner. The existing
runtime-linkage gates plus the new export gate passed 3/3. Build-performance policy
passes; its pre-existing global warning-options violation in SharedRuntimeProbe was
corrected to target-scoped options, retaining -Wall/-Wextra/-Werror for all four
fixture targets. No assertions, limits or expectations were weakened.

Evidence: `cna-demos/build/linux-full/reports/repair-build-support-export-native-v1.log`
and `repair-build-support-export-consumers-v1.log`, plus final native/wasm steps.log
under `cmake-build-multi/build-support-export{,-wasm}-probe/`. Jobs ran in the usual
4G-high/6G-max/no-swap scopes. No full CNA/demos sweep or new renderer parity claim
is made for this CMake-only prerequisite. No active job remains at the checkpoint.
Next: export CNA's actual modular target/dependency graph, preserve narrow Math
closure and C/shared runtime identity, integrate with the existing C ABI package
without silently replacing its version contract, then turn InstalledRuntimeProbe
green against relocated native shared/static installations. That two-file fixture
remains unregistered/uncommitted. All inherited content/ownership/ISSUE-113 changes
are untouched and excluded from this prerequisite commit.

## UP-020 publication confirmation and engine revalidation (2026-10-22)

UP-020 is RESOLVED_BY_DEPENDENCY_PUBLICATION, not a new code repair. The engine
reported that published CNA `08c1387ec` called Uri::getIsFileProperty/getLocalPathProperty
from CNA `abbd8a4ea`, while the then-published Sharp Runtime `f2aecfef` lacked those
members. They were implemented in Sharp `c26614e8`. The preceding authorized pushes
have now published that commit: `git ls-remote origin refs/heads/next` in Sharp
returns `1dc8785b35284b5cf50f3aec361703bfaf090b72`, exactly the local HEAD, and
`git merge-base --is-ancestor c26614e8 HEAD` succeeds. Both declarations are present
in modules/uri/include/System/Uri.hpp. Use `1dc8785b` (or a verified descendant), not
the intermediate package revision `42847935` whose embedded-install policy needed
the follow-up. No production code or version change is needed for this publication
finding. Both projects still declare 0.1.0; this does NOT make that version alone
a reliable check for the presence of the new members in an older checkout.

Engine-reported validation, not a new run by this agent: CNA `08c1387ec` with Sharp
`c26614e8` built successfully and passed 338/339 engine tests. The one failure is
the engine calibration test expecting the pre-UP-019 software shadow orientation;
the engine explicitly confirms the corrected orientation is intended. Record this
as consumer confirmation of UP-019, not a fully green engine suite. The engine
agent owns its oracle/pin updates; none of its files were changed here. This report
does not separately establish a rerun of its UP-006 mixed-runtime reproducer.
UP-001 remains actionable and is the next implementation task.

## UP-001 dependency-package checkpoint (continuation, 2026-10-22)

CNA runtime source baseline is published `08c1387eca7f9707d6180ff57d07f0360e059672`
(this handoff-only checkpoint advances no runtime source);
cna-demos remains `1295899bbaa2d657348a115b831f31b928d2df6c`.
Sharp Runtime now has a genuine installed, relocatable modular package, implemented
in `4284793523bd157ff16661344955ae1c0d4d7673` and published with the required
embedded-policy follow-up as `1dc8785b35284b5cf50f3aec361703bfaf090b72`.
Use the latter revision for source consumers. This is a prerequisite, NOT closure
of UP-001: CNA's installed C++ aliases are still missing and its red fixture is
still intentionally unregistered/uncommitted. No engine/extended/museum source,
pin or worktree was edited.

Sharp's full pre-change baseline passed 18,193/18,193 tests across 41 executables,
after setting XNA_SAMPLES_ROOT to the existing mapping of the original ShipGame
and Spacewar fixtures. The initial five skips are retained in before-v1 evidence.
The post-change full gate exposed an independent pre-existing stochastic oracle
bug: a one-byte entropy tail was required to differ from0xA5 (false failure1/256).
An unchanged binary stopped at iteration293 with n=257 under a diagnostic bounded
repeat. A separate test-only repair `7046edbb8ea6863f7331b32041c590e850fa6b16`
uses a fixed32-draw matrix over EVERY byte position and two deterministic oracle
controls; it changes no production RNG. Full gate after-v2 is18,195/18,195, zero
fail/skip/compiler warnings, boundary validator PASS. Memory peak349,163,520 bytes,
all cgroup high/max/OOM events zero. No retry-until-green or assertion suppression.

Installed full and Core.Base-only consumers pass after renaming the prefix, with
the original prefix absent. Full package exercises Guid, JSON, XML/tinyxml2,
DEFLATE/zlib and ZIP/miniz; minimal package excludes unrelated module targets and
headers. Both reject unknown required components and incompatible version99.0;
minimal also rejects known-but-uninstalled Xml. final-v2 checks project/.NET and
private-miniz licence notices too. Install gate peak615,297,024 bytes/events0.
Evidence: cna-demos build/linux-full/reports/repair-runtime-package-{before-v1,
before-v2,after-v1,after-v2}-* and repair-runtime-install-final-v{1,2}-*.
All builds used one compiler, shared ccache, standard Sharp build/build-modular/
build-consumer directories, never /tmp. Four older install copies (~416MiB) were
removed after recording evidence. Three superseded final-v2/v3 copies (~397MiB)
were also removed after final-v4 passed; only final-v4 full/core copies remain
under Sharp build-probe. They are reproducible by the committed fixture. No build
tree/source/foreign file was deleted.

Integration follow-up: enabling Sharp exports automatically when embedded exposed
CNA's private cna_instrumentation/debug_info/emscripten_exception_abi/size_options
targets, which have no export set. The published42847935 therefore requires the
immediate follow-up in progress: SHARP_RUNTIME_INSTALL defaults ON standalone,
OFF in add_subdirectory, with explicit parent opt-in for SDK composition. A parent
build-policy regression is added separately from source-independent package tests.
CNA cmake-build-multi now configures/generates successfully with this correction;
all SDL/platform audits pass, existing Draco CMP0148 developer warning unchanged.
No new CNA runtime build/full-test result is claimed for this configure check.

Final follow-up `1dc8785b` is committed/pushed; Sharp worktree clean and remote SHA
verified. Full after-v3 gate passed 18,195/18,195, zero failures/skips/warnings,
boundary validator PASS, peak100,278,272 bytes/events0. Installed final-v4 passed
both parent-policy configure cases, full2/2 and minimal1/1 runtime cases, all
component/version rejection checks, licence-manifest and relocation checks;
peak617,811,968 bytes/events0. The final-v3 fixture-generation failure is retained:
its strict compiler options had targeted the newly introduced INTERFACE helper;
they now apply only to test executables. No test expectation was relaxed.
No job remains active. Exact next task: implement CNA's exported module graph.
For CNA, preserve narrow Math/Core closures, shared/static runtime identity and
the existing C ABI package/version. Its build-policy helper currently links raw
parent targets into Sharp; explicit installed contracts must retain sanitizer and
Emscripten ABI flags without leaking producer-only debug/linker targets. Do not
paper over that by exporting absolute producer paths or forcing every module
through the full runtime. Preserve inherited content/ownership/ISSUE-113 changes.

The earlier 12 unpushed Sharp campaign commits were included in the authorized
normal fast-forward push to7046edbb and are covered by the complete gate;
package42847935 and follow-up1dc8785b were pushed separately. No force-push,
merge or tag was performed. CNA-owned uncommitted UP-001 work is this document and
cmake/Tests/InstalledRuntimeProbe; all other dirty CNA files are preserved inherited
work, not part of these commits. Original campaign still has 156 historical IDs,
40 without a final disposition, plus residual DC-R1/DC-R2 work; it is not complete.

### Earlier UP-001 preparation (historical)

UP-001 preparation: cna_add_module currently publishes absolute source include paths;
CoreHeaders and Core additionally expose the generated Version.hpp include root.
Both need build/install interfaces, not copied source-tree paths in an export.
Keep the minimal module link-closure contracts in cmake/Tests/ModuleProbes.cmake:
for example Math must remain math + Sharp Runtime, not acquire the whole runtime.
The installed C ABI package already owns CNAConfig/CNAConfigVersion and uses the
C ABI version with SameMajorVersion compatibility; do not replace that contract
silently with the project's independent release version. Sharp Runtime's modular
targets need a proper relocatable dependency package rather than opaque build-tree
archive paths. These are inspected requirements, not an implemented package.
Sharp Runtime preparation: next c26614e8 is clean. Its component factory already
uses BUILD_INTERFACE includes and records enabled components/aliases in global
properties; there are no install/export rules. Preserve selective component
boundaries, generated SharpRuntime/Version.hpp and native-int128 compile definition.
Xml has its namespaced tinyxml2 archive; IO.Compression.Zip has a miniz archive;
IO.Compression uses ZLIB (except the Emscripten port), and Storage can use SDL on
Android. Export/install the actual configured dependency closure, not an All-only
umbrella or absolute source/vendor paths. Repository policy requires the full
Sharp Runtime gate before committing a dependency change, at most two workers
(this campaign uses one), existing build directories, and immediate normal push
to next under SA-1. No Sharp Runtime files changed during this preparation.

UP-001 executable red baseline (2026-10-22, after published08c1387ec): added the
uncommitted standalone fixture cmake/Tests/InstalledRuntimeProbe/{CMakeLists.txt,main.cpp}.
Configuring it in the standard build-consumer directory against the genuinely relocated
C ABI installation finds CNAConfig but fails for all seven requested CNA module targets
and SharpRuntime::Core.Base/IO. No source paths or add_subdirectory fallback are allowed.
This is the expected package defect, not a compiler failure; main.cpp is not compiled yet.
Command: cmake -S cmake/Tests/InstalledRuntimeProbe -B build-consumer
-DCMAKE_PREFIX_PATH=../cna-demos/build/linux-full/CNA/modules/c-api/installed-consumer/relocated
-DCMAKE_CXX_COMPILER_LAUNCHER=ccache (the actual invocation used the absolute prefix).
build-consumer did not previously exist; it is the approved reusable consumer directory,
not a new CNA build. Configure used shared ccache/one worker in a4G-high/6G-max scope.
No job remains active. Next: implement the proper Sharp Runtime export/install layer,
validate it with the dependency's full gate, then integrate CNA's modular package and
turn this same installed-consumer fixture green in shared/static and relocated cases.
Uncommitted work for UP-001 is this handoff plus the two fixture files only; preserve
all inherited ownership/ISSUE-113 and storage/input preparation changes separately.

Baseline: CNA `a8800619e1b063e4e7f8bed5cf1b19744362c189`, branch `next`;
reported engine pin `900d0db3b` is an ancestor. Preserve the inherited dirty
content/audio ownership and ISSUE-113 changes. The uncommitted storage-path
regressions from the preceding batch are also retained, not part of this intake.
Their 29-check genuine XNA oracle passed; native red baseline is recorded in
cna-demos/build/linux-full/reports/repair-storage-path-before-v1-*.

| Finding | Current disposition | Evidence / next task |
| --- | --- | --- |
| UP-019 | Fixed in `144d9f909`; engine confirms intended corrected orientation; DirectX runtime validation pending | CNA renderer regressions pass as recorded below. Engine reports 338/339 at CNA `08c1387ec` + Sharp `c26614e8`; remaining failure is its calibration assertion for the old orientation, not a full-green consumer result. No Windows runtime claim. |
| UP-018 | CLOSED — NOT_A_CNA_DEFECT / ENGINE_EXPECTATION_ERROR | Engine confirmed and fixed in `8b7610c`: store target.getFormatProperty(), not the preference. Existing exact pin/next checks prove Reach selects Color and HiDef Single retains float precision. No CNA production repair required. |
| UP-006 | ALREADY_FIXED and CNA-validated; engine revalidation pending | `470b12a5d` repairs mixed umbrella/static-sidecar ordering (ISSUE-016). Engine-style consumers pass in both orders with zero duplicate strong Graphics/Input/Vector3 symbols. Engine owns pin movement/revalidation. The museum's separate C adapter duplication is now fixed and published in `89fb1493f`. |
| UP-001 | FIXED / NATIVE_VALIDATED — installed C++ SDK | Commit containing the installed SDK checkpoint above exports CNA and SharpRuntime component targets. Relocated Linux shared/static clients and native C compatibility pass. Windows/WebAssembly export graphs pass; installed runtime validation on those platforms remains pending. Engine/extended own pin movement and consumer revalidation. |
| UP-002 | Documentation corrected | `docs/migration-guide.md`, CNAEXT and C++ language artefacts, distinguishes declaration markers from implicit/synthesized C# semantics and explicitly retains strict compiler diagnostics. No runtime/header/test changes needed. |
| UP-020 | RESOLVED_BY_DEPENDENCY_PUBLICATION | Required Sharp Runtime `c26614e8` is an ancestor of verified published next `1dc8785b`. No new runtime repair/version bump. The shared 0.1.0 version still does not distinguish older checkouts; use the verified commit. |

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

## Engine confirmation and package requirements (2026-10-22)

Owner forwarded the engine's explicit closure of UP-018. Read-only inspection of
engine commit `8b7610cef6dd74ee0a6e3b9a2f82ba0a3153e4c2` confirms ShadowMap now stores
`target_->getFormatProperty()` and its upstream inventory preserves the original
mistaken report under a resolved/not-a-CNA-defect status. No engine or extended
files were edited by this campaign. This finding is not counted as a CNA code fix.

The engine acknowledged UP-019 `144d9f909` and UP-006 `470b12a5d`, but has not
revalidated either at its pinned revision yet. Do not replace that pending state
with CNA's focused test results. This message does not authorize a new push.

UP-001 remains the engine's sole open item from this intake. The owner reports
cna-extended0.2.0 already tries `find_package(CNA CONFIG)` before attaching CNA
targets supplied by the consumer. Required exports: CNA::Core, CNA::Math,
CNA::Input, CNA::GraphicsCore, CNA::Content, CNA::Runtime, CNA::Audio, plus the
SharpRuntime:: targets their interfaces require. Validate a relocated installed
package without source-tree paths and without embedding a second runtime. Keep
the existing C API package working; its version follows the C ABI, not the C++
project version. Complete the active shared C adapter/package validation first.
