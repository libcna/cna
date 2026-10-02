# Handoff — CNA C# compatibility campaign (cna-cs)

Written 2026-10-02 at the end of a ~30-hour session. `/goal` remains authoritative; this file is the
state it reached and how to continue. Nothing is pushed.

## 1. Campaign identity

- Started **2026-10-01**. Goal (short form): unchanged Microsoft XNA Game Studio 4.0 C# games and
  samples run on CNA through cna-cs (CNA.NET), the way FNA/MonoGame run them — CNA is the native
  runtime, cna-cs the managed XNA surface. Linux desktop first, then genuine browser (WebAssembly)
  and Android; Windows/macOS/iOS only with real evidence.
- This session: ABI migration and native defects (CBIND-128..148, FX-140..142, Task 1120), the
  sample gallery on desktop/browser/Android, then real XNA games outside the gallery, ending with
  the XNA 4.0 Racing Game Kit.
- Working rules kept throughout (from `/goal` and owner instructions): fix in the layer the defect
  lives in with a regression test at the lowest layer; never edit Microsoft sample/game `.cs`;
  official XNA-built content only; no managed workaround for a native defect; ABI admission is an
  exact reviewed version list; never run windows on the live desktop (`:0`/`wayland-0`) — use
  `cna/tools/platform/run_gpu_tests_private.sh` or a private Xvfb; GamerServices tests with an
  isolated XDG and no session bus; shared ccache (`CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv`),
  `-j8` max, no builds in `/tmp`; max 3 subagents; no `handoff.md` unless asked (this one was asked
  for); **do not push**.
- The running ledger with full evidence per task is `cna-cs/CAMPAIGN.md` (task table + dated
  ledger, newest first). Games: `cna-cs-samples/games/README.md`. Gallery rows:
  `cna-cs-samples/plan.md`, `samples/manifest.tsv`, `gallery-inventory.md`.

## 2. Executive status

**Completed and validated**
- cna-cs binds CNA C ABI **0.40.0** (exact point policy; 0.39.0 retired). ABI verification,
  api-compat (0 diagnostics vs XNA 4.0 Windows runtime metadata) and all managed suites green.
- Desktop Linux (OPENGLES3, compiled effects ON): every checked-in gallery row builds Debug+Release,
  runs, captures and exits by its own exit path; 82 of the 84 gallery entries are covered
  (Yacht and PerformanceUtility are not — §9).
- 25 real XNA 4.0 games outside the gallery run on Linux from unchanged source (§13), including
  the XNA 4.0 Racing Game Kit **through its menu into a race** (§15).
- Native defects found through C# consumers fixed in CNA with tests (§6), all committed.

**Implemented but incompletely qualified**
- Browser: all 84 manifest rows build and run in **headless** Chromium with SwiftShader WebGL2
  (CNA `ffb82bc0d`, 2026-10-02). Not interactive Chrome, not a hardware GPU, no browser audio (§10).
- Android: all 84 rows build, run and draw on an **x86_64 emulator** (API 35) with touch, Back,
  pause/resume, relaunch. No physical device, no ARM, no audible audio (§11).
- Real games: all 25 tried in the browser (21 reach title/play; HeliumBiker, Escape From Enceladus
  and the Racing Game Kit start threads, Playing in Traffic plays a video) and on the Android
  emulator (24 run; Playing in Traffic refuses its video).
- CNA.WindowsFormsCompat: `Opacity`/`FormBorderStyle` are kept, not applied.

**Investigated only / open**
- 2 `EasyGLCompiledEffect*` ctests still fail on OPENGLES3 (§6); five more were fixed by FX-143.
- CSX-044 (remaining C API routes for GamerServices/Net extras) is `doing`, low priority.

**Not proven (hypotheses)**
- None open from this session; the Racing Game's "lost input" was settled as a harness timing
  artefact (§15).

## 3. Repository state (verified with `git rev-parse HEAD` / `git status`)

| Repo | Path | Branch | HEAD | Working tree | Unpushed |
|---|---|---|---|---|---|
| cna | `/rv/data/development/github.com/libcna/cna` | `next` | the handoff commit (`docs: handoff ...`), parent `3c72165e6` (CBIND-150) | clean | 35 (`origin/next..next`): 32 fix/feature commits (CBIND-128 … CBIND-150) + 3 handoff commits |
| sharp-runtime | `../sharp-runtime` | `next` | `db86514c5bb86a5886d8015b8e2916d49be04ae8` | clean | none (untouched this campaign) |
| cna-cs | `../cna-cs` | `develop` | `74b74f7d4b698a4d7faa4b4c2f2d402c2b64a243` | clean | 76 (`origin/develop..develop`) |
| cna-cs-template | `../cna-cs-template` | `develop` | `2a763886cf7660ac10557163d680783dde5c01f3` | clean | 2 |
| cna-cs-samples | `../cna-cs-samples` | `develop` | `671df07a36709f0555f364bea4c202fbf4510431` | clean | 82 |
| samples.libcna.com | `../samples.libcna.com` | `main` | `8e48825c003368dbc24bebd8f024d392379700d6` | clean | none (untouched) |

There are no intentional uncommitted changes anywhere. In cna, `handoff.md` is committed by the
commit right after `fb5cb3ba2` (`docs: handoff ...`), so cna's HEAD is that commit and its parent
is `fb5cb3ba2`. Build trees and probe leftovers are gitignored or under `/rv/tmp`.

## 4. Commits

All local, none pushed. Full lists (oldest first) are in the appendix at the end of this file.
The important ones:

