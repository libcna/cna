# Handoff — CNA C# compatibility campaign (cna-cs / CNA.NET)

Written 2026-10-03 for a future agent starting with no context. It replaces the earlier
2026-10-02/03 handoff drafts. The session behind it ran from 2026-10-01 to 2026-10-03. On the
owner's word, everything was committed and **pushed** together with this file (§3). There are no
uncommitted changes anywhere. The only work that exists locally and nowhere else is two
experimental side branches (`zelda-oracle-forms` in cna-cs, `zelda-oracle` in cna-cs-samples;
§14).

## 0. Read this first

- **What this is.** CNA is a C++23 reimplementation of the XNA 4.0 programming model with a C ABI
  (this repository). **cna-cs** (product name *CNA.NET*) is the managed side. It exposes the real
  `Microsoft.Xna.Framework.*` API over that C ABI, so unchanged XNA Game Studio 4.0 C# games build
  and run on CNA, playing the role FNA and MonoGame play for theirs.
- **The goal.** As much existing XNA 4.0 C# software as possible should run **unchanged**: no edits
  to Microsoft or game `.cs` files, and official XNA-built content. Linux desktop first, then
  browser (WebAssembly) and Android, proven by real runs. Windows, macOS and iOS count only with
  real evidence.
- **The loop.** The campaign works one program at a time:
  1. Take a real XNA program and run it.
  2. When it fails, reproduce and minimize the failure.
  3. Find the layer that owns the defect (native CNA, the C ABI, cna-cs, the build glue, or the
     game itself).
  4. Fix the root cause there, with a regression test at the lowest layer that can see it.
  5. Rerun the program, record the evidence, make one commit per task, and continue.
- **Where the truth lives.**
  - `cna-cs/CAMPAIGN.md`: task table CSX-001..142, with evidence in each row, plus a dated ledger.
  - `cna-cs-samples/games/README.md`: every real game, its source commit, how its content was built,
    what was measured, and the blocked list.
  - `cna/plans/plan_binding.md` (CBIND-*) and `cna/plans/plan_fx.md` (FX-*): native fixes.
  - This file: the overview and how to continue.
- **Next action:** §20.

## 1. Rules (owner instructions and machine rules — keep all of them)

- **Push only on the owner's explicit word.** Commit locally after every finished task: one task
  per commit, staging explicit paths (never `git add -A`/`.`), with the task ID in the message (e.g.
  `fix(CSX-142): ...`). Commit messages carry **no** AI attribution lines.
- Never modify Microsoft sample or game `.cs` sources. Adapt only project glue, hosts and the opt-in
  compatibility assemblies. Gallery rows are hash-verified.
- Content must be official XNA pipeline output: the content a game ships, or content built from its
  own `.contentproj` by XNA's BuildContent under Wine. A **labelled stand-in** is allowed only for
  assets Wine cannot build (WMA/MP3 songs and sounds, WMV videos: Windows Media Format).
