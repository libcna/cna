# SDL3-Free CNA Plan

## Goal

Make SDL3 and SDL3_mixer optional: a supported CNA application must be able to configure, build,
link, and run without either dependency. SDL3 remains supported; this plan makes room for SDL4 or
an entirely different toolkit through the existing platform and capability contracts.

## Current Position

`CNA_PLATFORM` already selects SDL3, SDL2, HEADLESS, or TERMINAL. Game code uses CNA platform
services and capability queries rather than native SDL APIs.

The current SDL2-only path proves part of the design:

- SDL2 owns the window/input platform, and SDL2 audio owns a native callback playback device.
- `cna_audio` links only SDL2 for `CNA_AUDIO_PLATFORM=SDL2`; it excludes SDL3 device and
  SDL3_mixer engine sources.
- OpenGLES3 is accepted; SDL3-owned renderers (`SDL_RENDERER`, `SDL_GPU`, `FNA3D`,
  `FREEDIRECT`) are rejected in the SDL2-only configuration.
- The high-level XNA mixer is SDL3_mixer based, so `SOUND_ENABLED` is deliberately off in this
  profile. Enabling it without a replacement would be a hidden SDL3 dependency.

## Definition of Done

1. A supported application target has no SDL3 or SDL3_mixer in its final dependency closure.
2. Selected audio either supports SoundEffect/dynamic audio/XACT/WaveBank/media semantics or
   reports an explicit, capability-gated limitation.
3. Every advertised renderer is SDL-independent or has a documented non-SDL integration path.
4. CMake configures SDL3 only if the selected production target, renderer, demo, or test needs it.
5. Core demos and test harnesses are platform-neutral; native fixtures are isolated.
6. CI runs graphical and headless SDL3-free profiles and audits their final link dependencies.

## First Supported Profiles

| Profile | Platform | Audio | Renderer |
|---|---|---|---|
| SDL2 desktop | SDL2 | SDL2 | OpenGLES3 |
| SDL2 CPU | SDL2 | SDL2 or NULL | SOFTWARE / compatible CPU presenter |
| Headless CI | HEADLESS | NULL or portable audio | HEADLESS / SOFTWARE |
| Terminal CI | TERMINAL | NULL or portable audio | Blend2D terminal presenter |

`SDL_RENDERER` and `SDL_GPU` cannot be SDL3-free by wrapping them: SDL is their implementation.
An SDL3-free profile must select another renderer.

## A. Replace the SDL3_mixer Engine

**Problem:** SDL3_mixer currently implements decode, mixing, tracks, streams, callbacks, gain,
looping, and native resource lifetime. A different output device alone cannot replace it.

**Tasks:**

- Define a backend-neutral mixer/decode contract below `MixerEngine`.
- Decide between a CNA-owned mixer with independent decoders and an SDL2_mixer adapter where its
  API can meet existing semantics faithfully.
- Implement PCM mixing, gain/pan/pitch, pause/resume, loop points, track/post-mix callbacks,
  dynamic queued streams, and callback-safe shutdown.
- Support existing content paths: WAV/PCM, encoded sound effects, WaveBank/XACT, and media audio.
- Treat recording as an independent capability; add SDL2 capture separately or report unsupported.
- Restore `SOUND_ENABLED` only after full non-SDL3 audio conformance passes.

**Acceptance:** SDL2 and portable-audio profiles run sound effect, dynamic sound, XACT, WaveBank,
media, lifecycle, and no-hardware suites without linking SDL3.

**Estimate:** 120–250 hours; highest risk.

## B. Remove Direct SDL3 Renderer Edges

**Tasks:**

- Maintain a renderer manifest: `SDL3 required`, `SDL-independent`, or `conditional`.
- Keep SDL3-only renderers rejected by SDL3-free selections.
- Move presentation, native handles, resize, swap interval, clipboard, and input behind platform
  services where the renderer does not intrinsically require SDL3.
- Investigate an independent route for FNA3D and FreeDirect; otherwise retain them as SDL3-only.
- Verify every selected renderer's transitive link closure.

**Acceptance:** every renderer is classified and every advertised SDL3-free renderer has no
SDL3/SDL3_mixer dependency.

**Estimate:** 70–150 hours.

## C. Make Third-Party Configuration Lazy

**Tasks:**

- Resolve platform, renderer, and audio selection before optional native dependencies.
- Add CMake predicates for SDL3, SDL2, SDL3_mixer, and other native packages.
- Configure `ThirdPartySDL.cmake` only for selected targets that need it.
- Preserve private native target edges and SDL-free public headers.
- Add configuration tests proving SDL3 is not configured for SDL3-free profiles.

**Acceptance:** SDL2 + SDL2-audio + OpenGLES3 does not configure, fetch, build, or link SDL3.

**Estimate:** 15–35 hours initially; 30–60 with broad CI coverage.

## D. Make Demos and Tests Platform-Neutral

**Tasks:**

- Audit demos, examples, benchmarks, and tests for direct SDL3 includes and links.
- Use CNA entrypoints and selected-platform services for generic host setup.
- Keep unavoidable native tests in backend-specific executables.
- Convert `demo_2d`, `house3d`, input demo, and audio demo to select the host in CMake.
- Add pseudo-TTY terminal coverage and SDL2 dummy-driver coverage.
- Label renderer-native integration tests as SDL3-only and exclude them from SDL3-free CI.

**Acceptance:** core demos and portable contract tests run under every supported SDL3-free profile.

**Estimate:** 80–160 hours.

## E. CI, Link Audits, and Documentation

**Tasks:**

- Add CI for SDL2 + SDL2-audio + OpenGLES3 and HEADLESS + CPU renderer.
- Run native fixtures with dummy audio/video; optionally add terminal pseudo-TTY coverage.
- Add a post-link check (`ldd`, `otool`, or equivalent) that fails on SDL3/SDL3_mixer.
- Document supported profiles, exclusions, capability limits, and build commands.

**Estimate:** 25–55 hours, overlapping other streams.

## Recommended Order

1. Add dependency manifests and lazy CMake configuration.
2. Freeze SDL2 + OpenGLES3 + NULL audio as the first complete SDL3-free profile.
3. Convert core demos and portable test harnesses.
4. Implement and validate the replacement mixer/decode backend.
5. Enable real SDL2 XNA audio; add capture only when required.
6. Classify/migrate optional renderers individually.
7. Make CI link audits mandatory.

The NULL-audio milestone proves the platform, renderer, and demo path before undertaking the
highest-risk mixer work.

## Risks

| Risk | Mitigation |
|---|---|
| SDL2_mixer lacks required semantics | Prefer a CNA mixer contract and prove semantics before committing to an adapter. |
| Audio decoder regressions | Preserve golden audio, lifecycle, WaveBank, and XACT tests. |
| SDL3 returns transitively | Enforce manifests at configure time and audit final link commands. |
| False confidence from SDL3-linked tests | Isolate native fixtures and audit every SDL3-free executable. |
| Optional renderer scope grows | Deliver SDL2 + OpenGLES3 first; classify rather than promise parity. |

## Effort Summary

| Delivery scope | Estimate |
|---|---:|
| Minimal SDL3-free profile with NULL audio | 140–250 hours |
| Full profile with XNA-compatible audio, core demos, and CI | 300–550 hours |
| Broad optional-renderer parity | Additional work based on selected renderer set |

These estimates preserve existing XNA behavior and require CI verification. A build that merely
configures without SDL3 does not meet this plan's runtime definition of done.