- **cna**: CBIND-128 (component C handlers inside the game's callback scope), CBIND-129..131,
  CBIND-130/132/134/141/145 (ABI 0.36→0.40), CBIND-136..139 (Android), CBIND-135/143 (browser),
  CBIND-140 (title-relative content root), FX-140/141/142 (MojoShader), Task 1120 (SpriteBatch with
  a stock effect), CBIND-142 (video frame texture ownership), CBIND-144 (NoSuitableGraphicsDevice),
  CBIND-146 (ADPCM content sample rates), CBIND-147 (ClientBounds position), CBIND-148 (+ budget
  follow-up `836b796ae`: SDL3 mouse reads the live pointer).
- **cna-cs**: ABI migration 0.21→0.35→0.40 (CSX-010..014, -101, -111), facade behaviour fixes
  CSX-080..114, browser/Android heads (CSX-060..071), GamerServices/Avatar/Net (CSX-040..043),
  phone compat (CSX-045/094..098/103/106), interceptor generator (CSX-110), Windows Forms compat
  (CSX-114).
- **cna-cs-samples**: the gallery rows, browser/Android generators and requalify scripts, the
  `games/` corpus (25 running games), capture-harness fixes.
- **cna-cs-template**: `ae6b906` (CSX-030 consumer verification), `2a76388` (CSX-031 browser and
  Android heads from the template).

## 5. CNA ABI and native integration

- Current C ABI: **0.40.0** (`modules/c-api/include/CNA/C/abi.h`: MAJOR 0, MINOR 40, PATCH 0);
  3,209 exports at CBIND-145. cna-cs `CnaNativeAbiPolicy.ConsumerVersion` = 0.40.0; accepted list
  is exactly {0.40.0}; fixtures: exact-0.40.0 accepted, retired-0.39.0 and unreviewed-0.41.0 refused,
  shape canaries at 0x2800.
- Migrations this campaign: 0.21.0 (cna-cs at start, refused by its own policy) → 0.35.0
  (CSX-010..014, removed the retired `CNA.Graphics.Experimental` surface) → 0.36.0 (CBIND-130,
  packet reader copy) → 0.37.0 (CBIND-132, canonical exception behind a failure) → 0.38.0
  (CBIND-134, host-driven one-frame run, used by the browser loop) → 0.39.0 (CBIND-141, another
  thread's calls run on the game thread) → 0.40.0 (CBIND-145, adapter routes accept the game
  handle before the first callback).
- Admission process for a new minor (do all of it): cna-cs policy JSON + `CnaNativeAbiPolicy.cs`,
  `CnaAbiTests`, `scripts/Verify-NativeAbiCompatibility.sh` fixtures, CI pins in
  `.github/workflows/build.yml` (`CNA_UPSTREAM_COMMIT` full SHA), `tools/abi-verify`; in CNA:
  `abi_baseline.json`, `docs/c-api/COVERAGE.md`, `LIMITATIONS.md`, `RELEASE_GATE.md` regenerated
  (`tools/c-api/*.py --write`), `ABI_VERSIONING.md`.
- CBIND-147/148 and FX-142 changed behaviour, not signatures: no ABI bump.
- Release gate: `docs/c-api/RELEASE_GATE.md` says **Not ready — 1 criterion unmet**; that criterion
  is the pre-existing CNB content-format backlog (CBIND-103..112), unchanged by this campaign.
- Remaining ABI concerns: none known. Adding imports to cna-cs (e.g. borderless window, native
  message box for CSX-114) requires updating its import inventory and abi-verify, not the ABI.

## 6. CNA fixes (all committed, each with a test that fails without it)

| ID | Commit | What changed |
|---|---|---|
| CBIND-128 | `bdf239360` | A component's C handlers run inside the game's callback scope (DrawableGameComponent.GraphicsDevice borrowable). |
| CBIND-129 | `6e0de68e8` | A component's C initialize handler runs before its base loads content. |
| CBIND-131 | `abd005ab8` | EasyGL context lease survives exit-time static destruction. |
| CBIND-133 | `e77ae09ed` | GraphicsResource.hpp defines the lease its template holds. |
| CBIND-135 | `1e7c4d323` | Static C API archive builds under Emscripten. |
| CBIND-136..139 | `552a0d8cb` `747b02493` `80572aa00` `1dc4ce06a` | Android: NDK-only build; Back seen once as Escape and player-one Back; portrait games; models draw whole. |
| CBIND-140 | `9976f4909` (+tests `9ea1aef4b`) | A relative content root is the title's directory, not the working directory. |
| FX-140 | `1c2923efd` | SM3 point-size output takes a scalar write on GLSL. |
| Task 1120 | `7661f6981` | A stock effect passed to `SpriteBatch.Begin` places sprites by its matrices (3D sprites). |
| CBIND-142 | `fad9fabe0` | A video frame texture belongs to the player's game (SpriteBatch accepts it). |
| CBIND-143 | `ffb82bc0d` | Avatars load in a single-threaded browser build. |
| CBIND-144 | `a71cc2415` | Device-creation failure in the Game constructor surfaces as `NoSuitableGraphicsDeviceException` (XNA IL wrapping), not PlatformException. |
| FX-141 | `46231e857` | MojoShader: SM3 programs may mix absolute and ordinary float-constant reads (Microsoft's compiler output; reversed SOFTWARE-410's refusal). |
| CBIND-145 | `22b30b30e` | ABI 0.40.0: adapters readable with the game handle in a constructor. |
| CBIND-146 | `51d9c84cc` | SoundEffect content reader accepts XAudio2's 1000–200000 Hz (XNA's ADPCM writes e.g. 48056 Hz); public constructors keep 8000–48000. |
| CBIND-147 | `e6f9d5384` | `GameWindow.ClientBounds` X/Y are the client area's desktop position (XNA IL: `PointToScreen(Point.Empty)`); were forced to 0. |
| CBIND-148 | `6e273cd89`, `836b796ae` | SDL3 mouse on x11/windows/cocoa reads `SDL_GetGlobalMouseState` minus window position (XNA: `GetCursorPos`+`ScreenToClient`, `GetAsyncKeyState`; FNA does the same); was stale after a window move and clamped at the edge. Follow-up keeps the test inside the PLAT-123 non-production SDL budget (74). |
| FX-142 | `fb5cb3ba2` | MojoShader link-time centroid rule now requires pixel SM ≥ 2.0, matching the emitter; a ps_1_x COLOR0 input no longer meets a vertex shader writing `cna_centroid_10_0` (Racing Game normal-mapping effect failed to link on GLSL ES). |
| FX-143 | `d9d599263` | MojoShader GLSL ES 3 preamble declares `precision highp sampler3D;` (ES 3.00 has no default for it), so shaders with volume samplers compile on OPENGLES3/WEBGL2; five failing compiled-effect tests pass. Static effect gallery rows pixel-identical; SpriteEffects moves ≤2/255 in one quadrant. |
| CBIND-149 | `d459f42aa` | EasyGL enables `GL_SAMPLE_MASK`/`glSampleMaski` only on desktop GL 3.2+ / GLES 3.1+ (decided from the context, not from a resolving entry point); fixes a pending INVALID_ENUM on ES 3.0/WebGL 2. Test `EasyGL_Es30SampleMask` (forced ES 3.0). |
| CBIND-150 | `3c72165e6` | EasyGL skips `GL_TEXTURE_SWIZZLE_*` on one-/two-channel render targets where the profile has none (WebGL, GLES 2); those channels read 0 there instead of D3D's 1 (recorded divergence). Test `EasyGLProfile.OnlyDesktopGlAndGles3SwizzleTextures`. |

Unresolved native items (confirmed, pre-existing, not introduced here):
- OPENGLES3 `EasyGLCompiledEffect*` after FX-143: 2 of 686 fail —
  `SharedVertexSamplerContract` at its vertex LOD-bias step ("vertex LOD-bias transition did not
  select mip one"; OpenGL ES 3 has no sampler LOD bias, so this needs emulation in the shader or a
  recorded capability refusal) and `FlippedSourceRetainsFormatAndFullFloatPrecision` aborting
  ("terminate called without an active exception"). Root causes not investigated.
- Timing-sensitive C smoke tests under `-j8`: each run lost one to three *different* tests that pass
  alone (`RuntimeComponentsSmoke` stage 8 asserts one Update per fixed-timestep frame; audio smokes;
  one `DevicesSmoke` SIGSEGV). Suspected load flakiness, not reproduced in isolation (stress of 10
  concurrent copies passed).
- Stale example binaries in `build-probe` (built 2026-09-30) crash against today's `libcna.so`;
  rebuilding the target fixes it (seen with `EasyGL_RealWindowResize`).

## 7. cna-cs status (what behaves differently now)

- **ABI/interop**: 0.40.0; `Verify-NativeAbiCompatibility.sh` and abi-verify clean; handles held as
  64-bit (CSX-085, WebAssembly).
- **Game lifecycle** (CSX-086/088/090/082/084): XNA order of Initialize/LoadContent/BeginRun/
  EndRun/Exiting/Disposed; components initialised inside `base.Initialize()` once; `base.Update/Draw`
  drive components at the call; callback exceptions unwind out of `Run` with their own type;
  SIGTERM/SIGINT end through the game's own exit.
- **Exceptions** (CSX-080): native refusals map to the XNA exception the same call throws, including
  `NoSuitableGraphicsDeviceException` from the Game constructor.
- **Graphics**: device state cache coherent with native SpriteBatch (CSX-083); default profile from
  the `RuntimeProfile` resource (CSX-081); `Color.Transparent` = transparent black (CSX-091);
  adapters in a constructor (CSX-111); `GraphicsDeviceManager.GraphicsDevice` null until created
  (CSX-112); 3D sprites via stock effect (CSX-107, CNA Task 1120).
- **Content**: title-relative paths (CSX-087, CNA CBIND-140); case-insensitive file *and directory*
  resolution (CSX-096, CSX-113); custom readers by simple assembly name (CSX-102); model tags of the
  game's own types and every stock effect in models (CSX-089/092/093); workers loading content
  (CSX-100/101, ABI 0.39.0).