- No managed workaround for a native defect. Fix it in CNA with a native test.
- ABI admission is an exact reviewed version list (§6), never "this version or newer".
- Never claim a platform from a successful compile. Only a real run counts.
- Do not weaken tests. Preserve unrelated changes. Do not declare the campaign complete.
- **Never run windows, tests or games on the owner's live display** (`DISPLAY=:0`,
  `WAYLAND_DISPLAY=wayland-0`). Use one of:
  - `cna/tools/platform/run_gpu_tests_private.sh <build-dir> [ctest args]` (or `--exec <cmd>`): a
    headless Weston plus a private Xwayland.
  - A private Xvfb on a display number nobody else uses (this session used `:166`; `:128` is
    requalify's).
  - Always `unset WAYLAND_DISPLAY`.
- GamerServices and storage tests run with an isolated `XDG_*` and **no**
  `DBUS_SESSION_BUS_ADDRESS` (otherwise they reach the owner's keyring).
- Builds:
  - Shared ccache only: `CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv`.
  - Use at most `-j8`.
  - Never build in `/tmp` or a session scratchpad.
  - Only the closed list of build directories (`build/`, `build-probe/`, `build-consumer/`, the
    sanitizer trees, and CNA's `cmake-build-<variant>/` trees). Reuse them; don't reconfigure
    without a reason.
  - Third-party dependencies live in `~/deps/`.
  - Build only the targets you need: a full `cmake-build-vulkan` rebuild is ~83 GB of writes.
- At most **3 concurrent subagents**.
- Stop the Android emulator after every run (`adb -s emulator-5554 emu kill`). A leftover one burns
  ~12 cores.
- Never public-sign anything with Microsoft's key. Never send the owner's e-mail address anywhere.
- Use explicit paths with `rm -rf` (or `"${var:?}"`). Never edit a script while a background job is
  executing it.
- The owner writes in Czech; answer in Czech unless asked otherwise. Use they/them for the owner.
- The .NET 11 browser thread-start fault (§11) is an upstream .NET problem. The owner said
  "Neřešit" (don't pursue it).
- Owner direction since 2026-09-28: less AI-driven breadth, fewer special cases, less Markdown —
  record one concise row per fix instead of essays.

## 2. Executive status (2026-10-03)

**Validated**
- cna-cs binds CNA C ABI **0.44.0**, exactly. All managed suites pass (§9). `api-compat` showed 0
  diagnostics against XNA 4.0's Windows runtime metadata (256/256 types) at its last run on
  2026-10-02.
- **Gallery** (Microsoft's XNA samples, `samples.libcna.com`): 82 of 84 entries run on the Linux
  desktop from unchanged source (Yacht and PerformanceUtility are excluded, §10). All 84 manifest
  rows build and run in headless Chromium and on the Android x86_64 emulator.
- **Real games and programs** outside the gallery: `cna-cs-samples/games/README.md` has **84
  rows**, several of them collections. The collections include:
  - 48 samples of Petzold's *Programming Windows Phone 7*;
  - 19 samples of Apress' *Windows Phone 7 Game Development*;
  - 14 samples of *2D Graphics Programming for Games*;
  - 8 Windows Phone Pong demos;
  - 7 of willcraftia's TestXna demos;
  - the 4 games of *XNA 4.0 Game Development by Example*.

  All run unchanged on the Linux desktop. Among them is the XNA 4.0 Racing Game Kit, played into a
  race (§16). Most were also run in the browser and on Android (§11, §12).
- Every native defect those programs exposed was fixed in CNA with a test (§7). Every managed one
  was fixed in cna-cs with a test (§8).

**Implemented but incompletely qualified**
- Browser: tested only in **headless** Chromium on SwiftShader WebGL2. Not tried: interactive
  Chrome, a hardware GPU, browser audio.
- Android: tested only on the **x86_64 emulator**. Not tried: a physical device, ARM, audible
  audio.
- Multithreaded browser bundles work, with two limits:
  - WebGL calls are proxied to the page thread, so they are slow (the Racing Game Kit runs at ~0.65
    fps).
  - The .NET 11 thread-start fault caps how many threads a game can start.
- `BinaryFormatter` works on desktop and, through the supported out-of-band compatibility package,
  in the qualified browser and Android emulator runs (CSX-141/144; §19).
- CNA.WindowsFormsCompat is a small subset: `Form.FromHandle`, `FormBorderStyle` and `MessageBox`
  (`MessageBox` uses CNA's native dialog while a game is alive, with stderr as the no-service or
  pre-game fallback). `Opacity` is kept but not applied because CNA exposes no window-opacity
  operation.

**Open**: §19.

## 3. Repositories and state

All under `/rv/data/development/github.com/libcna/`, remote `git@github-libcna:libcna/<repo>`.

| Repo | Branch | HEAD at handoff | Notes |
|---|---|---|---|
| cna | `next` | the `docs: handoff ...` commit that adds this file (parent `25d709ab0`) | 56 first-parent campaign commits after `8b55f7f4d`; branch `work` merged at `46d55fa26` |
| sharp-runtime | `next` | `db86514c` | untouched by the campaign |
| cna-cs | `develop` | `c5e6950` | 122 campaign commits since `9e30519^`; side branch `zelda-oracle-forms` (local only) |
| cna-cs-template | `develop` | `2a76388` | CSX-030/031 |
| cna-cs-samples | `develop` | `c1e673d` | 137 campaign commits since `26d06c1^`; side branch `zelda-oracle` (local only) |
| samples.libcna.com | `main` | `8e48825` | untouched |

To list commits: `git -C cna log --oneline --first-parent 8b55f7f4d..next`,
`git -C cna-cs log --oneline 9e30519^..develop`, and
`git -C cna-cs-samples log --oneline 26d06c1^..develop`. Build trees and probe leftovers are
gitignored or under `/rv/tmp`.

## 4. Architecture in brief

- **CNA (native)**: XNA API in C++ (`modules/*`). The C ABI lives in `modules/c-api`
  (`include/CNA/C/*.h`, ~3,210 exports). The desktop tree for C# work is `cna/build-probe`:
  Release, `CNA_GRAPHICS_RENDERER=OPENGLES3`, `CNA_BUILD_C_API=ON`,
  `CNA_EASYGL_COMPILED_EFFECTS=ON`. Its library is
  `build-probe/modules/c-api/libcna_c_api.so`, and managed code finds it through
  `CNA_NATIVE_LIBRARY=<path>`. Compiled XNA effects (`.fxb`/`.xnb`) go through MojoShader. Its
  patch series is `cmake/patches/mojoshader-*.patch`, applied idempotently to the shared
  `~/deps/FNA3D/MojoShader`. Append new patches at the end of the list in
  `cmake/ThirdPartyFNA3D.cmake` and never remove a middle one.
- **cna-cs** (`../cna-cs`):
  - `src/CNA.Interop`: P/Invoke, handles, ABI policy.
  - `src/CNA.Framework`: CNA-shaped managed runtime (`CNA.Game`, devices, content).
  - `src/CNA.XnaCompat`: the strict `Microsoft.Xna.Framework.*` facade, with a `BackendGame` per
    XNA `Game`. Its `build/CNA.XnaCompat.targets` holds per-game build behaviour, such as XNA
    reference retargeting, BinaryFormatter and the interceptor generators in
    `src/CNA.XnaCompat.Generators`.
  - Opt-in assemblies: `CNA.PhoneCompat` (Windows Phone device APIs), `CNA.BrowserCompat`
    (IsolatedStorage in a browser) and `CNA.WindowsFormsCompat`.
  - `src/XnaAssemblies`: XNA-named forwarders, so libraries compiled against XNA 4.0 load.
  - `eng/browser`, `eng/android`: the platform heads.
  - Tests: `tests/CNA.Framework.Tests`, `tests/CNA.XnaCompat.Tests`, `tests/CNA.Integration.Tests`
    (real native library), `tests/CNA.GamerServices.IntegrationTests`.
- **cna-cs-samples** (`../cna-cs-samples`):
  - `samples/`: one row per gallery sample, pointing at unchanged Microsoft source.
  - `games/`: real games. Each `games/<Game>/<Game>.csproj` names the game's own project in
    `<GameProject>`, and `games/Directory.Build.targets` compiles exactly that project's `Compile`
    items.
  - `scripts/`: content builds, capture, requalification, browser and Android generators.
- **cna-cs-template**: the `dotnet new` template, with browser and Android heads.

## 5. How to work (recipes that are not obvious)

**Run a game on a private display and capture it.** This session used a small script in the job
scratch (gone with the job; recreate it):

```bash
# runcap.sh <bin-dir> <exe> <name> [wait]: run on Xvfb :166, screenshot its window
export DISPLAY=:166 CNA_NATIVE_LIBRARY=/rv/data/development/github.com/libcna/cna/build-probe/modules/c-api/libcna_c_api.so SDL_AUDIODRIVER=dummy
unset WAYLAND_DISPLAY; cd "$1"
(timeout $((wait+6)) "./$2" > "$T/rc-$3.log" 2>&1) & sleep "$wait"
wid=$(xdotool search --pid $(pgrep -n -f "^./$2") | tail -1)
eval "$(xdotool getwindowgeometry --shell "$wid")"
import -window root -crop "${WIDTH}x${HEIGHT}+${X}+${Y}" +repage "$T/rc-$3.png"
```

Start the Xvfb once with `Xvfb :166 -screen 0 1920x1200x24 &`. For key input, use `xdotool keydown
--window $wid <key>; sleep 0.25; xdotool keyup ...`, because a quick `key` or `click` is shorter
than a polling game's frame. The committed equivalent is `cna-cs-samples/scripts/capture-sample.sh`.
To capture audio, set `SDL_AUDIODRIVER=disk SDL_AUDIO_DISK_OUTPUT_FILE=<file>`. To get a managed
stack from a hung game, use `~/.dotnet/tools/dotnet-stack report -p <pid>` (installed 2026-10-03).

**Add a real game:**
1. Find candidates with `gh search code 'XnaFrameworkVersion>v4.0<' --extension csproj` (also
   `'XnaPlatform>Windows< XnaProfile>HiDef<'`). Clone into `/rv/tmp/xna-games/<owner>_<repo>`. Use
   a commit whose `.csproj` still matches its sources (LilyPath and SharpMik needed older commits).
2. Triage. These block a game: Windows Forms beyond CSX-114, Win32 P/Invoke, content, libraries or
   code missing from the repository, Windows fonts the repository doesn't ship (Calibri, Consolas,
   Lucida Console, …), WPF, XNA 3.x, MonoGame.
3. Build the content if none is shipped:

   ```bash
   scripts/build-xna-content.sh --project <x.contentproj> --out /rv/tmp/xna-games-content/<key>/Content --profile Reach|HiDef
   ```

   Options: `--font <ttf the repo ships>`, `--extension <pipeline csproj>` (in dependency order),
   `--song-standins`, `--sound-standins`, `--video-standins`, `--official SRC:XNB`. The XNA Wine
   prefix is `~/.wine-cna-xna40`. An extension that Mono's mcs refuses is compiled with the
   prefix's Microsoft csc automatically.
4. Add `games/<Game>/<Game>.csproj`, modelled on `games/GearsVge/GearsVge.csproj`. Set
   `XnaProfile`, `AssemblyName`, `RootNamespace`, `EnableDefaultCompileItems=false`,
   `CnaSampleDefineConstants` (the original configuration's defines; `WINDOWS` by default,
   `WINDOWS_PHONE` for `CnaPhoneGame` titles), `GameRoot`, `GameContent` and `GameProject`.
   - Link the content as `<None Include="$(GameContent)/**/*" LinkBase="Content" .../>`. Use the
     content project's `ContentRootDirectory`; when it has none, use the project's own name, as
     XNA does.
   - Each library the game references gets its own glue project beside it.
   - NuGet replaces .NET Framework-only pieces, for example
     System.Configuration.ConfigurationManager for `ApplicationSettingsBase`.
   - Opt-ins: `<CnaPhoneGame>Ns.Game1</CnaPhoneGame>` (phone titles), `<CnaPhoneCompat>`,
     `<CnaWindowsFormsCompat>`.
5. Build with `dotnet build -c Release games/<Game>/<Game>.csproj`, run it, then fix what breaks
   in the right layer.
6. Record a row in `games/README.md` (source and commit, content, what was measured, with date and
   versions), and the blocked list for failures outside cna-cs. Extend `CAMPAIGN.md` CSX-104's
   evidence and add a CSX row for each fix.
7. Browser: `scripts/browser-sample.sh games/<Game> [--threads]` (headless Chromium; output in
   `/rv/tmp/cs-samples/browser*/`). Android: `scripts/android-sample.sh games/<Game>` (emulator; stop
   it afterwards). Rebuild the native archives first if CNA changed:
   `cna-cs/scripts/Build-BrowserNative.sh [--threads]` and `cna-cs/scripts/Build-AndroidNative.sh`.

**Compare with real XNA.** An XNA reference build runs under Wine:
- Compile the game with `csc` from `~/.wine-cna-xna40`'s .NET 4.
- Copy in the XNA references from its `GAC_32`/`GAC_MSIL`.
- Embed `profile.txt` as `Microsoft.Xna.Framework.RuntimeProfile`.
- Run under the private Xvfb.

TerrainDemo, LiSPSM, Asteria's lighting demo and Thieves Like Us were compared this way.

**Read XNA behaviour from IL.** "Where FNA and XNA disagree, XNA wins." Use
`ikdasm ~/.wine-cna-xna40/drive_c/windows/Microsoft.NET/assembly/GAC_32/<asm>/<ver>/<asm>.dll`
(monodis aborts on some assemblies), or `~/deps/xna40-windows-assemblies`. Name the IL in the fix's
comment. FNA's source is at `/rv/data/library/github.com/FNA-XNA/FNA`.

**Run the managed tests:** see §9.

## 6. CNA C ABI

- Current version: **0.44.0** (`modules/c-api/include/CNA/C/abi.h`), 3,210 exports.
  `CnaNativeAbiPolicy.ConsumerVersion` = 0.44.0, and the accepted list is exactly {0.44.0}. The CI
  pin (`cna-cs/.github/workflows/build.yml` `CNA_UPSTREAM_COMMIT`) is `ed885a8ae` (CBIND-155).
- Migrations this campaign: 0.21 → 0.35 (CSX-010..014; the `CNA.Graphics.Experimental` surface was
  removed) → 0.36 … 0.41 (CBIND-130/132/134/141/145/152) → 0.42.0 (CBIND-153) → 0.43.0 (CBIND-154)
  → 0.44.0 (CBIND-155). CBIND-156 and FX-145 changed behaviour without bumping the ABI.
- **Admitting a new minor** takes all of the following:
  - cna-cs: the policy JSON and `CnaNativeAbiPolicy.cs`, `CnaAbiTests`, the fixtures of
    `scripts/Verify-NativeAbiCompatibility.sh` (exact accepted, previous retired, next
    unreviewed), the CI pin (full SHA) and `tools/abi-verify`.
  - CNA: `abi_baseline.json`, plus `docs/c-api/COVERAGE.md`, `LIMITATIONS.md` and
    `RELEASE_GATE.md` regenerated with `tools/c-api/*.py --write`, and `ABI_VERSIONING.md`.

  A cna-cs build older than an admission refuses the new library by design, so rebuild a game
  before running it.
- Release gate (`docs/c-api/RELEASE_GATE.md`): "Not ready — 1 criterion unmet". That criterion is
  the CNB content-format backlog (CBIND-103..112), which predates the campaign.

## 7. CNA native fixes (all committed, each with a test that fails without it)

| ID | Commit | What changed |
|---|---|---|
| CBIND-128 | `bdf239360` | A component's C handlers run inside the game's callback scope (DrawableGameComponent.GraphicsDevice borrowable). |
| CBIND-129 | `6e0de68e8` | A component's C initialize handler runs before its base loads content. |
| CBIND-131 | `abd005ab8` | EasyGL context lease survives exit-time static destruction. |
| CBIND-133 | `e77ae09ed` | GraphicsResource.hpp defines the lease its template holds. |
| CBIND-135 | `1e7c4d323` | Static C API archive builds under Emscripten. |
| CBIND-136..139 | `552a0d8cb` `747b02493` `80572aa00` `1dc4ce06a` | Android: NDK-only build; Back seen once as Escape and player-one Back; portrait games; models draw whole. |
| CBIND-140 | `9976f4909` | A relative content root is the title's directory, not the working directory. |
| FX-140 | `1c2923efd` | SM3 point-size output takes a scalar write on GLSL. |
| Task 1120 | `7661f6981` | A stock effect passed to `SpriteBatch.Begin` places sprites by its matrices. |
| CBIND-142 | `fad9fabe0` | A video frame texture belongs to the player's game. |
| CBIND-143 | `ffb82bc0d` | Avatars load in a single-threaded browser build. |
| CBIND-144 | `a71cc2415` | Device-creation failure in the Game constructor is `NoSuitableGraphicsDeviceException` (XNA IL). |
| FX-141 | `46231e857` | MojoShader: SM3 programs may mix absolute and ordinary float-constant reads. |
| CBIND-145 | `22b30b30e` | ABI 0.40.0: adapters readable with the game handle in a constructor. |
| CBIND-146 | `51d9c84cc` | SoundEffect content accepts XAudio2's 1000–200000 Hz (XNA's ADPCM writes e.g. 48056 Hz). |
| CBIND-147 | `e6f9d5384` | `GameWindow.ClientBounds` X/Y are the client area's desktop position (XNA IL). |
| CBIND-148 | `6e273cd89`, `836b796ae` | SDL3 mouse reads the live global pointer minus the window position (XNA `GetCursorPos`+`ScreenToClient`). |
| FX-142 | `fb5cb3ba2` | MojoShader centroid link rule requires pixel SM ≥ 2.0 (Racing Game normal mapping linked on GLSL ES). |
| FX-143 | `d9d599263` | GLSL ES 3 preamble declares `precision highp sampler3D;`. |
| CBIND-149 | `d459f42aa` | `GL_SAMPLE_MASK` only on desktop GL 3.2+/GLES 3.1+ (WebGL 2 INVALID_ENUM). |
| CBIND-150 | `3c72165e6` | No `GL_TEXTURE_SWIZZLE_*` where the profile has none (WebGL, GLES 2); recorded divergence. |
| FX-144 | `47b5f7665` | Test fix: CnaTests' static meta-gl copy initialised from the renderer's loader. |
| CBIND-151 | `cde2251fa` | A game thread that is a browser worker (threaded bundles): shared-memory link, proxied WebGL frame commit, IDBFS flag from the page thread. |
| CBIND-152 | `c77f983b1` | ABI 0.41.0: the game thread runs other threads' queued calls while it waits. |
| merge | `46d55fa26` | Branch `work` (GS-AUDIT P1-P4, GamerServices persistence/locking/authorization fixes). |
| CBIND-153 | `938a22069` | ABI 0.42.0: a game lends its device before its run begins (`ApplyChanges` in a constructor). |
| CBIND-154 | `23d7bad1e` | ABI 0.43.0: fire-and-forget `SoundEffect.Play` answers on any thread. |
| CBIND-155 | `ed885a8ae` | ABI 0.44.0: Activated/Deactivated handlers run in a callback scope with the device. |
| CBIND-156 | `6c278b21d` | A stock effect's draw samples the device's sampler slot: Apply binds its texture (null too); a texture set on `GraphicsDevice.Textures` after Apply is drawn (LilyPath). |
| FX-145 | `26b477686` | Compiled effects sample float textures at highp on GLSL ES 3 (MojoShader patch `mojoshader-6333f74-glsles3-sampler-precision.patch`; Mesa read lowp Vector2 textures at fp16 — LiSPSM's variance shadow). Fixture `tests/fixtures/compiled-effects/variance-moments-xna4.*`. |
| FX-146 | this commit | GLSL ES 3 compiled vertex `TEXLDL` adds the live sampler's LOD bias in generated shader code; the full compiled-effect selection passes 686/686. |

Known native issues. None of them was introduced by this campaign:
- `CnaRendererTests` aborts at its first test that constructs a `GraphicsDevice`. The binary links
  its own static meta-gl copy, which is never loaded (the same defect FX-144 fixed for CnaTests).
- In `CnaContentTests`, `GltfRendererPbrFallbackPolicy...` fails on WebGPU source-text evidence.
  `CnaContentTests` also needs the repository root as its working directory.
- At `-j8`, the C smoke tests lose 1–3 different tests per run (timing assumptions such as
  `RuntimeComponentsSmoke` stage 8). All of them pass alone.
- CBIND-158 closed the former `callbackFailure_` suspicion. A checked-in oracle run against real
  XNA 4.0 under Wine/DXVK proved that an Update exception leaves `Run`, then `Dispose` still calls
  `UnloadContent` exactly once, after `Content.Unload`. CNA now does the same: only the cleanup
  callback may run after an earlier failure; later frame/event callbacks remain suppressed.

## 8. cna-cs: what behaves like XNA now (by area; CSX row numbers in `CAMPAIGN.md`)

- **Lifecycle** (CSX-082/084/086/088/090/125/142/146):
  - XNA's order of `Initialize`, `LoadContent`, `BeginRun`, `EndRun`, `Exiting` and `Disposed`.
  - Components are initialised inside `base.Initialize()`, once. `base.Update`/`base.Draw` drive
    the components at the call.
  - A callback's exception unwinds out of `Run` with its own type. A game event handler's exception
    leaves `Run` at the next `Update`.
  - What `UnloadContent` throws leaves `Dispose`, after the native game is released (CSX-142).
  - An earlier Update callback failure does not suppress `UnloadContent`; content unloads first,
    as measured on XNA 4.0 (CSX-146 / CBIND-158).
  - SIGTERM/SIGINT end the game through its own exit.
- **Exceptions** (CSX-080/127/133): native refusals map to the exception XNA throws for the same
  call. Examples: `NoSuitableGraphicsDeviceException`; `ContentLoadException` with XNA's message;
  `DrawString` with a character the font lacks throws `ArgumentException`.
- **Graphics**:
  - The device state cache stays coherent with native SpriteBatch (CSX-083).
  - The profile comes from the `RuntimeProfile` resource (CSX-081). `Color.Transparent` is
    transparent black.
  - Adapters are available in a constructor (CSX-111), and `ApplyChanges` in a constructor
    creates the device (CSX-121).
  - An Immediate SpriteBatch draws each sprite when it is given (CSX-130).
  - Stock effects use the device texture slot (CSX-135), and float textures are sampled at full
    precision (CSX-136).
- **Content**:
  - Paths are title-relative. File and directory lookup is case-insensitive (CSX-087/096/113).
  - Custom readers are found by simple assembly name. Model tags may be of the game's own types.
  - A `ContentManager` that overrides `OpenStream` serves built-in types too, e.g.
    `ResourceContentManager` (CSX-129).
  - Workers can load content (CSX-100/101).
- **Audio**:
  - Songs play from an `.ogg`/`.oga`/`.qoa` beside the `.wma` (CSX-105;
    `scripts/convert-xna-songs.sh`).
  - XACT and `DynamicSoundEffectInstance` go through CNA.
  - Sound effects play from any thread (CSX-124).
- **Threads**: a game thread waiting on a worker runs the calls that worker queued for it
  (CSX-118). Several workers can share one StorageDevice (CSX-109).
- **Input**: XNA's static input classes answer before a game exists (CSX-123). Phone titles get
  the mouse as a finger and Back on Escape.
- **Phone titles** (CNA.PhoneCompat; CSX-119/132/137):
  - The Guide prompts need no GamerServicesComponent.
  - `IsolatedStorageSettings` is available.
  - `Guide.IsTrialMode` is the phone's license check (`SimulateTrialMode`).
- **Components** (CSX-138): `VisibleChanged`/`DrawOrderChanged` fire only when the value changes.
- **.NET Framework versus .NET semantics.** CNA.XnaCompat applies these to code built against XNA:
  - `List<T>.ForEach` keeps 4.0 semantics, via a Roslyn interceptor (CSX-110).
  - `Assembly.LoadFile` loads a file once (CSX-122).
  - A game's own `Range`/`Index`/`PriorityQueue` wins over .NET's (CSX-126).
  - Infinity, the minus sign and AM/PM spacing print as on Windows' .NET Framework, which matters
    for SpriteFonts (CSX-128/139).
  - A `using` of a namespace that existed only in .NET Framework compiles (CSX-131).
  - Explicit 32-bit struct layouts are realigned for 64-bit (CSX-134, BEPUphysics).
  - `BinaryFormatter` is enabled (CSX-141; `<CnaBinaryFormatter>false</CnaBinaryFormatter>` opts
    out; desktop only).
- **Browser/Android heads**:
  - Browser storage and threads (CSX-108/115/116/117).
  - CoreLib is kept whole for reflection-heavy libraries such as protobuf-net (CSX-140; both
    heads).
  - The generators copy `Deterministic`/`AllowUnsafeBlocks` and set
    `DisableImplicitFrameworkDefines`, so the SDK's `ANDROID`/`BROWSER` symbols never reach game
    code.
- **GamerServices/Avatar/Net/Guide** (CSX-040..043): 24/24 integration tests, SystemLink loopback.
  CSX-044 (remaining C routes for extras) is `doing`, low priority.
- **Windows Forms** (CSX-114/120): `Form.FromHandle` returns the game window, and `FormBorderStyle`
  is applied. CSX-147 routes `MessageBox` to CNA's native dialog service and returns the selected
  button; `Opacity` is a documented platform/API limitation. Zelda Oracle's larger surface
  (CSX-120: `Icon`, `MinimumSize`, `Shown`) is only on the side branches.

## 9. Test state (verified 2026-10-04 by CSX-153)

Native library: `cna/build-probe/modules/c-api/libcna_c_api.so`. Environment for the native-backed
suites:

```bash
env -u WAYLAND_DISPLAY -u DBUS_SESSION_BUS_ADDRESS DISPLAY=:166 SDL_AUDIODRIVER=dummy \
  XDG_DATA_HOME=<scratch>/xdg/data XDG_CONFIG_HOME=<scratch>/xdg/config \
  CNA_NATIVE_LIBRARY=<lib> dotnet test tests/<Suite> -c Release
```

Alternatively, use `../cna/tools/platform/run_gpu_tests_private.sh --exec dotnet test ...`.

| Suite | Result | When |
|---|---|---|
| CNA.Framework.Tests | **653/653** in Release and Debug | 2026-10-04 |
| CNA.XnaCompat.Tests | **319/319** in Release and Debug | 2026-10-04 |
| CNA.BrowserCompat.Tests | **5/5** in Release and Debug | 2026-10-04 |
| CNA.Integration.Tests | **260/260** in Release and Debug | 2026-10-04 |
| CNA.GamerServices.IntegrationTests (isolated XDG, no session bus) | **24/24** in Release and Debug | 2026-10-04 |
| cna-cs api-compat (Windows runtime; GamerServices/Avatar/Net) | **256/256; 75/75**, 0 diagnostics | 2026-10-04 |
| cna-cs ABI verifier | 1,137 values; 1,419 imports/prototypes; 23 callbacks; 604 constants; 12/12 negative controls | 2026-10-04 |
| CNA C API (`run_gpu_tests_private.sh build-probe -R '^CApi_' -j4`) | **107/107** | 2026-10-04 |
| CNA Runtime/Game (`CnaRuntimeTests`) | **208 passed, 2 expected platform skips** | 2026-10-04 |
| CNA compiled effects (selected CTest regex) | **710/710** on isolated Mesa GLES 3.2 | 2026-10-04 |
| CNA.NET ownership stress | **100/100** game recreations; 3,000/3,000 releases | 2026-10-04 |
| CNA SDL boundary gates (`tools/platform/*.py --check`) | pass | 2026-10-02 |

The first C API run exposed three audio smoke tests whose CTest environment depended on a host
sound server. All three passed immediately with SDL's dummy backend; CNA `8a7e13ff5` applies that
deterministic backend for every renderer, and the complete 107-test set then passed at `-j4`. The
two Runtime/Game skips are explicit: Headless and Terminal cannot back an EasyGL OPENGLES3 build.

Representative unchanged applications also passed: Project Babsang and Ronald the Snake on the
isolated Linux display, and Gemstone Hunter after a fresh browser publish and Android package build.
The browser evidence is `/rv/tmp/cs-samples/final-stability-browser-20261004/`; the x86_64 emulator
evidence is `/rv/tmp/cs-samples/final-stability-android-20261004/`.

Not rerun recently: `scripts/Package-Acceptance.sh`, which last passed on ABI 0.40.0.

## 10. Gallery (samples.libcna.com), Linux desktop

- 84 gallery entries. **82 run** with unchanged Microsoft source (hash-verified), content from the
  C++ ports' evidence (official XNA output), Debug and Release builds, a capture, and the sample's
  own exit path.
  - Yacht (SAMPLE-071) is excluded by owner decision: it needs Windows Phone push and Shell APIs
    beyond CNA.PhoneCompat.
  - PerformanceUtility (SAMPLE-104) has no C++ port, so it has no C# row.
- `samples/manifest.tsv` has 84 runnable rows: the gallery's runnable rows plus ClientServerSample
  and NetRumble. Requalify with `scripts/requalify.sh --out DIR`, which writes captures, the C++
  port's capture and `requalification.md` with pixel differences.
  - Last full run: on 0.44.0 (2026-10-03), no status change.
  - The run before: `/rv/tmp/cs-samples/requal-20261002d`, 84/84 build and run; 26 rows are
    pixel-equal to their C++ port.
- Inventory: `python3 scripts/gallery-inventory.py --markdown gallery-inventory.md`.

## 11. WebAssembly

- Toolchain:
  - .NET 11 SDK `11.0.100-rc.1.26425.128` at `~/deps/dotnet11`, with the wasm-tools workload
    (emscripten 6.0.3).
  - Chrome for Testing headless shell 151.0.7922.34, from the Playwright cache
    `~/.cache/ms-playwright/chromium_headless_shell-1234`.
  - SwiftShader WebGL2.
  - CNA static archive from `cna/cmake-build-webgl2`, built by `cna-cs/scripts/Build-BrowserNative.sh`.
- Run with `scripts/browser-requalify.sh` (all rows) or `scripts/browser-sample.sh <Row|games/Game>
  [--threads]`. Generated projects go to `cna-cs-samples/build-consumer/browser/<Row>/`.
- Gallery: **84/84** in a single run (`/rv/tmp/cs-samples/browser-requal-20261002-st/`).
- Games: most were run (README paragraphs under the table). The single-threaded archive was rebuilt
  on 2026-10-04 after CSX-145; its staged `libcna_c_api.a` SHA-256 is
  `8a74ac43981541b64f90d78add3043802e179285bd68ce666c6b61820f2f9f74`.
- Not working in a browser:
  - Playing in Traffic: it plays a video, and the browser has no video backend.
  - Forge sample: its local UDP server.
  - TiledTerrainDemo: its `Rgba64` normal map needs `EXT_texture_norm16`, and CNA refuses it by
    name.
  - MDTerrainDemo: its thread-pool work items never run (the .NET fault below).
- TerrainDemo is no longer a rendering failure (CSX-145). Unchanged original source draws its
  instanced terrain in headless Chromium/SwiftShader when the headless harness supplies a
  deterministic pointer position. Browsers cannot physically implement `Mouse.SetPosition`, so
  SDL3 now preserves a requested position virtually and applies later physical deltas. The
  no-action harness begins at `(0,0)`; TerrainDemo does not centre the pointer before its first
  update and therefore legitimately turns its camera away. `move:600,350@2000` both avoids that
  harness artefact and exercises the virtual recenter path.
- Gemstone Hunter is no longer blocked (CSX-144): the supported out-of-band formatter package
  restores trusted legacy-data compatibility on .NET 9+.
- **Threaded bundles** (`--threads`; CSX-115..117, CBIND-151). Built with `Build-BrowserNative.sh
  --threads` (`cna/cmake-build-webgl2-threads`).
  - `Main` runs on .NET's deputy worker, which proxies WebGL to the page (`-sOFFSCREEN_FRAMEBUFFER`).
  - Frames come from `emscripten_set_main_loop_arg`, so the deputy returns to its event loop and
    input arrives.
  - The page must be cross-origin isolated (COOP/COEP) and preloads
    `max(16, 3*processors+8)` workers.
- **.NET 11 RC1 fault** (upstream; the owner said not to pursue it): a `Thread.Start` that needs a
  worker created after `Main` never returns. The probe is
  `build-consumer/browser/ThreadStartProbe`.
- Not qualified: interactive Chrome, a GPU, audio, other browsers, and a threaded requalification
  of the gallery.

## 12. Android

- Emulator: AVD `Medium_Phone`, API 35 x86_64, emulator 36.5.11.0, `-no-window -no-audio`.
- NDK 29.0.14206865; CNA in `cna/cmake-build-android-x86_64` (`cna-cs/scripts/Build-AndroidNative.sh`).
  Uses the .NET 11 android workload.
- Run with `scripts/android-requalify.sh` or `scripts/android-sample.sh <Row|games/Game> [--then
  'input keyevent KEYCODE_BACK']`. Apps go to `build-consumer/android/<Row>/`.
- Gallery: 84/84 draw; 78 end on one Back press. Most games run as well (README). **Stop the
  emulator afterwards.**
- Not qualified: physical devices, ARM, audible audio.

## 13. Windows / macOS / iOS

Not qualified at runtime, and nothing was run on them. The Windows 10 VM no longer exists. The
fact that code builds is not a claim.

## 14. Real games (`cna-cs-samples/games/`)

`games/README.md` is authoritative: 84 rows, each with its source and commit, content method,
measurement and date, plus the browser/Android paragraphs and the blocked list. Highlights of what
runs unchanged:

- **Commercial and indie releases:** Speedy Blupi, Rookie Drivers, TIE Fighter Forever, NePlus,
  Escape From Enceladus, Resonance, Spineless, Bubble Bound, Virulent, and others.
- **Microsoft's** XNA 4.0 Racing Game Kit (plays a race), Network Prediction, Peer to Peer and
  Network Game State Management.
- **GitHub finds:**
  - SKraft, Sonic 3, HauntedHouse, Disentanglement, the Forge sample, Mario3, Spelunky Tiles.
  - Farseer 3.5 samples, MP3Sharp and SharpMik players, LilyPath, willcraftia's TestXna demos.
  - cocos2d-x for XNA's tests, BoneAnimation, xna-camera-2d, UTS tower defence.
  - ExEn's samples, Project Mercury, Xen kits, Asteria demos, XPF.
  - gearsvge's GearsDebug.
- **Book samples:** Petzold, Apress *Windows Phone 7 Game Development*, *Windows Phone 7 Recipes*,
  *2D Graphics Programming for Games*, *XNA 4.0 Game Development by Example*.

The final bounded CSX-151 sweep is complete and must not be replenished. Exactly ten preselected
projects were investigated. Unchanged Project Babsang ran from its shipped XNA content and
Farseer/DebugView binaries. Unchanged Ronald the Snake ran through arcade gameplay with its custom
content readers, tiled maps and four effects after authentic XNA 4.0 BuildContent produced all 56
assets; its unavailable Palatino Linotype was the one recorded content substitution (Liberation
Serif). Bamboozled, Voodoo Boy and Hunted omit source files named by their own projects. Project
Heist and Adventure Time are MonoGame applications with non-XNA middleware. Pixel Blast is a
Silverlight/Windows Phone XAML host. Engine Nine's required engine graph depends on
System.Xaml/WPF/WinForms/Win32. MunchKlone requires System.Drawing plus unpublished card data from a
hard-coded historical MySQL service. No general CNA/CNA.NET defect was found. Saturation condition
B is reached; `cna-cs-samples/games/README.md` has the exact source revisions and outcomes.

Run recipes that are not obvious:
- **Racing Game Kit:** run from its own `bin/Release`, because its XACT paths are relative to the
  working directory (as on Windows).
- **SKraft** reads `<cwd>\Maps\Test` (`/rv/tmp/cs-samples/skraft/`).
- **Forge** runs five levels below its `Assets` (`/rv/tmp/cs-samples/forge/1/2/3/4/5`).
- **Mahjong** needs `mousedown`/`mouseup` with a pause.
- **Spelunky Tiles** needs held presses.
- **GearsDebug:**
  - Built as Release, it shows its title. Built as Debug (`-c Debug`), it opens its debugger menu
    and Radial Assault.
  - At exit it ends with a `PlatformNotSupportedException` from its own `Thread.Abort`. That is
    expected on .NET 5+ (CSX-142).

Blocked, with reasons in the README:
- Windows Forms or WPF hosts: Kodu, XNALara, Almirante, EvoNet, The Lost Levels, DirtyGame.
- Win32 P/Invoke: Divine Right, YCPU, Mannux, Tactile Engine; Zelda Oracle's x86 window-procedure
  hook.
- Missing content, libraries or data: Quarx, StarWarrior, Space Conquest, Jxqy HD, and others.
- Windows fonts the repository doesn't ship: Second Realipony, Rugby League, TestBench1, Voxeliq,
  and others.
- Other: a dead server (Samurai), Silverlight apps, Xbox-only shaders (Shading), 21 `Game` objects
  at once (Design Patterns Game), XNA 3.x projects.

Side branches, local only and not merged: cna-cs `zelda-oracle-forms` (`23e3900`) and
cna-cs-samples `zelda-oracle` (`851590a`). They hold CSX-120's extra Windows Forms surface. Zelda
Oracle compiles with it, then stops at a `user32` window-procedure hook that only works in an x86
process.

## 15. Mahjong (CBIND-147/148)

Runs; a held click on Play deals. It found two defects: `ClientBounds` X/Y were 0, and the SDL3
mouse was stale or clamped. Both were fixed natively with tests.

## 16. XNA 4.0 Racing Game Kit — plays

- Source:
  `/rv/tmp/XNAGameStudio/Samples/XNA-4-Racing-Game-Kit-master/RacingGameWindows1/RacingGame/RacingGame`,
  unchanged; byte-identical to cna-samples SAMPLE-152.
- Content: real XNA 4.0 output from the former Win7 VM,
  `/rv/tmp/samples/SAMPLE-152-.../evidence/xna4-authentic-build/Debug/Content`.
- Fixes it caused: CSX-114 (Windows Forms), CSX-113 (directory case), FX-142 (centroid link),
  CSX-116 (browser threads).
- Measured:
  - Attract mode, main menu, car selection, Advanced-track selection and a race (lap 1/3, HUD,
    shadows and post-processing). The final CSX-149 run used unchanged C# source, current CNA.NET
    and CNA `16dce8d5a`/C ABI 0.44.0, windowed at 1024×768 from a seeded
    `RacingGameSettings.xml`.
  - The retained C++ port over CNA OPENGL33 ran the same six stages at the same size. Its scene,
    models/materials, menu composition, track, post effects and HUD agree with CNA.NET; animation,
    camera and elapsed time make this a structural rather than pixel-identity comparison.
  - The authentic XNA-built `RacingGame.exe` was attempted unchanged under Wine. Without the XNA
    SDK's `XnaLiveProxy.exe` it fails on that missing file; with the authentic proxy retained from
    Microsoft's RolePlayingGame sample it fails to initialize the defunct Games for Windows - LIVE
    service. RolePlayingGame independently has the same Wine failure. This is an external GFWL
    oracle limitation, not a CNA defect, and the game was not patched to bypass it.
  - Space must be held 2–3 s, because the game polls slowly under llvmpipe.
  - Browser (threaded) and Android: attract mode.
- Evidence: `/rv/tmp/cs-samples/final-racing-differential-20261004/`. No new general defect was
  found. Audibility remains unqualified.

## 17. Historical blockers retested and obsolete

These are resolved or irrelevant:
- CNA-REPORT-001..004 (resolved, or were cna-cs bugs).
- `CNA.Graphics.Experimental` (retired natively at ABI 0.30).
- Compiled-effects concerns (work), and the `io_10_0` link failures (FX-142).

Building CNA for wasm needs .NET 11, whose emscripten has `std::jthread`. Old `plan.md`/`NEXT.md`
blockers are historical. `cna-cs/docs/native-behavior-blockers.md` is now the ABI-0.44 detailed
history behind the finite classification in `cna-cs/docs/final-compatibility-audit.md`.

## 18. Evidence and artifacts

- Desktop gallery: `/rv/tmp/cs-samples/requal-*`.
- Browser: `/rv/tmp/cs-samples/browser-*` (games from 2026-10-03: `browser-20261003*`).
- Final threaded browser qualification: `/rv/tmp/cs-samples/final-threaded-20261004/`. The archive
  was rebuilt from CNA `28f8312f0` with Emscripten 6.0.3; its SHA-256 is
  `ce8db116c3d9100f363864d355ad205c7334e5c5b630167c4fee1641c588b6c2`.
- Final bounded external sweep: `/rv/tmp/cs-samples/final-sweep-20261004/`; Ronald the Snake's
  complete XNA 4.0 pipeline output and log are under
  `/rv/tmp/xna-games-content/final-ronald-complete/`.
- Android: `/rv/tmp/cs-samples/android-*`.
- Games: `/rv/tmp/cs-samples/games-*`.
- Game checkouts: `/rv/tmp/xna-games/*`. Built content: `/rv/tmp/xna-games-content/*`.
- XNA Wine prefix: `~/.wine-cna-xna40`. XNA reference assemblies: `~/deps/xna40-windows-assemblies`.
- The job scratch of this session (`~/.claude/jobs/602f242d/tmp`) holds the helper scripts and
  will disappear: `runcap.sh` (§5), `content-batch.sh`, `gen-petzold.py`, `browser-batch*.sh`,
  `android-batch*.sh`, plus the candidate lists `new-candidates.txt` and
  `contentproj-repos.txt`. Recreate them from the descriptions here.
- Probe leftovers that are safe to delete:
  `cna/build-probe/{mojodisasm-src,mojodisasm,fx141-mojoshader,fxabs.exe,d3dx9_43.dll,D3DCompiler_43.dll,cbind140-probe}`.

## 19. Open work, by value

**Compatibility defects and gaps**
1. **Resolved (CSX-144): `BinaryFormatter` on .NET 9+** (browser/Android). The out-of-band
   `System.Runtime.Serialization.Formatters` package is selected for net9+ heads, still behind
   `CnaBinaryFormatter`; unchanged Gemstone Hunter runs in the browser and Android emulator.
2. **Resolved (CSX-145): TerrainDemo's clear-only browser symptom.** This was input state, not
   rendering: its mouse-look loop calls `Mouse.SetPosition` every frame, while a browser cannot
   move the physical cursor. CNA's SDL3 browser mouse now exposes a virtual warp anchored to the
   current raw pointer. Pure regression tests cover persistence, deltas, recentering, reset and
   invalid coordinates; unchanged TerrainDemo renders with deterministic headless input. The
   runner's default initial `(0,0)` remains a harness state, not an invented global auto-centre.
3. **Resolved (CSX-146 / CBIND-158): the native `callbackFailure_` observation in §7.** Real XNA
   calls `UnloadContent` during Dispose after an Update failure, with `Content.Unload` first; CNA
   now matches it and carries native plus managed regressions.
4. **Resolved (FX-146): vertex-texture LOD bias on GLES 3.** Compiled vertex `TEXLDL` applies a
   per-sampler bias uniform in the GLSL ES 3 generated-code path; 686/686 compiled-effect tests pass.
5. A game blocked in a Guide `End*` wait does not see SIGTERM until the Guide closes.
6. **Resolved/classified (CSX-147): Windows Forms.** `MessageBox` uses
   `cna_message_box_show_ext`; 1419/1419 imports pass abi-verify. `Opacity` cannot be applied because
   CNA has no runtime-window/C ABI opacity operation, and remains an explicit platform limitation.

**Performance**
7. **Investigated/classified (CSX-148):** threaded browser bundles proxy every GL call. Emscripten
   can transfer an OffscreenCanvas only as part of creating the owning pthread; .NET owns its deputy
   thread creation and exposes no supported canvas-transfer hook. Keep the correct proxy path. A
   direct worker canvas is a future .NET/browser-host platform optimization, not broad XNA work.

**Qualification**
8. **Done/classified (CSX-149):** Racing Game Kit matched the C++ port structurally through a real
   race; unchanged XNA under Wine is blocked by the defunct Games for Windows - LIVE dependency.
9. **Done (CSX-150):** a current threaded browser archive ran AimingSample with keyboard movement
   and clean Escape shutdown. Unchanged Resonance, after rebuilding its 282 assets with XNA 4.0
   BuildContent/XACT, loaded its level on its own thread, initialized its shipped BEPUphysics,
   entered the 3D arena and reacted to movement. Both ran in headless Chromium/SwiftShader without
   a CNA/CNA.NET exception; no new general defect was found.
10. **Done/classified (CSX-151):** the fixed ten-project final application sweep produced two
    unchanged successful runs and eight exclusively external/non-XNA constraints; no general
    defect was found. Saturation condition B is met. Do not select replacements or search for more
    games.
11. **Done/classified (CSX-152):** the finite strict-surface audit found zero executable
    `NotImplementedException` sites, zero strict-facade TODO/FIXME sites, and accounted for all 24
    static `NotSupportedException` sites. The Windows runtime profile remains 256/256 and the
    GamerServices/Avatar/Net profile 75/75 with zero metadata diagnostics. The finite remainder and
    platform matrix are in `cna-cs/docs/final-compatibility-audit.md`; the native blocker table now
    verifies against C ABI 0.44.0 (`cna-cs` `076d128`).
12. Interactive Chrome with a GPU; a physical arm64 Android device.

**Hardening**
13. The `-j8` C smoke flakiness.

**Not actionable here**
14. Yacht (owner decision), PerformanceUtility (needs a C++ port), and games blocked by missing
    content, fonts or servers.

## 20. Exact next action

Do not search for another application: CSX-151 reached saturation condition B with the fixed final
ten, CSX-152 completed the finite surface audit, and CSX-153 completed the final stability matrix.
Write the English source-build/migration guide next, then complete the requested C++/C/C#
`cna-multi-language-3d-demo` qualification (the same C# source on FNA and CNA.NET). Finally write
the campaign-closure section and enter maintenance mode.
Commit each coherent task locally; do not push without the owner's instruction.
