# handoff.md — current state of CNA

This file describes the repository as it is now and is kept true. Until 2026-10-11 it was `NEXT.md`
and also carried the dated session log of 2026-07 to 2026-10; that log is in Git history
(`git show 8b5564f1e:NEXT.md`).

## 0. Test-quality campaign handoff for Codex — 2026-11-06

Read this section first. Codex continues the campaign handed over by Claude Code; the owner
authorised sharing project files for read-only Claude Code reviews using its remaining weekly quota.
The goal the owner set (Robert Vokac) is open-ended: "keep improving the tests in
CNA for many more hours — improve existing tests and add new ones". Everything below is measured fact from that
session unless marked otherwise.

Claude Code's `/usage` rounded to100% earlier on2026-11-07, while later read-only
reviews still succeeded. The subsequent TQ-XACT-25 private-consumer/next-control
review now returned an **actual weekly quota refusal**: “You've hit your weekly limit”,
reset **November8,19:00 Europe/Prague**. Its log and exit status are archived under
`../cnaplan/evidence/rev002/xact-instance-counts/`. The requested use-until-exhausted
condition is satisfied; no further calls, account changes or paid-extra usage were made.

### 0.1 Where things live

| What | Where |
|---|---|
| Campaign ledger (every task, commits, statuses, 60 open follow-ups) | `/rv/data/development/github.com/libcna/cnaplan/test/TQ_TASKS.json` (cnaplan is not a git repo) |
| Things only the owner or hardware can do, with exact commands | `/rv/data/development/github.com/libcna/cnaplan/owner-tasks/OWNER_TASKS.md` (+ `oracles/` C# sources) |
| Reusable campaign scripts and the binding lane rules | `/rv/data/development/github.com/libcna/cnaplan/test/tools/` (`README.md`, `codex/TQ_RULES.md`, `PHASE1w6.md`, `PHASE2.md`) |
| Sweep evidence | `/rv/data/development/github.com/libcna/cnaplan/evidence/rev002/` (sweeps 1–11); sweep 12: see 0.8 |
| Owner decisions and lessons in prose | `/home/robertvokac/.claude/projects/-rv-data-development-github-com-libcna-cna/memory/MEMORY.md` (index) and `owner-decisions-tq-2026-11-05.md` |
| Behaviour decisions where CNA follows XNA over FNA | `docs/xna-fna-behavior-decisions.md` |
| Defects in the FNA3D/MojoShader forks (open and fixed) | `docs/fna3d-dependency-defects.md` |

### 0.2 What the campaign did (all pushed)

Method: for each area a Codex "lane" designs tests and a fix against an oracle (XNA 4.0 Refresh IL in
`~/deps/xna40-windows-assemblies`, 4.0.30901, beats the original 4.0.20823; then FNA, then FAudio, then hand-derived
values — never CNA's own output), applies the tests red, then the fix, then production mutations that must make the
tests fail; the integrator then runs everything the lane's sandbox could not (GPU, Wine, loopback sockets), fixes what
breaks, commits one task per commit, pushes.

CNA `next` (newest last within a theme): XACT audio: GAP-01a `aff1f533e` (typed event IR), 01b `3100d88ff` (cue clock),
01c `be1a1b70d` (pitch/volume events), 02 `c5a99a874` (finite loops), 03 `23e2ab1b3` (per-loop variation),
04 `f6c9ece36` (reverb/DSP wet bus, FAudio reverb in `third_party/faudio-reverb`), 05a `5439a8230` (WMAv2 via optional
FFmpeg, option `CNA_ENABLE_WAVEBANK_CODECS`). Graphics: GAP-07a `76f45ad80`/07b `ab4b7d754` (sprite position), 08a
`7a311138b` (Present regions + native window adoption via `IPlatform::AdoptNativeWindowHandle`, SOFTWARE; FNA3D refuses,
FNA3D-8), 08b `b3e855cae` (EasyGL, DirectX 9), 08c `bea3e6511` (SDLGPU, WebGPU), 10 `44f04d107` (float RTs), 13+FORK-PIN
`2a9273ba7` (compiled effects, EasyGL default ON for fresh OPENGL33-only builds), 14a `be933add6` (DX9 multi-stream),
14b `8d1aaa319` (WebGPU split streams), 15 `7ab15fdae` (WebGPU mips), FIX-14A `f02ccefef`, FIX-14B `65c1ea4d6`. Content:
GAP-21 `d00a9ffb9` (LZX), GAP-22 `7e5f92fab` (+ sharp-runtime `c793ef3e`). Online: GAP-16b `63cca7784` (guests),
GAP-17b `2b230b986` (pre-join QoS), GAP-18b `4de39f871` (Recent leaderboard = rolling 7 days, CNA policy). Platform:
UB-006 `0b8424e9b` (Android APK listing cache; verified on a real APK by the Galaxy Eggbert session). Earlier TQ-0xx
tasks (several dozen test-quality waves) are in the ledger.

Other repositories (all pushed): `cna-gamer-services-server` branch `develop`: `245f088` GAP-17a (CNQ v1), `ed0008b`
GAP-18a (schema 26), `c85cead` GAP-16a (schema 27 + use-after-free fix), plus `26bb055`. Forks: `libcna/mojoshader`
branch `cna` `04616e1` (duplicate-object leak, FNA3D-2/-6), `6ae750b` (SPIR-V `ps_1_4` BEM and TEXDEPTH);
`libcna/FNA3D` branch `cna-audit-fixes` `912e26d` (FNA3D-1 stencil, FNA3D-4 upload order, FNA3D-5 unsigned mip bounds),
`b301e3a` (FNA3D-7 investigation test), `5c0b9b8c13f5cca36a336d481cd5f696d0223275` (MojoShader gitlink). CNA pins that
FNA3D SHA in `cmake/ThirdPartyFNA3D.cmake`. Downstream repos (cna-demos, cna-dotnet, cna-examples, cna-engine,
cna-extended, cna-template) were migrated and pushed earlier in the day; every repo's AI policy now starts with "valid
since 1st December 2026".

### 0.3 Standing rules (owner)

- Build only in the closed directory names (`build`, `build-asan`, `build-ubsan`, `build-tsan`, `build-probe`,
  `build-consumer`) and the stable `cmake-build-<variant>` trees; never under `/tmp`; reuse trees; at most 8 jobs;
  `CCACHE_DIR=/rv/cnaccache CCACHE_BASEDIR=/rv`; never set `CCACHE_MAXSIZE`. See `/rv/data/development/github.com/libcna/CLAUDE.md`
  and this repo's `CLAUDE.md`.
- Never touch the owner's live display. GPU/window tests only through
  `tools/platform/run_gpu_tests_private.sh <tree> [ctest args]` or `... --exec <binary> [args]`.
- One task = one commit, staged by explicit file name (`git add <file>…`, never `-A`); other agents may stage into the
  shared index, so run `git reset -q` before staging and prefer `git commit --only -- <paths>`. No attribution lines in
  commit messages. The original handed-over Claude session had permission to push `next`;
  this Codex continuation has no current push request, so keep its commits local. Pushing
  the server or fork repositories needed the owner's explicit word each time.
- AI changes to the FNA3D and MojoShader forks are allowed again (owner, 2026-11-06). Commit on their `cna` line with a
  `CNA-Patch:` trailer, add a regression to the fork's own harness, pin by full SHA only.
- Architectural questions go to the owner. Owner decisions 2026-11-05/06: XNA beats FNA when they disagree; follow
  XNA Refresh over original 4.0; Present override = full native window adoption; GPU partial present = history texture
  (own window) + readback through the platform region presenter (foreign window); G05 WMA/XMA through optional FFmpeg
  (XMA stays refused by name until a lawful fixture exists; WMA Pro refused by name: native duration 37713 frames @48 kHz
  vs 35665 decoded); stereo spatial audio kept (no X3DAudio multichannel); compiled effects default ON only where the
  family passes the corpus (OPENGL33 yes, DirectX 9 no because `LegacyBumpEnvironment` cannot be proven on Wine/DXVK);
  online guests in PlayerMatch and Ranked lobbies (no own progress, Ranked has no join-in-progress); Recent leaderboard
  window = rolling 7 days (CNA policy, not XNA); FAudio reverb ported under zlib in `third_party`; additive wire
  protocols only (CNQ v1, online-guests-v1), schema 26 then 27.
- Do not weaken a test to make it pass, do not copy observed output into an expectation, do not skip silently: a case
  must pass, or fail with a clear reason, or be gated by an honest named capability.
- Keep tracked files committable (no placeholders); never `rm -rf` with variables; never `pkill -f` (it kills your own
  shell); never use `git checkout -- <dir>` to undo edits.

### 0.4 How to run things (recipes that worked)

Trees (`cmake-build-…`): `multi` (all renderers, C API on, runtime selection with `CNA_GRAPHICS_RENDERER=<X>`),
`sdlgpu` (SDL3 audio, C API on), `webgpu`, `fna3d`, `software`, `headless` (NULL audio), `opengl33` (compiled effects
now ON), `debug` (OPENGLES3, SDL3 audio: net/gamer-services/audio SDL3 legs), `mingw` (DIRECTX9, Wine). Never reconfigure
with different options; regeneration happens automatically and breaks if another agent is mid-edit in `cmake/`.

- Window-free: `env -u DISPLAY -u WAYLAND_DISPLAY SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy <binary>`.
- DirectX 9 under Wine/DXVK 3.1.1: `env -u SDL_VIDEODRIVER CNA_D3D9_WINEPREFIX=$HOME/deps/wineprefix-cna-dxvk-3.1.1
  WINEPATH='Z:\usr\lib\gcc\x86_64-w64-mingw32\14-win32;Z:\usr\x86_64-w64-mingw32\lib' tools/platform/run_gpu_tests_private.sh
  --exec scripts/run-wine-dxvk9.sh cmake-build-mingw/<bin>.exe --gtest_filter=…`. C smokes in `mingw` fail only because
  `libcna_c_api.dll` cannot find SDL3/libcurl under Wine (environment, not a result). Presented clients cannot be captured
  under Wine/DXVK, so DX9 pixel-of-the-window tests need real Windows.
- Native pixel capture of GPU-presented windows must use X Composite (`XGetImage` on the window returns BadMatch);
  helper `tests/support/PresentedWindowCapture.hpp`.
- Service end-to-end (needs loopback sockets): server repo `build/` (RelWithDebInfo, `-DPython3_EXECUTABLE=/usr/bin/python3` —
  pyenv's python lacks `websockets`) and `build-asan/` (ASan+UBSan; use it for any memory-safety suspicion). Run with
  `CNA_SERVICE_{CLIENT,DIRECTORY_CLIENT,RELAY_CLIENT,SESSION_CLIENT,AVATAR_CLIENT}_HARNESS=` pointing at the matching
  `cna_service_*_client_harness` in `cmake-build-debug` (the ASan run needs `/rv/…/cna/build-asan` harnesses) and
  `CNA_AVATAR_CATALOGS=…/modules/gamer-services/assets/avatars`; the C API guests test uses
  `cmake-build-sdlgpu/modules/c-api/cna_service_guest_c_api_harness` with `LD_LIBRARY_PATH` set to that directory. NAT
  variants skip (exit 77) until `slirp4netns` is installed.
- Forks: each has untracked `build/` and `build-asan/`; fork GPU tests run through the private runner with
  `FNA3D_FORCE_DRIVER=OpenGL|SDLGPU`. `~/deps/libcna-FNA3D` and every `cmake-build-*/_deps/fna3d-src` checkout must sit on
  the pinned FNA3D SHA with its MojoShader submodule updated (`git fetch origin cna-audit-fixes`, `checkout --detach <sha>`,
  `submodule update --init --recursive`).
- Gates after relevant changes: `python3 tools/platform/{sdl_inventory,sdl_classify,sdl_ratchet,renderer_sdl_audit}.py --check`,
  `tools/platform/hot_path_lint.py`, `tools/platform/nonproduction_sdl_audit.py --repo . --check`,
  `scripts/check_licence_headers.py`, `tools/c-api/{generate_coverage_inventory,generate_limitations,check_release_gate}.py --check`
  (never `--write` or approve symbols as a shortcut: add a rule in `tools/c-api/coverage_mappings.json` with a task prefix
  `CBIND|MOD|TQ|GAP` and wording that matches a theme in `tools/c-api/limitations.json`; update the counts in
  `docs/c-api/README.md` when the symbol totals move).
- Tools: `cnaplan/test/tools/integrate.sh <lane>` snapshots HEAD + a lane's files and runs those gates before staging.

### 0.5 Lessons that cost time (do not repeat)

- A GPU/pixel test that calls `SetData` on a buffer still bound to the device throws in XNA and in CNA: unbind first.
- Codex sandboxes cannot bind loopback, run Wine or a GPU: anything of that kind must be run by whoever has the access.
  A sandboxed lane that cannot compile a path (for example a DX9-only test block) hands over code that does not build:
  always build every tree a change touches before committing.
- Tests gated behind an option that is OFF in the stable trees never ran (compiled effects, `CNA_DEVICES`); turning the
  option ON exposed real failures (centroid, vertex textures, bump). Prefer a stable tree where the option is ON.
- The Codex "possible cybersecurity risk" content filter blocks sessions whose context holds memory-safety or
  adversarial wording; start a fresh `codex exec` with neutral wording (model `gpt-6-astra` also worked).
- When a lane edits a shared file together with another lane, split by hunk (`git diff -U0`, filter, `git apply --cached
  --unidiff-zero`) and verify the split state builds in a snapshot.
- ASan found a real use-after-free (`Admission::leave`) that normal runs only showed as flaky heap corruption. Run `build-asan`
  for server changes.
- Two lanes must not run `ninja` in the same tree; give each lane its own tree.

### 0.6 State of the work

All lanes of the owner-approved list cnagaps A and D (`cnaplan/…/GAPS_ANALYSIS`; the file is `codex/gaps/GAPS_ANALYSIS.md`
in the Claude scratchpad and its conclusions are in the ledger) are implemented, qualified and pushed. Not done by
decision: G06 multichannel spatial audio, G19/G20 (deliberately unsupported), XMA/WMA Pro playback, DX9 compiled-effects
default, OPENGLES3/WEBGL2 compiled-effects corpus, the FNA3D presentation fix (FNA3D-8, CNA keeps a named refusal),
FNA3D-3 sample mask, FNA3D-5 positive-level, FNA3D-7 (crash, unattributed; reproducer test is in the fork), the six
MojoShader SPIR-V opcodes still refused by name (TEXREG2AR, TEXREG2GB, TEXREG2RGB, TEXDP3TEX, TEXM3X2DEPTH, TEXDP3), and
the cnagaps B items that wait on SDL: G09 (SDL_GPU occlusion queries), G11 (per-draw MSAA switch), G12 (sample mask).

### 0.7 Next steps for Codex, in priority order

1. Read the sweep 12 result (0.8) and fix any failure it shows, one task per commit, with a regression test.
2. Work through the owner items that became possible without the owner: install nothing without asking; prepare
   `OWNER_TASKS.md` updates when a new hardware dependency appears.
3. Installed-package leg for the new OPENGL33 default: completed as GAP-13-PKG below.
4. Another test-quality wave: pick the ledger's open follow-ups (`TQ_TASKS.json` → `open_followups`, about 60, many are
   report-only findings from earlier waves), subsystems with weak assertions, and the coverage listing
   `python3 tools/audit_xna_runtime_surface.py --write-reports`; follow `cnaplan/test/tools/codex/TQ_RULES.md`.
5. FNA3D-3/-5/-7 and the MojoShader opcodes are now fork work that AI may do (see 0.3); each needs a regression in the
   fork's harness and a GPU run through the private runner before the pin moves.

### 0.8 Sweep 12 (finished) — failures to fix first

Sweep 12 follow-up TQ-SWEEP12-1: `SafeInputProvenance` failed in all seven trees because GAP-13
changed `CompiledEffectFixtures.hpp` without updating its source hash in
`tools/safe_inputs_manifest.py`. The verified hash is now recorded; the focused headless CTest and
licence-header check pass. Other sweep 12 failures below remain open.

Sweep 12 follow-up TQ-SWEEP12-2: the explicit GAP-08/GAP-13 CTest entries now run the aggregate
`CnaTests` binary built by the default target, instead of stale focused binaries excluded from
`all`. The directory-service contract harness is included in the default build. Headless and
SDLGPU full builds pass; the headless directory contract, GAP-13 policy and GAP-08 dispatch pass;
the SDLGPU policy, surface/native-window contracts and GPU presented-window suite pass on the
private runner. This fixes the missing executable and zero-test cases in those configurations;
other sweep failures still need per-renderer investigation.

Sweep 12 follow-up TQ-SWEEP12-3: `CaptureNativeClient` is compiled only when SDL3 is in the binary,
so the two headless failures are no longer discovered there. Its CPU-presenter pixel witness now
uses X Composite when available, avoiding the direct window read that timed out under the busy
sweep. Headless and SOFTWARE full builds pass; the two SOFTWARE capture cases pass both serially
and at CTest `-j8` on the private runner. Diagnostics confirm Composite selected and all 256
expected quadrant pixels checked. WEBGPU/FNA3D/OPENGL33 still need a fresh sweep.

Sweep 12 follow-up TQ-SWEEP12-4: the SOFTWARE native-window ownership failure was reproduced
with `Gap08NativeWindowTest.*` at CTest `-j8`: direct window capture read another test's pixels.
The suite's observation windows now use X Composite when available. All seven cases pass in the
same private-runner parallel command after the change; the SOFTWARE full build passes. The
other renderer configurations still need a fresh run.

Sweep 12 follow-up TQ-SWEEP12-5: OPENGL33 `Gap08bPresentedPixels.*` reproduced six failures
of thirteen at CTest `-j8`, including the sweep's MSAA case: parallel native windows obscure
the direct window reader before any region assertion. The X11 capture now prefers Composite;
the Win32 capture remains as before. The OPENGL33 full build and all thirteen cases at `-j8`
pass on the private runner after this change.

Run on `f6c9ece36` (all seven trees built, exit 0; evidence archived in
`/rv/data/development/github.com/libcna/cnaplan/evidence/rev002/sweep12/`: `summary.txt`, `<tree>-failed.txt`, gzipped
ctest logs). Every tree passed 99 %: multi 13 of 15349 failed, sdlgpu 4 of 14696, webgpu 5 of 14272, fna3d 5 of 14165,
software 5 of 14213, headless 5 of 13144, opengl33 6 of 15289. Sweep 11 (commit `1fd900487`) had 0 failures, so all of
these are new from this wave. None has been investigated yet. They fall into these classes:

1. `GamerServices_DirectoryServiceContract`, `Gap08_SurfacePresenterContract`, `Gap08_NativeWindowContract` report
   "Not Run" in most trees: fixed by TQ-SWEEP12-2 for the affected configurations above; other
   trees have not yet been rerun.
2. `SafeInputProvenance` ("provenance sanitizer tools") failed in every tree; fixed by
   TQ-SWEEP12-1 above.
3. `Gap13_CompiledPolicy_<IDENTITY>` fails (ten identities in the multi tree, one per default renderer elsewhere), and
   `Gap13_CompiledCorpus_{WEBGPU,SDLGPU}` in multi, plus `SdlGpuCompiledEffectDrawTest.SharedLegacyBumpEnvironmentContract`:
   policy cases in the single-renderer headless/SDLGPU trees were stale focused-binary runs and
   now pass after TQ-SWEEP12-2;
   the fresh multi qualification and its genuine WEBGPU/SDLGPU defects are recorded in §0.9.
4. `CaptureNativeClient.*` (new native-capture controls from GAP-08c) fail in webgpu, fna3d, software, headless and
   opengl33: TQ-SWEEP12-3 removes the two headless registrations and passes the SOFTWARE cases;
  focused WEBGPU/FNA3D/OPENGL33 qualification now passes (see §0.12); full sweeps remain pending.
5. Single failures: `Gap08NativeWindowTest.ReusesAKnownNativeIdentityWithoutTakingOwnership` (software),
   `Gap08bPresentedPixels.MultisampledRegionResolvesBeforeScaling` (opengl33). Both and their
   parallel suites are fixed by TQ-SWEEP12-4/-5.

Forcing a single renderer in the multi tree makes the `GraphicsRendererSelection*` and fallback tests fail by design; do not
count those. After fixing, rerun `cnaplan/test/tools/rev002_sweep12.sh` (about 3.5 h; set `SP` and edit `OUT`).

### 0.9 Focused multi-renderer qualification after the sweep fixes

TQ-SWEEP12-6 (2026-11-06): `cmake-build-multi` was rebuilt at the five preceding sweep-fix commits
and the ten `Gap13_Compiled(Policy|Corpus)_*` CTests (eight policy wrappers, two GPU corpus
wrappers) ran through the private GPU runner (`-j4`).
All six policy cases outside WEBGPU/SDLGPU pass. The four WEBGPU/SDLGPU cases expose real defects
that the stale focused binary had hidden. WEBGPU's policy case passes its two capability/pixel
checks, then wgpu-native aborts with `invalid bind group entry for bind group descriptor` inside
`Gap13CompiledEffectSpirvTest`; an isolated rerun reproduces the abort. SDLGPU's policy case
passes those two checks, then the SPIR-V envelope case throws that pixel shader sampler `s0`
has no bound texture. The SDLGPU corpus runs all 44 cases; 39 pass and five fail:
`Predication`, `LegacyTextureMatrix3Sample`, `LegacyTextureMatrix3Specular`,
`LegacyBumpEnvironment`, `SamplerResultSwizzle`. In that run WEBGPU's corpus also failed,
including a MojoShader predication refusal and a SPIR-V validation error; its inventory file
was missing after the process abort. Keep remaining cases red
until the effect translation/binding defects are repaired; do not relax the qualification gate.

TQ-SWEEP12-7: WEBGPU's compiled-effect sampler fallback now creates a neutral texture view of
the declared dimension before binding. Previously the uninitialized 2D white texture supplied
a null view, and wgpu-native aborted in the BEM arithmetic policy witness. The multi build and
`Gap13_CompiledPolicy_WEBGPU` now pass privately; an explicitly WEBGPU-selected cube/volume
sampler contract also passes. At that point `Gap13_CompiledCorpus_WEBGPU` still aborted because
a group2 binding expected Cube but received a D3 view. TQ-SWEEP12-8 below repairs that separate
dimension issue; it was not fixed by the neutral fallback.

GAP-13-PKG (owner-authorised, 2026-11-06): `cmake-build-opengl33` was configured with
`CNA_INSTALL_CXX=ON`. The installed C++ package checker ran with `--compiled-effects
--compiled-renderer OPENGL33 --keep-artifacts`; all 54 recorded steps passed, including the
relocated consumer's real compiled-effect pixel oracle on the private display. Audit and logs:
`cmake-build-opengl33/installed-cpp-probe/run-v_g25h97/audit.json` (untracked build output).

TQ-SWEEP12-8: legacy ps_1.x texture-matrix instructions have no sampler declaration, so
MojoShader initially defaults a 3-component lookup to Cube. The effect's sampler symbol can
instead specify a 3D volume. EasyGL's existing symbol-based reparse was moved to a shared
internal helper and applied to WEBGPU and SDLGPU. The formerly failing/crashing
`LegacyTextureMatrix3Sample` passes on both GPU renderers. OPENGL33's complete 44-case compiled
corpus still passes. Both GPU corpora now execute all 44 without this abort, with four other
failures each; WEBGPU policy passes and SDLGPU policy still fails on unbound sampler `s0`.
WEBGPU's four are `Predication`, `PredicatedTexkill`, `LegacyTextureMatrix3Specular` and
`SamplerResultSwizzle`. SDLGPU's four are `Predication`, `LegacyTextureMatrix3Specular`,
`LegacyBumpEnvironment` and `SamplerResultSwizzle`.

TQ-SWEEP12-9 investigation: the remaining SDLGPU policy failure is the ps_1_4 BEM arithmetic
case. MojoShader's `state_BEM` adds a reflected sampler with `texbem=2` to carry the bump-matrix
uniform, while its SPIR-V emitter does not sample an image. SDLGPU currently treats every
reflected sampler as requiring a bound texture and throws for `s0`. A simple CNA exception for
`texbem=2` would be unsafe if the same stage also has a real texture lookup. Next repair should
add a fork regression for BEM-only versus BEM plus sampling, expose whether the reflected stage
actually samples an image without changing its matrix layout, then let SDLGPU bind a neutral
texture only for the matrix-only case. Fork commits require their harness run and a full-SHA pin;
the other corpus failures remain separate.

### 0.10 Prepared fork repairs awaiting publication approval

TQ-SWEEP12-10/-11/-12/-13/-14/-15 are locally implemented on isolated CNA branch
`codex/tq-sweep12-forks`, source-only worktree `/tmp/cna-sweep12-forks`. They are **not merged
into `next`**: the owner has not yet approved the fork pushes, and `next` keeps its published
dependency pins. Qualification differs by task: task 15 has fork/FNA3D harness evidence;
the complete CNA renderer qualification below belongs to task 14. No build was created under `/tmp`.
The shared multi and OPENGL33 `CnaTests` targets were rebuilt after restoring those published
source pins, so their binaries no longer contain the isolated candidate's implementation.

| Task | CNA draft commit | MojoShader commit | FNA3D gitlink commit |
|---|---|---|---|
| TQ-SWEEP12-10 — matrix-only BEM metadata and neutral SDLGPU binding | `8e7a06d67` | `100025e` | `f2b34dc` |
| TQ-SWEEP12-11 — scalar-dot specular reflection | `b94acaa8c` | `b6125e5` | `c3d242f` |
| TQ-SWEEP12-12 — sampled-result selectors and typed TEXLDD gradients | `f4fe4d8e3` | `1ab34bd` | `d7d8adf` |
| TQ-SWEEP12-13 — Naga-compatible predicated TEXKILL | `480a1fbaa` | `599ef15` | `cba2142` |
| TQ-SWEEP12-14 — typed predicated destination writes | `2258cb263` | `2e281ca` | `856a302` |
| TQ-SWEEP12-15 — scale ps_1.x results before saturation | `c4061d687` | `4984c22` | `e95b485` |

MojoShader's local `cna` branch is in `../mojoshader`; FNA3D's local `cna-audit-fixes` branch
is in `/home/robertvokac/deps/libcna-FNA3D` (that worktree is returned to the published detached
pin; the branch retains all six commits). Do not disturb `../FNA3D`'s pre-existing submodule
change. Publish MojoShader before FNA3D only after explicit approval, verify remote SHAs, then
integrate the six CNA commits in order and rebuild/requalify the current tree.

Candidate evidence: MojoShader's normal harness passes 18/18 at task 10, 19/19 at task 11,
20/20 at task 12 and 21/21 at tasks 13–15. New numeric/module oracles pass under ASan/UBSan
at the pins where they were introduced or extended; FNA3D passes 29/29 at each rolled gitlink.
The BEM and sampled-texture refusal
controls pass, and deliberate wrong-denominator/dropped-selector/broad-fallback mutations fail
and were reverted. All eight multi policies and both original 44-case GPU corpora pass at
task 14, the first complete pass of both GPU corpora, with no changed expectations or skipped
cases. The BEM-pin OPENGL33
corpus also passed 44/44; the later pins have not been qualified on that route. These are
candidate results, not claims about the unmerged `next` source. Full details and exact pins
are in the draft branch's §0.9.

Task 13 selects zero for inactive TEXKILL components before the ordered-negative comparison;
its independent oracle covers 640 profile/predicate/selector/negation/negative-lane variants.
Task 14 selects the new or original destination immediately before storing, preserving scalar
depth, integer MOVA, boolean self-predicated SETP and masked/saturating float writes. Its oracle
adds 1,080 numeric variants. Original refusals/portability checks and deliberate dropped-negation
or bypassed-predicate mutations fail, and the mutations were reverted. The task-14 candidate's
ten GAP-13 wrappers pass privately; the shared multi target was subsequently rebuilt with
published pins and without the temporary BEM patch. Publication and current-tree integration
remain the next steps for these repairs.

Task 15 follows Claude's independent review and Microsoft's ps_1.x modifier contract: scaling
must precede saturation. Its 456 legal version/profile/shift/mask/MOV-or-ADD/saturation variants
reject the original order (156 value failures) and a wrong-x8-factor mutation. They also check
that saturation preserves masked-out values outside [0,1]. The 21-test fork harness, expanded
ASan/UBSan oracle, private real SDL GPU checks and 29-test FNA3D harness pass. The stand-in
ShaderCross case is explicitly skipped. CNA corpora were not rerun at task 15; its source
override was restored to the published pin without rebuilding CNA against the new fork.

### 0.11 Sanitizer fixture follow-ups

SAFE-007 follow-up (2026-11-06): `BufferDataBindingContractTests.cpp` no longer builds its
vertex arrays at namespace scope from another translation unit's dynamically initialized
`Color` constants. Two function-local static accessors retain the same vertices and assertions.
The refreshed `build-asan/CnaGraphicsTests` reproduced the invalid-vptr error before `main`;
after the fix, all nine buffer-binding tests pass on SOFTWARE with ASan/UBSan/LeakSanitizer
and halt-on-error enabled, on the private display, and in the normal OPENGLES3 multi aggregate.
No runtime/API behavior, stub or dependency
changed. The ledger's old SAFE-007 startup finding is resolved.

TQ-018 follow-up (2026-11-06): the unshared-cycle serializer refusal test now uses a local
scope guard to break its authored `shared_ptr` back-edge on every exit. The cycle stays intact
while serialization runs, and its original refusal/message assertions remain. The refreshed
sanitizer binary first reproduced a 96-byte leak in two `Chain` allocations; afterwards all
five `XnaIntermediateSerializerHardening` cases pass with ASan/UBSan/LeakSanitizer enabled.
The normal multi aggregate also passes all 14 affected buffer/serializer cases privately.
No product implementation or oracle was changed. The ledger's old test-cycle leak is resolved.

TQ-063 follow-up (2026-11-06): the shared-resource queue's FIFO-order fixture now breaks its
cycle with a scope guard rather than manual cleanup after `Compile`. An injected writer
exception during the queue drain reproduces a 432-byte leak in two nodes without the guard;
the same deliberate test failure leaks nothing with the guard. Fault injection was removed
and the final source rebuilt. All 16 `XnaContentWriter` cases pass under ASan/UBSan/LeakSanitizer;
the normal multi aggregate passes the queue case and the previous 14 fixture cases privately.
FIFO payload assertions and product code are unchanged. There is no remaining injected fault.

TQ-CAPI-NATIVE-1 (2026-11-06): the native shared-content witness now checks `root` before
reading it and `root->next` before cycle cleanup. Injecting a null root or null child made
the original helper SIGSEGV; both guarded variants instead report the failed native witness
and return status 1 through the C smoke's existing check. Both injections were removed.
All six `CApi_ContentValues_*` slices pass in the normal multi tree and, run directly on the
private display, with ASan/UBSan/LeakSanitizer. No C API export, public header or product code
changed. The sanitizer CTest attempt was blocked during unrelated global discovery by the
stale `build-asan/CnaTests` binary's missing `TouchCollection::operator[](size_t)` symbol;
rebuild that aggregate before a whole-tree sanitizer sweep. Its freshly built focused targets
and the six direct C smoke slices are the evidence here.

### 0.12 Remaining sweep-12 capture configurations qualified

TQ-SWEEP12-16 (2026-11-06): refreshed `CnaTests` and the directory-contract harness in the
existing WEBGPU, FNA3D and OPENGL33 trees, then ran the same 27 selected capture/window/harness
CTest entries through the private runner at `-j8`. WEBGPU and FNA3D each pass 14 entries and
explicitly skip the 13 GL/DX9-only `Gap08bPresentedPixels` entries; OPENGL33 passes all 27.
Both original `CaptureNativeClient` failures pass in each tree, as do the surface/native-window
wrappers and directory-service contract. This closes those focused qualification gaps, not
the full seven-tree sweep or the unpublished fork integrations in §0.10. No tests, assertions,
production code, dependencies or build options changed in this follow-up.
Source/configuration metadata and compressed build/test logs are archived at
`/rv/data/development/github.com/libcna/cnaplan/evidence/rev002/sweep12-capture-followup/summary.json`.

### 0.13 XACT test-quality follow-ups

TQ-XACT-1 (2026-11-06): added
`XactLoopVariation.InfiniteEffectVariationReappliesNewValuesWithoutTrackVariation` to the
shared two-adapter offline suite. The FAudio reference (`FACT_internal.c:271-278`, loop-mask
definitions in `FACT_internal.h:338-346`) forbids delegating an infinite wave to the mixer
when effects require NEW values on each loop, even if track variation is disabled. Literal
ADD pitch/gain receipts pin three successive activations, preserve the 255 budget and verify
explicit termination. Removing `!newEffects` from CNA's delegation guard leaves both old
offline suites green but fails the new case on each adapter. The mutation was reverted and
`Cue.cpp` restored byte-for-byte. Full final SDL3_mixer and CnaMixer suites pass 168/168 and
167/167 respectively. No production behavior, dependency, stub or public API changed.

TQ-XACT-2 (2026-11-06): added
`XactValueEvents.RpcOnlyReleaseRejectsAnEventAndRandomDrawAtItsExactDeadline`. A neutral
150-ms ReleaseTime curve and Stop at 20 ms admit the literal pitch event at 169 and reject
the random event at 170, including its random draw. The trace, admitted voice ratio, stopped
state and empty pending queue are independently asserted. Ignoring RPC expiry leaves both
earlier offline suites green but fails this case on both adapters; `Cue.cpp` was restored.

The RPC byte fixtures now use full 77-byte XGS headers, tool version 42 and six-byte name-index
records, retaining curve codes 107/130 and the existing Volume curves. The XSB helper uses
tool version 43 and can author per-track RPC lists. Neutral sound and track bindings enter
both CNA's existing sound-level release path and FACT's track-derived path without changing
gain/pitch. Layout and deadline rules were checked against local FAudio source, not against
observed CNA output; no native-FAudio binary execution is claimed.

Final complete SDL3_mixer/CnaMixer suites pass 169/169 and 168/168 normally and under
ASan/UBSan/LeakSanitizer. The shared-fixture NULL-audio clock suites pass 17/17 after an
explicit rebuild of the headless Cue object from restored production source. No production
behavior, dependency, stub, public API or expected value from an existing test changed.

TQ-XACT-3 (2026-11-06): seven new two-adapter regressions exposed and pin the missing
track-level volume/pitch/filter evaluation and the incorrect sound-level-only release scan.
Release now follows FACT's track lists, CUE accessibility and maximum last-point X; negative
cases cover sound-only, global, other-variable and pitch-only bindings. Indexed private
metadata lookup avoids making PUBLIC an internal RPC requirement. Each voice combines its
sound and own track values during playback, fades, release and immediate category changes.
Track filter axes inherit the sound values before their own overrides; an absent authored
filter stays disabled. The new high-frequency filter case also exposed a missing outer
coefficient cap: FAudio limits the result to1, while CNA returned1.2525 at9500Hz/44100Hz.
That case failed on both adapters before the helper correction; the old math tests were
corrected from the full reference, retaining independent low-frequency sine values.
Intrinsic AttackTime uses pause-adjusted elapsed and track origins,
while identically named globals retain their authored input. See `docs/xact-rpc.md`.

The first three release regressions and the two initial-value/composition regressions failed
before their respective production fixes on both adapters. Omitting track evaluation and
restoring the old wall clock each fails the added tests on both adapters; mutation sources
were restored byte-for-byte. Old AttackTime/ReleaseTime fixtures now use native full XGS
headers/name indexes and proper CUE variables; release XSBs bind the track list. Their expected
gains and timings were retained. No new public API, C ABI, stub, dependency or intentional
behavior deviation was introduced. The generated C inventory was refreshed; its public
symbol/mapping counts are unchanged, and its release-gate consistency check passes.

Final SDL3_mixer/CnaMixer suites pass 176/176 and 175/175 normally and with
ASan/UBSan/LeakSanitizer. The full focused audio executable passes 990 tests with one explicit
environment-default skip; NULL-audio clock regressions pass 17/17. Licence and whitespace
checks pass. FAudio source was inspected, not executed. Two pre-existing intrinsic-clock
differences identified during review remain explicit follow-ups in §5.1.

TQ-XACT-4 (2026-11-07): `CheckCueInstanceLimit` now restricts replacement candidates to
the same bank and cue definition, matching `FACT_internal.c:558-564,843-847`. The old code
and test incorrectly described an unfiltered bank-wide search as native behavior; no measured
XNA exception was found. `CueInstanceLimitReplaceOldestRetainsOlderDifferentDefinition`
first failed against that old implementation, then passed after the same-index guard.
It preserves an older unrelated cue, verifies the matching sibling's release and the incoming
cue's fade, and replaces the old sleep with an injected clock that is restored on every exit.
The broad exception-to-skip catch was removed; lack of a dummy-device voice remains explicit.
Full audio passes990 with one environment-default skip, both offline adapters pass176/175,
NULL clock tests pass17, and all195 cue/engine/category tests pass ASan/UBSan/LeakSanitizer.
Coverage-inventory, licence and whitespace checks pass. No public API, dependency, stub or
intentional reference deviation was added.

TQ-XACT-5 (2026-11-07): two new two-adapter tests pin independently derived per-track gain
through RPC-only release, its authored-fade alternative, and cue/category-triggered fade-in.
They cover Stop-relative ages0/15/99, zero intrinsic ReleaseTime outside the RPC phase,
distinct track values, category changes, exact termination, and mixed marker PCM. A delayed
voice is observed at dispatch before same-tick reconciliation can conceal a wrong initial
gain. Unlimited and FAIL controls verify that authored fade-in alone is not a trigger.

Three mutations (omit release RPC gain, omit the current fade on category changes, and
omit fades for late-starting voices) each leave both older complete offline suites green
(176/175 tests) but fail the new witnesses on both adapters. Production source was restored
byte-for-byte; only tests changed. Final complete suites pass178/178 and177/177 normally
and under ASan/UBSan/LeakSanitizer. Fixture offsets, floating-point literals and expected
values were independently reviewed against FAudio source. Immediate category-property
receipts pin CNA's current propagation timing; they do not establish native call timing or
Microsoft XACT parity. Evidence is retained in
`../cnaplan/evidence/rev002/xact-fade-shapes/`. No production API, behavior, dependency or stub
changed; licence and whitespace checks pass.

TQ-XACT-6 (2026-11-07): category replacement now searches only the incoming cue's bank,
while its live-instance count and volume remain engine-wide. The new two-adapter
`XactInstanceLimits` witness first failed against the old cross-bank eviction; a same-bank
control retains ordinary replacement. Both cases use unclamped, hand-derived gain and mixed
marker PCM through the half/end fade points and category changes. A mutation localizing the
count as well still passes both older complete suites178/177 but fails the new case on both
adapters. Mutation source was restored exactly.

Unmodified native FAudio `play_sound` was also executed on minimal stack-backed records,
without initializing an audio engine/device: both cases allow the new sound and enter fade-in
with category count2, but only the same-bank case marks the previous cue stopping. This
qualifies that selector branch, not native asset loading/playback or Microsoft XACT. No
measured XNA exception was found. Complete final offline suites pass179/179 and178/178,
including ASan/UBSan/LeakSanitizer; full audio passes990 with one environment-default skip,
and NULL clocks pass17. Public API, dependencies and stubs are unchanged; coverage inventory,
licence and whitespace checks pass. Preparation order/QUIETEST differences remain in §5.1.

TQ-XACT-7 (2026-11-07): both replacement selectors now traverse the bank's lifetime registry
in preparation order. QUEUE/OLDEST choose the first eligible playing cue; the native QUIETEST
stub chooses the last even when it is louder. LOWEST_PRIORITY retains native strict comparison
against255, so ties use preparation order and all-255 candidates produce no victim. Reserved
mode5 likewise produces no victim, while the triggered limit still applies fade-in. Two new
regressions first failed on both adapters: a table covers reversed play order, controls,
priority ties/sentinel and reserved mode, and unequal RPC gains distinguish the QUIETEST stub
from a minimum-volume implementation. Per-cue gain and mixed PCM pin half/end fades.

The unmodified native `play_sound` helper independently passed56 minimal-record cases across
both limit scopes, both play orders, priorities0/255 and modes1–7, without audio initialization
or asset/playback execution. Four production mutations (play-order traversal, first-candidate
QUIETEST, priority shortcut and reserved fallback) each leave older complete offline suites
179/178 green but fail the new tests on both adapters. Source was restored byte-for-byte.
Complete final offline suites pass181/181 and180/180 normally and with ASan/UBSan/LSan; full
audio passes990 with one explicit environment-default skip, all195 cue/category/engine tests
pass sanitizers, and NULL clocks pass17. No public API, dependency or stub changed; coverage,
licence and whitespace checks pass. Prepared-but-unplayed eligibility remains open below;
no measured Microsoft XACT parity or owner-approved exception is claimed.

The shared scheduler fixture now explicitly calls category `SetVolume(1)` before playback.
A separate unmodified native parser/SetVolume probe on literal XGS data measured initial
currentVolume1, authored byte128 amplitude0.508764982, and that amplitude after SetVolume(1).
This corrects the gain-oracle setup without changing its expected values or CNA runtime.
The constructor difference is repaired by TQ-XACT-8 below; the native probe opened no
audio device and its small harness passed ASan/UBSan/LSan.

TQ-XACT-8 (2026-11-07): fresh category current gain now remains1 until an explicit
SetVolume. Authored gain stays separate; the setter and its unusual child cascade are
unchanged. Two new offline regressions first failed on both adapters. They pin fresh
root/child state, lookup/update stability, live PCM after updates, direct setter
idempotence and child compounding independent of the parent's requested multiplier.
Existing value/loop/reverb/variation oracles now explicitly request their authored gain;
their expected values and tolerances were preserved. Incorrect constructor comments
were corrected. No public API, dependency or stub changed.

A second unmodified native parser/SetVolume probe parsed the same literal123-byte,
two-category XGS shape and measured both initial gains1, then child gains0.508764982,
0.258841813,0.254382491 and0.129420906 through the matching setter sequence. It opened
no audio device and its harness passed ASan/UBSan/LSan (reference library uninstrumented).
The PCM expectations are separately hand-derived, not captured native playback or a
claim of measured Microsoft XACT behavior. Two production mutations (authored constructor
seed and parent multiplier cascade) pass the older complete offline suites181/180 but
fail the new tests on both adapters. Source was restored byte-for-byte.
Final complete offline suites pass183/183 and182/182 normally and with ASan/UBSan/LSan;
full audio passes990 with one explicit environment-default skip, sanitizer cue/category/
engine cases pass195, and NULL clocks pass17. Coverage inventory, licence and whitespace
checks pass; the C API release-gate report remains consistently not-ready for its existing
four planned symbols. Evidence is archived at `../cnaplan/evidence/rev002/xact-category-initial/`.

TQ-XACT-9 (2026-11-07): reverb send now shares the volume/pitch/filter RPC variable
resolver. Only CUE variables select intrinsic clocks (PUBLIC is irrelevant); global
AttackTime/ReleaseTime keep stored values. Sound codes use track zero's origin and track
codes their own origin; RPC release uses whole elapsed milliseconds and remains distinct
from authored fades. The obsolete wall-clock field was removed. Five new offline cases
cover both CUE accessibility variants, two delayed tracks with/without sound codes,
release/fade precedence and CNA's existing pause-freezing policy. Four cases first failed
on both adapters; the authored-fade control already passed. Intrinsic inputs intentionally
bypass authored variable bounds. Fractional testing uses an aligned release start and
qualifies duration truncation, not arbitrary native timestamp quantization.

The literal reverb XGS builder now emits native name indexes. Unmodified native parsing
accepted all five emitted settings variants (three/four RPCs and two22-parameter presets).
A separate probe compiled unmodified `FACT_internal.c` and exercised its static RPC
resolver in seven minimal-record cases, confirming global/private/intrinsic/release input
selection with no engine/device initialization. Both probe harnesses pass ASan/UBSan/LSan;
the resolver translation unit is instrumented, the linked FAudio library is not.
Native FAudio accumulates the send but leaves routing unimplemented at line1601. Thus the
PCM arithmetic qualifies CNA's approved routing policy, not native reverb playback or
Microsoft XACT. The shared sound-level mutable-origin issue is repaired by TQ-XACT-10
below; the native-pause limitation remains. No measured XNA exception or new routing policy was invented.
Six isolated send mutations (scope, absent origin, sound using voice origin, fractional
release, release during authored fade and dropped retained scheduler time) each pass the
older complete offline suites183/182 but fail the new cases on both adapters. Sources
were restored exactly before final comment cleanup. Complete final offline suites pass
188/188 and187/187 normally and with ASan/UBSan/LSan; full audio passes990 with one explicit
environment-default skip, sanitizer cue/category/engine cases pass195, and NULL clocks
pass17. No public XNA API, dependency or stub changed. Coverage, licence and whitespace
checks pass; the existing four-planned-symbol C API release report is current, not ready.
Evidence is archived at `../cnaplan/evidence/rev002/xact-reverb-rpc/`.

TQ-XACT-10 (2026-11-07): sound-level AttackTime now uses track zero's mutable first
event-instance origin, snapshotted before each scheduler dispatch. Track-level curves
retain authored origins. Both ordinary RPC and reverb callers explicitly distinguish
sound/track codes. Repeated getters reuse the pass's origin; a second engine update at
the same clock time captures the event's newly advanced timestamp. Elapsed-time getter
evaluation remains CNA's existing policy; off-update native asynchronous equivalence
was not established.

Unmodified native UpdateSound was compiled into an ASan/UBSan/LSan harness and executed
on two wave-less tracks, no audio/engine initialization or assets. A first volume event
with two10ms repeats moves instance time0→10→20. At elapsed0,5,10,10,11,20,21 the sound
curve yields0,-4000,0,-4000,-4000,0,-200cB while both track curves yield
0,-1000,-2000,-2000,-2200,-4000,-4000cB. Both RPC passes execute before event advancement;
unsigned underflow between repeats clamps the sound curve to its upper endpoint. The
other track's -1000cB value remains independent. The linked FAudio archive is uninstrumented;
the included reference translation unit and harness are instrumented. No native voice
playback, asset loading or Microsoft XACT claim is made.

Two new offline regressions pin hand-derived per-voice gain and mixed PCM for sound-only,
track-only and combined bindings, including repeat boundaries, the duplicate10ms update,
post-final-event time and repeated getters. The volume case first failed on both adapters;
the reverb companion also rejects the restored old authored-origin logic. Send PCM remains
CNA policy because native leaves routing unimplemented. Four mutations (authored sound
origin, post-dispatch live origin, mutable track-zero origin, reverb sound flag) each pass
the older complete suites188/187 but fail the new tests on both adapters. Source was
restored byte-for-byte. Final complete offline suites pass190/190 and189/189 normally and
with ASan/UBSan/LSan; full audio passes990 with one explicit environment-default skip,
sanitizer cue/category/engine cases pass195, and NULL clocks pass17. Public XNA API,
dependencies and stubs are unchanged; coverage, licence, whitespace and report-consistency
checks pass. Evidence is archived at `../cnaplan/evidence/rev002/xact-sound-rpc-origin/`.

TQ-XACT-11 (2026-11-07): a scheduled wave entering during RPC-only release no longer
uses a leftover fade-in multiplier. StartWave now permits fade-in only while Playing;
authored fade-out retains its own fraction. The new two-adapter test triggers fade-in
through a cue instance limit, stops it early, and observes the delayed voice's configured
gain and actual mixed PCM before reconciliation can repair it. Ongoing fade-in and
authored fade-out controls remain separate. An explicit category write establishes the
existing voice's current gain before that capture. The test first failed only in the
RPC-release branch on both adapters; old complete suites190/189 remained green.

An instrumented native transition/UpdateSound probe measured0.4,0.8 and1 for those
three phases. Wave state/volume/pitch APIs and the clock were wrapped; no engine/device
was initialized and no native playback or first-frame timing was measured. It executes
unmodified reference phase math; the linked FAudio archive is uninstrumented. Two
production mutations (retain fade-in during release, omit authored fade-out at start)
each pass older complete suites190/189 but fail the new case on both adapters. Source
was restored byte-for-byte. Complete final suites pass191/191 and190/190 normally and
with ASan/UBSan/LSan; full audio passes990 with one explicit environment-default skip,
sanitizer cue/category/engine cases pass195, and NULL clocks pass17. No public API,
dependency or stub changed; coverage, licence, whitespace and report-consistency checks
pass. Evidence is archived at `../cnaplan/evidence/rev002/xact-start-release-phase/`.

TQ-XACT-12 (2026-11-07, native qualification only): an isolated probe now executes
actual native FACTSoundBank_Prepare, FACTCue_Play and FACTCue_Destroy on a manually
backed single-sound bank. It includes unmodified FACT_internal.c for the fade-expiration
update/destruction path, uses a real SDL mutex and injected clock, and initializes no audio
engine/device or native assets/waves. Four cases cover cue/category limits and both
preparation orders. Prepare allocates a playingSound but leaves both counters0. With
one genuinely playing cue, the incoming Play raises both counters to2 and OLDEST selects
the prepared cue if it is earlier, otherwise the playing cue. After the selected fade
expires and native DestroySound runs, both counters are1 in either case, although two
cues are still actually playing when the prepared cue was selected. A fifth case calls
public Destroy on a lone never-played prepared cue and measures both counters0→255.
Different authored fade lengths (cue50/category70) select the correct expiry target.
An intermediate UpdateSound at1030 measures sound AttackTime input1030 for the never-played
victim (start0), versus30 for the played victim (start1000). After destruction the former
retains PREPARED alongside STOPPED. These are controlled wave-less helper observations,
not the native API thread's lifetime or audible output.

The probe passes ASan/UBSan/LSan; its included internal reference translation unit is
instrumented, while public Prepare/Play/Destroy come from the uninstrumented reference
archive. These are measured native single-sound lifecycle effects, not parsed-bank,
variation-shape, asynchronous-thread or Microsoft XACT qualification. CNA runtime/tests
were not changed by this qualification task. Its derived live counts cannot reproduce
these effects merely by admitting Prepared candidates. The §5.1 follow-up therefore
needs complete preparation/count/destruction regressions, including future plays after
the counter diverges, before a predicate-only fix can be called complete. Evidence is
archived at `../cnaplan/evidence/rev002/xact-prepared-cue-lifecycle/`.

TQ-XACT-13 (2026-11-07): Cue gain no longer has a unity cap. All seven dispatch/update/
category paths use a private SoundEffectInstance setter with FACT wave bounds0..16777216,
after the complete category/event/RPC/fade product. The public Volume setter retains its
measured XNA0..1 validation, including NaN refusal. No public API, object layout, C ABI,
stub or dependency was added. A native FACTWave_SetVolume probe measured1.25/8 unchanged,
2^24 unchanged,2^25 capped to2^24 and negative input floored to0, including the forwarded
FAudio voice write. Its harness passed ASan/UBSan/LSan; the linked reference archive was
uninstrumented. No device, native PCM or Microsoft XACT execution is claimed.

Four new two-adapter regressions pin literal positive gain and actual mixed PCM before
dispatch reconciliation, during steady/category updates, fade-in/settling, authored
fade-out and RPC release. The bound test distinguishes cap-after-fade from cap-before-fade
for a newly scheduled voice as well as an existing one. Small literal PCM markers and
master attenuation avoid clipping; this does not qualify mixer clipping parity. All four
first failed against the old producer while the old complete suites191/190 passed.
Nine production mutations cover each of the seven gain writes, early maximum application
and private unity cap; every new four-case suite fails each mutation on both adapters.
Older suites also reject release, steady, start and private-cap mutations after the oracle
corrections; they stay green for the other five. Both producer sources were restored
byte-for-byte, with SHA256 receipts.

Existing tests that had encoded the old cap now use independent authored-byte amplitudes:
byte180 gives1.0024019735836718, byte255 gives1.9977355869281581, and composed gains
retain these factors. PCM tolerances were retained, with the new tests using2ppm and a
1e-10 absolute floor. Shared category fixtures explicitly establish/restore their gain;
two wall-clock fade checks now use exact injected80% times and add numeric assertions.
Complete offline suites pass195/195 and194/194 normally and with ASan/UBSan/LSan; full
audio passes990 with one explicit environment-default skip in both configurations, and
NULL clocks pass17. Four touched gain/fade tests also pass three shuffled repetitions.
Coverage counts remain467 headers/8152 symbols (7510 implemented,17 partial,4 planned,
621 not applicable); only the header fingerprint/line inventory was regenerated.
Coverage, licence and report-consistency checks pass; the C API release report still
records its existing four planned entries. Evidence is archived at
`../cnaplan/evidence/rev002/xact-positive-gain/`.

TQ-XACT-14 (2026-11-07): total Cue pitch now uses FACT's two-octave bounds
(-2400..2400 cents), rather than the public SoundEffectInstance.Pitch one-octave range.
A private Cue-owned setter carries the full normalized pitch to the existing `2^pitch`
composition, while public setter validation remains-1..1. The complete base/effect/
sound-RPC/track-RPC/event cent sum is truncated toward zero before the total bound, also
on tracks without value events. No public API, C ABI, object layout, stub or dependency
changed. Qualification is limited to finite, signed16-bit-representable sums; it makes no
overflow/non-finite or Microsoft XACT parity claim.

Four new two-adapter regressions cover both signs at1500,2400 and3000 composed cents,
fractional RPC with no value events, all four authored/RPC/effect/event contributors,
dispatch-before-reconciliation, changing pitch during every fade branch, and a sloped
RPC release with a delayed wave. A literal441Hz sine verifies actual rendered frequency
at8192 frames, complete-period count within1.5 cycles, RMS bounds and identical channels.
Threshold hysteresis rejects small resampler startup ripples without discarding frames
or increasing tolerance. A separate native FACTWave_SetPitch probe measured both signs
through1500/2400/3000 cents and captured voice ratios0.25..4. It opened no device and
measured no native PCM; its harness passed ASan/UBSan/LSan while the reference archive
was uninstrumented.

Eighteen production mutations cover a restored unity cap at all six writes, omission of
each write, wrong total/private bounds, missing truncation, separately truncated terms,
an early RPC cap and the complete old producer. Every mutation fails the new suite on
both adapters; older suites also reject five of them. In particular the complete old
producer passes195/194 older cases but fails all four new tests on both adapters. Both
producer sources were restored byte-for-byte with SHA256 receipts. Final complete offline
suites pass199/199 and198/198 normally and with ASan/UBSan/LSan; full audio passes990
with its one explicit environment-default skip in both configurations, and NULL clocks
pass17. Coverage counts remain467 headers/8152 symbols, with only the header fingerprint/
line inventory regenerated. Coverage, licence, whitespace and report-consistency checks
pass; the existing C API release status remains not ready. Evidence is archived at
`../cnaplan/evidence/rev002/xact-pitch-range/`.

TQ-XACT-15 (2026-11-07, native qualification): a continuation probe checks90 native
snapshots across14 fresh fixtures, covering both cue/category limits and both preparation
orders. Successive evictions after an unplayed prepared victim leave counters1 with two
Playing-bit cues; destroying their stopped handles does not decrement again. Stopping
another playing cue can then leave counter0 with one still playing, so the next Play
bypasses the limit with no victim or fade-in. Cleanup after the undercount leaves255
with no playing cues. After lone unplayed Destroy wraps both counts to255, the next
Play hits both limits, including the nominally unused255 limit; category's60ms fade-in
overrides cue's40ms without a victim, and increment wraps both counts to0. A second Play
then coexists without fading; a third selects the first with the selected scope's50/70ms
fade-out and40/60ms fade-in.

FAIL rejection is now measured too: a new cue's prepared sound is stopped before its
increment, so a rejection changes counts1→0 while the existing cue keeps playing. The
next Play bypasses the limit. A second rejection decrements again; draining the two
playing cues leaves254. Rejected handles retain PREPARED|STOPPED and their later Destroy
does not decrement again. Separate normal controls measure exactly one decrement on
Stop or Destroy during an authored fade, no decrement on repeated Stop/Destroy of an
already stopped cue, and final counts0. The authored Stop control uses cue fade50ms
even in the category fixture. Playing-bit counts include genuinely played fading victims.

The probe calls actual public Prepare/Play/Stop/Destroy and includes unmodified native
UpdateSound/DestroySound for controlled expiry, with a real SDL mutex and injected clock.
ASan/UBSan/LSan are clean; the included internal reference unit is instrumented, while
the linked public-function archive is not. Unused, potentially uninitialized fade fields
are not read. This is single-sound, wave-less helper qualification, with no native asset
parser, device, PCM, API thread or Microsoft XACT claim. CNA behavior is unchanged; its
derived-count source comments now qualify the discrepancy, and affected targets build.
The durable current/native boundary is `docs/xact-instance-limits.md`; complete CNA
preparation/count/destruction regressions and other native cue shapes remain outstanding.
Evidence is archived at `../cnaplan/evidence/rev002/xact-prepared-continuation/`.

TQ-XACT-16 (2026-11-07): pitch RPC scopes now remain separate until VoicePitch adds
integer base/effect, sound RPC, track RPC and event contributions in native order.
The private RpcResult no longer carries an unused normalized pitch/combined cent sum.
Merging sound-1199.9f and track+.9f before base1200 rounds the RPC sum to-1199 exactly,
giving1 cent; native left-to-right addition gives16776806/16777216 cents and truncates0.
The negative mirror also truncates0. Exact binary -1199.5f/.5f controls produce+/-1 cent.
Volume, filters, reverb, public API/ABI and object layout are unchanged; no stub or
dependency was added.

An instrumented native UpdateSound probe checks30 samples with actual public
FACTWave_SetPitch and captured forwarded ratios. Its wave state/voice writes are wrapped,
with a real SDL mutex and float evaluation method0; public functions come from the
uninstrumented reference archive. ASan/UBSan/LSan are clean. It preloads integer base and
event values; native effect creation, event activation, phase timing, PCM, API threads,
Microsoft XACT and out-of-range/non-finite behavior are not claimed. Two new two-adapter
tests separately bind sound/track RPC codes and check both signs, base600+effect600,
event-last, sound-only/exact controls, live boundary changes, all fade paths and delayed
dispatch before reconciliation. One-cent checks use real forwarded ratios, not PCM.

Seven production mutations merge/swap scopes, move effect/event contributions, truncate
early, or restore the complete old Cue.cpp/Cue.hpp pair. Every mutation fails the new
suite on both adapters; two are also rejected by older tests. The complete old producer
passes199/198 older cases but fails both new cases on both adapters. Both files were
restored byte-for-byte, with SHA256 receipts. Final complete offline suites pass201/201
and200/200 normally and with ASan/UBSan/LSan; full audio passes990 with its one explicit
environment-default skip in both configurations, and NULL clocks pass17. Coverage stays
467 headers/8152 symbols with the same fingerprint; licence, whitespace and current
coverage/release reports pass consistency checks (release remains not ready). Evidence is
archived at `../cnaplan/evidence/rev002/xact-pitch-order/`.

TQ-XACT-17 (2026-11-07): effect ADD state now remains congruent modulo65536, and
VoicePitch narrows the combined sound/effect integer base before any float RPC/event
contribution. This matches native's stored int16 base while retaining CNA's delta
representation. Bounded integer-valued delta storage avoids float precision loss during
long sequences. Public setters, RPC order, volume, filters, object layout, API/ABI,
dependencies and stubs are unchanged.

Unmodified native GetNextWave plus actual public SetPitch check114084 forced calls,
covering both signs, ADD+NEW, NEW, HOLD and no-pitch controls. Sound900/draw300 gives
base32700 at call106 and-32536 at107; the final wave clamp then changes ratio4→0.25.
The negative mirror reverses that change. The draw301 extension verifies every step
through55742 and records final base+/-2026. Its unbounded-float diagnostic instead
folds to+/-2024. The harness uses equal min/max bounds, wrapped wavebank preparation and
voice writes, a real SDL mutex, and zeroed audio/master records only for default filter
math. Included internal reference code is instrumented; public SetPitch comes from the
uninstrumented archive. ASan/UBSan/LSan are clean. This measures native integer narrowing
on Linux x86_64, not portable C narrowing, Microsoft XACT, asset/device/PCM execution,
complete native repeat scheduling or infinite255 parity.

Two new two-adapter regressions first failed while old complete suites201/200 passed.
One renders255 finite complete ADD voices, with NEW/HOLD controls, real ratio receipts
and the16x PCM-duration change at106/107. The other directly advances real effect state
and checks the actual next voice after55742 activations, explicitly without intermediate
voices/timing claims. Six production mutations omit either narrowing, clamp state too
early, drop ADD, ignore NEW or restore the old producer. Every mutation fails the new
suite on both adapters; two also fail older tests. In particular omitting delta narrowing
alone fails the long case and omitting base narrowing alone fails the finite case.
Sources were restored byte-for-byte with SHA256 receipts. Complete final suites pass
203/203 and202/202 normally and with ASan/UBSan/LSan; full audio passes990 plus its one
explicit environment-default skip, and NULL clocks pass17. Coverage counts/fingerprint
stay unchanged; licence, whitespace and current coverage/release reports pass consistency
checks (release remains not ready). Evidence is archived at
`../cnaplan/evidence/rev002/xact-effect-pitch-wrap/`.

TQ-XACT-18 (2026-11-07): voice volume now retains authored sound/track and effect
centibels, adds the separate sound RPC, track RPC and event sums in native float order,
and converts once before multiplying category and fade gains. All seven gain-write paths
share that composition. The parser retains its existing amplitude projections as metadata;
authored sound/track byte0 is negative infinity, preserving true silence. Internal IR and
playback records change representation; public XNA APIs and the native C API/C ABI do not.
Pitch, filters, existing NaN refusal and the effect-byte0 parser policy are unchanged.
No dependencies or stubs were added.

An instrumented probe of unmodified native GetNextWave performs80 forced ADD draws with
sound/track bytes180 and effect byte90, carrying active/upcoming numeric state. The base
is-95526.1015625cB. Actual public category SetVolume(100000000) gives100080000; UpdateSound
before/after a live76000cB RPC change writes0 then0.01727016642689705 through actual public
FACTWave_SetVolume. Every input and the expected final gain are finite; the old separately
converted effect underflows to zero and permanently silences the voice. The probe wraps
wave preparation/state and voice writes, uses a real SDL mutex and zeroed audio/master
records only for filter defaults, and links public functions from the uninstrumented
archive. ASan/UBSan/LSan are clean. It qualifies arithmetic, not asset parsing, complete
native loop scheduling, fades, release, devices, PCM or Microsoft XACT.

Three two-adapter regressions reject that underflow at dispatch, after79 complete waves,
during live RPC/category writes and in every fade/release branch, checking actual small
PCM markers at2ppm as well as forwarded gains. The finite loop fixture uses raw80 and
stops on its80th voice before exhausting the budget; it makes no native repeat-cardinality
claim. Two direct controls check nonzero authored bases under HOLD/NEW/ADD and silent
sound/track inputs. Read-only Claude implementation and control reviews found no defect.
Fourteen production mutations independently convert the base too early at each gain site,
restore component conversion, omit the authored base, use wrong RPC/event units, drop
either parser contribution, or restore the complete old producer. All14 fail the new
suite on both mixers; five also fail older tests. The old producer passes203/202 older
cases but fails all three new cases on both. All six production files were restored
byte-for-byte with SHA256 receipts. Final complete suites pass206/206 and205/205 normally
and with ASan/UBSan/LSan; full audio passes992 with its one explicit environment-default
skip in both configurations, and NULL clocks pass17. Licence, whitespace and current
coverage/release consistency checks pass; coverage remains467 headers/8152 symbols with
the same fingerprint, and release remains not ready. Evidence is archived at
`../cnaplan/evidence/rev002/xact-volume-composition/`.

TQ-XACT-19 (2026-11-07): a native Prepare/Play/DoWork probe qualifies event-repeat
cardinality separately from the earlier forced arithmetic helpers. Twelve cases cover
plain PlayWave and effect-only PlayWave with flags0x2201 at raw0,1,2,80,254,255.
Initial preparation consumes a positive count: finite cases activate max(raw,1) waves.
Plain255 prepares/activates one wave with wave-level loop255; effect255 instead activates
255 finite waves because NEW disables delegation. The first Claude derivation incorrectly
said raw+1 despite describing that initial decrement; a correction and executable native
results establish the difference. No quota refusal occurred during those reviews.

The harness calls real public SoundBank Prepare, Cue Play and Engine DoWork and unmodified
internal GetNextWave, ActivateEvent and UpdateSound. It wraps all wave operations with
pooled records, injects STOPPED completion states, and services prefetch before promotion.
Included internal code is instrumented; public APIs come from the uninstrumented archive.
All12 rows and cleanup pass ASan/UBSan/LSan, using a real SDL mutex and injected clock,
without a parser, device, native PCM, API thread or Microsoft XACT. Plain255 proves
delegation only. CNA still uses finite additional repeats and owner-approved infinite255;
no runtime policy changed here. `docs/xact-looping.md` and §5.1 now state the discrepancy
and the required finite whole-wave/PCM regression scope; the obsolete DESIGN.md pointer
in the buffer-level test helper was corrected. Existing TQ-XACT-18 audio builds/results
remain applicable: only documentation and that comment changed. Evidence is archived at
`../cnaplan/evidence/rev002/xact-finite-cardinality/`.

TQ-XACT-20 (2026-11-07): Cue now manages every positive finite event, with raw-1
future activations after its initial voice. Every activation passes wave-level loop0,
so plain events replay complete waves just like varied events; raw0 and raw1 both play
once. Lower-level SoundEffectInstance/mixer buffer counts remain additional passes.
The existing owner-approved infinite255 behavior is preserved, with its nondelegated
native difference explicitly commented. Public APIs, C ABI, effect/RPC composition,
dependencies and stubs are unchanged. Native prefetch timing is not implemented or claimed.

Two new two-adapter regressions first fail against the old producer while all206/205
older cases pass. They cover finite0/1/2/80/254, all four PlayWave event families,
unconsumed budgets on early updates, ordered selection/draw counts, and complete PCM8,
PCM16 and ADPCM prefix/region/suffix across resident/streamed and through-EOF metadata.
Simple/effect counts use TQ-XACT-19's native measurements; other families and selection
controls are source-derived. Older event fixtures now author the total activation count
needed for their original pitch/RNG/filter assertions. Their rendered EOF guards remain
explicit, followed by a control-thread Update; no PCM is discarded or callback promotion
invented. WMAv2 event fixtures likewise replay complete golden decoded waves, while two
separate parameterized buffer tests retain the previous additional-pass/region oracles.
NULL tests arm the controller explicitly and retain their one-shot/audio-free boundary.
The long finite pitch-wrap fixture now uses254 complete voices; its106/107 and55742
arithmetic witnesses are retained.

Eight production mutations alter the finite budget, plain delegation, per-voice loop,
region application, decrement, promotion, completion gate or complete old producer.
All8 fail the new suite on both mixers; all older-suite runs also reject them. The first
premature-promotion attempt exposed a stored raw voice pointer in an older pause test;
identity assertions now reject replacement before dereferencing it, so the final32 runs
all terminate with ordinary gtest status1 rather than a crash. Both production files were
restored byte-for-byte with SHA256 receipts. Final complete suites pass210/210 and209/209
normally and with ASan/UBSan/LSan; full audio passes992 plus its one explicit
environment-default skip in both, and NULL clocks pass17. Read-only Claude implementation
review found no actual defect. Licence, whitespace and current coverage/release consistency
checks pass:467 headers/8152 symbols and the same fingerprint; release remains not ready.
Evidence is archived at `../cnaplan/evidence/rev002/xact-finite-event-budget/`.

TQ-XACT-21 (2026-11-07): accepted XSB43 now uses its five-byte complex-track metadata
and one-byte legacy effect tail. The version is passed to the shared type4/type6 decoder;
legacy pitch/volume flags are mapped, absent filter/Q/frequency fields remain disabled/zero,
and modern44/45/46 fields pass through unchanged. Following sounds and type6 candidate
tables retain the correct cursor. Header-validation and effect-byte0 policies are unchanged;
no public API/C ABI, dependency or stub changed.

Actual public native CreateSoundBank/Destroy parses259 literal banks: all256 legacy flag
bytes plus44/45/46 controls, with two type4/type6 tracks, a five-byte gap, a following
sound at221/263 and a cue referencing it. All259 cases check mapping, filter/effect fields,
candidate weights, offsets, input fields and cue sound codes. Included internal source is
unmodified and instrumented; public entry points come from the uninstrumented archive.
Real SDL apiLock/sbLock and ordinary allocation suffice; no device, thread or PCM runs.
Native UBSan first diagnoses its packed read_u16 at header offset19. That diagnostic is
retained; only native alignment checking is excluded in the final run. ASan, remaining
UBSan and LSan pass, with exactly512 expected diagnostics for ignored0x02/0x08 bits.
CNA keeps all sanitizer checks, including alignment, enabled. These are LE format
measurements, without Microsoft XACT or native playback claims.

Two new regressions first fail while all993 older registered audio tests remain green
apart from their one explicit environment-default skip. A third new control refuses all
truncated event-block prefixes without executing malformed native buffers. Final tests
cover the same259 sound/track inputs, modern metadata, following-sound cue resolution,
both effect families and default fields. Nine production mutations restore modern track
or effect layouts, mis-map flags, retain unknown bits, lose version propagation, enable
an absent filter or restore the old parser. All9 fail the new suite; only the mutation
using legacy tails for modern files also fails the16 older event-IR controls. The source
is restored byte-for-byte with a SHA256 receipt. Complete suites pass210/210 and209/209;
full audio passes995 plus its one explicit skip, normally and with full CNA ASan/UBSan/LSan.
The NULL parser/clock selection passes36. Read-only Claude reviews found no XSB
implementation defect; their scope observations strengthened the native cue/input checks.
Licence, whitespace and current coverage/release consistency checks pass, with unchanged
467/8152 counts/fingerprint and release still not ready. Format boundaries are documented
in `docs/xact-bank-formats.md`; evidence is at `../cnaplan/evidence/rev002/xact-xsb43/`.

TQ-XACT-22 (2026-11-07): the shared XWB segment reader now consumes the separate
header-version word for content>41, matching native. Content42/43 no longer reads every
segment four bytes off; resident and streaming-header paths change together. The old
synthetic no-header43 control is correctly authored as41, and stale threshold comments
in test/example builders and the internal field documentation are corrected. No header
whitelist, lower-version compatibility, public API/C ABI, dependency or stub changed.

Sixteen native valid LE banks cover41 without that word and every42–46/header42/43/44
pair. Public CreateEngine/CreateInMemoryWaveBank, property queries, DoWork, Destroy and
Release locate one PCM entry at168/172, with format0x810AC444 and duration2 derived from
play length4. There is no Initialize, device, voice, audio thread or native PCM rendering.
The first LSan run exposes the pinned Release losing sbLock/wbLock after ShutDown zeroes
them:1280 bytes in32 allocations across16 engines. The diagnostic is retained. The final
harness captures those handles and cleans the orphaned mutexes after Release; this is
specific to the pin, not a reference-source change or a claim that native Release is
leak-free. Included internal code is instrumented; public APIs come from the uninstrumented
archive. With explicit cleanup all16 rows pass full native ASan/UBSan/LSan and stderr is
empty. Header combinations are native accepted formats, not claimed historical Microsoft
encoder output or native streaming/encoded-bank qualification.

Two new regressions first fail while all996 older registered audio tests pass apart
from their one explicit environment-default skip. They cover16 resident and32 source-derived
CNA streaming/embedded controls at0/2048, exact metadata, marker-byte placement and lazy
prefix sizes. Six production mutations miss42, read an extra word for41, discard the
reported header, consume another word, repair only the resident caller or restore the
old producer. All6 fail the new suite; three also fail older metadata controls. The
resident-only mutation passes the resident test and fails only the streaming test.
The source is restored byte-for-byte with a SHA256 receipt. Final complete suites pass
210/210 and209/209, and full audio997 plus its one explicit skip, normally and with
ASan/UBSan/LSan; the NULL metadata/clock selection passes31. Read-only Claude reviews
confirm the layout, cleanup and implementation. Licence, whitespace and current coverage/
release consistency checks pass, with467/8152 counts and unchanged fingerprint; release
remains not ready. `docs/xact-bank-formats.md` records the scope, and evidence is archived
at `../cnaplan/evidence/rev002/xact-xwb-header/`.

TQ-XACT-23 (2026-11-07): compact XWB final entries now exclude their own alignment
slack, with a64-bit offset+deviation guard before subtraction. The former A-12 test
asserted pinned FAudio's final-slack behavior; it is corrected against untouched
Microsoft SDK authoring outputs and Microsoft's independent format reader. No public
API/C ABI, dependency, stub, codec gate, compact Duration or non-last arithmetic changes.

The new owned CC0 corpus has normal/compact XactBld3.exe9.29.1962.0 Windows banks
from the same mono PCM16 sources at22050Hz:8/7/33 frames and markers4096/8192/12288.
Every payload equals the source WAV body. Compact offsets0/2048/4096 in a6144-byte
wave-data segment have deviations2032/2034/1982, hence16/14/66-byte bodies. The final
1982 padding bytes previously added991 silent frames, making SoundEffect1024 instead
of33 frames. Microsoft DirectXTK861f3e7df437b50d1f28c964769329e9b23a50fb's
ComputeLocations also subtracts final deviation. This is independent SDK-format evidence,
not a claim of measured XNA behavior; the XNA-over-FNA ledger is intentionally unchanged.

A genuine Microsoft XACT3.7 SDK interface probe now works with the owner's existing
June2010 package, extracted only into ignored build-probe/xact-microsoft. Wine-prefix
XACT DLLs proved to be Wine builtin, so the probe explicitly loads authentic retail
x86/x64 DLLs9.29.1962.0, verifies loaded paths and method ownership, and initializes
against Wine's XAudio2 using only the private display/audio-null runner. Both architectures
report format0x810AC444 and durations8/7/33 for the normal corpus bank. A compact bank
requires Streaming=1 for SDK emission. It reaches PREPARED/count3, but Microsoft's
bank/instance property calls misread compact words as normal metadata and entries1/2
Prepare fails8AC70019. Those diagnostics are retained and explicitly excluded from
compact-duration/playback evidence. No PCM is played; no managed XNA/native Windows
parity is claimed. The SDK binaries/headers are not committed. The probe must wait for
stream PREPARED before querying; an initial premature-query failure is retained.

Four new tests fail on the old producer while all998 older registered audio tests pass
apart from their one explicit skip. They check exact authored PCM and shared format,
normal-bank controls, resident/streaming/embedded offsets and lazy prefix sizes, final
empty-span acceptance and underflow (including2^32 sum), and all three cached SoundEffect
durations in both storage modes. Six production mutations all fail the4 new tests with
ordinary assertion failures; three also fail older compact controls. Exact source
restoration is verified by SHA256. Final full audio passes1001 with its one explicit
HarnessRuntimeDefaults skip; complete loop suites pass210/210 and209/209. The same
counts pass ASan/UBSan/LSan; NULL passes26 parser/metadata/storage controls, including
all4 new tests. Read-only Claude reviews confirm arithmetic/test independence and
honest Microsoft evidence boundaries; NULL execution disproves its speculative NULL
failure concern. Licence, whitespace, coverage and release-consistency checks pass:
467/8152 counts and fingerprint unchanged, release still not ready. See
`docs/xact-bank-formats.md` and evidence `../cnaplan/evidence/rev002/xact-compact-tail/`.

TQ-XACT-24 (2026-11-07): genuine Microsoft marker scheduling now qualifies CNA's
existing pause clock. Refresh Xact.dll 4.0.30901.0 statically forwards Cue.Pause/Resume
to IXACT3Cue slot 8 and selects retail CLSID 248D8A3B-6256-44D3-A018-2AC96C459F47:
that is XACT 3.6, not the 3.7 used for earlier bank-metadata controls. The explicit
class factory probe therefore loads authentic 3.6 x86/x64 DLLs 9.28.1886.0 from the
owner's existing February 2010 cabinets; no install/COM registration or SDK redistribution.
Engine/sound-bank/cue method ownership is checked, and the loaded module inventory
identifies genuine XACT plus Wine's builtin xaudio2_6, with null output/private display.

An owned SDK project builds an eight-second PCM16 wave, one cue and four markers:
IDs 11/22/33/44 at 500/1000/2000/3000 ms. Real Initialize, bank creation, Prepare/Play,
Pause/GetState and DoWork produce actual public MARKER notifications. The final probe
brackets calls with QPC before logging, records native timestamps/callback arrivals,
method/module paths and exact input hashes. Ten final cells cover both architectures:
control; pause 200/resume 1400; Pause 200+again after 400 ms+only one Resume 1400;
pause 700/resume 1900; and two holds 200/1400 plus 1900/2500. Forty markers arrive
at their authored times plus only preceding holds, with 1–3 ms delivery latency.
State 72 while paused becomes 8 after the single Resume. Initial 3.7 controls and
earlier 3.6 harness revisions are retained, but the final exact 3.6/QPC matrix is
the qualification. This executes native Microsoft scheduling under Wine; the Refresh
wrapper relationship is static IL only. It does not qualify managed-wrapper execution,
native Windows/PCM output, AttackTime RPC, category pauses, paused stops or every
host update cadence. The duplicate's individual timestamp is not logged; its 400 ms
threshold is enforced by the probe.

The old FAudio minimal-record pause observation and evaluated clock expression remain
true; FAudio playback was not executed. Microsoft evidence now supports CNA's retained
freeze and T,T,F semantics, so the pause discrepancy is removed from §5.1 and recorded
with scope in docs/xna-fna-behavior-decisions.md. No runtime algorithm, public API/C ABI,
dependency or stub changes. A source comment names the measured divergence. The own
SDK banks, provenance and reproducible native C++ probe are retained;
docs/xact-pause-clock.md states the exact reference and limitations.

Three new tests pass on the existing algorithm: parser IDs/times and two shared SDK
scheduler cases, running on both real offline mixer adapters with an injected clock.
They check exact before/at deadlines, no paused dispatch, delayed duplicate Pause followed
by one Resume, an earlier marker, cumulative holds and duplicate updates. Inclusive
deadlines use the FAudio source predicate; native measurements qualify pause exclusion,
not zero-jitter boundaries. Seven mutations charge paused time, lose the active prefix,
overwrite accumulated time, count duplicate Pause, dispatch while paused, delay event
boundaries or erase marker IDs; all 13 target runs fail with ordinary assertions, and
both production sources restore to exact hashes. Final loop suites pass 212/212 and
211/211; full audio passes 1002 plus its one explicit HarnessRuntimeDefaults skip, with
the same counts under ASan/UBSan/LSan. NULL parser/clock selection passes 18. Read-only
Claude reviews confirm the result and tightened the native/FAudio timing claims.
Licence, whitespace and current coverage/release consistency checks pass: 4155 source
files, 467/8152 coverage counts and unchanged fingerprint, release still not ready.
Evidence: ../cnaplan/evidence/rev002/xact-pause-clock/.

TQ-XACT-25 (2026-11-07): `NumCueInstances` now matches the measured Microsoft3.6
per-handle publication model instead of returning the authored initial value. Each
fresh handle starts0 even when its same-bank/definition count is1 and stays0 after
150ms DoWork. A changed count publishes to all retained same-definition Prepared,
Playing, Paused and Stopped handles; other definitions and cloned sound-bank objects
stay independent. Paused Playing cues count, authored Stopping tails stop contributing
before audio retirement. Prepare, failed Play and nonplaying Destroy do not publish
an unchanged count or cause FAudio's unsigned underflow. Public lookup still requires
an authored public cue variable and preserves name/disposal guards. RPC and interactive
selection consume the same stored value. No public API/C ABI, stub or dependency added.

The exact Refresh4.0.30901.0 bridge calls native GetVariableIndex slot5 and GetVariable
slot7 and returns its float unchanged. The static retail3.6 selection is inherited
from TQ-XACT-24; genuine Microsoft x86/x64 native engines run under Wine/null output,
private display, checked loaded paths/method ownership and untouched SDK banks.
Twelve admission cells record300 snapshots, two cells establish cache/scope, and six
cover direct playing Destroy,50ms authored tail and real100ms wave EOF. Two retained
own C++ probes reproduce eight cache/scope/end cells on both architectures after
unused marker helpers are removed. Exact hashes, source inputs, protocol, scope and
reproduction commands are in the corpus README/PROVENANCE and
`docs/xact-instance-limits.md`. No managed wrapper, native Windows, PCM output,
Microsoft RPC/interactive or exact decrement instant is qualified. The authored-stop
count changes sometime between the call and20ms, while state remains Stopping.

Admission remains open in §5.1: leading Prepared P plus OLDEST maximum1 leaves P
Prepared and L/I both Playing/count2 in Microsoft; CNA currently evicts L instead.
Victim identity is not proven by that sequence. Category FAIL may reject Prepare
with8ac70008/no handle, whereas CNA defers admission. Existing CNA admission still
counts Stopping separately. Historical FAudio underflow observations are retained
as FAudio evidence, not implemented as Microsoft's contract. The permanent decision
ledger records the bounded measured difference.

Three initial offline regressions fail on both original mixers, while their existing
212/211 tests pass. The fixed set has five offline tests plus two clock/lifetime tests
that also run with NULL audio. Literal RPC/interactive fixtures derive their expected
pitch from authored curves/ranges, without a native Microsoft consumer parity claim.
Thirteen initial mutations plus three private-consumer mutations build successfully
and yield32 ordinary assertion
failures across both adapters, covering live/constant/seeded reads, unchanged
publication, omitted recipients, Stopping/definition errors, lookup and each internal
consumer, natural completion, and wrong PUBLIC-bit gates. Private controls follow
the SDK Visibility documentation; no native private-variable execution is claimed.
The EOF test reads its peer before querying the completed cue, proving Update-driven
publication. Sources restore to exact hashes. Final debug and
ASan/UBSan/LSan runs each pass1004 audio tests plus one existing explicit default-harness
skip, and217/216 offline tests. NULL clock selection passes19; the existing native
C API XACT smoke passes privately (no new C ABI-specific count assertion). Licence
checks4159 source files; whitespace and coverage/limitations/release gates pass.
C API counts remain467 headers/8152 symbols; only the inventory fingerprint changes
with header bytes/locations, and release remains not ready. Evidence and review
receipts: `../cnaplan/evidence/rev002/xact-instance-counts/`.
The final successful read-only Claude review found no concrete production defect
and confirmed scoped readiness. Its peer-first EOF suggestion and private consumer
controls were added afterward with three additional mutation witnesses. The follow-up
review was refused by the actual weekly quota; it did not review those additions or
design the next OLDEST control. Their private-content oracle is the retained SDK
Visibility documentation, not a claimed Claude approval/native measurement.

Claude continues to accept useful reviews despite rounded100% weekly usage; no hard
quota refusal has occurred, so the owner's delegation goal remains active.

### 0.14 Full FNA3D qualification and clock-reference correction

TQ-SWEEP13 (2026-11-07): the complete stable FNA3D configuration built and the private runner
completed all 14,165 registered CTests: **13,547 passed, 618 explicit skips, zero failures**,
677.76 seconds. The source was clean at `c1dd6cdbc58a9558ee55c3b9bdbca4e34cad2ba4`.
This qualifies the published FNA3D `5c0b9b8c13f5cca36a336d481cd5f696d0223275` /
MojoShader `6ae750b59e6d6e08eadf14d241e38d051fae45a2` configuration; the six unpublished
fork candidates remain separate. The long XNA/CNA content-build comparison passed in638.47s.
Evidence, individual outcomes, logs, cache and binary/source hashes are archived under
`../cnaplan/evidence/rev002/sweep13-fna3d-full-c1dd6cdbc/`. The earlier audio follow-ups have
their own73-log archive at `../cnaplan/evidence/rev002/xact-followups-c1dd6cdbc/`.

Review also disproved a reference claim in TQ-XACT-3's pause test comment: the paused-time
assertions pin CNA's existing scheduler policy, not native FACT resume accounting. A static
build of local FAudio `6839b88e304a046ae1a609ff14a085371e02a4e0` executed unmodified
`FACTCue_Pause` with an injected `FAudio_timems`, minimal stack-backed cue/bank/engine records
and a real SDL mutex, without initializing an audio engine or opening a device.
Start1000, pause1500 and resume2500 leave native start1000/elapsed2000.
At2600 the expression used by native UpdateSound yields3600, while active elapsed excluding
the pause is600. The private fields were measured; the UpdateSound expression was evaluated
in the probe, not through native playback/RPC execution. Probe source, logs and hashes are
in the same archive. Microsoft XACT has not been measured, so no XNA-over-FNA decision was
invented and no runtime/test expectation changed. Corrected comments/docs qualify the claim;
TQ-XACT-24 subsequently measured the exact Refresh-selected Microsoft XACT3.6 and
confirmed CNA's existing pause exclusion. The FAudio observation remains valid, but is
no longer a CNA defect; see `docs/xact-pause-clock.md` for the measured boundary.

## 0b. CNA/cna-demos repair campaign handoff — 2026-10-24

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

- CNA-FIX-D24 (2026-11-02): EasyGL and Software only bypass blending for One/Zero with
  Add/Add. CNA's FNA3D adapter preserves non-Add equations with an effective unit constant,
  retaining the public BlendFactor and subsequent weighted states. A genuine-XNA/Wine/DXVK
  oracle measures all 25 colour/alpha function pairs plus a weighted control: nine render,
  sixteen Min/Max pairs refuse with NotSupportedException. The current plain-RGBA regression
  fails with the original EasyGL shortcut; prior Software and FNA3D witnesses fail as well.
  Final witness passes on all six compiled rendering identities, plus FNA3D/OpenGL; each includes
  dynamic-factor preservation, all refusals and the weighted draw. All 50 affected blend/range/
  profile tests pass on OPENGLES3, OPENGL33 and SOFTWARE. The compiled BlendFactor pixel control
  passes on FNA3D's SDL_GPU and OpenGL drivers. Graphics/renderer targets and the shared library
  build; ten provenance/metadata/C ABI gates and docs links pass. The measured XNA-over-FNA3D
  decision and reproducer are retained. No upstream source edit, new stub/package/API signature,
  native Windows, browser or exhaustive HDR/MSAA/MRT qualification is claimed.
- CNA-FIX-D15 (2026-11-02): VideoPlayer's C association query validates the original
  token and, after its release, publishes a player-scoped borrowed handle to the retained native
  Video. The next call on that player invalidates the renewed borrow; ownership is not
  transferred. Play secures its input before invalidating views, allowing same-player replay.
  Rejected Play still invalidates the previous frame. Strict-C real-decoder regression fails
  before correction with width-query INVALID_HANDLE, then passes privately on OPENGLES3 and
  SDLGPU, including independent retention by a second player, Stop, Dispose and destruction.
  The affected target builds; three ABI/header/export gates, generated routes and docs links
  pass. No new stub, package or public signature change; no fresh sanitizer or decoder-disabled
  qualification is claimed.
- CNA-FIX-D07 (2026-11-02): unlit fallback occlusion no longer selects DualTextureEffect.
  Uncoloured rigid unlit content stays on BasicEffect/stride 32; PBR retains its authored
  occlusion map. Three importer/runtime/framebuffer regressions fail before correction and pass
  on private OPENGLES3, SOFTWARE and SDLGPU afterwards. The black-occlusion pixel witness still
  renders the red base texture. The 622-case glTF/export/CNB selection has 619 passes and three
  optional-asset skips after the stale report digest is corrected and all 19 corpus cases rerun.
  The fixture generator and independent byte goldens are corrected; generation check passes.
  Only the affected L7 fixture is recaptured, in two independent processes per EasyGL/SOFTWARE
  renderer, using the CI-pinned human-authored viewer and existing multi-renderer CNA build.
  Asset-level report provenance records exact viewer/library/patch hashes; other captures retain
  historical provenance. Ten provenance/metadata/C ABI gates and local docs links pass. No new
  runtime stub, dependency package or public API change; no fresh full-corpus L7, native Windows
  or additional renderer qualification is claimed.
- CNA-FIX-D06 (2026-11-02): selected configured/system fonts now become byte-hashed
  native input dependencies in both the canonical processor and XNA processor bridge. Declared
  source roots retain alias-relative identities; other native inputs use explicit machine-local
  host-file records. Authored references retain the existing containment checks. Manifest v11
  rejects old incomplete records, and the font importer identity is version 2. The external-font
  warm-cache regression fails before the fix and passes afterwards. All 61 selected font/bridge/
  source-route tests pass privately. The 132-case core selection has 131 passes and one opt-in
  large-file skip after rerunning the manifest suite to correct its v10 expectation; the 4096-node
  graph passes. Ten provenance/metadata/C ABI gates and docs local links pass. Affected targets
  build; no new runtime stub, dependency package or C ABI change. Cache hits do not rescan the
  font candidate set; no native Windows or sanitizer qualification is claimed for this repair.
- CNA-FIX-D04 (2026-11-02): Effect/Gamer boxed Current getters now delegate to typed
  Current, preserving null at default/before/end/reset positions. Both corrected native cases
  fail with the old guards, then pass; six model cursor and 98 gamer collection/property cases
  pass privately after rebuilding. A retained oracle reproduces fresh genuine-XNA/Wine null
  observations for default/empty/one-null-entry cursors and records loaded assembly hashes.
  Model/gamer C smoke, three ABI/header/doc gates, two provenance gates and local docs links pass.
  The C cursor invalid-state contract, mutation checks, layouts and borrowed ownership are
  unchanged; no new runtime stub or physical/native Windows/signed-in service qualification.
- CNA-FIX-D03 (2026-11-02): XNB DateTime preserves Kind and stores/restores Local
  through UTC ticks, using Sharp Runtime’s existing current-zone conversion. Actual Microsoft
  compiler output and isolated reader invocations under Wine measure the Local conversion and
  refusal of Kind 3; the committed oracle reproduces three fixtures byte for byte and records
  loaded assembly hashes. This measured XNA-over-FNA decision is in the permanent ledger. Four
  regression groups fail before the fix; afterwards seven focused cases pass in each of UTC+2,
  UTC and New York, all 313 selected native XNB cases pass, and eight provenance/metadata gates
  pass, privately. The TimeZone component is linked privately; no runtime stub or public C ABI
  change. Both XNB guides match; the writer guide’s broken command/duplicated tail is repaired,
  and all docs links/fences validate. No ambiguous-DST or native Windows qualification is claimed.
- CNA-FIX-D11 (2026-11-02): all three native FileDialog wrappers retain shared
  once-only completion state and return an empty result on provider PlatformException rejection.
  Results delivered before rejection are preserved; late/duplicate callbacks are ignored and an
  empty callback remains empty at the provider. The active CNA_DEVICES/UBSan dialog regressions
  fail before correction and pass after it, including deferred completion on another thread.
  All 60 DevicesExt cases pass privately with dummy audio and halt-on-error UBSan/float-cast-overflow;
  only canned dialogs are used. Final target build passes after incrementally resuming an initial
  SIGTERM interruption. Public Doxygen, devices guide and local links match; no API/ABI change or
  stub, and no claim of new physical dialog qualification.
- CNA-FIX-D10 (2026-11-02): CNJ version extraction now checks representability before
  narrowing and retains the raw unsupported value; validation avoids an integer cast. Inline
  AnimationClip indices must be finite whole Int32 values before conversion. Both new regression
  groups fail before the fix and pass after it, preserving signed Int32 boundaries. The inline
  version probe triggers float-cast-overflow on 1e30 before the fix; eleven extreme/boundary/ordinary
  inputs pass after it. All 51 focused envelope/clip/codec cases pass; privately, the broader
  rebuilt CNJ/tool/codec set passes 186/188 with two optional asset skips and dummy audio (the first
  broad run had no ALSA device). Targets build, format documentation and local links match. No
  public API/ABI change or stub; this does not qualify every numeric field against hostile JSON.
- CNA-FIX-D08 (2026-11-01): standalone gltf_to_cnj unitScale parsing now requires a
  complete finite positive float before export. The subprocess regression fails with the old
  parser; afterwards twelve invalid tokens return 1 without writing files and four valid
  decimal/scientific/signed-positive tokens export successfully. The complete private converter
  suite passes 33/35 with two optional reference-asset budget skips; existing vertex/bone/node
  scale checks pass. Affected content/tool targets build; the conventions guide matches. No
  public API/ABI change or new dependency.
- CNA-FIX-D09 (2026-11-01): supported boxed float/double comparisons now use Sharp
  Runtime’s existing default equality comparer. The native pair regression fails before the fix
  and passes afterwards for same/opposite-sign NaNs, signed zeros, infinities and mismatches,
  including preservation on failed removal. The complete private HEADLESS/dummy-audio
  GamerServices run passes 744/745 with one expected renderer-capability skip; the C gamer-property
  smoke and three ABI/header/doc gates pass. The explicit boxed type boundary is unchanged. The
  limitations page matches, and all local docs links resolve. No ABI/dependency change or stub.
- CNA-FIX-D19 (2026-11-01): direct managed-device disposal now forwards DeviceDisposing
  after releasing the frame context. Later manager cleanup skips an already-delivered notification;
  teardown removes subscriptions. The new direct-device test fails before the fix; afterwards
  all 18 native manager tests pass privately on OPENGLES3 and SDLGPU, covering repeated device
  setup, sender identity, one UnloadContent, active-frame disposal and subscription removal. The
  combined SDLGPU game-event run passes 19 cases with four capability skips; C manager/game smoke
  and three ABI/header/doc gates pass. The manager guide matches; no ABI change or stub.
- CNA-FIX-D18 (2026-11-01): both preferred backbuffer dimension setters now reject
  nonpositive values before changing preferences, with the FNA exception type, parameter and
  message. The strict-C regression fails before the correction and passes after it for zero, -1
  and INT32_MIN. All 16 native manager property/lifecycle cases pass privately, including
  unchanged values and accepted positive boundaries without allocating them; the C manager smoke
  and three ABI/header/doc gates pass. Public Doxygen and the manager guide match. No ABI change
  or stub. Direct-device disposal forwarding remains a separate open defect below.
- CNA-FIX-D01 (2026-11-01): all seven affected SDLGPU stock fragments now multiply fog
  color by completed output alpha. The registered shared fog contract expands to 41 pixel cases
  across five stock effects, including texture/vertex alpha, lit/unlit BasicEffect, both dual
  textures and textured/untextured skinning. SDLGPU fails nine before the fix (32/41), then passes
  41/41; independent EasyGL and Software controls also pass 41/41. Official regeneration changes
  exactly seven fragment arrays; the other 33 SPIR-V arrays are byte-identical. After rebuilding
  all 375 executables, the full private Linux/Vulkan Debug SDLGPU sweep passes 391/393 with two
  unsupported-query skips, zero failures and no profile deaths. ShaderCross/compiled effects
  remain OFF. The matrix and five effect guides match; no new D3D12/Metal/device qualification.
- CNA-FIX-D13 (2026-11-01): session-event and local-packet injection now share the ordinary
  send routes' defined-bit validation. All eight Reliable/InOrder/Chat combinations are accepted;
  unknown bits are refused. Strict-C tests fail with both original ordinal checks and still fail
  after correcting only the session route; with both corrected they pass, including payload
  round trips and unchanged queue state after three unknown-bit cases. No public ABI change.
- CNA-FIX-D02 (2026-11-01): content-manager creation/destruction counts only game owner
  tokens, using the existing owner-kind check. Both standalone constructor routes no longer block
  an unrelated game's teardown, and destroying a standalone manager cannot decrement a live
  game-child count. The strict-C regression fails before the fix and passes afterwards; it also
  proves the standalone managers survive game destruction and a genuine game-owned manager
  blocks shutdown until destroyed. Owned-device/content/runtime-game suites pass on OPENGLES3,
  and owned-device/content suites pass on SDLGPU, all privately. Internal helper signatures change;
  the public ABI is unchanged. Standalone device lifetime remains the caller's documented duty.
- CNA-FIX-D17 (2026-11-01): the generic resource-device query now resolves an independently
  owned device token and checks its native identity before returning the existing handle. Game
  borrowing and device-less declarations retain their behavior. The strict-C regression fails
  before the fix, then verifies two distinct resource/device pairs, the surviving pair after the
  other device is destroyed and stale-resource output invalidation. Four focused C suites pass
  on OPENGLES3; the owned-device and generic-resource suites pass on SDLGPU, all privately.
  Public contract comments match; no ownership transfer, ABI signature/layout change or stub.
- CNA-FIX-D12/D16 (2026-11-01): both C buffer info snapshots now expose their native dynamic
  ContentLost flag instead of constant false. Strict-C regressions fail before the fix and pass
  after it on OPENGLES3 and SDLGPU through the private runner. Each covers two loss/reupload
  cycles, callback counts, empty-write preservation, nonempty-write reset and static-buffer false.
  The index route is regenerated from its TOML source; generator freshness and its five unit cases
  pass. Public field comments and owning contracts match; no ABI layout/signature change or stub.
  The owner-authorized repair queue is CNA_REPAIR_STATUS.json, seeded from all 79 §5 groups at
  37c69080d; Galaxy Eggbert intake, full local tests and GitHub CI remain subsequent workstreams.
- Independent documentation review (2026-11-01, DOCS-REVIEW-001): a separate GPT-6.1 Sol
  xhigh session reviewed the cleanup through bdf18a7d5 using source-backed samples and structural
  checks. All eight confirmed documentation finding groups are corrected: callback diagnostic and
  CNB-loader lifetimes, actual C result/category names, DDS cube normalization, content-manager
  ownership, root ABI/consumer/qualification status, BasicEffect fog alpha and execution recipes.
  Three public C header comments are reconciled without ABI/runtime changes. The reviewer checked
  the corrections and the additional source/artifact-verified SDLGPU fog finding; that defect and
  the global content-manager counter remain explicitly open in §5.1. Existing affected targets
  build; 13 focused C API cases and 20 metadata/header/ABI gates pass privately, as do 57 C API
  tooling and 22 runtime-audit tooling unit cases. Private input-demo startup reaches its expected
  five-second timeout; this is not interactive hardware qualification. All 1,277 local Markdown
  links resolve; frozen baseline/archived ledger bytes and generated report rendering are intact.
  This independent review was risk-based, not a second exhaustive line-by-line audit of all pages.
- Documentation cleanup complete (2026-11-01, DOCS-177): all 177 tracked docs Markdown pages have their
  own review recorded in the English documents.md inventory. The final docs/README.md audit
  replaces July currency/scope notices with a topical index covering all 138 other top-level
  pages; the C API index covers all 37 nested contract pages. Descriptions distinguish current
  contracts, generated declaration/map checks, historical evidence and unimplemented proposals;
  the feature matrix's incoming section link is updated. Across docs and documents.md, all
  1,270 local links resolve to existing paths/Markdown anchors. Current and frozen runtime-member
  reports render exactly from their retained JSON, and the current JSON matches the fresh full
  audit from this pass. Related builds, private renderer/content/runtime tests and nine pipeline
  metadata gates are recorded in the entries below; no new runtime change or platform
  qualification accompanies this final index work. Findings remain in §5.
- Historical task-index reconciliation (2026-11-01, XNA-MEMBER-001/002): all 133 deleted
  plans are represented and quoted deletion dates agree with Git history. The index separates
  plan retirement from feature completion and current qualification, corrects the a-0001 versus
  SAMPLE-104 grouping, explains the 164-to-160 runtime-gap stages, and routes Software's compact
  evidence map and later XNB/sample work accurately. Local links and inventory checks pass; no
  runtime or generated report changes.
- Software/EasyGL evidence-map reconciliation (2026-11-01, SOFTWARE-100/164/165/457): the
  391,991-byte cumulative ledger is replaced by a compact maintained map; every original byte is
  verified recoverable at 9b51bbab4. The page separates historical verdicts/counts/skip lists from
  current contracts, routes public state/resource and optional compiled-shader evidence to its
  owning sources/guides, preserves the invalid ps_1_4 fixture correction and qualifies old CNAEXT
  deferrals. The renderer guide now points to that map/archive rather than promising live per-task
  tables. All 195 executable targets in the current 196-case Software label rebuild in the existing
  Release/compiled-effects-OFF tree; privately, 194 pass/two declared skips/zero fail, no profile
  deaths. Links/archive/inventory checks pass. No runtime change or new compiled-effects-ON,
  EasyGL differential, genuine-XNA, sanitizer or physical-device qualification.
- XNB writer/interoperability guide reconciliation (2026-11-01, XNAP-12/82, XNAPP-191/192):
  the page corrects format-aware processing, XNB-only routes, PCM8/schema-2 retention, manifest
  version 10/per-reader versions, real-fxc evidence, eight writable versus 16 readable targets and
  retained Compact Framework Phone/Xbox compiler witnesses. It distinguishes native-device
  qualification and public-field harness assertions, qualifies compression/provenance/custom-
  writer limits and uses private reproduction commands. DateTime Kind is now a documented open
  XNB defect (§5.1), not an absent Sharp Runtime feature; the read-side guide receives that narrow
  related correction. Existing targets build; related 456 native and eight LZX cases pass, five
  XML-route cases pass, 16 independent parser reports match explicit manifests, and nine metadata
  gates pass. No runtime/wire change or new Microsoft/real-fxc/device/sanitizer qualification.
- XNB runtime-loader guide reconciliation (2026-11-01, XNB-42/42A/45/46): the page now
  describes typed/lazy built-in registration, current CNB/CNJ resolution, generic families and
  VideoReader. It distinguishes ReflectiveReader generic names and binary writer order from XML,
  documents serialized reference traits beyond shared_ptr, manifest CNB reporting and four 1500-
  mutation cases, and qualifies format/platform/fixture and sanitizer evidence. Existing
  CnaContentTests builds; 456 selected runtime reader/graph/manifest/ownership cases and eight
  LZX decoder/differential/fuzz cases pass privately. Custom-reader syntax, all 16 FNA platform
  bytes and 14 fixture manifests are checked. No runtime/wire change, fresh Mono/Microsoft,
  Xbox/Phone, sanitizer or cross-renderer campaign.
- SDLGPU renderer-guide reconciliation (2026-11-01, SDLGPU-39/58/92/115, GFX-165): the page
  distinguishes finite historical parity, SDL driver selection and current capability reporting;
  specifies partial active-sample-mask refusal and corrects proxy activation for pre-Present
  flushes/headless presentation as well as readback. Fixture registration/debug patterns and
  optional-feature qualification are scoped precisely. The renderer and six stale consumer
  programs/shared-library dependency build; after resolving their loader-symbol mismatch, the
  final full private Vulkan Debug run covers all 392 SdlGpu registrations: 388 pass/four declared
  skips/zero failures, no profile deaths. ShaderCross/compiled effects are OFF. No runtime change
  or new Windows/Apple/mobile qualification.
- XNA/FNA decision-ledger reconciliation (2026-11-01): the ledger now preserves historical
  binding findings without claiming an external consumer's current maintenance status and states
  the approved native-C boundary. It names the local FNA revision, corrects GetValueString's real
  cached-value implementation versus its unimplemented string setter, qualifies finite oracle
  families and replaces the browser teardown check's stale ordinal with its semantic purpose.
  All 18 cited CNA commits and named regression/evidence paths were checked. Existing targets
  build; 19 selected native cases and ten Game clock/reset cases pass privately. No historical
  all-family replay or optional compiled-effect/Windows/browser/device qualification claim.
- Enumerator-reference reconciliation (2026-11-01, XNA-ENUM-001): the page separates old
  326/331 and 323/329 counts from the current 331/331 type census, corrects default Touch's inline/
  empty state, names borrowed lifetimes/native-iterator boundaries and fixes the description of
  XNA's boxed Current getters. Original Graphics/GamerServices IL confirms both wrappers call the
  typed getter; CNA instead checks hasCurrent_ and throws (§5.1). The existing CnaTests target
  builds and all 12 cases across six enumerator suites pass privately, but effect/gamer tests
  currently assert that mismatching exception. No runtime fix, replacement regression or new
  genuine-XNA/sanitizer execution was bundled into the docs audit.
- Current runtime member-report reconciliation (2026-11-01, XNA-MEMBER-002): a fresh
  Microsoft XML/DLL/Clang audit reproduces the stored JSON exactly: 331/331 types, 3627/3627
  members, no AST errors, zero missing/review entries, the same 164 represented baseline gaps.
  All per-entry behavior_assessed flags remain false. Generated prose now explains declaration
  classifications, overlapping subset counts, HEADLESS/four-worker AST scope, local input/tool
  prerequisites and the CLI's type-only exit-status check (§5.2). Only Markdown rendering changed;
  classifier logic, stored JSON and frozen baseline rendering remain unchanged. The 22 audit-tool
  regressions pass from the adjacent baseline audit; no new behaviour/renderer qualification.
- Runtime member-baseline documentation reconciliation (2026-11-01, XNA-MEMBER-001): the
  generated page clearly labels its 3463/3627 representation and 164 gaps as the frozen pre-fix
  snapshot, links current coverage and explains how to render retained evidence without rerunning
  a baseline audit against today's headers. All 3627 entries, tier totals (4/153/7), XML/DLL hashes
  and unassessed-behaviour flags were checked; baseline JSON is byte-identical to a17e954c8.
  Only Markdown rendering changed in xna_runtime_members.py; audit logic and current report output
  are unchanged. All 22 existing audit-tool tests pass. No historical missing-member work repeated
  or new runtime behaviour claim.
- Runtime exception-profile documentation reconciliation (2026-11-01, SAMPLE-104): the page
  now states the process-wide AppContext default/override lifetime, exception-construction timing,
  Game constructor-body boundary and current shared Substring validation. The old 193-case and
  seven-image campaign is retained as dated external evidence rather than a current result or ABI
  guarantee. The XML guide receives the related standalone-versus-Game diagnostic qualification.
  Existing CnaRuntimeTests builds; both profile regressions pass in separate private CTest
  processes with OPENGLES3/dummy audio. No runtime change or full historical campaign rerun.
- Content Pipeline parity-report reconciliation (2026-11-01, XNAPP-014/020): the generator
  now labels percentages as maintained classifications, distinguishes annotation/freshness gates
  from compilation/runtime proof and explains named refusal tests versus successful target/device
  qualification. Explicit null rendering aligns EffectProcessor.Defines with the component guide.
  Fourteen map notes and seven input-route notes correct current media/XMA service boundaries,
  completed CreateVertexBuffer, partial project output, sibling references and finite evidence;
  both generated reports are refreshed. Only note fields changed in the JSON maps: classifications,
  frozen denominator and gate functions remain unchanged. Nine metadata CTests pass privately;
  47 property rows preserve nine false defaults and one null. No runtime/wire change or new
  Microsoft/Phone/Xbox qualification run.
- Intermediate XML reference reconciliation (2026-11-01, XNAPP-070–079): the guide separates
  the 265 retained oracle outcomes from CNA test counts and incorporates later direct-read,
  section-fixup and character-boundary evidence. It corrects external-reference deduplication to
  type/filename, replaces the stale std::map carrier with OrderedDictionary, removes the already
  repaired NUL exception gap and qualifies rectangular-array, parser timing/depth and time-zone
  claims. Existing content-pipeline test/example targets build; 75 relevant cases pass privately,
  including parameterized section/fixup/alias families. Case-name/link/inventory checks pass.
  No runtime/schema change or fresh Microsoft/Windows/sanitizer campaign.
- XNA media-source guide reconciliation (2026-11-01, XNAPP-201/202/220/136/043/332): the
  guide separates the 56 retained XNA/Wine records from current FFmpeg-backed CNA policy, preserves
  all twelve MP3 rows, states the 30 ms duration-test allowance, corrects WmaImporter/MP3 failure
  timing and documents real WMA output/quality policy versus the native streaming-copy route.
  Prefix-limited WMA/WMV/encoder failures are not treated as a native-Windows refusal contract.
  Optional reproduction now uses a prepared prefix/private runner/new output, with explicit
  partial-report/zero-exit caveats. Existing content/compiler targets build; 31 media/audio/
  processor cases and three CLI audio-route/path cases pass privately on OPENGLES3/dummy audio
  without skips. Stored record/duration/link/inventory and shell-syntax checks pass. A synthetic
  driver proves wrapper exit 0 after driver exit 42 (§5.2); no Microsoft code/prefix installation,
  fresh XNA oracle, runtime-playback, native-Windows or sanitizer qualification was run.
- XNA Content Pipeline API-guide reconciliation (2026-11-01, XNAPP-030/038–044): the guide
  corrects exact C++ carrier checks versus CLR casts, ordered dictionary carriers, current described
  XNB writing, member/factory registration, bitmap resampling thresholds, identity-transform normal
  preservation, material mutation and XNB EffectMaterial support. Nested builds now describe numeric
  names, synchronous execution, source-identity exception wrapping and the runtime-reference-only
  planned-sibling shortcut; the native pipeline guide receives that narrow related correction.
  Explicit graph-cycle checks are not claimed as a universal nested-call recursion guard.
  Existing CnaContentPipelineTests target builds; privately on OPENGLES3/dummy audio, 112 selected
  core/bridge/bitmap/material/compiler cases pass, followed by 75 intermediate/model cases including
  parametrized families. Links/inventory pass. Bitmap region overflow and lazy Vector3 factory
  scope are source-inspected below; no new runtime correction/regression, sanitizer or genuine-XNA
  campaign was bundled into the docs task.
- Generated pipeline final-audit reconciliation (2026-11-01, XNAPP-330): the report/generator
  now name each predicate kind and report source/metadata checks rather than claim all 26 runtime
  acceptance conditions hold. They distinguish maintained counts/flags, path existence, source
  TEST/add_test patterns and the executed default-annotation helper. All original conditions,
  ordering and evidence arguments are unchanged. A temporary scope probe confirms commented-out
  TEST/add_test text satisfies the source matchers; the report states that limit. Existing
  CnaContentPipelineTests target builds, the audit regenerates, five related CTest metadata gates
  pass privately, and all numbered rows/local links/inventory checks pass. No checker hardening,
  new runtime/sanitizer/interop execution or current all-conditions-complete claim.
- Generated Content Pipeline component-reference reconciliation (2026-11-01, XNAPP-320):
  component_reference.py no longer hides nine measured false defaults as empty cells and renders
  EffectProcessor.Defines' measured null explicitly, with its existing empty-C++-string mapping.
  Generator prose distinguishes map/freshness checks from execution, native versus XNA parameter
  names, CacheImportedData metadata versus output caching, and frozen metadata versus rediscovery.
  Four shared notes correct family/style font search, CNAEXT SpriteFontContent::Data, finite FBX
  comparisons and texture-policy witness scope; both generated reference/parity documents are
  regenerated. The parity report still awaits its separate full audit in documents.md.
  Existing content-pipeline targets build; 133/133 selected component/default/font/texture/model/
  media/importer cases pass privately on OPENGLES3/dummy audio, as do all nine metadata/parity/
  freshness/default/virtual/whitespace gates. Independent checks preserve all 47 property rows,
  nine false values and explicit null/zero rendering; links/inventory pass. Measured denominator,
  statuses, runtime API/wire and existing external qualification remain unchanged.
- Content Pipeline guide reconciliation (2026-11-01, CP-063/XNAPP-021/255/281/320): native
  coordinator versus XNA façade, nested BuildAsset/BuildAndLoadAsset, DDS/image/media routes,
  PCM8/16/schema-2 output, probed metadata, output-format overrides, CMake CONTENT_PROJECT/XNB
  arguments, physical module separation and runtime C ABI exclusions replace outdated claims.
  Manifest/font-path and fixed-service concurrency boundaries are explicit; old sanitizer/Windows
  counts remain historical rather than current qualification. Compiler/custom compiler/content/
  façade targets and CMake fixtures build; privately on OPENGLES3/dummy audio, the native selection
  passes 263/264 with one opt-in large-file skip, and the project/task/source/texture selection
  passes 38/38. Temporary probes confirm warm strict parameter refusal and reproduce stale cached
  fonts after a same-path external font replacement (§5.1); global font-search reconfiguration is
  source-inspected (§5.2). Links/inventory pass. No runtime/ABI fix, permanent new regressions,
  new native-Windows or sanitizer campaign.
- glTF conformance reconciliation (2026-11-01, GLTF-002/007/008/015/403/458): the guide
  separates dated project milestones/framebuffer reports from current numeric witnesses, fixes
  withdrawn mesh-node compensation, tangent fallback/authored-tangent exceptions, L5 layouts/index
  width scope, current alpha test names, seven-map versus five-map test coverage, private commands,
  pin/provenance boundaries and native punctual/Direct3D routes. Existing unlit/unitScale/parity
  wrapper defects remain explicit rather than hidden by historical green rows. CnaTests,
  CnaContentTests and converter targets build; 634 glTF cases yield 631 passes and three external
  input skips privately on OPENGLES3/dummy audio, followed by 26/26 corpus/ladder/ledger cases after
  the prose changes. The exact pinned specification digest, 148-row generated table, 744 corpus
  files and two-process generator determinism pass. Pinned Khronos Validator rechecks all 296
  containers: 276 valid, 20 expected-invalid, 47 warnings. Inventory/links pass; no runtime/wire
  changes, new full framebuffer/reference capture or sanitizer qualification.
- glTF conventions reconciliation (2026-11-01, GLTF-105/132/241/337/349/392): the guide corrects
  asset +Z front versus camera -Z, column-vector TRS order, test/byte-identity claims, current
  coloured PBR strides, seven-map transforms, CPU morph work/retained weight tracks, empty-light
  fallback, native punctual/IBL versus automatic import, lossy specular-glossiness conversion and
  renderer/source-gate boundaries. Khronos's primary specification supports coordinate/TRS/unlit
  corrections. The related limitations rows now record unlit occlusion selection and parked
  skinned lights accurately. Temporary exports reproduce that selection and lax unitScale parsing;
  identically failing synthetic runners reproduce the parity wrapper's zero exit (§5). Content/
  converter targets build and all 139 selected convention/layout/material/morph/animation/skin/
  shape/source/documentation cases pass privately on OPENGLES3 with dummy audio. Inventory/local
  links pass. No runtime/wire change, permanent regressions for those new findings, new full
  all-renderer framebuffer campaign or upstream-header hash qualification.
- GamerServices limitations reconciliation (2026-11-01, GS-011/GSH-01–22): all 46 stable
  entries/summary rows remain, with current FNA3D/Linux avatar evidence, bounded request replay,
  privacy/poll/reconnect timing, native/cross Opus and Android curl conditions, admission/request
  budget scope and title-dependent port impact. Avatar AO absence is no longer justified by a
  nonexistent lack of CNA SkinnedEffect vertex color: the avatar layout omits that stream.
  PropertyDictionary's explicit boxed type set and NaN equality defect are documented, with
  Microsoft's floating-point Equals references and the defect in §5.1. Current targets build;
  privately on HEADLESS/dummy audio, 744 GamerServices cases yield 743 passes and one deliberate
  vertex-buffer-refusal skip, and all 565 Net cases pass. Canonical protocol drift, links and
  inventory checks pass. No new runtime implementation/ABI, NaN regression, physical/remote-
  network/native-Windows/macOS or console qualification.
- GamerServices deployment/architecture reconciliation (2026-11-01, GS-003–009/GSP-O6/GSH):
  the guide replaces its staged construction diary with current control/relay/session ownership,
  packet/resource bounds, origin capture, retries, APM and validation boundaries. It corrects
  configuration precedence/failure, profile paths/durability, independent gamer zone/reputation,
  keyring timeout scope, event reconnect timing, live policy refresh and cache budget claims;
  database migration numbers are no longer presented as a client compatibility selector.
  The canonical six-file protocol drift check passes against companion fb357f0; current targets
  build, 53 focused GamerServices and 80 Net tests pass privately with HEADLESS/dummy audio, and
  links/inventory pass. Cache capacity concurrency is recorded in §5.2. No companion build or
  deployment, live accounts/keyring, physical audio, WAN/NAT or Windows/macOS qualification.
- GamerServices manual-qualification reconciliation (2026-11-01, GS-AUDIT-P4/GSH-16): the plan
  names the actual service/fake/voice targets, required build features, synchronized harness
  barriers/departure order, external crash coordination and recovery-clock scope. It distinguishes
  loopback acoustic measurements and individual phase statuses from a complete audio acceptance
  gate, and CTest skips from seven executed namespace passes. All seven companion gate definitions
  and return-77 policies match source (server checkout fb357f0); it has no configured build tree
  here, so none of those gates was executed. The seven selected CNA harness/test targets build,
  shell syntax and links pass, and GamerServices_FakeBackend passes privately with HEADLESS/dummy
  audio. All 19 manual cases remain unexecuted; no physical devices, remote accounts, WAN/NAT,
  Windows/macOS runtime, server deployment or power-loss qualification is claimed.
- CNB-format reconciliation (2026-11-01, CNBF-001/003/105/114/115/121): the specification
  separates generic container parsing from asset decoding/runtime construction, bounds its
  conformance/determinism claims, corrects SoundEffect v1/v2 support, equal compressed lengths,
  24 TiB arithmetic, actual region sorting, glTF policy conditions, sidecar endianness, mip rules
  and dated measurement scope. Read limits are not a decoded-memory budget, and --refs does not
  escape embedded newlines (§5.2); a one-reference independent sample reproduces the latter.
  The two obsolete text expectations are corrected in CnbSpecConformanceTests. Content/inspector
  targets build, all 423 CNB tests pass privately on OPENGLES3 with dummy audio, the independent
  golden generator verifies Model-v2 length/hash, and links/inventory checks pass. No wire/runtime
  implementation or ABI change, large allocation stress, or new cross-platform qualification.
- CNJ-format reconciliation (2026-11-01, CNB-3/32/37/110): the guide corrects stream/cache/literal
  resolution, manifest-relative sourceFile versus root-relative sidecars, cached assets versus
  raw file reads, writing/dependency limits, custom version-1 dispatch and load shapes. It scopes
  optional-field/old-runtime compatibility, empty/skipped geometry, hierarchy-version handling,
  native root identity, diagnostic strings and actual per-type metadata. Content/converter targets
  build; privately on OPENGLES3 with preselected dummy audio, 140/142 CNJ/envelope/offline tool cases
  pass and two unconfigured external large-asset budget cases skip. The first broad run's audio
  case failed ALSA setup before choosing dummy at process launch; the corrected run passes it.
  Numeric narrowing source defects are now in §5.1. No native implementation/ABI or all-renderer
  pixel/large-asset performance qualification.
- Model-loading documentation reconciliation (2026-11-01, Tasks 431–439/916): the guide now
  scopes the runtime contract and native helpers, stream/cache/lower-tier resolution, both CNB
  schemas, stored roots/bounds, limited tag shapes, sharing and unload ownership. It corrects
  the rigid-only Model claim and describes Model versus SkinnedModelEXT and animation/draw
  responsibilities. Current FNA also copies by Bones.Count, so the obsolete CHECKLIST oversized-
  array deviation is removed. Graphics/content targets build; 111 runtime/collection and 40
  selected loader/shape/ownership cases pass privately on OPENGLES3. No new genuine-XNA/all-route
  pixel qualification, native implementation or ABI change.
- Content-pipeline benchmark reconciliation (2026-11-01, CP-028/XNAP-93/XNAPP-301): the page
  separates dated timing records from current harness evidence, defines file-digest scope, fresh
  output versus cold caches, p95/provenance limits and node-level workers. It corrects inferred
  codec/serialization costs, overclaimed linearity/equality, actual size/timing gate scopes and
  historical RSS/source ratios (13.0x/8.5x, not 6x). Stock/custom compilers and focused pipeline
  tests build; all seven scaling tests pass privately with HEADLESS. A reduced workers-1/2,
  two-iteration harness run passes all four scenarios' file/manifest digest checks and emits valid
  sample JSON. No full historical performance/RSS campaign, generated-asset renderer/semantic
  qualification, implementation or ABI change.
- CNB measurement reconciliation (2026-11-01, CNBF-105/108): both records retain historical
  tables with provenance/missing raw inputs, correct MiB/s, codec comparison/savings arithmetic
  and actual adaptive-mean versus minimum-of-three probe methods, and replace universal NVMe/
  size/mmap conclusions with bounded workload/model statements. Parser ownership, stored CRC,
  eager expansion and current writer policy are source-checked; related CNB-spec paragraphs and
  spike READMEs are reconciled without completing the spec's own pending audit. The content target
  builds and all 23 compression/known-answer/hardware-table CRC cases pass privately with HEADLESS
  and zstd on the x86 host. Linux mmap and Zstandard primary specifications support the semantic
  corrections. No spike/old-payload timing replay, cold-cache/ARM/multiprocess qualification,
  implementation or ABI change.
- Build-performance documentation reconciliation (2026-11-01, COMP-001–010): dated tables retain
  their original scope, while current policy now names AUTO linker selection, optional focused
  groups versus the full CTest inventory, private display recipes, declared audit-cache inputs,
  shared C/C++ runtime identity, environment/cache-based ccache defaults and instrumentation scope.
  The guide describes console-edge parsing/cumulative counters and clean-driver profile/provenance/
  sampled-RSS limits. The policy checker passes, the focused content tool builds and the report
  helper emits valid existing-graph JSON without timing inputs. Related nine link-policy gates
  already pass in this pass. No clean/PCH/unity/IPO/sanitizer/compiler/timing campaign, global cache
  changes, implementation or ABI change.
- Link-size documentation reconciliation (2026-11-01, LINK-032/040): the guide scopes unused
  feature elimination to tested static composition, distinguishes shared-runtime deployment and
  mixed-consumer ordering, corrects public registry-query installation and current size/IPO/
  compressed-effect options, and treats the audit helper as an extraction model rather than a
  complete linker. Historical byte tables are dated/provenanced, not current size limits. All six
  static footprint consumers build; six symbol gates and three isolated CMake runtime/export
  policy tests pass (9/9) in the Linux Debug multi tree. No fresh Mobile Eggbert/size campaign,
  browser/Windows or all-linker full-runtime qualification, implementation or ABI change.
- Skinned-model documentation reconciliation (2026-11-01, Task 11.3/11.5): the page distinguishes
  the independent container from Model's current skinning support, owned resources from public
  descriptors, count-only attachment checks, palette target space, sampling order and the separate
  72-bone draw ceiling. It corrects outside-root bundle containment, raw/.cnj clip references,
  52-byte declarations and partial binary validation/converter scope. Graphics/content/probe
  targets build; 46 graphics and 14 manifest tests plus the attached-part pixel probe pass privately
  on OPENGLES3. Python converter tests cannot start without pygltflib in the active environment;
  no new converter/other-renderer/big-endian qualification, implementation or ABI change.
- Texture-stream documentation reconciliation (2026-11-01, SOFTWARE-304–307): the guide corrects
  fit/cover scale selection, enlargement and integer rounding, distinguishes DXT1 fixture coverage
  from all DDS codecs/header checks, and describes compressed-format gates, current-position/EOF
  handling and internal DIB/BMP paths. The graphics test target builds; all 23 focused format,
  resize and image-loader cases pass privately on SOFTWARE and OPENGLES3. No new Microsoft-XNA
  oracle capture, runtime implementation, dependency or ABI change.
- IEffectRenderer audit reconciliation (2026-11-01, IEFFECT-AUDIT-001): the page separates its
  original repair/microbenchmark record from the current source recheck and limits D3D9's CTAB
  search statement to implemented scalar/vector/matrix uploads. Dependency revisions and current
  EasyGL/WebGPU/SDLGPU/D3D9 cache/lifetime paths agree with the recorded findings. The focused
  two-program pixel target builds and passes privately on OPENGLES3 and OPENGL33; a fresh
  sizeof-only current-flag probe confirms 7,856-byte GpuDrawParams on Linux x86_64. No new timing,
  browser/other-renderer/recovery qualification, implementation, cache or ABI change.
- Compiled-effect/fuzzing documentation reconciliation (2026-11-01, FX-051/053): the guide
  distinguishes dirty-value synchronization from native uploads, borrowed texture bindings,
  disposal checks, conditional capabilities, route/profile limits and actual parser/dependency
  composition. Historical performance and sanitizer/fuzz records no longer imply current
  zero-cost/equivalence or exhaustive safety. Fuzz recipes use private displays and describe
  actual metadata/clone/apply coverage, early skips, swallowed exceptions, corpus loading and
  process-budget limits. The current twelve effect/fourteen crash binaries replay on both
  FNA3D drivers. Multi targets build; privately 106 core cases pass, each FNA3D driver passes
  66/67 (Reach volume skip), SDLGPU 61/62 (ShaderCross not linked), and native WEBGPU 64/64.
  GLSL ES precision wording is checked against Khronos §4.5. No new million-run, sanitizer,
  browser/EasyGL/SOFTWARE/DIRECTX9 or performance campaign, implementation or ABI change.
- glTF-limitations reconciliation (2026-11-01, GLTF-447): the registry's classifications/claims
  stay unchanged while the guide corrects its mechanical checks' real scope, unknown-extension
  meaning, strip/fan/line-loop conversion, mirrored cull ownership, recognized legacy tangent
  attributes, stride-80 tangents and generic versus dedicated normal-drop reporting. COLOR_0
  support now names all six PBR families. The obsolete instancing API/environment sketch becomes
  a current unsupported boundary; 3D validation is distinguished from pixel/readback capability,
  and point/spot approximation is scoped to this importer. The Content target builds and all 55
  selected registry/document/report/topology/mirroring/light cases pass privately with HEADLESS.
  No registry promotion, native instancing implementation, L7 recapture, hardware or ABI change.
- Windows-IDE documentation reconciliation (2026-11-01): the guide now describes the overridable
  Sharp Runtime root, source archive/submodule boundary, C++23 toolset requirement, static Windows
  module composition versus ELF DSO policy, limited preset feature set/two-job budgets, MSVC GUI
  entrypoint and configured-payload deployment scope. Preset inheritance, CI filters and source
  contracts are checked; the Windows preset validator passes. Related local configuration probes
  were already validated during this pass. No native Windows/IDE execution, queried remote CI
  result, relocated Windows-SDK or new hardware qualification, implementation or ABI change.
- FNA-reference harness documentation reconciliation (2026-11-01, Task 480): the guide separates
  current value/effect modes, selected-category comparison and genuine-XNA authority. It corrects
  target gates, unselected root fields, dictionary/numeric/category limits, version/provenance
  checks and obsolete environment blockers. The remaining hardcoded managed-project HintPath is
  explicit rather than presented as checkout policy. The native target builds and freshly emits
  valid 21-enum/16-preset/17-packed-type/5-viewport JSON with corrected IndexElementSize values.
  Local FNA source and Mono/xbuild/dotnet exist, but the reference DLL/managed submodule inputs are
  absent; no fresh running-FNA/native-effect comparison, implementation or ABI change is claimed.
- glTF performance/retrospective reconciliation (2026-11-01, GLTF-433/460): current CPU-time
  estimators, ASan-scaled ceilings, value-copy/cache sharing and per-command parsing replace old
  timing/identity claims. Accessor totals are logical dense bytes, multiple unpacked arrays can
  overlap, and the morph test's hard-coded 200% excludes deltas, scratch and native/shadow storage.
  Historical timing/RSS tables are retained with scope; current accessor totals are 24,072/26,084
  bytes. The retrospective distinguishes eight L0–L7 layers, algorithm versus wiring defects,
  selected pixel coverage, dated environment blockers and current six-family source partitions.
  The Content target builds; privately all nine small performance/lifetime tests pass, while both
  external large-reference tests skip for absent configuration. No new large-asset RSS, sanitizer,
  performance campaign, conformance milestone, implementation or ABI changes.
- glTF renderer documentation reconciliation (2026-11-01, GLTF-374/389): PBR now inventories
  all six shading families including FNA3D/SOFTWARE, seven map roles, GPU-neutral versus exact CPU
  defaults, current specular/vertex-color support and material/transform/coverage contracts.
  Obsolete register/blob/block-size/environment snapshots are replaced by current source/test
  routing. The stride page distinguishes canonical declarations, seven-stride witness coverage,
  allowed refusals, submission and pixels, and corrects CI to six identities including WEBGPU.
  Content/Graphics targets build; privately all 42 policy/buffer cases pass, three stride cases
  pass for each of seven compiled identities, and STUB skips its three for lack of 3D. The
  cross-renderer diffuse-color witness passes. HEADLESS remains validation-only; DIRECTX9 and
  browser WEBGL2 are not newly executed, and no whole-corpus L7 recapture, shader regeneration,
  implementation or ABI change is claimed.
- Diagnostics/Inspector documentation reconciliation (2026-11-01, DIAG-0001/INSP-0001): four
  pages correct the obsolete immutable-foundation claim, OFF macro type checking, explicit
  offline allocations, UTF-8/name bounds, producer teardown, resource/snapshot limits, UI batch
  polling and provider-thread/preview-ticket semantics. Performance tables remain historical;
  synthetic preview bytes, per-process estimators, excluded work and unavailable raw intervals
  are explicit. New isolated Release HEADLESS/NULL/SDL-free trees build; private diagnostics tests
  pass OFF 2/2, STATS 16/16 and FULL 41/41, Inspector 27/27. The bridge HTTP asset/CSP/Host/UI-token/
  disconnected smoke passes; benchmark smokes complete with zero idle provider calls. An old
  museum-ASan tree points to a removed source checkout and is not used as current sanitizer
  evidence. No implementation/ABI changes, new performance campaign or Windows/macOS/browser/
  sanitizer qualification.
- Terminal-platform documentation reconciliation (2026-11-01, PLAT-140/TCR-2): the feasibility
  proposal becomes a current provider contract with the actual CPU presenter, independent
  platform quantizer, capability/null-service table, requested versus resized virtual dimensions,
  Kitty flags 15, timed legacy key release, cell mouse limits, session leases/restoration and
  measured diff budget. Full redraw/blocking-write and unrecoverable-exit limits replace universal
  speed/restoration promises. The existing platform target builds; private Terminal suites pass
  116/117, with only the inherited-stdin-TTY preservation case skipped for lack of stdin TTY.
  Pseudo-TTY signal/terminate restoration runs. End-to-end terminal demo registration is checked
  in source, not newly built/run; no remote/emulator/Windows qualification or implementation change.
- Platform-performance report reconciliation (2026-11-01, PLAT-120): all eight table rows match
  the retained JSON and all sixteen stored verdicts agree with the current noise-floor rule.
  The page distinguishes pooled percentiles from paired-run deltas, regression detection from
  equivalence, historical llvmpipe from hardware/current-host performance and aggregate reports
  from missing raw runs. Neither recorded old revision resolves in the current object database;
  exact old-tree reproduction is therefore not claimed. Re-run instructions use private displays
  and explicit scene/device settings. Source/report inspection only; no new timing campaign,
  implementation or artifact changes.
- Platform-input documentation reconciliation (2026-11-01, PLAT-77): the seam map now records
  all eight removed duplicate adapters, current global-pointer/enumeration/rich-haptic contracts,
  platform-neutral legacy names and the single owning event path. Platform notes correct lazy
  controller startup, touch enumeration/fallback, cursor-description ownership, browser branches,
  SDL3 mapping API spelling and GUID formatting. Wayland global-read success is distinguished
  from useful desktop coordinates; native OS/device claims are bounded by evidence. Focused
  targets build; all 84 selected input bridge/gamepad tests and the native translation/warp filter
  pass on private displays. MDN browser references and pinned SDL headers are checked (SDL wiki
  retrieval timed out). No new physical-device or non-Linux qualification, implementation or ABI
  changes.
- Entrypoint/lifecycle documentation reconciliation (2026-11-01, PLAT-54/PLAT-29): obsolete
  entrypoint findings are replaced by the current platform header, Android/iOS public SDL edge,
  MSVC GUI mainCRTStartup helper and optional SDL3main target behavior. SDL lifecycle now records
  lazy platform-owned gamepad startup, retained haptic discovery, the separate process-wide camera
  startup exception, implicit events dependencies and explicit versus fallback static shutdown.
  Production-source scan finds no SDL_Init/SDL_Quit calls. Device/harness targets build; private
  tests pass all nine shutdown/sensor cases and the vibration concurrency case. Platform ownership
  tests pass 48/49; the camera-isolation case skips after earlier initialization and passes alone
  in a fresh process. No new native Windows/mobile or physical-device qualification, implementation
  or ABI changes.
- Platform-boundary documentation reconciliation (2026-11-01, PLAT-57/PLAT-76): the platform
  guide and renderer boundary now describe current factory/link behavior, capability probes,
  service/window/context ownership, native handles including UIKit, surface snapshots and actual
  Game event reactions. Obsolete proposed suspend reasons and universal allocation/qualification
  claims are removed. The two inventory generators receive prose-only corrections; their reports
  regenerate cleanly and all seven platform mechanical gates pass. Platform/graphics/runtime test
  targets build; privately, all three platform conformance entries and all three cross-platform
  event transcripts with HEADLESS rendering pass. The GL-default surface filter passes five cases
  and skips five guarded ownership/adoption cases; its event filter passes SDL3 and skips the other
  two until a compatible renderer is selected. No implementation or ABI changes, native-handle
  replacement support or new OS/hardware qualification.
- Filesystem-path documentation reconciliation (2026-11-01): historical CP1252 measurements
  are separated from current helpers; TryPathFromUtf8, Unicode versus legal-name validation,
  NUL, POSIX raw bytes, host separator parsing, portable-name limits and exact containment
  behavior replace universal round-trip/platform claims. The obsolete Win32Utf reference is
  removed, long-path/library claims are scoped, and title/save plus OpenStream/CNAEXT-pointer
  boundaries are explicit. Core/Runtime targets build; 71 focused path cases and 18 title cases
  pass on private Linux displays. No fresh native-Windows, Android packaged-asset or long-path
  qualification, implementation or ABI changes.
- Development-environment documentation reconciliation (2026-11-01): the Debian recipe now
  distinguishes root/shared-runtime CMake requirements, compiler versus standard library, actual
  CI sibling-branch resolution, base/video/media/C API dependency gates, ALSA versus SDL dummy
  audio, Wayland probe versus optional decoration, patch provenance and host-specific tooling.
  All forty install-block package names exist in the local apt index; Debian CMake/Doxygen/Draco/
  DXVK metadata is checked. All nine focused dependency/cache/patch/header/deployment configuration
  checks pass in the existing multi tree privately. No package install/upgrade, fresh-machine
  bootstrap, Wine-prefix provisioning, new cross build, implementation or ABI changes.
- Physical-module documentation reconciliation (2026-11-01): current direct CNA/Sharp Runtime
  dependencies replace the obsolete table, adding Platform/ContentPipelineBuild, optional
  GamerServices and avatar/C API boundaries. Static/shared umbrella composition, mixed-C singleton
  placement, compiled renderer families, focused versus aggregate tests, root harness ownership
  and the validator's actual modules-only scope are explicit. All fifteen minimal-link targets
  build in the existing multi tree; all 43 module probe/closure checks and three renderer registry/
  combination/target checks pass privately. The registry's first run caught the rewritten numeric
  count format; the corrected page passes its recheck. Current configuration evidence does not
  requalify every static/shared/cross-target composition. No implementation or ABI changes.
- Console documentation reconciliation (2026-11-01): the page now describes the absence of a
  qualified CNA port, separates upstream SDL/framework support from CNA qualification, and
  replaces device/SDK/certification/licensing absolutes and unmeasured first-target/memory advice
  with concrete integration boundaries. Existing precompiled-effect paths are distinguished
  from a missing console shader package. Platform/build sources and pinned SDL platform docs
  are checked; public FNA/MonoGame console pages are fetched. The SDL wiki fetch failed, so its
  pinned local counterpart is the stated evidence. No SDK, console build, handheld run or new
  porting commitment, implementation or ABI changes.
- Framework.Design documentation reconciliation (2026-11-01): current module/registration
  boundaries are explicit, including the concrete-header hook versus a base-only include.
  The original IL audit is separated from fresh metadata/source evidence. Local genuine XNA
  assembly metadata confirms thirteen public converter types and three non-public descriptor
  helpers; the focused Design target builds and all 25 tests pass on the private runner.
  Culture, invalid input, descriptor mutation/reconstruction and protected members are covered.
  No new complete IL/PropertyGrid/reflection qualification, API or implementation changes.
- Video/XMA documentation reconciliation (2026-11-01): the FFmpeg page corrects the shared
  VIDEO/MEDIA_PIPELINE detection gate, enabled shared-runtime link claims, URI refusal order and
  target policy versus upstream possibility. The XMA page bounds codec/licensing assertions to
  project evidence, preserves the three-target fixture fields without generalizing one sample
  into an encoder algorithm, and records parser/loop/identity/CMake-fallback limitations. The WAV
  parity row receives the related bounded correction; its own full audit remains pending. Focused
  Media/ContentPipeline targets build; 23 availability/decoder cases and ten XMA/target-processor
  cases pass on private displays. The independent XNB parser accepts all three committed target
  fixtures; host FFmpeg 7.1.5 has no XMA encoder. No OFF-artifact dependency check, real encoder,
  console playback, cross-target decode, implementation or ABI changes are claimed.
- Input API/emulation documentation reconciliation (2026-11-01): the signature reference
  corrects disjoint-tier/all-member-freeze claims, TouchPanel setters, missing object equality,
  formatting, emulation and nested enumerator entries, with current module paths and an explicit
  guard-gap table. The keyboard page qualifies software-session toggle barriers, Game.IsActive,
  native Guide availability, allocation/cost and historical browser evidence. Current headers,
  source and local FNA are compared; InputModule and aggregate test targets build. On private
  GL-default multi displays both compile/link guards, 32 keyboard-gamepad cases and three
  Game-pump/orientation cases pass; the four keyboard-accelerometer cases passed in the preceding
  Devices UBSan suite. No declarations, implementation, ABI or test guards were changed.
- Location proposal reconciliation (2026-11-01): the scope guard defers to the owner's 2026-10-25
  AOSP-only/provider-placement/event-thread decisions, removes obsolete CNA-local System paths
  and names the real compass test seam. The proposal separates planned services from current
  source, corrects explicit-interface PropertyChanged, removes zero-cost/rate/universal-provider
  claims and restricts optional ABI planning to native C routes. The owner's LOC-11 merge trigger
  is preserved; implementation has not started. CNA/Sharp Runtime sources confirm the service and
  compatibility types are absent, current compass TrueHeading still equals MagneticHeading,
  existing runtime prerequisites and local links are checked, and Microsoft API references are
  consulted. Documentation only; no location implementation, binding, build or hardware claim.
- Device documentation reconciliation (2026-11-01): the API matrix replaces closed campaign
  findings with current declarations, protected Dispose(bool), environment/keyboard support,
  normalized attitude, tilt-aware compass and bounded timestamp/fusion claims. The event contract
  closes its obsolete Android silent-catch finding and distinguishes per-instance/worker catches
  from propagating direct C++ injection, including aborted remaining handlers. The extension page
  qualifies generic system-info defaults, capability probes and file-dialog callback rejection;
  the hardware report template now covers all 19 numbered checklist sections and evidence scope.
  Focused UBSan targets build; on private displays Devices passes 484/488 with four unsupported
  hardware skips, and DevicesExt passes all 56. No new Android/physical-device/TSan qualification,
  implementation or ABI changes. Archive references are distinguished from freshly retrieved
  Microsoft SensorBase.Start and SensorReadingEventArgs.SensorReading pages.
- Camera documentation reconciliation (2026-11-01): `docs/cna-devices-camera-design.md`
  replaces the historical proposal with the implemented platform/session ownership, first-device
  selection, cached state and RGBA upload contracts. It records that Closed is not returned,
  frames can be consumed before validation refuses upload, upload exceptions can propagate and
  physical unplug/revocation is not tracked by the SDL3 wrapper. The C API page corrects the
  scripted-provider injection description. `CnaDevicesExtTests` builds with CNA_DEVICES=ON in
  the existing UBSan tree; all 13 canned Camera tests pass on a private display. No physical
  camera, permission or recovery qualification, implementation or ABI changes.
- Avatar documentation reconciliation (2026-11-01): `docs/avatars.md` corrects synchronous
  signed-in cache hits, native versus inline Emscripten loading, assembly failure, item/descriptor
  format distinctions, revision identity, pack-store roots, fallback bodies and qualification
  limits. `avatar-art-direction.md` replaces the obsolete six-head proposal/Blender repair log
  with current original-art criteria and freshly measured revision-5 proportions; `avatar-demos.md`
  corrects GL-default target gates, Update-count smoke budgets, screenshot timing and discovery
  retries/socket troubleshooting. The C API page receives the related first-byte IsValid correction.
  Static acceptance checks the catalog, 20 documentation claims and 36 golden metadata entries;
  the full bind-pose show-through audit passes. All five demo targets and focused test targets
  build. On private GL-default multi displays, 187/188 avatar tests pass with one explicit
  unsupported failure-injection skip, visual regression and all three C avatar smokes pass, and
  four local demos capture screenshots (dual/gallery inspected). Network demo play, all-renderer
  acceptance, external service deployment and performance were not requalified. No asset, golden,
  implementation or ABI changes.
- Apple-platform documentation reconciliation (2026-11-01): `docs/apple-platforms.md` now
  separates current CMake/workflow contracts from historical M4/retired-renderer measurements,
  removes contradictory live-CI claims and moves the long finished campaign table to Git history.
  iOS recipes use the recorded SOFTWARE baseline, default/allow-list status is separated from
  SDLGPU qualification, the mobile event pair is corrected to WillEnterBackground/DidEnterForeground,
  and storage precedence, signing/deployment and pixel-density boundaries match source. The
  host-portable Apple CMake script passes, and workflow/source/local-link checks pass. The cited
  old remote run could not be independently re-fetched in this audit; no current remote success,
  new Xcode build, Apple runtime or physical-device evidence is claimed. Implementation and ABI
  are unchanged.
- Release-procedure documentation reconciliation (2026-11-01): `docs/releasing.md` corrects
  the product-version example, removes the stale develop push recipe, identifies the owner-approved
  branch and explicit tag, and separates product/installed-package/C ABI version and readiness
  rules. Dependency pins are distinguished from system/override qualification, and the multi tree
  is no longer called the only configuration exercising all siblings. Root version, Doxyfile,
  README and changelog identities agree at 0.2.0; referenced CMake rules, local links and whitespace
  are checked. Documentation-only: no release/tag/push or dependency change was performed.
- Installed C++ SDK documentation reconciliation (2026-11-01): `docs/installed-cpp-sdk.md`
  replaces the obsolete Sharp Runtime version example with the actual modular-export requirement,
  records the separate combined static C archive build prerequisite, and documents the installed
  Emscripten helper boundary. `CnaInstalledCppPackage` passes in the current shared multi-renderer
  Linux tree: SDK build, install, relocation, configuration and runtime/C-only/C++/mixed-consumer
  checks complete in about two minutes. The producer has C_API_BUILD_STATIC=OFF, so the static C
  archive prerequisite is source-inspected rather than qualified by that run. Current evidence is
  distinguished from earlier static-SDK and cross-platform campaign results. Links pass; no runtime
  implementation or ABI changes.
- Browser boundary/lifetime documentation reconciliation (2026-11-01):
  `docs/browser-network-readiness.md` separates the accepted unavailable browser transport from
  historical SAMPLE-104 acceptance, corrects the Android libcurl exception and removes an implied
  CNA-local sample-evidence directory. The five browser refusal tests are identified as negative
  transport evidence, separately from the C API lifecycle probe. `docs/emscripten-mainloop-game-lifetime.md`
  distinguishes blocking C++ Asyncify from host-driven C frames, exception termination, source-build
  aliases versus installed SDK exports, and the historical spike's actual path. The current WEBGL2
  C ABI probe builds; all three Wasm link/module/real-Chrome browser checks pass through the private
  runner. This does not rerun the five browser-network refusal tests or qualify an installed C++
  blocking-loop consumer. Runtime implementation and public ABI are unchanged.
- Standalone graphics-extension documentation reconciliation (2026-11-01):
  `docs/cnaext-engine-layer.md` now distinguishes the always-registered module target from
  CNA_CNAEXT-guarded APIs, lists the actual umbrella composition, qualifies shader/readback support
  and preserves the sole DebugGizmos design constraint. `docs/ascii-post-process-effect.md` is now
  a current usage/control/alpha/cost contract instead of an extended migration rationale, correcting
  universal-renderer and unconditional GPU-readback claims. The focused module target builds and
  runs 73 tests on private SDLGPU: 71 pass, two HLSL-specific cases skip. All four ASCII quantizer,
  atlas, pixel and 3D-source oracle targets build and pass through the private runner. Local links
  and documentation inventory are checked; no runtime code or ABI changes.
- ALSA audio documentation reconciliation (2026-11-01): `docs/audio-alsa.md` now describes
  current selection/dependencies, negotiated playback/capture, bounded queues, mixer rules and
  encoded-memory streaming. Host ALSA plugin routing and hardware/latency claims are qualified;
  the microphone-layer defect remains explicit. Recipes use separate build trees and the private
  runner, with current registered null/file test names. All eight audio-platform selection tests
  pass; source references and local links are checked. Documentation-only: no new ALSA-enabled
  runtime build, physical playback or microphone capture was performed in this audit.
- Android graphics documentation reconciliation (2026-11-01):
  `docs/android-graphics-limitations.md` distinguishes earlier full builds/application runs from
  the later SDL/platform-only dependency-audit build and the unrebuilt final pin. It records the
  emulator detection rule, actual GLES float-readback regression and existing volume context-loss
  defect without implying physical-device or SDLGPU qualification. The CMake default is checked
  with an isolated script (Android → OPENGLES3), workflow/source references and local links are
  verified. Documentation-only; no Android cross-build, emulator or physical-device run was
  performed in this audit.
- Native consumer-boundary documentation reconciliation (2026-11-01): `docs/binding.md` now
  describes the approved native C API/C ABI scope instead of prescribing language-specific
  binding architectures, packaging or implementations. It corrects single-game versus runtime
  identity, per-family ownership/event rules, thread exceptions, struct prefixes, stream/completion
  mappings and the distinction between a consistent release checker and its Not ready verdict.
  External consumer status/pins remain their own evidence. The shared/static C ABI targets build,
  and `CApi_InstalledConsumer` installs both package forms, builds the standalone strict-C99 sample
  and runs successfully through the private SDLGPU runner. Local links pass; implementation and
  ABI are unchanged.
- C API texture/vertex-buffer contract reconciliation (2026-11-01): `TEXTURES.md`,
  `TEXTURE_VOLUMES.md` and `VERTEX_BUFFERS.md` now cover owned/pre-run devices, typed element-size
  views, BYTE/Color round trips, count-output timing, volume/cube constructor versus DDS format
  boundaries, and concrete destroy/retention rules. Vertex documentation corrects positive fixed
  capacity, prefix preservation, Discard zeroing, dynamic raw options, whole-buffer upload cost
  and CPU-shadow readback. The native ContentLost event is distinguished from the C info snapshot's
  constant-false defect in §5.1. Matching header comments are corrected with all non-comment bytes
  preserved. All selected resource/device/effect/model and C17/C++23 header targets build; all nine
  focused tests pass through the private SDLGPU runner and all nine C API static gates pass.
  Historical renderer/sanitizer claims are replaced with the current verification boundary;
  runtime implementation and public ABI are unchanged.
- C API storage-contract reconciliation (2026-11-01): `docs/c-api/STORAGE.md` now states that
  indexed listings are freshly enumerated and unordered, completion callbacks can precede output
  publication/final failure, and zero-length I/O still validates handles but bypasses native
  capabilities. It documents content-reader stream retention, protected device tokens, container
  disposal versus final release, retained registrations and the actual desktop/Android/browser
  root policy. Obsolete share-repair history is removed. Storage and content-reader targets build
  and both focused tests pass with build-local data roots through the private SDLGPU runner.
  Runtime implementation and public ABI are unchanged.
- C API shared-boundary documentation reconciliation (2026-11-01): `OWNERSHIP.md`,
  `SHARP_RUNTIME_BOUNDARY.md` and `STRINGS_AND_BUFFERS.md` now distinguish owned views, callback
  loans, independent devices, native disposal and release. They remove the nonexistent generic
  future/operation-handle mapping and universal event-cleanup assumptions, replace the obsolete
  level-zero-only texture boundary, and correct SpriteCommand minimum structure size, fixed array
  stride and non-finite-value behavior. Copy-probe costs, output-count timing, mutable-query races
  and empty-buffer exceptions are explicit. The six selected existing test targets build; all
  nine ownership/runtime/content/texture/UTF-8/boundary/header tests pass through the private
  SDLGPU runner. Local links and inventory are checked; no public ABI or runtime code changes.
- C API networking-contract reconciliation (2026-11-01): `docs/c-api/NET.md` now describes the
  four-byte color round trip, absent property defaults, diagnostic retention and independently
  owned advertised properties. It distinguishes caller-owned roster/sender views from callback
  loans, corrects SendData flag combinations, and records the event-injection validation and
  standalone-gamer parent-lifetime limitations in §5.1. Packet-extension prose no longer proposes
  a language-specific consumer. The networking and C17/C++23 header targets build; all five
  focused networking/ABI tests pass through the private SDLGPU runner and all nine C API static
  gates pass. Runtime implementation and public ABI are unchanged.
- C API model/animation contract reconciliation (2026-11-01): `docs/c-api/MODELS.md` now covers
  the current model graph, content-owned resource tokens, owned views and device lifetime, including
  independently owned/pre-run device inputs. It removes obsolete next-slice and repeated
  HEADLESS/sanitizer claims, replaces the stale morph stride list with the shared layout boundary,
  and adds tangent/flat-normal controls, import reports, cameras, skins, material variants, content
  tags and scene-clip target spaces. Array count timing, adapter collection identity and the
  loaded-resource disposal/token boundaries are explicit. Public owned-context Doxygen now requires
  paired context/callback values and follows shared-bundle release timing. All selected model,
  morph, animation, content and ABI targets plus C17/C++23 header checks build; all 12 focused
  tests pass through the private SDLGPU runner, and all nine coverage, generation, limitation,
  export and release gates pass. Public ABI and runtime implementation are unchanged.
- C API media-contract reconciliation (2026-11-01): `docs/c-api/MEDIA.md` now describes owned
  catalog/queue view handles, explicit view destruction, real thumbnail generation/fallbacks,
  cleared absent-relation outputs, disposal-query exceptions and queue path/name copies rather
  than full metadata clones. Video creation recognizes owned/pre-run devices; frame borrow
  validation timing, descriptor outputs and decoded-frame generation are explicit. Matching
  public-header and spec comments are corrected. A strict-C real-playback probe reproduced the
  stale association token returned after original Video handle release; §5.1 records the open
  defect and workaround without an implementation change. All selected media/video, content,
  lifetime, ABI and C17/C++23 header targets build; nine focused tests and all seven teardown
  variants pass through the private SDLGPU runner, including VideoSmoke's real-decode branch.
  All nine coverage, generation, limitation, export and release gates pass. Public ABI and runtime
  behavior are unchanged, and `documents.md` records completion in English.
- Documentation inventory language correction (2026-11-01): `documents.md` is entirely in
  English, including status labels, audit guidance and duplicate-review notes, on the owner's
  request. Its tracked file list and review states are preserved; future updates stay in English.
- C API math/3D-value contract reconciliation (2026-11-01):
  `docs/c-api/MATH_AND_3D_VALUES.md` is now a current value, geometry, curve and vertex contract
  rather than the accumulated CBIND-035 implementation plan. It records implemented outside-origin
  frustum ray clipping and non-unit ray parameters, all-zero built-in vertex defaults, and the zero
  matrix's successful decomposition with NaN rotation. Public geometry/matrix Doxygen is corrected
  accordingly. The page distinguishes POD lifetime from temporary native allocation, qualifies
  required-count output timing, documents sequential bulk-transform aliasing, retained curve views
  and declaration validation, and links the implemented vertex-buffer/binding path. Current native
  IEEE and signed-zero hash behavior remain explicit. All selected math, geometry, curve, color,
  packed/vertex, ABI and C17/C++23 header targets build; all 15 focused tests pass through the
  private SDLGPU runner, and all nine coverage, generation, limitation, export and release gates
  pass. Public ABI and runtime implementation are unchanged.
- C API input-contract reconciliation (2026-11-01):
  `docs/c-api/INPUT_SNAPSHOTS.md` now covers the current input families and their value, handle
  and callback lifetimes. It distinguishes C capture from platform polling, consuming relative
  mouse reads from stored joystick trackball deltas, platform touch snapshots from gesture-only
  C event injection, and missing hardware from argument or platform failure. Cursor ownership
  and SDL3 installation are documented. IME coordinates are characters, not UTF-8 byte offsets;
  activation requires the published handle/id pair, which a raw-handle setter clears rather than
  restores. Matching C-header and smoke-test comments are corrected, including the smoke's lack
  of real IME activation evidence. The obsolete complete-input-coverage claim is replaced with
  the generated inventory's current boundary. All selected C API targets and C17/C++23 header
  checks build; all nine focused input/ABI/identity tests pass through the private SDLGPU runner,
  and all nine coverage, generation, limitation, export and release gates pass. Public ABI and
  executable test/implementation behavior are unchanged; physical hardware validation remains
  separate from these smoke results.
- C API index-buffer contract reconciliation (2026-11-01):
  `docs/c-api/INDEX_BUFFERS.md` now records owned/pre-run device inputs, positive fixed capacity,
  the HiDef requirement for 32-bit indices and the dedicated base type name for both buffer kinds.
  Caller-array windows are distinguished from byte offsets into the buffer; prefix preservation,
  Discard zeroing, CPU-shadow readback, full-buffer upload cost, resource-in-use guards, empty
  transfers and output-count failure behavior match the current adapters. ContentLost events work;
  the C snapshot's constant-false defect was reproduced and is recorded in §5.1 without an
  implementation change. Explicit disposal is separated from typed handle destruction, including
  ModelMeshPart retention and event-registration cleanup. Matching public Doxygen and a stale
  smoke-test comment are corrected. All selected C API targets and C17/C++23 header checks build;
  all nine focused tests pass through the private SDLGPU runner, and all nine coverage, generation,
  limitation, export and release gates pass. The owned-device strict-C probe also confirms prefix
  preservation and event/cleanup behavior. Public ABI signatures, values and layouts are unchanged.
- C API handle-contract reconciliation (2026-11-01):
  `docs/c-api/HANDLES.md` now matches the process-wide registry and family adapters. It removes
  nonexistent public runtime/generic-release/kind-query assumptions and separates the single
  C-owned game from multiple independent devices. Registry slots store generation, kind, a shared
  object reference, creation thread and C tag; family code enforces parent/device/borrow rules.
  The page distinguishes copied tokens, owned aliases, callback loans, native disposal and handle
  release; it qualifies zero-handle and validation-order exceptions, documents foreign-thread
  dispatch and records that the registry has no generic `SHUTTING_DOWN` gate. Generation retirement
  remains explicit, with the helper-boundary test distinguished from public churn evidence.
  Matching `abi.h` comments no longer imply a separate runtime identity check. The selected registry,
  runtime, lifetime and C/C++ ABI targets build; all 11 focused tests pass through the private SDLGPU
  runner and all nine generation, ABI, export, route/Boolean and release checks pass. Public ABI
  signatures, values and layouts are unchanged.
- C API shared graphics-resource/surface contract reconciliation (2026-11-01):
  `docs/c-api/GRAPHICS_RESOURCES.md` now covers the current resource, state, display, target and font
  boundary. It adds OcclusionQuery to the generic resource kinds, per-handle C tags, disposal
  retention guards, pre-run borrowing, profile-limited vertex samplers and the implemented
  effect/transform SpriteBatch begin. Legacy display-handle refusals are separated from supported
  GameWindow interop; adapter refresh remains refused. Render-target documentation now recognizes
  explicit content-loss notification and unchanged-binding no-ops, and font documentation records
  copied glyph data, atlas retention and constructor/setter fallback differences. Matching public
  Doxygen is corrected. A strict-C probe confirmed the independently owned resource-device getter
  defect recorded in §5.1; no implementation fix is included. Resource/surface, lifetime, effect,
  model, device and ABI targets build; all 11 focused C API tests pass through the private SDLGPU
  runner and all nine coverage, generation, limitation, export and release gates pass.
- C API standalone graphics-extension contract reconciliation (2026-11-01):
  `docs/c-api/GRAPHICS_EXT.md` now records build availability, owned-device/game-child lifetimes,
  CRT/depth controls and shader-execution limits, ASCII readback and opaque output, DebugDraw's
  begin/clear/end and vertex-copy rules, and the always-built IBL descriptor. IBL completeness
  checks registered texture kinds, a positive mip count and non-negative intensity; it does not
  prove texture contents, actual mip count, common ownership or finite intensity. Public Doxygen
  corrects BlackWhite's opaque black fill, the IBL predicate, owned/pre-run device inputs,
  DebugDraw's invalid-device result and the obsolete no-failure claim; depth-state restoration is
  qualified to successful draws. The selected C ABI/header and ASCII targets build; all seven
  focused C API tests and both native ASCII pixel/3D-source probes pass through the private SDLGPU
  runner with `CNA_CNAEXT=ON`. All nine coverage, generation, limitation, export and release gates
  pass. The page keeps renderer pixel evidence separate from the C smoke's accepted refusals.
- C API graphics-device contract reconciliation (2026-11-01):
  `docs/c-api/GRAPHICS_DEVICE.md` now describes the current device surface and removes a duplicated
  frame-control/buffer-binding section. It corrects default Viewport to all-zero fields including
  MaxDepth, records pre-run borrowing and standalone-device ownership, and documents the active
  vertex-sampler ranges and the distinction between a native binding and a recorded C handle.
  The page no longer promises allocation-free text formatting or unavailable post-processing;
  it covers the implemented content-loss, format, shader-source, IBL and display-color-space
  queries, including the color-space probe's temporary transition, and the current occlusion
  state machine and precision query. Public device Doxygen now states that event registrations
  do not block game destruction and recognizes the pre-run borrowing window. The selected
  device/ABI header targets build, all 11 focused C API tests pass through the private SDLGPU
  runner, and all nine coverage, generation, limitation, export and release gates pass. SDLGPU's
  occlusion test verifies refusal; native renderer tests remain the evidence for query pixels.
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

The owner has started the repair campaign: first verify and fix the actionable §5 items, then
verify CNA reports in ../../openeggbert/galaxy-eggbert, then resolve failing CNA tests and GitHub
CI jobs. CNA_REPAIR_STATUS.json assigns stable IDs from the 37c69080d inventory and preserves
every workstream. A scope/capability boundary or missing platform qualification is not a completed
runtime fix. Keep its evidence and disposition explicit as the queue is worked.

The museum Pipeline Workshop items are all answered: 1 (build-wide nested names, `98aa655f2` and
`419723275`), 9 (`bbed8c61e` on Sharp Runtime `f990d074`, measured against
genuine XNA, demos `9598b43`) and 16 (owner: the documentation half is enough; the task sources
stay in `modules/content-pipeline`). Three validations from that earlier campaign still need a host
this machine does not have: the Windows/XNA cases (003, 101, 152; §8.1). The museum's reruns of
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

Continue the active CNA_REPAIR_STATUS.json queue; retain the native-Windows obligations in §8.1.

Historical repository checkpoint: **2026-10-31** (`next`, `b8e05abf8`), after the graphics
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
| Linux x86_64 | Primary host for the recorded native builds and private-display suites. Evidence is scoped to their renderer, driver, feature configuration and fixtures; physical peripherals, services and optional modes retain the limitations in §5 and their subsystem pages. |
| Windows | MinGW-w64 cross builds run under Wine: `DIRECTX9` through DXVK, `SDLGPU`'s Direct3D 12 backend through vkd3d-proton, `SOFTWARE`. `windows-msvc-ci.yml` now automatically builds and tests an SDL-free native-MSVC contract and an SDLGPU Direct3D 12 smoke set; it was added on 2026-10-30 and first passed on 2026-11-02 (`4fef632cf`). The deeper `DIRECTX9` workflow remains manual. No real-Windows hardware validation is current (§5.2). |
| Web (Emscripten) | `WEBGL2` and `WEBGPU` builds run in Chrome (`docs/web-emscripten-graphics-limitations.md`). |
| Android | `OPENGLES3`, the default there, on an x86_64 emulator; `SDLGPU` on Android is not qualified (`docs/android-graphics-limitations.md`). |
| macOS / iOS | macOS CI builds CNA with `SOFTWARE` and runs the portable platform suites and a bundle smoke app, and (gating, since RRC-051, not run yet) builds `SDLGPU` and runs its smoke set on SDL's Metal backend; iOS is a `SOFTWARE` simulator smoke app, with non-gating `SDLGPU` legs. `SDLGPU` is the default on both but is not re-qualified there (§5.2, `docs/apple-platforms.md`). |

**XNA API surface.** Every public type and every documented member of the Microsoft XNA 4.0 runtime
reference is represented; the generated census is
[`docs/xna-4-runtime-member-coverage.md`](docs/xna-4-runtime-member-coverage.md)
(`python3 tools/audit_xna_runtime_surface.py --write-reports`). Representation is not behaviour:
behavioural fidelity is held by each module's tests.

**Native C boundary.** The current native C API is experimental ABI **0.52.0**, defined by
`modules/c-api/include/CNA/C/abi.h`; its change ledger is `docs/c-api/ABI_VERSIONING.md` and its
release verdict is `docs/c-api/RELEASE_GATE.md`. The approved planning boundary is the native
C API/C ABI (`docs/binding.md`); language-specific work needs separate owner authorization.
External consumers own their current admission, packaging and platform evidence. Earlier CNA.NET
branch/ABI pins in this section are historical (recoverable at `bdf18a7d5:handoff.md`), not a claim
that the current external consumer admits this ABI or that CNA maintains a C# binding here.

**Dependencies.** FNA3D and MojoShader are CNA's forks, `libcna/FNA3D` and `libcna/mojoshader`,
pinned at `5c0b9b8c13f5` (FNA3D `cna-audit-fixes`) / `6ae750b59e6d` (MojoShader `cna`)
(`docs/fna3d-renderer.md`). The 2026-11-06 owner-authorized fork repairs fix stencil winding,
SDL_GPU upload order, unsigned mip bounds, duplicate object ownership and SPIR-V BEM/TEXDEPTH.
Owner fork GPU witnesses passed with pre-fix/mutant failures; CNA post-pin pixels, whole modules,
original fuzz seeds and owner LSan replay remain separate integration gates. Both
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
| What other repositories must change because of a CNA change | `docs/downstream-migrations.md` |
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

- **XACT instance-limit admission differs from measured Microsoft3.6** (TQ-XACT-25,
  2026-11-07). With OLDEST maximum1 and a leading never-played Prepared P, Microsoft
  leaves P Prepared and both L/I Playing/count2 after the first overflow; subsequent
  X/Y each replace one, settled count2. This does not yet prove the victim identity.
  CNA excludes Prepared victims and evicts L on I.Play, so the sequences differ.
  Microsoft category FAIL can reject Prepare with8ac70008 and no handle; CNA defers
  admission to Play. Its existing limit checks still count Stopping, separately from
  the now-fixed public NumCueInstances cache. Complete preparation/admission/state
  regressions and the distinguishing OLDEST control remain open. Earlier wave-less
  FAudio undercounts, unsigned wrap and failed-play decrements are real FAudio results,
  but genuine Microsoft3.6 does not reproduce them; do not implement them as the XNA
  contract. TQ-XACT-25 fixes public/internal count publication only. See
  `docs/xact-instance-limits.md` for exact native/managed/PCM/timing boundaries.
- **A local host that leaves while another local gamer stays leaves the network session without a host**
  (found 2026-11-04 by TQ-043, confirmed by TQ-054; open: needs an owner policy decision or a native XNA
  measurement). `NetworkSession::RemoveGamer` clears `Host`, as XNA's `ProcessGamerLeft` IL_006d..007e
  does, and nothing supplies a successor; a SystemLink host then welcomes later joiners with no host, so
  their `EndJoin` waits, and existing peers are not told about a CNAEXT local `RemoveGamer`. Managed IL
  does not say whether the session continues, which survivor becomes host or how `AllowHostMigration`
  applies. A conditional design and five candidate tests are in the TQ-054 lane notes
  (`TQ054ExplicitLocalSuccessorIsVisibleToTheEarlierLeave` pins the explicitly supplied case).
- **Signing out never removes a gamer from its network session** (found 2026-11-04 by TQ-043,
  confirmed by TQ-054; open, same evidence gap). The gamer-services dispatcher disposes the identity and
  raises `SignedOut` (`HandlePlayerSignInChanged` IL_0022..0035 / IL_0087..009b), but nothing in
  `modules/net` listens: a session can keep a disposed local identity, and a pending `AddLocalGamer` can
  still join it. The kernel's removal and end policy is not in managed IL.
- **FNA3D dependency repairs (2026-11-06).** FNA3D-1 two-sided stencil is fixed in fork
  `912e26d`; the OpenGL skip is removed from
  `Fna3dPipelinePixels.TwoSidedStencilUsesClockwiseOrdinaryState`. FNA3D-2/-6 duplicate small/large
  Effect object ownership is fixed in MojoShader `04616e1`, delivered through the current pins.
  Historical seeds 20261103001801/20261103032166 (MTH-SAFE-007) and 20261104107187 (TQ-018) remain
  required replay inputs with the repaired dependency identity and LSan active. Fork ownership
  checks and mutants passed; sandbox `detect_leaks=0` runs do not qualify LSan. Full attribution,
  regressions and outstanding evidence: `docs/fna3d-dependency-defects.md`, Fixed on the forks.
  FNA3D-3, FNA3D-5's positive-level CNA SpriteBatch issue, FNA3D-7 and FNA3D-8 remain open.

Before these, none was open. The FNA3D OpenGL-driver null EnvironmentMapEffect defect was fixed
on 2026-11-03 (MTH-REV-001): the renderer now creates its opaque-black null cube before binding
s0, because FNA3D's OpenGL driver creates every texture through texture unit 0. Before it,
CNA-FIX-D05 was resolved on 2026-11-02 by reproducing the measured XNA behaviour for overflowing
bitmap regions; see `docs/xna-content-pipeline-compat-api.md`.

### 5.2 Limitations

- **FNA3D cannot pass the physical-coordinate presentation witness** (measured 2026-11-03,
  MTH-CONF-005). FNA3D renders into and reads back its logical back buffer, and FNA3D_SwapBuffers
  scales it into the window. `SpriteBatch_Presentation` reads physical window coordinates (letterbox
  bars, far edge), so six of its checks fail on both FNA3D drivers. It is not registered for the FNA3D
  family. The destination layouts in `docs/fna3d-renderer.md` are implemented; physical-drawable
  readback is not. Closing it needs a physical rendering/readback surface or a separate
  final-image observation contract. SOFTWARE has no physical presentation layout at all
  (`SetPresentationMode` is inert); it presents its logical framebuffer.
- **Projective SpriteBatch matrices are unqualified on WEBGPU and SDLGPU** (MTH-EXIT-002 review,
  2026-11-03). Both expand sprite corners on the CPU, without a homogeneous clip-W vertex stream.
  Affine Begin transforms, including a Z component, are covered by `spritebatch_transform_depth`.
  A matrix with a perspective row is not.
- **The static C API archive with the C++ SDK is unsupported and refused at configure time**
  (decided 2026-11-03 by the owner, MTH-PLAT-004). `libcna_c_api_static.a` remains the encapsulated
  pure-C form; mixed C/C++ programs use the shared adapter and SDK, sharing one runtime.
  Installed CMake targets enforce this rule; pkg-config/plain linker consumers must follow it.
- `shader.h` does not name the result of `cna_shader_code_copy_text_ext`/`copy_bytes_ext` on the
  other payload form. The adapter returns `CNA_RESULT_INVALID_STATE`, and `CApi_OutputContracts_code`
  pins it.

- The retained XNA/Wine wma/mp3_mono_44100_128k.mp3 record returns AudioContent labelled Wma
  with readable metadata, then fails Data access. CNA's AudioContent::ReadThroughMediaDecoder
  instead enforces a Windows Media codec and rejects that MP3 during Import. The current
  XnaWmaImporter.RefusalsMatchXna test covers only missing/empty/truncated WMA, so it does not
  qualify the cross-format call stage. docs/xna-content-pipeline-media.md records the discrepancy;
  obtain native-Windows measurements with working WMA positive controls before treating the
  prefix-limited result as the complete Microsoft contract. No new regression/runtime change
  was added in this documentation task.
- XnaPipelineFinalAuditIsGreen remains an evidence-metadata gate, not runtime qualification
  (CNA-FIX-L05 / MTH-PRE-005). Both it and CApiRouteTestCoverage now separate declaration presence,
  configured CTest registration and validated retained execution. Source scans exclude comments,
  literals and literal false branches; unknown conditions, macro expansion and build inclusion
  remain unevaluated. Optional --execution-record joins ctest_evidence.py artifacts; the explicit
  --require-execution gate refuses missing/disabled/skipped witnesses and wrong evidence categories.
  Passing empty bodies still establish no assertion coverage: runtime qualification stays
  not-established and assertion counts unknown. The 23 new CPU-only regression cases and five
  selected metadata CTests pass in the software tree; no broader runtime/interop/sanitizer or GPU
  qualification is claimed. See docs/xna-content-pipeline-final-audit.md and the route-presence
  evidence section in docs/c-api/README.md for scope and capture identity limits.
- CNB read limits are not a total process-memory budget. Owned input, expanded chunks, decoded
  objects and GPU resources can coexist, and concurrent asset loads remain application-owned.
  Shared animation ranges additionally obey materialized track/key element totals; see
  `docs/cnb-format.md` §12.1 for the bounds and their scope.
- Original CNBF-105/108 payload hashes and raw timing samples are absent, so their historical
  tables cannot be replayed exactly. Corrected probes validate covered I/O/codec paths for new
  stable inputs; they do not establish full CNB-load performance. Production CRC tests compare
  selected dispatch against the table, not every forced hardware/fallback architecture; see
  `docs/cnb-compression-measurements.md` and `docs/cnb-mmap-measurements.md`.
- The historical August clean-build matrix cannot be reconstructed from current defaults alone.
  Current benchmark/report tools record effective cache values and source/dependency identities;
  those hashes are not source/package archives or a capture of every transitive configuration
  input. Matching before/after snapshots cannot exclude transient source changes. RSS remains a
  sampled process-tree sum; the CI report's cache counts are cumulative and its executed-edge
  counts parse console descriptions. See `docs/build-performance.md` for measurement scope.
- SkinnedModelEXT's indexed-skeleton and device checks cannot prove semantic joint identity:
  joint names are absent, and callers still need pieces from a common source rig. Raw clips have
  file/count/range preflight, but input and decoded arrays coexist and do not constitute a total
  process/GPU budget. Vertex joints/weights are not exhaustively validated. Legacy skeleton/vertex
  scalars remain native copies of little-endian converter output, without big-endian execution
  qualification; see `docs/skinned-model-ext.md` for the current validation scope.
- No real XMA encoder output or console playback is qualified: the external adapter's XMA2
  checks, measured loop, fingerprint, processor version and Xbox WAV routing (L16, 2026-11-02) are
  verified with stub and fake encoders only (`docs/xma-encoder-backend.md`).
- Native Camera has no public reopen/device-selector route after Lost. SDL3 removal and denial
  events and a revoked permission now end a session (CNA-FIX-L18), verified with pushed SDL events
  only: no physical unplug or revocation has been run (`docs/cna-devices-camera-design.md`).
- `RasterizerState.MultiSampleAntiAlias` is honored by `SOFTWARE`, desktop `OPENGL33` (also by
  runtime selection) and, since CNA-FIX-L20, `FNA3D`'s OpenGL driver; `DIRECTX9` writes render
  state 161 as XNA does, but its pixel effect is unmeasured because DXVK reports the state
  unhandled. `OPENGLES3`/`WEBGL2`, `WEBGPU`, `SDLGPU` and FNA3D's SDL_GPU-based drivers have no
  per-draw multisample switch, so the property has no effect there. This is separate from
  multisampled target storage, which those families still allocate and resolve
  (`docs/rasterizerstate-support.md`).
- `SDLGPU` applies `BlendState.MultiSampleMask` on a multisampled target only when it keeps every
  sample or none: the pinned SDL reserves `SDL_GPUMultisampleState::sample_mask`/`enable_mask`, so
  a mask of none drops the draw and any partial mask throws `System::NotSupportedException`
  (DC-R2, owner decision, `c7f6c68e5`). `RendererFeature::MultiSampleMask` reports each renderer's
  answer; `ColorWriteChannels` is honoured.

- `SurfaceFormat::Alpha8` render targets are stored natively by SOFTWARE only (L22, 2026-11-02:
  one-byte declared texels, sampled as (0, 0, 0, A)). The GPU renderers answer
  `SupportsSurfaceFormatAsRenderTargetEXT(Alpha8)` false and fall back to `Color` as XNA's
  preferred-format rule allows (`GraphicsCapabilityFloatRenderTargetTests.cpp`): GL has no
  renderable alpha-only format, SDL maps `A8_UNORM` to a swizzled `R8` on Vulkan that a colour
  attachment does not honour, and the shaders would have to route alpha to the stored channel.
  How the Xbox 360 allocated an Alpha8 target was never captured.
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
- The installed C++ SDK (`CNA_INSTALL_CXX`, `docs/installed-cpp-sdk.md`) is validated relocated on
  native Linux (shared, all-static with the C API off, video OFF) and, for WEBGPU, as a moved
  Emscripten SDK whose application ran three frames in headless Chrome 155 (MTH-PLAT-004,
  2026-11-03); Firefox, Safari and WEBGL2 installed consumers are not run. Its pure-C
  `CNA::CApiStatic` (+ `CNA::CApiStaticAsyncify`) is validated the same way, for WEBGPU only:
  Node and one Chrome page (`docs/c-api/CONSUMING.md`, Emscripten). MinGW-w64 only
  configures and exports the package: no relocated Windows SDK has been run.
- googletest's own stream capture and death tests create temporary files through the ANSI API, so
  any gtest binary aborts under a Windows `TEMP` holding a character outside the ANSI code page
  (measured under Wine, ACP 1252, 2026-11-02). The content and content-pipeline suites are
  otherwise Unicode-clean: under MinGW+Wine they give identical results with an ASCII and a
  non-ASCII (code-page-representable) `TEMP`. No MSVC run has repeated that measurement.
- **`SDLGPU` is the default renderer on Windows, macOS and iOS but is not qualified there** since
  the 2026-10-13 retirement of `DIRECTX11` and `METAL` (`docs/removed-renderers.md`). Evidence that
  exists: the full tree passed with `SDLGPU` (Metal backend) on a Mac mini M4 in the AM4 campaign,
  before the retirement; Direct3D 12 runs under Wine. The automatic hosted native-MSVC Direct3D 12
  smoke gate (added 2026-10-30) first passed on 2026-11-02 (`4fef632cf`): all eight SDLGPU programs on
  the hosted runner's Direct3D 12, which multisamples `D32_FLOAT` but neither stencil format. Not done:
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
- **MojoShader SPIR-V envelope** (FNA3D's SDLGPU driver, CNA's SDLGPU and WEBGPU compiled effects):
  the repaired `6ae750b59e6d` pin implements BEM and TEXDEPTH. TEXREG2AR, TEXREG2GB, TEXREG2RGB,
  TEXDP3TEX, TEXM3X2DEPTH and TEXDP3 remain refused by name. Enabled native/browser pixel gates
  now require BEM and independent register depth output (44 shared corpus cases); OFF builds
  retain all eight capability refusals. See `docs/fx-compiled-effects.md` for qualification limits.
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
- **Audio output device changes are measured nowhere.** The SDL3 backend opens
  `SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK`, so a change of the system default is whatever SDL3 does
  with a default-device stream. The ALSA backend reopens a PCM that disappears (CNA-FIX-L45,
  `docs/audio-alsa.md`), tested with injected `ENODEV` failures only; no physical unplug or
  default-device switch has been run on either backend (former AUD-04-010/011).
- **Three audio reports from the 2026-07-17 audit stay unconfirmed, not open:** high-pitched sound
  effects, missing audio files, and distorted or sped-up gameplay audio. They were never
  reproduced against the game they came from, which is not available. The audit narrowed the
  pitch report to a wrong sample rate reaching CNA before the mixer -- the class CBIND-146 fixed
  (`51d9c84cc`, 2026-10-02, an XNA-built sound's content sample rate).
- Two content-pipeline residues against genuine XNA output, none visible in the 7,726-reference
  sample sweep (`tools/xna-sample-sweep/README.md`, *What is left*): a generated `.x` normal can
  carry the opposite sign of a zero (13 of 1,291 probe normals, `XNASWEEP-231`) -- deciding it
  needs a genuine-importer run with `CNA_MODEL_ORACLE_BITS=1` over probes that separate the
  candidate orders (generation in the file's space before the basis change, or an accumulator
  that starts at `-0`); and FBX bone transforms keep an ulp-level residue that grows with depth,
  inside the float tolerance (`XNASWEEP-228`..`232`), which is the FBX SDK's own arithmetic.
  (`MeshHelper.OptimizeForCache` is exact on all 1,210 probes since `XNASWEEP-149`'s round six.)

### 5.3 External and driver limitations

Behaviour CNA does not own, measured and reported rather than worked around.

- **FNA3D's SDL GPU driver applies no MultiSampleMask** (2026-11-03, MTH-CONF-005). It writes the mask
  into `SDL_GPUMultisampleState.enable_mask`/`sample_mask`, which SDL documents as reserved
  (`FNA3D_Driver_SDL.c` ~1640-1650 at the pinned fork). CNA reports
  `RendererFeature::MultiSampleMask=Unsupported` for that driver (Supported on FNA3D's OpenGL
  driver), and `blend_msaa_boundaries` skips the mask cases with that reason. SDLGPU itself is
  Restricted: only all/none masks.
- **Conformance programs on drivers and capability boundaries** (`*_Conformance_*`,
  `modules/graphics/examples/conformance/`, measured 2026-10-15/17):
  - FNA3D: `SkinnedEffect.VertexColorEnabled` is ignored outright (B equals A) and saturates before
    the fold; its back buffer keeps a 3, 5 or 7 sample request unrounded while its render targets
    round it down; `GraphicsDeviceManager`'s default `Depth24` request is not applied in
    `graphics_adapter_query`. FNA3D-4 upload ordering (ordinary cube/volume/mipmapped textures
    and render-target updates after queued clears/resolves) is fixed by fork `912e26d` at the
    current pin. The owner qualified native fork pre/fixed/mutant witnesses. CNA's interim
    synchronous one-texel render-target upload readback is removed; the existing RT cube/mip
    tests and `texture_setdata_after_draw` conformance must pass on both drivers after rebuilding
    against the repaired pair. There is no CPU stall or implicit Present in the fork repair;
    extra submissions per texture update are deliberate. FNA3D-5's negative/native-bound mip
    conversion is also fixed there, while its positive-level CNA SpriteBatch report remains open.

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

- **Licence-header convention** -- decided 2026-11-02: CNA's own sources are MS-PL, including
  `CNA::Internal::Net`, examples, tests, tools and spikes. MIT remains only on the copies of other
  projects that `scripts/check_licence_headers.py` names (DirectXMesh's adapted face optimizer and
  the cna-gamer-services-server protocol files, which keep that project's LICENCE); the
  `LicenceHeadersAreMsPl` CTest enforces it and `CHECKLIST.md` states the rule.

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