- **Audio**: songs play from the `.ogg`/`.oga`/`.qoa` beside a `.wma` (CSX-105; `scripts/
  convert-xna-songs.sh` writes them); XACT via CNA; ADPCM rates via CNA CBIND-146.
- **Storage**: one StorageDevice shared by several worker threads (CSX-109); IsolatedStorage in the
  browser via `CNA.BrowserCompat` (CSX-108).
- **Input**: phone titles get the mouse as a finger and Back on Escape (CSX-095/098); mouse position
  now live via CNA CBIND-148.
- **.NET vs .NET Framework semantics**: `List<T>.ForEach` 4.0 semantics by a Roslyn interceptor
  generator shipped in CNA.XnaCompat (CSX-110; opt out `<CnaNetFx40ListForEach>false`);
  libraries compiled against XNA 4.0 run unchanged via XNA-named forwarders (CSX-099).
  BinaryFormatter, CodeDom, ConfigurationManager are per-game project glue (NuGet/opt-in), not
  facade changes.
- **GamerServices / Avatar / Net / Guide** (CSX-040..043): GS/Net profile 75/75, avatar renderer
  draws, SystemLink loopback measured; 24/24 integration tests (isolated keyring/profiles).
- **Opt-in assemblies**: `CNA.PhoneCompat` (Microsoft.Devices, Sensors.Accelerometer, Phone.Shell
  PhoneApplicationService), `CNA.BrowserCompat` (IsolatedStorage), `CNA.WindowsFormsCompat`
  (CSX-114: `Control/Form.FromHandle` for the game window, `Opacity`/`FormBorderStyle` kept not
  applied, `MessageBox` → stderr + first button). Opt in from cna-cs-samples with
  `<CnaPhoneCompat>` / `<CnaWindowsFormsCompat>`.
- **Shaders/effects**: compiled XNA effects through CNA's MojoShader (FX-140/141/142).

## 8. Exact final test state (2026-10-02, CNA `fb5cb3ba2`, cna-cs `ff08b93`+docs)

Native library: `cna/build-probe/modules/c-api/libcna_c_api.so` (Release, OPENGLES3,
`CNA_EASYGL_COMPILED_EFFECTS=ON`, `CNA_BUILD_C_API=ON`).

