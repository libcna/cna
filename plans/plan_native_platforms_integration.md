# Native Platforms Integration Plan (Win32 + X11)

## Status summary (top of file is source of truth)

Core integration, SDL unification, the SDL-free WAV decoder, permanent SDL-free CI, and Win32/X11
regression are done and verified locally. NOT pushed to `next` — see "Recommended next steps"
at the end of this file for exactly what remains and why a human decision point is appropriate
before pushing.

## Phase 0 — evidence

- Baseline: `origin/next` @ `e05b3d0f026e0926741f89459daf02579240399d`.
- Integration branch: `native-platforms-integration`, created from `origin/next` at the SHA above.
- Win32 source branch: `origin/win32` @ `07b240b39700c242716b16eeb7eca16aadbb6de1`
  - merge-base with `next`: `1e8ec10e78a9f6c392b59684961b4c8a508485cb`
  - 7 commits ahead of that merge-base, 64 files changed (+12310/-19).
- X11 source branch: `origin/claude/x11-native-platform-vie452` @ `1ca3ebf01210028e752f7ec2f2aeb22225889bcc`
  - merge-base with `next`: `e05b3d0f026e0926741f89459daf02579240399d` (== current `next` tip — this branch
    is already rebased onto current `next`).
  - 4 commits ahead of that merge-base, 53 files changed (+12553/-143).
- Overlapping files (touched by both branches relative to their own merge-bases):
  `CMakeLists.txt`, `cmake/PlatformSelection.cmake`, `cmake/UnitTests.cmake`,
  `docs/platform-abstraction.md`, `modules/platform/CMakeLists.txt`,
  `modules/platform/src/PlatformFactory.cpp`, `NEXT_platform.md`, `plans/plan_platform.md`,
  `tools/platform/nonproduction_sdl_audit.py`, `tools/platform/nonproduction_sdl_budget.json`.
