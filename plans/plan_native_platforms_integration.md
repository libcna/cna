# Native Platforms Integration Plan (Win32 + X11)

## Status summary (top of file is source of truth)

In progress. Updated as work proceeds. See task table below for live status.

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
| NPI-0004 | Squash-merge Win32 branch content, resolve conflicts, commit | in progress |
| NPI-0005 | Squash-merge X11 branch content, resolve conflicts, commit | pending |
| NPI-0006 | Unify SDL gating (adopt CNA_ENABLE_SDL, drop Win32 inline heuristic) | pending |
| NPI-0007 | Reconcile PlatformFactory / PlatformSelection for WIN32+X11 coexistence | pending |
| NPI-0008 | SDL-free WAV decoder implementation | pending |
| NPI-0009 | WAV decoder regression tests (CNA_ENABLE_SDL=OFF) | pending |
| NPI-0010 | Wire new decoder into WavDecoder.cpp / remove SDL dependency | pending |
| NPI-0011 | Prove SDL-free cna_content build + dependency inspection (Linux/X11) | pending |
| NPI-0012 | Prove Win32 SDL-free configure (mingw cross, compile-level only — no native Windows host) | pending |
| NPI-0013 | Add SDL-free CI cells (Linux X11 native; Win32 compile-level) | pending |
| NPI-0014 | AUTO/ON/OFF configuration matrix tests | pending |
| NPI-0015 | Win32 regression matrix (mingw cross-compile + Wine where meaningful) | pending |
| NPI-0016 | X11 regression matrix (Xvfb, Xvfb+openbox, ASan/LSan) | pending |
| NPI-0017 | Real X11 desktop/GPU validation — SKIPPED, no real X11 desktop on this host (documented) | n/a |
| NPI-0018 | Broad platform regression (SDL3/SDL2/HEADLESS/TERMINAL) | pending |
| NPI-0019 | Renderer regression spot-check | pending |
| NPI-0020 | Documentation updates | pending |
| NPI-0021 | SDL containment audit ledger | pending |
| NPI-0022 | Final git history / authorship audit | pending |
| NPI-0023 | Push to next (only after all gates pass or gaps are honestly documented) | pending |

(Updated throughout; see commit SHAs recorded per task as they land.)