| Suite | Command (from the repo root) | Result |
|---|---|---|
| cna-cs integration | `CNA_NATIVE_LIBRARY=<lib> SDL_AUDIODRIVER=dummy ../cna/tools/platform/run_gpu_tests_private.sh --exec dotnet test tests/CNA.Integration.Tests/CNA.Integration.Tests.csproj` | **232/232** |
| cna-cs Framework | `dotnet test tests/CNA.Framework.Tests/CNA.Framework.Tests.csproj` | **646/646** |
| cna-cs XnaCompat | `dotnet test tests/CNA.XnaCompat.Tests/CNA.XnaCompat.Tests.csproj` | **297/297** |
| cna-cs GamerServices | `../cna/tools/platform/run_gpu_tests_private.sh --exec <job>/run-gs.sh <lib>` (isolated `XDG_*`, `DBUS_SESSION_BUS_ADDRESS` unset; script body in §17) | **24/24** |
| cna-cs api-compat | `XNA_REFERENCE_PATH=~/deps/xna40-windows-assemblies dotnet run --project tools/api-compat -c Release --no-build -- --format text` | **0 diagnostics** (256/256 types) |
| CNA C API | `SDL_AUDIODRIVER=dummy tools/platform/run_gpu_tests_private.sh build-probe -R '^CApi' -j4` | **119/119** |
| CNA doc gates | `ctest --test-dir build-probe -R '^CApi(CoverageMatrix\|Limitations\|ReleaseGate\|DocExportCounts\|AbiBaseline)$'` | 5/5 |
| CNA runtime | `run_gpu_tests_private.sh --exec build-probe/CnaRuntimeTests` | 206 pass, 2 skipped (Headless/Terminal golden: platform cannot back this tree's GL renderer) |
| CNA platform/mouse/window groups | `run_gpu_tests_private.sh build-probe -R '^(Sdl3\|Mouse\|GameWindow\|GameEventSemantics\|GameTest\|InputParity\|Platform)' -j8` and `-R 'Mouse\|ClientBounds\|Window\|Cursor\|^CApi'` | 417/417 (4 skips), 476/476 after rebuilding one stale example |
| CNA compiled effects | `run_gpu_tests_private.sh build-probe -R 'EasyGLCompiledEffect' -j8` | **684/686** after FX-143 (was 679/686; the 7 were identical with FX-142 removed); the 2 left are §6 |
| CNA SDL boundary gates | `python3 tools/platform/{sdl_inventory,sdl_classify,renderer_sdl_audit,sdl_ratchet}.py --check`, `hot_path_lint.py`, `nonproduction_sdl_audit.py --repo . --check` | all pass |

Parallel-only flakiness: `^CApi` at `-j8` lost 1–3 different smokes per run (all pass alone and at
`-j4`); `DynamicSoundEffectInstance` stress in CnaAudioTests is known flaky (passes alone 3/3).
Not rerun after CSX-114: `scripts/Package-Acceptance.sh` (last passed on 0.40.0 after CSX-111;
CSX-114 adds a project packed only under `CnaPackageAcceptance`, like PhoneCompat).

## 9. samples.libcna.com gallery — Linux desktop

Inventory regenerated 2026-10-02 (`python3 scripts/gallery-inventory.py --markdown <file>`; the
checked-in `gallery-inventory.md` dates from 2026-10-01 and still shows the older ⬜ states):

- **84 gallery entries; 82 ✅** run on cna-cs with unchanged Microsoft source (hash-verified by the
  row tooling), XNB content from the C++ ports' evidence (official XNA pipeline output), Debug and
  Release builds, a capture on a private Xvfb, and the source's own exit path.
  `TransformedCollision` and `TransformedCollisionTest` are two gallery entries (SAMPLE-020).
- **Yacht** (SAMPLE-071) 🛑: needs Windows Phone `Microsoft.Phone.Shell`/push `Notification` APIs
  beyond CNA.PhoneCompat; recorded as an owner decision (cna-cs-samples `plan.md` CSSAMPLE-071).
- **PerformanceUtility** (SAMPLE-104): no completed C++ CNA port (C++ plan 🟡), so no C# row.
- `samples/manifest.tsv` has 84 runnable rows: the gallery's ✅ rows plus ClientServerSample and
  NetRumble (Net samples outside the gallery).
- Evidence: `scripts/requalify.sh --out DIR` writes per-row captures, the C++ port's capture taken
  the same way, and `DIR/requalification.md` with a pixel-difference column against the C++ port.
  Exact pixel equality with the C++ port holds only for rows reporting `0.00% (0 px)`; animated or
  seeded samples differ by construction. Baseline runs: `/rv/tmp/cs-samples/requal-20261002b`
  (clean rerun `-c`) on CNA `22b30b30e`/CNA.NET `1326161` — 84 rows, all static rows pixel-identical
  to the previous C# captures.
- Final run on CNA `fb5cb3ba2` / CNA.NET `ff08b93`: `/rv/tmp/cs-samples/requal-20261002d`
  (2026-10-02 afternoon, after CBIND-147/148, FX-142, CSX-113/114): **84/84 build (Debug+Release)
  and run**; exit column identical to the baseline — 73 exit on Escape with code 0, 5 have no exit
  key in their source, 6 menu-driven games do not end on one key under the harness (GameStateManagement,
  CardsStarterKit, ClientServerSample, ShipGame, NetRumble, RolePlayingGame; their menus need more
  input, as on the baseline and on Android). Against the baseline C# captures: 40 pixel-identical,
  44 differ — 43 of those also differed between two earlier runs of identical code (animation or
  seeded randomness); the 44th, TrianglePicking, now draws its mouse crosshair and pick label and
  matches its **C++ port within 1 px** (was 1992 px) because CBIND-148 reports the real pointer.
  26 rows equal their C++ port exactly (`0 px`). Comparison script: `<job tmp>/cmp-requal.sh`
  (compare -metric AE per row; recreate from this description if the job dir is gone).

## 10. WebAssembly

- Qualified: all 84 manifest rows **build and run in headless Chromium** (Chrome for Testing
  headless shell 151.0.7922.34, Playwright cache `~/.cache/ms-playwright/chromium_headless_shell-1234`),
  WebGL2 through **SwiftShader**, .NET 11 SDK `11.0.100-rc.1.26425.128` (`~/deps/dotnet11`,
  wasm-tools workload, emscripten 6.0.3), CNA static archive from `cna/cmake-build-webgl2`
  (`cna-cs/scripts/Build-BrowserNative.sh`). Run: `scripts/browser-requalify.sh` (all rows) or
  `scripts/browser-sample.sh <Row|games/Game>`; generated projects in
  `cna-cs-samples/build-consumer/browser/<Row>/`. Last full pass CNA `ffb82bc0d` (2026-10-02):
  77 on the first pass, 7 after CSX-108/CBIND-143/generator fixes; evidence
  `/rv/tmp/cs-samples/browser-requal-csx102-{a,b}`, `browser-requal-csx108`.
- Games: 21 of 25 reach title or play. HeliumBiker, Escape From Enceladus and the Racing Game Kit
  start threads (`new Thread(...).Start()` → `PlatformNotSupportedException` in single-threaded wasm);
  Playing in Traffic plays a video (no browser video backend). The newest nine were measured on CNA
  `3c72165e6` after CBIND-149/150 (WebGL 2 sample mask and texture swizzle) and the generator fix
  for NuGet packages and `<CnaWindowsFormsCompat>` (cna-cs-samples `4994508`).
- **Not qualified**: interactive desktop Chrome, a hardware GPU, browser audio, other browsers,
  the template's browser head beyond its own CSX-031 run, anything after CNA `ffb82bc0d`.

## 11. Android

- Qualified: all 84 rows **build, run and draw on the x86_64 emulator** (AVD `Medium_Phone`,
  `system-images/android-35/google_apis_playstore/x86_64`, emulator 36.5.11.0, `-no-window`,
  `-no-audio`); 78 end on one Back tap (six need more input on desktop too); touch, pause/resume,
  relaunch exercised. CNA C API via NDK 29.0.14206865 in `cna/cmake-build-android-x86_64`
  (`cna-cs/scripts/Build-AndroidNative.sh`); .NET android workload on .NET 11; generated apps in
  `cna-cs-samples/build-consumer/android/<Row>/`. Run: `scripts/android-requalify.sh` or
  `scripts/android-sample.sh <Row|games/Game> [--then 'input keyevent KEYCODE_BACK']`. Last full pass
  CNA `ffb82bc0d`; evidence `/rv/tmp/cs-samples/android-requal-20261002{,-fix}`.
- Games: 24 of 25 run (Playing in Traffic stops at the refused video); the newest nine on CNA
  `3c72165e6`, including the threaded Enceladus and Racing Game Kit. The emulator must be stopped
  after the runs (`adb -s emulator-5554 emu kill`): a leftover one used ~12 cores.
- **Not qualified**: any physical device, arm64/armv7 (an arm64 CNA build exists only for the C++
  Racing port), audible audio, GPU drivers other than the emulator's.

## 12. Windows / macOS / iOS

Runtime-unqualified. Nothing was run on them in this campaign (the Win10 VM no longer exists).
Buildability or architecture (resolver keeps `.dll`/`.dylib`, iOS `__Internal`) is not a claim.

## 13. Real XNA games outside the gallery (`cna-cs-samples/games/`)

Original `.cs` unmodified in every row; project glue only (`games/<Game>/*.csproj`, which point at
checkouts under `/rv/tmp/xna-games` etc.). Content: shipped XNA build output or built from the
game's own content project by XNA's BuildContent under Wine (`scripts/build-xna-content.sh`).
Web/Android columns: "—" = not tried.

| Game | Linux | Web | Android | Fixes it caused (all with tests, committed) |
|---|---|---|---|---|
| Speedy Blupi | plays | title | runs | see README |
| Rookie Drivers | race screen, music | yes | yes | see README |
| TIE Fighter Forever | menu, battle | yes | yes | — |
| NePlus | plays (Farseer, Mercury, Tiled, Krypton) | yes (after generator fixes) | yes | see README |
| HeliumBiker | waits on its CONNECT screen (Wii Remote only, as on Windows) | no: starts a thread | yes | — |
| Zombie Smashers X | menu (Xbox gamepad only) | yes | yes | — |
| Moto Trial Racer (WP) | menu, race | yes | yes | see README |
| Solitaire (WP) | deals, plays by touch | yes | yes | see README |
| Megaman vs. Zombies | plays, music | yes | yes | — |
| Virulent | intro to tutorial level | yes | yes | — |
| Kosmic Warz (WP) | level by touch | yes | yes | see README |
| Dominó Tropical | deals | yes | yes | — |
| A Princess' Request | first map | yes (after generator fix) | yes | — |
| My Big Head… | level with Box2D | yes | yes | — |
| Playing in Traffic | splash video, menu | no: video | stops at video | see README |
| Resonance | plays (threaded load) | menu only | yes | see README |
| Escape From Enceladus | first room | no: threads | title, save slots | FX-141, CSX-109, CSX-110, CBIND-146 |
| __Defense | menu, wave 1 | menu | menu | CBIND-145, CSX-111, CSX-112 |
| Missile Command | plays | title | title | — |
| Super Mario World (Sprint 4) | level 1 | title menu | title menu | — |
| Zelda clone | first screen | title | title | — |
| Bubble Bound | plays | title (after CBIND-149/150) | title | CBIND-149, CBIND-150 (browser) |
| Spineless (GGJ13) | plays | plays | plays | CBIND-146 |
| Mahjong (Jomata) | deals on click | menu | menu | CBIND-147, CBIND-148 |
| XNA 4.0 Racing Game Kit | menu, a race on the Advanced track (§15) | no: threads | attract mode | CSX-113, CSX-114, FX-142 |

(The first 16 rows' Web/Android results are from CNA `ffb82bc0d`; per-row detail is in
`games/README.md`. "yes" means reaches title or play. "see README": the fixes the first 16 games
caused are listed together in the README paragraph under its table — CSX-094/095/096/099/100/101/
103/105/106/107, CNA FX-140, Task 1120, CBIND-141/142 — without a per-game attribution here.)

Not running, each for a reason outside cna-cs (details and SHAs in `games/README.md`):
- Missing content/libraries/code in the repository: StarWarrior (Artemis API mismatch), Space
  Conquest (missing `ModelConfig.txt`), Diseased Toast (Nuclex.Input), Sleepwalker (older Swf2XNA
  runtime), EvoNet (YamlDotNet + Windows Forms beyond CSX-114), Snails (published source builds on
  no platform), others listed there.
- Windows P/Invoke/Win32: Divine Right (Kernel32).
- Commercial fonts not shipped: Second Realipony (Bookman Old Style, Calibri), Eva Frontier
  (Vrinda), Divine Right (Parchment).
- Dead server: Samurai (AppHarbor).
- Silverlight / Windows Phone UI: Nu, Pogodi!, Drumkit.
- Windows Forms beyond the CSX-114 subset: Mannux, infinecraft, Flux (with System.Drawing) — **not
  re-tried after CSX-114**.
- .NET change, not XNA: Minor Destruction (`BitConverter.GetBytes(sbyte)` ambiguous since .NET 7).

## 14. Mahjong

Runs (cna-cs-samples `347cc68`, games/Mahjong): 850×700 menu over its tile table; a held click on
Play Mahjong deals against three computer players (`/rv/tmp/cs-samples/games-mahjong-play5-*`, 4/4
runs). It found CBIND-147 (ClientBounds X/Y were 0) and CBIND-148 (stale/clamped SDL3 mouse); both
committed with native tests (`GameWindowTests.ClientBoundsCarryTheClientAreasDesktopPosition`,
`Sdl3InputTest.MouseReadsThePointerBesideTheWindowAsXnaDoes` — 63 without, 94 with). Its menu starts
on "Options" when the capture harness moves the window under a still pointer: the game hit-tests the
previous frame's position, and XNA reports the same, so this is not a discrepancy. The harness now
repeats its window move until it holds (`eb005a4`); a quick `click` is too short for a polling game
(use `mousedown`/`mouseup` with a pause).

## 15. XNA 4.0 Racing Game Kit Master (final task of the session) — plays

- Source: `/rv/tmp/XNAGameStudio/Samples/XNA-4-Racing-Game-Kit-master/RacingGameWindows1/RacingGame/RacingGame`
  (`RacingGame.csproj`, XNA 4.0 HiDef, Release defines `TRACE;WINDOWS`); byte-identical to cna-samples
  SAMPLE-152 `xna4-original`. **Unchanged.**
- Content: `/rv/tmp/samples/SAMPLE-152-XNA-4-Racing-Game-Kit-master/evidence/xna4-authentic-build/Debug/Content`
  — its `RacingGameContent.contentproj` built by real XNA 4.0 on the former Win7 VM (339 `.xnb`,
  XACT `.xgs/.xsb/.xwb`, tracks, CombiModels). The same build has an XNA `RacingGame.exe` usable as
  a Wine oracle (not used yet).
- Project: cna-cs-samples `games/RacingGame/RacingGame.csproj` (`2c3058c`), `<CnaWindowsFormsCompat>`.
- Run it by hand **from its own directory** (XNA's `AudioEngine` resolves the game's relative
  `Content\Audio\RacingGameManager.xgs` with `Path.GetFullPath`, i.e. against the working directory —
  XNA IL; started elsewhere it throws `DirectoryNotFoundException`, as on Windows):
  `cd cna-cs-samples/games/RacingGame/bin/Release && CNA_NATIVE_LIBRARY=<cna>/build-probe/modules/c-api/libcna_c_api.so ./RacingGame`.
- Initial state: did not compile (`System.Windows.Forms`).
- Progression and fixes, in order:
  1. Windows Forms (`Form.FromHandle(Window.Handle)` in `BaseGame`'s constructor, `MessageBox` in
     `Program`) → cna-cs **CSX-114** `ff08b93` (integration test
     `WindowsForms_FromHandleFindsTheGamesOwnForm`, unit tests `WindowsFormsCompatTests`).
  2. `ContentLoadException 'Content\models\Cube'` vs `Models/Cube.xnb` → cna-cs **CSX-113**
     `7780921` (`XnaContentPathCaseTests.ADirectoryThatDiffersOnlyInCaseResolvesToo`).
  3. Render loop "Begin cannot be called again" — consequence; the game's own log
     (`~/.local/share/IsolatedStorage/**/Log.txt` under the capture's private HOME) showed
     `io_10_0` link failure on OPENGLES3 → CNA **FX-142** `fb5cb3ba2` (MojoShader patch
     `cmake/patches/mojoshader-6333f74-glsl-sm1-color-input-not-centroid.patch`; native test
     `EasyGLCompiledEffectTest.AuthenticXna4LegacyPassBindsSamplersAndSurvivesClone` failed before,
     passes after). Diagnosis tool: `MESA_GLSL=dump`.
- Current Linux state: loads ~40 s, switches to full screen (its saved default, 1920×1200 on the
  Xvfb), runs its **attract mode** — city and mountain sections flown with the car, shadows, "Press
  START to continue" — with **0 render-loop errors** in its own log. Evidence:
  `/rv/tmp/cs-samples/games-racing-final/` (committed state), `games-racing-4..7/`.
- **Plays** (settled after the first handoff draft): the input that seemed lost after the
  full-screen switch was the harness's 0.3 s key press falling between two polls of a game that takes
  longer than that per frame under llvmpipe (`Input.KeyboardSpaceJustPressed` compares consecutive
  polls). Held 2–3 s, Space opens the main menu in full screen (`games-racing-fullscreen-menu/`) and
  windowed; windowed 1024×768 from a seeded `RacingGameSettings.xml` (`<capture home>/.local/share/
  game/RacingGame/Player1/`, `<Fullscreen>false`, `<ResolutionWidth>1024`, `<ResolutionHeight>768`),
  three more held Space presses and the up arrow start a race on the Advanced track — lap 1/3,
  62 MPH in 2nd gear, HUD, post-processing, 0 render-loop errors (`/rv/tmp/cs-samples/games-racing-race/`;
  xdotool: `keydown space sleep 2 keyup space sleep 4` ×4, then `sleep 45 keydown Up sleep 12`).
  SDL's "Time out elapsed after mode switch … reverting" on Xvfb is harmless. No CNA/CNA.NET change
  was needed for this (cna-cs-samples `409415e`, cna-cs `faf7470`).
- Not done: frame comparison with the C++ port (its references are OPENGL33,
  `cna-samples/racing_milestone*.md`) or with the XNA exe under Wine; a full lap/race end, other
  tracks, sound audibility; browser and Android.
- Known limitation from CSX-114: its form's `Opacity = 0`/`FormBorderStyle.None` are kept, not
  applied, so the loading screen is visible and the window keeps its border.

## 16. Historical blockers retested and obsolete

- CNA-REPORT-001 (resolved in CNA `8c713f1d8`), -002 (was a cna-cs bug, `5f6c212`), -003 (not
  reproduced), -004 (fixed by CBIND-128). `docs/native-behavior-blockers.md` and older
  `plan.md`/`NEXT.md` blockers are historical; `CAMPAIGN.md` CSX-021 re-measured them.
- `CNA.Graphics.Experimental` (post-process chain etc.) — retired natively at ABI 0.30; removed.
- Compiled effects (CNA_EASYGL_COMPILED_EFFECTS=ON) work on the current tree (CSX-022).
- The `io_10_0` GLSL link failures noted mid-session in CnaTests were FX-142's case: after FX-142
  no `EasyGLCompiledEffect*` ctest output mentions `io_10_0` (the 7 remaining failures are §6).
- `.NET 10/8` cannot build CNA for wasm (emscripten 3.1.x libc++ lacks `std::jthread`); .NET 11 can.

## 17. Evidence and artifacts

- Desktop gallery: `/rv/tmp/cs-samples/requal-2026100{1*,2,2b,2c,2d}/` (+ `.log`), each with
  `requalification.md`; per-row `cpp/` captures of the C++ ports.
- Browser: `/rv/tmp/cs-samples/browser-requal-*`; Android: `/rv/tmp/cs-samples/android-requal-*`.
- Games: `/rv/tmp/cs-samples/games-*` (e.g. `games-spineless-play`, `games-mahjong-play5-*`,
  `games-racing-final`); game checkouts `/rv/tmp/xna-games/*`, built content
  `/rv/tmp/xna-games-content/*`.
- XNA reference assemblies: `~/deps/xna40-windows-assemblies` (monodis for IL; never public-sign
  with Microsoft's key). XNA 4.0 Wine prefix `~/.wine-cna-xna40` (content builds).
- GamerServices runner used here (`/home/robertvokac/.claude/jobs/602f242d/tmp/run-gs.sh`, job
  scratch — recreate if gone): `cd cna-cs; export CNA_NATIVE_LIBRARY=$1; iso=<scratch>/gs-xdg;
  rm -rf $iso; export XDG_DATA_HOME=$iso/data XDG_STATE_HOME=$iso/state XDG_CACHE_HOME=$iso/cache
  XDG_CONFIG_HOME=$iso/config; unset DBUS_SESSION_BUS_ADDRESS; dotnet test
  tests/CNA.GamerServices.IntegrationTests -c Release --no-build`.
- Capture: `cna-cs-samples/scripts/capture-sample.sh ../games/<Game> --exe games/<Game>/bin/Release/<Exe>
  --window '<regex>' --out <dir>/x --settle N --display :13x --no-exit-check [--xdotool '...']`
  (create `<dir>/games` first; use a display number nobody else uses — `:128` is requalify's).
- Probe leftovers safe to delete: `cna/build-probe/{mojodisasm-src,mojodisasm,fx141-mojoshader,
  fxabs.exe,d3dx9_43.dll,D3DCompiler_43.dll,cbind140-probe}` (results recorded in FX-141/CBIND-140).

## 18. Toolchains (verified)

Debian 13.6; .NET SDK 8.0.424 (desktop; `/usr/share/dotnet`), .NET 11.0.100-rc.1.26425.128
(`~/deps/dotnet11`, browser/Android); CMake 3.31.6; GCC 14.2.0; ccache 4.11.2 (shared cache);
Mesa 25.0.7 (llvmpipe, GLES 3.2); Android NDK 29.0.14206865 (also 23.1, 25.2 present), emulator
36.5.11.0, AVD `Medium_Phone` API 35 x86_64; Chrome for Testing headless shell 151.0.7922.34.
CNA desktop C API tree `cna/build-probe`: Release, `CNA_GRAPHICS_RENDERER=OPENGLES3`,
`CNA_BUILD_C_API=ON`, `CNA_EASYGL_COMPILED_EFFECTS=ON`. MojoShader patch series applied to the shared
`~/deps/FNA3D/MojoShader` (append new patches at the end of the list in
`cmake/ThirdPartyFNA3D.cmake`; never remove a middle one).

## 19. Remaining work (by value/risk)

Genuine compatibility defects
1. The two remaining OPENGLES3 compiled-effect failures (§6): vertex-texture LOD bias on GLES 3 and
   the `FlippedSource…` abort.
2. CSX-114 completeness: apply `FormBorderStyle.None` through CNA's existing
   `cna_game_window_set_is_borderless_ext` and `MessageBox` through `cna_message_box_show_ext`
   (new cna-cs imports; update import inventory/abi-verify).
Qualification
3. Racing Game Kit: compare with the C++ port / the XNA exe under Wine; finish a race.
4. Multithreaded WebAssembly (§20) so threaded games run in a browser.
5. Re-try Mannux/infinecraft/Flux now that CSX-114 exists (only if they need nothing beyond it).
Platform
6. Interactive Chrome with a GPU, browser audio; a physical Android device / arm64.
Hardening
7. The `-j8` C smoke flakiness (`RuntimeComponentsSmoke` stage 8 is a timing assumption).
8. Regenerate and commit `cna-cs-samples/gallery-inventory.md` (stale ⬜ states).
External / not actionable
9. Yacht (owner decision), PerformanceUtility (needs its C++ port first), games blocked by missing
    content/fonts/servers.

# Exact next action

Make threaded XNA games run in a browser: a multithreaded WebAssembly build of CNA and CNA.NET.
Three real games stop there today on `new Thread(...).Start()` (HeliumBiker, Escape From Enceladus,
the Racing Game Kit; Resonance loads its levels on a thread too), and all of them run on the desktop
and on Android. Steps: (1) check what .NET 11's browser runtime needs for threads
(`<WasmEnableThreads>true</WasmEnableThreads>`, a `-pthread` Emscripten link, COOP/COEP headers so
`SharedArrayBuffer` exists — the headless runner `cna-cs/scripts/Run-BrowserPage.mjs` must serve them);
(2) build CNA's WebGL2 C API with `-pthread` in a separate build directory only if
`cmake-build-webgl2` cannot carry both (prefer a configure option on the existing tree; never a new
ad-hoc directory name), staged by `cna-cs/scripts/Build-BrowserNative.sh`; (3) verify CNA's
game-thread rule still holds — WebGL contexts live on one thread and CBIND-141 already serves other
threads' calls on the game thread; (4) run `scripts/browser-sample.sh games/RacingGame --seconds 180`
and `games/Enceladus`, then the full `scripts/browser-requalify.sh` to show the threaded build
breaks none of the 84 rows. Record it as a CSX task in `cna-cs/CAMPAIGN.md`.

## Appendix — campaign commits (oldest first)

### cna (campaign commits on `next`, first parent after `8b55f7f4d`; the last one adds this handoff)

```
bdf239360 fix(CBIND-128): a component's C handlers run inside the game's callback scope
6e0de68e8 fix(CBIND-129): run a component's C initialize handler before its base loads content
abd005ab8 fix(CBIND-131): EasyGL's context lease state survives exit-time static destruction
402c1aaa9 feat(CBIND-130): C ABI 0.36.0 reads a received packet out of a packet reader
e9dd5d879 feat(CBIND-132): C ABI 0.37.0 reports the canonical exception behind a failure
e77ae09ed fix(CBIND-133): GraphicsResource.hpp defines the context lease its template holds
f445fef8c feat(CBIND-134): C ABI 0.38.0 lets a host drive a run one frame at a time
1e7c4d323 fix(CBIND-135): build the static C API archive under Emscripten
552a0d8cb fix(CBIND-136): CNA's C API builds for Android with the NDK alone
747b02493 fix(CBIND-137): Android's Back press is seen once, as Escape and as player one's Back
80572aa00 fix(CBIND-138): a portrait game runs in portrait on Android
1dc4ce06a fix(CBIND-139): models draw whole on the Android emulator
9976f4909 fix(CBIND-140): a relative content root is the title's, not the working directory
9ea1aef4b test(CBIND-140): ContentSmoke expects the title-rooted asset path
3fe003007 test(content): Unicode content-root fixtures no longer collide under parallel ctest
123aba816 test(XNAP-71): the object-referenced Video test no longer deletes its committed fixture
0a57ccad2 feat(CBIND-141): C ABI 0.39.0 runs another thread's calls on the game thread
1c2923efd fix(FX-140): a Shader Model 3 point-size output takes a scalar write on GLSL
7661f6981 fix(Task 1120): a stock effect in SpriteBatch.Begin places the sprites by its matrices
fad9fabe0 fix(CBIND-142): a video frame texture belongs to the player's game
ffb82bc0d fix(CBIND-143): an avatar loads in a single-threaded browser build
a71cc2415 fix(CBIND-144): a game whose graphics device cannot be created throws NoSuitableGraphicsDeviceException
46231e857 fix(FX-141): Shader Model 3 programs may mix absolute and ordinary float-constant reads
22b30b30e feat(CBIND-145): C ABI 0.40.0 lets a game read the graphics adapters before its first callback
51d9c84cc fix(CBIND-146): an XNA-built sound's content sample rate loads, as XNA's content reader takes it
e6f9d5384 fix(CBIND-147): GameWindow.ClientBounds carries the client area's desktop position, as XNA's does
6e273cd89 fix(CBIND-148): the SDL3 mouse reads the pointer where it is, as XNA's Mouse.GetState does
836b796ae fix(CBIND-148): the desktop-pointer test stays within a recorded SDL budget
fb5cb3ba2 fix(FX-142): a Shader Model 1 pixel shader's colour input stays a plain varying at link time
809f3004f docs: handoff for the CNA C# compatibility campaign (2026-10-01 to 2026-10-02)
d9d599263 fix(FX-143): a GLSL ES 3 shader may declare a volume sampler
9265c6ca9 docs: handoff updated after FX-143 and the Racing Game Kit's race
d459f42aa fix(CBIND-149): EasyGL applies the multisample mask only where the context has it
3c72165e6 fix(CBIND-150): EasyGL swizzles a one- or two-channel render target only where the profile can
(+ this handoff commit on top of 3c72165e6)
```

### cna-cs (76 commits)

```
9e30519 feat(CSX-010..014): migrate the binding from CNA C ABI 0.21.0 to 0.35.0
741e441 fix(CSX-082): a callback's exception unwinds out of the call that ran it
5f6c212 fix(CSX-021): ModelMesh.Draw no longer disposes the effect's own technique
6bfb9ab docs(CSX-021): re-measure the native blocker register against ABI 0.35.0
7c179f5 fix(CSX-030): package the installed CNA C API component, not a build-tree library
fbff5e7 feat(CSX-040): bind the GamerServices and Net C ABI, proven against the headers
f103251 feat(CSX-041): XNA GamerServices and Guide over CNA's native gamer services
62fba2d feat(CSX-042): XNA AvatarDescription, AvatarAnimation and AvatarRenderer over CNA's avatars
4f02b8f feat(CSX-044): admit CNA C ABI 0.36.0 and import cna_packet_reader_copy_data_ext
b0fbab8 fix(CSX-041): release each gamer handle through its own kind's native route
a3656c6 feat(CSX-043): XNA Microsoft.Xna.Framework.Net over CNA's network sessions
a07f385 feat(CSX-045): CNA.PhoneCompat, an opt-in assembly for the Windows Phone device API
0c18dca fix(CSX-045): include cna_accelerometer_set_started_for_tests_ext in the ABI fixture symbols
e069aee fix(CSX-081): the default GraphicsProfile comes from the project's XnaProfile, as in XNA
ac63974 feat(CSX-080): admit CNA C ABI 0.37.0 and import the canonical-exception queries
ce1ddf8 feat(CSX-080): re-raise native failures as the exception the XNA call throws
b48db04 fix(CSX-083): the device's state cache follows what native SpriteBatch applies
496858b fix(CSX-084): SIGTERM and SIGINT end a running game through its own exit
626cdde docs(CSX-050): the checked-in samples are requalified
febc0d6 fix(CSX-085): hold CNA handles as the 64-bit values they are
13f4aa9 feat(CSX-060): the binding runs on WebAssembly
ba65cd2 feat(CSX-061): admit CNA C ABI 0.38.0 and import the host-driven run
6f4fc3d fix(CSX-086): deliver the game lifecycle in XNA's order, Disposed included
1c0b649 feat(CSX-061): Game.Run runs in a browser
274be55 feat(CSX-062): build, link and run a CNA.NET game in a browser
5209da0 feat(CSX-063): root CNA.NET against trimming in a browser build
8fcd961 feat(CSX-070): an unchanged XNA game runs on Android
ec7d3cb fix(CSX-087): managed content paths resolve against the title, not the working directory
399fcc8 fix(CSX-088): components initialize inside base.Initialize(), content included, once
bf51b4f fix(CSX-089): a Model tagged with the game's own type loads through the game's reader
1312e24 feat(CSX-071): every checked-in row runs on the Android emulator
64c5095 fix(CSX-090): base.Update and base.Draw run the components at the call
445a137 test(CSX-089): the unsupported-reader test expects the typed refusal
6f64e5c fix(CSX-091): Color.Transparent is XNA 4.0's transparent black
63e36fa fix(CSX-092): the managed model path reads every stock effect XNA writes into a model
590d007 fix(CSX-093): a model's tag holds XNA's types
6fafacc fix(CSX-094): a Windows Phone title's IsFullScreen stays in the game on a desktop
eb2f2ee feat(CSX-095): a Windows Phone title's player one is the phone, with Back on Escape
a6e0c50 fix(CSX-096): XNA's file-taking APIs read a path as Windows did
9360a28 feat(CSX-097): PhoneApplicationService for phone games off the phone
c68a246 feat(CSX-098): a Windows Phone title off a phone gets the mouse as a finger
c72ed71 test(CSX-098): the native import tripwire counts the touch bridge's two routes
28671a5 feat(CSX-100): a game's worker thread loads content and creates resources, as XNA allowed
9d3be89 test(CSX-098): the native ABI fixtures stub the touch bridge's two routes
60668f0 feat(CSX-101): admit CNA C ABI 0.39.0 and run a loading thread's remaining calls on the game thread
4fc7dfb feat(CSX-099): a library compiled against XNA 4.0 runs unchanged on CNA.NET
11e5bd3 fix(CSX-102): a content reader is found by its assembly's simple name
61b1068 feat(CSX-103): PhoneApplicationService.StartupMode -- a desktop process is always launched
a33d674 docs(CSX-104): record the real-games corpus in the campaign plan
82cd310 fix(CSX-105): a song plays from the converted file beside the .wma its .xnb names
de58956 feat(CSX-106): ActivatedEventArgs.IsApplicationInstancePreserved
880b6bc test(CSX-107): a stock effect in SpriteBatch.Begin places the sprites in 3D
66a769e docs(CSX-104): the real-games corpus at sixteen games
7ae5f08 feat(CSX-108): System.IO.IsolatedStorage in a browser build
bb48a05 fix(CSX-104): a browser capture holds a canvas as large as 1920x1080
fb21659 docs(CSX-063,CSX-071): the browser and Android corpora at 84 rows on CNA ffb82bc0d
11169d2 docs(CSX-104): the real games on Android, 15 of 16
189af32 docs(CSX-050): the desktop corpus on CNA ffb82bc0d, 84/84 and unchanged from its baseline
3c8e556 feat(CSX-031): CNA.Browser.targets serves any browser project, not only generated ones
024f2b2 docs(CSX-031): the platform layout, and the template in a browser and on Android
cde5fe1 docs(CSX-104): ten XNA codebases that cannot run here compile with no XNA API missing
f31f921 fix(CSX-080): new Game() without a usable graphics device throws NoSuitableGraphicsDeviceException
c01c7f9 docs(CSX-080): done for every boundary the corpus catches around
2f02e99 fix(CSX-109): a storage device several worker threads share works from each of them
b4bedce feat(CSX-110): List<T>.ForEach as the .NET Framework 4.0 that XNA games target ran it
f317c07 docs(CSX-104): Escape From Enceladus is the seventeenth real game running; Snails recorded
d96cc5c feat(CSX-111): admit CNA C ABI 0.40.0; the graphics adapters answer in a game's constructor
1326161 fix(CSX-112): a graphics device not yet created is null, not an exception, as in XNA
bc7674b docs(CSX-104): 22 real games run; 19 recorded with the reason they do not
827128a docs(CSX-104): Spineless and Mahjong run; CNA CBIND-146/147/148 and the desktop requalification recorded
7780921 fix(CSX-113): a content asset's directories resolve ignoring case, as CNA's native loader does
ff08b93 feat(CSX-114): CNA.WindowsFormsCompat, the Windows Forms an XNA game uses on its own window
b999ff1 docs(CSX-104): the Racing Game Kit to its attract mode, CSX-113/114 and CNA FX-142 recorded
faf7470 docs(CSX-104): the Racing Game Kit races; its lost input was the capture harness's short key press
324acac docs: CNA FX-143 (volume samplers on GLSL ES 3) recorded
74b74f7 docs(CSX-104): the nine newest games in a browser and on Android, CNA CBIND-149/150 recorded
```

### cna-cs-samples (82 commits)

```
26d06c1 chore(CSX-052): point the samples at the current tree and the current rules
4ebd96c feat(CSX-051): derive the C# corpus from the published gallery
2d62243 build(CSX-050): each sample declares its original XnaProfile
e22d67b feat(CSX-050): scripts/requalify.sh measures every row against its C++ port
b0c85bf feat(CSSAMPLE-016, CSSAMPLE-021): phone-only samples run through a generated host
bd9d069 docs(CSX-050): every checked-in row requalified; seven rows reach done
8bd3bdb feat(CSX-062): run a sample in a browser without touching it
43456e1 feat(CSX-063): every checked-in sample runs in a browser
3090734 feat(CSX-070): run one sample on a headless Android emulator
599786f feat(CSSAMPLE-084): AccelerometerSample runs, first frame identical to the original
dbd3421 feat(CSSAMPLE-050): SimpleAnimation runs unmodified
f6da136 feat(CSSAMPLE-076): SplitScreen runs unmodified
b670f9e feat(CSSAMPLE-042): ShatterEffect runs unmodified, its effect matching the C++ port
0b8446e feat(CSSAMPLE-052): CustomModelClass runs unmodified
19d7744 feat(CSSAMPLE-012, -030, -033, -038): four more rows run unmodified
b1fc761 feat(CSSAMPLE-031, -034, -041, -053): four effect rows run unmodified
94cfdc5 feat(CSSAMPLE-039, -040, -058, -099): four more rows run unmodified
87df037 feat(CSX-071): every checked-in row runs on the Android emulator
1a9bff1 fix(tooling): captures run with a private home and no session bus
efd9d87 feat(CSSAMPLE-003, -032, -035, -036, -037, -046, -047, -049, -054, -057, -060, -074): twelve more rows run unmodified
e5aead8 fix(CSSAMPLE-002): Primitives3D carries XNA's own compiled font again
0da9450 feat(CSSAMPLE-043, -044, -045, -048, -051, -055, -056, -073): eight more rows run unmodified
adfc22c fix(tooling): requalify captures the port's game and checks phone rows' exit
5fb9bda feat(CSSAMPLE-005, -013, -017, -072, -077, -081, -082): seven more rows run unmodified
8129176 feat(CSSAMPLE-061, -063, -067, -069): the starter kits run unmodified
dbd3f8e docs(plan): seven rows newly eligible upstream, SAMPLE-068 cancelled by the owner
766b19b fix(tooling): captures play no sound; a project at the upstream root gets its own tree
63fae89 feat(CSSAMPLE-014, -062, -066, -070, -091): five of the newly eligible games run unmodified
159f892 feat(CSSAMPLE-065): NinjAcademy runs unmodified
7249864 docs(CSSAMPLE-071): Yacht waits on an owner decision about Silverlight's WCF client
7f23398 docs(CSSAMPLE-077): DynamicMenu's tabs answer the mouse (CNA.NET CSX-098)
c3f3935 feat(CSSAMPLE-games): build an XNA 4.0 content project with XNA's own BuildContent under Wine
b2d1115 feat(CSSAMPLE-games): four real XNA 4.0 games on CNA.NET, unchanged
33ac846 feat(CSSAMPLE-games): Microsoft's XNA Solitaire for Windows Phone, unchanged
18d609b feat(CSSAMPLE-games): Microsoft's Moto Trial Racer for Windows Phone, unchanged
fbdc718 feat(CSSAMPLE-games): a content project's prebuilt pipeline references reach BuildContent
6a7ae1a fix(CSSAMPLE-games): pipeline references that are Game Studio's own, or named twice, are left out
0be0d9c feat(CSSAMPLE-games): NePlus with five prebuilt XNA libraries; three games recorded as not running
7f3f0fe feat(CSSAMPLE-games): HeliumBiker; a game project's sources are found as Windows found them
2af04c8 docs(CSSAMPLE-games): Minor Destruction needs Windows Forms
23e1d1e feat(CSSAMPLE-games): Zombie Smashers X with the XNA build output its repository ships
167eb29 docs(CSSAMPLE-games): three more games recorded with the reason outside XNA
e3cef2a docs(CSX-104): the XNA Racing Game kit needs Windows Forms for its window
83edffb feat(CSX-104): XNASidescroller, and the games' music through converted songs
fe69c4a feat(CSX-104): Virulent, an Xbox Live Indie game's work-in-progress build
9a61e61 feat(CSX-104): Kosmic Warz, a Windows Phone 3D shooter with DPSF particles
80ca7e3 feat(CSX-104): Domino Tropical, unchanged from the content it ships
e4348f6 feat(CSX-104): A Princess' Request, a Ludum Dare 30 game
ec5ecaa feat(CSX-104): a pipeline extension compiles against the references its project names
2bea538 fix(CSX-104): a capture reads the window's geometry when it captures
4f1ade7 feat(CSX-104): XNA content with videos and an XACT project's absolute wave paths
8c74618 feat(CSX-104): two Flash-authored Swf2XNA games
1c9e622 docs(CSX-104): Microsoft's Drumkit XNA needs the Windows Phone SDK's font
5df68b1 fix(CSX-104): a song stand-in's .xnb in XNA's SongWriter layout
3e7e377 feat(CSX-104): a real game in the browser, and the files a build copies beside a game
dc336b6 fix(CSX-104): a game whose assembly name holds an apostrophe runs in the browser
1dcebf3 feat(CSX-104): a Windows-spelled path a game opens, on the desktop and in a browser
db4cf17 fix(CSX-104): Android: files beside a game, a nested entry point, Windows-spelled paths
edb6dbe feat(CSX-104): a real game on Android
4361796 fix(CSX-104): Android: a game's own Resource class, an apostrophe in its name
39f4617 docs(CSX-104): the real games in a browser (14 of 16) and on Android (15 of 16)
f64cb46 refactor(CSX-031): CNA.BrowserCompat comes with the browser targets, not a separate build
41dc7e2 feat: Escape From Enceladus runs on CNA.NET; Snails recorded as not building anywhere
fcb8e4b feat: __Defense (gamealgorithms/defense) plays on CNA.NET
3522ce4 feat: Missile Command (the-saif-ahmad/MissileCommand) plays on CNA.NET unchanged
ff39585 build-xna-content: list, not build, a source file the game's repository does not ship
4e0b2f6 feat: Super Mario World demo (buttsj/c-sharp-mario) plays on CNA.NET unchanged
f337626 feat: the Zelda clone (edgiardina/Zelda) plays on CNA.NET unchanged
23747bf build-xna-content: copy a content project's Content items too, as MSBuild does
a11c569 feat: Bubble Bound (zfedoran/bubblebound), a 3D undersea game, plays on CNA.NET unchanged
d5b4dd3 docs: three more XNA games recorded as not running here, with the reason
3cc61a7 docs: Eva Frontier lacks only its Vrinda font
e15ca91 docs: Space Conquest and Divine Right recorded as not running here, with the reason
64cffff docs: StarWarrior recorded as not building, with the reason
19f1a1d games(CSX-104): Spineless, a Global Game Jam 2013 game, runs unchanged
eb005a4 scripts: capture-sample repeats its window move until it holds
347cc68 games(CSX-104): Jomata's Mahjong, an XNA client with its own rules library, runs unchanged
2c3058c games(CSX-104): the XNA 4.0 Racing Game Kit runs unchanged to its attract mode
39e6fef docs(CSX-104): the Racing Game Kit leaves the not-running list; CSX-114 gave it its Windows Forms
409415e docs(CSX-104): the Racing Game Kit plays -- its menu and a race on the Advanced track
4994508 scripts: the browser and Android generators carry a game's opt-in Windows Forms and its NuGet packages
671df07 docs(CSX-104): the nine newest games in a browser and on Android
```

### cna-cs-template (2 commits)

```
ae6b906 fix(CSX-030): verify generated consumers outside /tmp and off the desktop
2a76388 feat(CSX-031): the game in a browser and on Android, from the template
```