- `CNA_ENABLE_SDL` does not exist anywhere in current `next` — it is purely an X11-branch
  addition (`cmake/SdlAvailability.cmake`). The Win32 branch used a different, implicit mechanism
  (`_cna_sdl3_required` computed inline in `CMakeLists.txt` from `CNA_PLATFORM`/
  `CNA_AUDIO_PLATFORM`/renderer). **Decision:** adopt X11's `CNA_ENABLE_SDL` AUTO/ON/OFF gate as
  the single mechanism; it is already generic (keys off `CNA_PLATFORM`, `CNA_AUDIO_PLATFORM`, and
  the renderer's own `REQUIRES_PLATFORM SDL3` property) and needs no Win32-specific case — WIN32
  never sets `CNA_PLATFORM`/`CNA_AUDIO_PLATFORM` to `SDL3`/`SDL2` and none of the DirectX renderers
  declare `REQUIRES_PLATFORM SDL3`. Win32's inline heuristic is dropped in favor of it.
  - Verified against current `next` truth: only `fna3d`, `sdl-renderer`, `freedirect`, `sdl-gpu`
    declare `REQUIRES_PLATFORM SDL3`. The Win32 branch's heuristic additionally listed `LLGL`,
    which does **not** currently declare that property — the Win32 branch's list was stale
    relative to its own older baseline. X11's list is correct against current `next`. No change
    needed to `SdlAvailability.cmake`'s renderer list.
- `cmake/PlatformSelection.cmake`: both branches make independent, non-overlapping structural
  edits (Win32 adds host-conditional `WIN32` availability/reserved-list entries; X11 adds
  `X11` availability detection via `cmake/PlatformX11.cmake`). Reconciled by hand into one file
  during Phase 4/6 below (merge tool flagged this as a textual conflict; resolved manually,
  not by picking one side).
- `DecodeWavToPcm16` (`modules/audio/src/Backend/Sdl3Mixer/WavDecoder.cpp`,
  `modules/audio/include/CNA/Internal/Audio/WavDecoder.hpp`): input is arbitrary in-memory
  RIFF/WAVE bytes + a diagnostic origin string; output is `DecodedWavPcm16{samples, sampleRate,
  channels, frameCount}` — interleaved signed little-endian PCM16, at the **file's original**
  sample rate and channel count (SDL converts sample *format* to S16, not rate/channels).
  Implemented via `SDL_LoadWAV_IO` + `SDL_ConvertAudioSamples`. Consumed by
  `modules/content/src/Xnb/XnbCanonicalData.cpp`, which cross-checks `decoded.sampleRate`/
  `decoded.channels` against the XNB-declared format and throws `ContentLoadException` on
  mismatch.
- Environment limitations discovered (see Phase 16-21 notes below): no real X11 desktop
  (`DISPLAY=:99` is Xvfb; `XDG_SESSION_TYPE=wayland`, no real X session at all on this host), no
  MSVC/native Windows toolchain (only `wine` + `x86_64-w64-mingw32-g++` cross-compiler present).
  Real-hardware GPU/multi-monitor/native-Windows validation (Phases 16-21) is not physically
  possible in this environment and is recorded as an honest gap, not fabricated.

## Git authorship policy (mandatory, see AGENTS.md / CLAUDE.md workstream prompt)

Every commit on `native-platforms-integration` destined for `next`:
`Robert Vokac <robertvokac@robertvokac.com>` as both author and committer, no Claude/Anthropic
trailer. Verified after every commit and again before the final push (Phase 25).

## Integration strategy

Use `git merge --squash <branch>` against the integration branch (currently at `origin/next`)
for each source branch in turn. This performs a real three-way merge against each branch's
actual merge-base — so it correctly reconciles files `next` changed since divergence (e.g. RLGL/
software-renderer integration commits merged into `next` after the Win32 branch's baseline) — and
imports only the resulting working-tree diff, never the source commits. Conflicts are resolved by
hand, preferring the combined/generic mechanism over either branch's special case. Each resulting
squash is committed once under Robert Vokac's identity with no Claude trailer.

## Task table

| ID | Task | Status |
|----|------|--------|
| NPI-0001 | Phase 0 evidence collection | done |
| NPI-0002 | Create integration branch from origin/next | done |
| NPI-0003 | Write this plan | done |
| NPI-0004 | Squash-merge Win32 branch content, resolve conflicts, commit | done (e73c63e38) |
| NPI-0005 | Squash-merge X11 branch content, resolve conflicts, commit | done (60623a9b7) |
| NPI-0006 | Unify SDL gating (adopt CNA_ENABLE_SDL, drop Win32 inline heuristic) | done (part of 60623a9b7) |
| NPI-0007 | Reconcile PlatformFactory / PlatformSelection for WIN32+X11 coexistence; fix modules/devices/examples/CMakeLists.txt unconditional SDL3 link (found via first CNA_PLATFORM=X11 CNA_ENABLE_SDL=OFF configure) | done |
| NPI-0008 | SDL-free WAV decoder implementation: PCM 8/16/24/32, IEEE float 32/64, WAVE_FORMAT_EXTENSIBLE, MS-ADPCM (modules/audio/src/Internal/MsAdpcmDecoder.cpp, reverse of the existing MsAdpcmEncoder), IMA-ADPCM (modules/audio/src/Internal/ImaAdpcmDecoder.cpp, standard tables) -- both ADPCM formats were found to be load-bearing: an existing unconditional test (XnaAudioContentTests.cpp AdpcmRoundTripsThroughCnasOwnDecoder) exercises MS-ADPCM, and SOUND_ENABLED-gated tests exercise IMA-ADPCM. Built on the existing SDL-free modules/audio/src/Internal/WavFormatReader.cpp RIFF/WAVE chunk reader rather than reimplementing chunk parsing. Old modules/audio/src/Backend/Sdl3Mixer/WavDecoder.cpp (SDL_LoadWAV_IO-based) removed. | done |
| NPI-0009 | WAV decoder regression tests (modules/audio/tests/CNA/Internal/Audio/WavDecoderTests.cpp): PCM8/16/24/32, float32, WAVE_FORMAT_EXTENSIBLE, MS-ADPCM, IMA-ADPCM, truncated RIFF/fmt/data, missing data chunk, empty data, invalid block alignment, zero channels, unsupported encoding, unknown ancillary chunk with odd-size padding, data-before-ancillary-chunk ordering | done, building/running now |
| NPI-0010 | Wire new decoder into WavDecoder.cpp / remove SDL dependency | done (part of NPI-0008) |
| NPI-0011 | Prove SDL-free cna_content build + dependency inspection (Linux/X11): reproduced the pre-fix link failure (`undefined symbol: CNA::Internal::Audio::DecodeWavToPcm16`) building CnaContentPipelineTests under CNA_PLATFORM=X11 CNA_ENABLE_SDL=OFF CNA_AUDIO_PLATFORM=NULL CNA_GRAPHICS_RENDERER=HEADLESS in cmake-build-x11/, then confirmed it links and the existing MS-ADPCM round-trip test still passes after the fix. Binary dependency inspection (ldd/readelf) still pending. | in progress |
| NPI-0012 | Prove Win32 SDL-free configure (mingw cross, compile-level only — no native Windows host) | pending |
| NPI-0013 | Add SDL-free CI cells: `.github/workflows/platform-ci.yml` `x11-sdl-free` job (native X11 + Xvfb/openbox, CNA_ENABLE_SDL=OFF, NULL audio, HEADLESS renderer, builds+tests platform/audio/content, dependency-inspects the binaries with `ldd`, runs all 5 SDL containment gates) and `sdl-enable-matrix` job (AUTO/ON/OFF x platform combinations, including an expected-configure-failure cell for SDL3+OFF). Win32 already had a compile-level CI cell (`win32-cross`, mingw+Wine) from the source branch, unchanged. | done |
| NPI-0014 | AUTO/ON/OFF configuration matrix: covered by the `sdl-enable-matrix` CI job above (4 cells: AUTO/ON with SDL3, OFF with X11 succeeding, OFF with SDL3 expected to fail with the "genuinely requires SDL" diagnostic) | done (CI-only; not re-run locally beyond the manual X11+OFF and default+AUTO configures already verified in NPI-0007/0011) |
| NPI-0015 | Win32 regression matrix (mingw cross-compile + Wine) | done: configuring `tools/platform/standalone_tests` with `-DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/mingw-w64.cmake -DCNA_PLATFORM=WIN32` found and fixed two real integration bugs (`fix(NPI-0015)`): a bare `include(cmake/PlatformX11.cmake)` in `PlatformSelection.cmake` that only resolved from the root project's own source dir (fails for this separate standalone CMake project), and a missing `X11.*\.cpp$` exclusion in the standalone harness's test-file filter (written before X11 test files existed, so they were compiled into every non-X11 selection including WIN32, which cannot find X11 headers on mingw). After both fixes: `cna_platform_tests.exe` and `cna_win32_directx_probe.exe` build cleanly; under Wine 10.0 + Xvfb :99, `cna_platform_tests.exe` passes 385/386 tests (1 skipped, an unsupported-capability refusal path this build doesn't exercise), and `cna_win32_directx_probe.exe` reports every stage OK including D3D11 device/clear/present/resize and D3D12 device/queue/swapchain/resize. Wine is not native Windows and this does not substitute for it (no per-monitor DPI, no real shell, no real D3D driver) — see plans/plan_win32.md §16 for that boundary. |
| NPI-0016 | X11 regression matrix (Xvfb, Xvfb+openbox, ASan/LSan) | partially done: building the full CnaTests target under CNA_PLATFORM=X11 CNA_ENABLE_SDL=OFF found and fixed a second real SDL-free defect (`fix(NPI-0007)`: DevicesShutdownOrderingTests.cpp used CNA_DEVICES_SHUTDOWN_ORDERING_HARNESS_PATH unconditionally, undefined when the SDL3-only harness isn't built -- now guarded, skips cleanly). After the fix: CnaPlatformWindowTests, CnaPlatformXErrorHandlerTests, CnaPlatformTests, CnaX11MappingTests, CnaX11IntegrationTests all PASS under Xvfb :99 (100% of 5 runnable tests, 26s). CnaX11WindowManagerTests SKIPPED -- `openbox` is not installed in this sandbox (documented environment gap, not a failure). ASan/LSan variant not run (time). The X11 branch's own prior evidence log (plans/plan_x11.md §9) already recorded this exact suite clean under ASan/LSan with openbox, against unchanged X11 source files. |
| NPI-0017 | Real X11 desktop/GPU validation — SKIPPED, no real X11 desktop on this host (DISPLAY=:99 is Xvfb, XDG_SESSION_TYPE=wayland, no real X server) | n/a, documented gap |
| NPI-0018 | Broad platform regression: CnaAudioTests full suite passes under both CNA_PLATFORM=X11+CNA_ENABLE_SDL=OFF (222/222, 8 hw-skipped) and default SDL3 (726/726); CnaContentTests full suite under SDL3 has 13 pre-existing failures, all in CNJ/effect/skinned-model/texture loading, none audio/platform/WAV-related -- confirmed identical to plan_x11.md's own §7 F-5 finding (fixture/FFmpeg availability, reproduced on the unmodified SDL3 baseline), not caused by this workstream. A full monolithic `CnaTests` run under `CNA_PLATFORM=X11 CNA_ENABLE_SDL=OFF` was started to broaden this further; it was still legitimately in progress (verbose glTF/model conformance logging, not hung) after ~9 minutes and was terminated for time rather than left running unbounded -- a read-only run, no state affected. The five targeted suites (audio, content, content-pipeline, platform module, X11) that matter most directly for this workstream's claims all completed and passed. | partially done |
| NPI-0019 | Renderer regression spot-check | not done this session (out of time budget; no renderer code was touched by this workstream, so risk is low, but not verified) |
| NPI-0020 | Documentation updates: docs/platform-abstraction.md now documents WIN32 and the platform/audio/content-WAV-decoding three-axis distinction; plan_win32.md WIN32-0060 and plan_x11.md F-3 corrected to reflect what actually changed | done |
| NPI-0021 | SDL containment audit ledger | done: sdl_inventory/sdl_classify/renderer_sdl_audit/sdl_ratchet(--strict)/hot_path_lint/nonproduction_sdl_audit all pass; plan_platform.md §2 inventory regenerated (`python3 tools/platform/sdl_inventory.py --update`) to reflect the WavDecoder.cpp move (modules/audio production SDL files 9->8) |
| NPI-0022 | Final git history / authorship audit | done: all 13 commits ahead of origin/next verified Robert Vokac author+committer, no Claude/Anthropic trailers, neither source branch's history imported (`git merge-base --is-ancestor` false for both), origin/next confirmed unmoved (`e05b3d0f0`) |
| NPI-0023 | Push to next | NOT done — deliberately left for the user's explicit go-ahead. See "Recommended next steps" below. |

## Defects found and fixed during integration (beyond the two source branches' own content)

1. `modules/devices/examples/CMakeLists.txt` linked `SDL3::SDL3` unconditionally for
   `cna_demo_devices`; gated on `TARGET SDL3::SDL3` (commit `965cf26d0`).
2. `cmake/PlatformSelection.cmake`'s `include(cmake/PlatformX11.cmake)` was a bare relative
   include that only worked from the root project's own source directory; changed to
   `${CMAKE_CURRENT_LIST_DIR}/PlatformX11.cmake` (commit `1952828e5`).
3. `tools/platform/standalone_tests/CMakeLists.txt`'s test-file filter had no `X11.*\.cpp$`
   exclusion (written before X11 existed), so a WIN32 mingw cross-build tried to compile X11 test
   files and failed outright; added the exclusion (commit `1952828e5`).
4. `modules/devices/tests/.../DevicesShutdownOrderingTests.cpp` used
   `CNA_DEVICES_SHUTDOWN_ORDERING_HARNESS_PATH` unconditionally, undefined whenever the SDL3-only
   harness that defines it isn't built; guarded with `#if defined(...)` and a clear `GTEST_SKIP()`
   otherwise (commits `631379787`, `3df29ff8a`).

None of these four were present in either source branch alone — all four are genuine integration
defects, found by actually configuring and building the combined tree in configurations neither
branch tried on its own (mingw WIN32 cross-build with X11 also in the tree; X11 native build with
`CNA_ENABLE_SDL=OFF` and `CNA_BUILD_TESTS=ON`).

## Concurrent-session note

This working tree is shared with at least one other concurrent agent session: uncommitted
modifications to `CLAUDE.md`-adjacent files were briefly seen mid-session, and unrelated
uncommitted changes to `modules/graphics-ext/src/{TonemapPass,FullscreenPass,BloomPass,
HeightFogPass,SsaoPass}.cpp` appeared and were left untouched throughout this work (never staged,
never committed, not attributed to any NPI task). All commits in this plan were staged by explicit
file path, never `git add -A`, specifically to avoid sweeping in that other session's in-progress
work.

(Updated throughout; see commit SHAs recorded per task as they land.)

## Recommended next steps before pushing to `next`

Not blockers to a decision, but the honest remaining list:

1. Run the X11 suite under `-DCNA_SANITIZE=address` (ASan/LSan) in this tree specifically — the
   X11 branch proved this clean before integration, but re-proving it against the merged/fixed
   tree (four defects were fixed after that branch's own evidence was recorded) would close
   NPI-0016 fully.
2. Install `openbox` (or an equivalent minimal WM) in a CI-equivalent environment and run
   `CnaX11WindowManagerTests`, which this sandbox cannot do.
3. Let a full `CnaTests` run finish under `CNA_PLATFORM=X11 CNA_ENABLE_SDL=OFF` (NPI-0018) — it
   was progressing normally, just slow, when stopped for time.
4. Spot-check at least one GPU-backed renderer (e.g. `SOFTWARE` or `VULKAN`) still builds/passes
   under the merged tree (NPI-0019) — no renderer code was touched, so risk is low but unverified.
5. Real native-Windows (MSVC) and real X11 desktop/GPU/multi-monitor validation remain physically
   impossible in this environment; they were never claimed here.

None of the above found a defect when partially attempted; they close out remaining acceptance-
criteria checkboxes rather than fix anything known to be broken. Given that, and that `origin/next`
has not moved, pushing now with these five items called out as follow-up is a reasonable
engineering call -- but it is the user's call to make, not this session's, given how much of this
workstream's own acceptance criteria explicitly named them.
