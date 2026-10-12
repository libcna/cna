# handoff.md — current state of CNA

This file describes the repository as it is now and is kept true. Until 2026-10-11 it was `NEXT.md`
and also carried the dated session log of 2026-07 to 2026-10; that log is in Git history
(`git show 8b5564f1e:NEXT.md`).

**§5 is the one authoritative list of current known bugs and limitations.** No other file keeps a
live bug list: `misc/known_bugs.md` and `misc/known_gaps.md` hold only the measurements behind some
§5 entries, the plans under `plans/` are history, and capability documents (renderer docs,
`docs/gltf-limitations.md`, `docs/gamer-services-known-limitations.md`, `CHECKLIST.md`'s accepted
deviations) describe what a subsystem does by design, not what is broken. When a bug is fixed,
delete its §5 entry (and its measurement record, if it has one) in the same commit.

Last reconciled against the code and a full test run: **2026-10-10** (`next`).

## 1. Project summary

CNA is a C++23 reimplementation of the XNA 4.0 programming model (`Microsoft::Xna::Framework`) on a
CNA-owned platform abstraction (SDL3 by default) and a curated set of graphics renderers. FNA's
source is the day-to-day behavioural reference; where FNA and XNA disagree, XNA wins (`CLAUDE.md`).

The project is leaving its AI-driven feature-expansion phase for long-term maintenance by a human
C++ developer. The renderer set, the platform layer and the content formats are deliberately kept as
they are: no new renderers, asset formats, graphics engines or subsystems. Work from here is fixing
§5, keeping behaviour faithful to XNA, and keeping validation deterministic.

## 2. Current status

**Renderers.** 14 public identities over 12 implementation families — `SDL_RENDERER`, `OPENGLES3`,
`OPENGL33`, `WEBGL2`, `VULKAN`, `WEBGPU`, `HEADLESS`, `SOFTWARE`, `STUB`, `DIRECTX11`, `DIRECTX9`,
`SDL_GPU`, `METAL`, `FNA3D`; the three GL identities share the internal EasyGL implementation.
The canonical list is `cmake/RendererIdentities.cmake` (held to the C++ enum and the C ABI by
`scripts/check_renderer_identities.py`); the table with gates and factories is
[`docs/renderer-registry.md`](docs/renderer-registry.md). 37 identities are retired and refused at
configure time by name; their reserved ABI values are in
[`docs/removed-renderers.md`](docs/removed-renderers.md). Do not add or restore a renderer without an
explicit owner decision.

**Platforms.** `CNA_PLATFORM` is `SDL3` (default; Windows, X11, Wayland, macOS, iOS, Android and the
browser are all reached through SDL3's drivers), `HEADLESS`, or POSIX-only `TERMINAL`. CNA's own
`WIN32`/`X11`/`WAYLAND` backends were retired on 2026-10-06 and are refused by name.
`CNA_AUDIO_PLATFORM` is `SDL3`, `NULL` or `ALSA`. Evidence differs per target:

| Target | What is actually verified |
|---|---|
| Linux x86_64 | Everything; this is where the suites run. |
| Windows | MinGW-w64 cross builds run under Wine (+DXVK) for `SDL_RENDERER`, `DIRECTX9`, `DIRECTX11`; MSVC builds in the Windows CI workflows. No real-Windows hardware validation is current. |
| Web (Emscripten) | `WEBGL2` and `WEBGPU` builds run in Chrome (`docs/web-emscripten-graphics-limitations.md`). |
| Android | `OPENGLES3` on an x86_64 emulator (`docs/android-graphics-limitations.md`). |
| macOS / iOS | macOS CI builds and runs the supported Metal contract; iOS is a simulator smoke app only (`docs/apple-platforms.md`). |

**XNA API surface.** Every public type and every documented member of the Microsoft XNA 4.0 runtime
reference is represented; the generated census is
[`docs/xna-4-runtime-member-coverage.md`](docs/xna-4-runtime-member-coverage.md)
(`python3 tools/audit_xna_runtime_surface.py --write-reports`). Representation is not behaviour:
behavioural fidelity is held by each module's tests. One member is deliberately left unimplemented
(§6).

**Content.** XNB readers (including LZX), CNA's `.cnb`/`.cnj`, direct glTF 2.0 loading, the
`cna-content` build tool and the XNA Content Pipeline façade: `docs/content-pipeline.md`,
`docs/xna-content-pipeline-parity-report.md`, `docs/gltf-limitations.md`. No further formats are
planned.

**Tests.** GoogleTest under CTest; `ctest -N` in a build tree gives its count. On 2026-10-10 the
HEADLESS Debug tree registered 8,808 tests and passed all 8,802 it ran (546 of them skip by design
on that renderer; the 6 Wine-based XNA interop tests run separately), and the run left the checkout
clean. CI: `general-tests-ci.yml` runs the unfiltered suite for `OPENGLES3` and fails on a dirty
checkout; the other workflows are subsystem- or platform-specific.

## 3. Where current information lives

| Question | Source of truth |
|---|---|
| Is X broken right now? | §5 of this file |
| Which renderers exist, how are they selected? | `docs/renderer-registry.md`, `docs/runtime-renderer-selection.md` |
| Does renderer R support feature F? | `docs/graphics-renderer-feature-matrix.md`, `docs/renderer-capability-profiles.md`, each renderer's own doc |
| Compiled XNA effects per renderer | `docs/fx-compiled-effects.md` §10 |
| Platform contract | `docs/platform-abstraction.md` |
| API-surface coverage | `docs/xna-4-runtime-member-coverage.md` (generated) |
| Deliberate deviations from FNA/XNA | `CHECKLIST.md`, `plans/plan_bindings_upstream.md` |
| GamerServices / Net / Avatar boundary | `docs/gamer-services-known-limitations.md` |
| Build rules (ccache, build directories, display isolation) | `CLAUDE.md` / `AGENTS.md` |
| Index of the other documents | `docs/README.md`, `plans/README.md` |

## 4. Working rules for this phase

- Fix defects, do not add surface. A fix comes with a focused regression test.
- Never run tests on the live desktop: use `tools/platform/run_gpu_tests_private.sh`.
- A test may write only to its build tree or a temporary directory. `scripts/check_clean_checkout.sh`
  enforces that a validation run leaves tracked files unchanged.
- Build into the existing `cmake-build-<variant>/` trees with ccache; do not create one-off build
  directories (`CLAUDE.md`, "Build locations & caching").

## 5. Known bugs and limitations

The single authoritative list. Every entry below was checked against the code on 2026-10-10.

### 5.1 Defects

1. **`VULKAN` applies no realistic `RasterizerState.DepthBias`.** `VulkanRenderer.cpp` passes XNA's
   normalized bias to `vkCmdSetDepthBias` unscaled (`draw.depthBias`), so a typical bias such as
   `-1e-4` has no visible effect. XNA's value must be multiplied by `2^(depth bits)`, as EasyGL,
   DirectX 11 (`XnaDepthBiasToD3D` in `modules/renderers/common/d3d/src/D3DStateMapping.cpp`),
   SDL_GPU, WebGPU and Metal already do. `Vulkan_DepthBias` uses `-1e6` and so still encodes the old
   premise, and `RasterizerDepthBiasContractTest` does not cover Vulkan. Measurement:
   `misc/known_bugs.md` §1.
2. **`SDL_RENDERER` ignores `TextureAddressMode::Wrap` and `Mirror`.** `SdlRenderer.cpp` has no
   sampler address-mode handling, so wrapped or mirrored sprites draw clamped, silently
   (`docs/sdl-renderer-2d-completeness.md`, Tasks 686/687).
3. **`DIRECTX9` `BasicEffect` draws 12 of its 32 shader variants.** The other 20 (Position-only,
   untextured lighting, lit with vertex colour) throw a named `std::runtime_error` from
   `D3D9EffectDraw.cpp`. Every register table exists; only the dispatch is missing.
4. **Test defects, not renderer defects.** With `SDL_GPU` as the default renderer, two
   `SdlGpuIndexedDrawRangeTest` buffer-rewrite cases fail, and with `WEBGPU` two cases of the same
   class: they rewrite a buffer that is still bound, which the device correctly refuses
   (`RRC-026`, `a18a5d374`).
5. **The XACT natural-completion tests are timing-dependent.** `CueTests.cpp`, `SoundBankTests.cpp`
   and `WaveBankTests.cpp` wait with fixed 50 ms sleeps; under parallel load a rotating subset fails
   (`misc/known_bugs.md` §5). They pass on an idle machine.
6. **`FNA3D` does not saturate the stock effects' `COLOR0` output**, which Direct3D 9 (and so XNA)
   clamps to [0, 1] before interpolation — the rule `AM4-106` gave WebGPU and Metal. Three
   `CnaTests` pixel checks fail with `FNA3D` as the renderer (measured 2026-10-10, SDL_GPU Vulkan
   driver, AMD RADV): `BasicEffectVertexLightingSaturationTest.ALitSumAboveOneIsSaturatedBeforeTheTexel`,
   `.TheVertexColourScalesTheLitSumBeforeItIsSaturated`, and
   `ClassicTextureFormat.PointSamplingExpandsChannelsAndPreservesDeclaredRanges` (line 536: a
   negative diffuse colour times a negative texel gives 128 instead of 0).
7. **`SendDataOptions` is not a flags enum, so `Chat` cannot be combined** (checked 2026-10-11;
   found by `GS-AUDIT-016`). XNA declares it `[Flags]` and documents `Chat` combined with
   `Reliable` and/or `InOrder`, with ordered chat packets ordered only among themselves
   (`xna4-spec/Microsoft.Xna.Framework.Net/SendDataOptions.xml`). CNA's
   `SendDataOptions.hpp` is an `enum class` without bitwise operators, and its comment wrongly says
   the values are not powers of two (`Reliable` 1, `InOrder` 2, `Chat` 4; `ReliableInOrder` is
   1|2). `NetPacketCodec::SendDataOptionsToEnetFlags` sends `Chat` exactly like `Reliable`, and a
   combined value 5-7 falls through to the same default.
8. **Three pure-C suites fail on Windows for reasons outside the C API** (found 2026-10-12 running
   the MinGW-w64 build under Wine with `CNA_GRAPHICS_RENDERER=SOFTWARE`; 102 of the 107 suites
   pass, one at a time). `ContentSmoke` writes its fixture with the narrow `fopen` and a UTF-8 file
   name, which the C runtime reads in the ANSI code page, so CNA (correctly UTF-8) cannot find the
   file. `GraphicsSurfaceSmoke` passes every check and returns from `main`, then the process never
   exits (100% CPU in teardown); `AudioSoundEffectSmoke` stalls the same way after its other-thread
   playback (it passed once). A minimal game plus a second-thread call exits cleanly, so it takes
   those suites' resources; unconfirmed on real Windows. (`MediaLibrarySmoke` fails only because
   Wine maps Pictures to the real home directory; `ForeignThreadCallsSmoke` is timing-sensitive.)

### 5.2 Limitations

- `BoundingFrustum::Intersects(Ray)` throws `System::NotImplementedException` for the case it does
  not compute — reserved for the human author, see §6.
- Native `SurfaceFormat::Alpha8` render-target storage is unavailable; an Alpha8 request falls back
  to `Color` as XNA's preferred-format rule allows (`misc/known_gaps.md` §1).
- Online sessions and invitations are native-only; a browser build refuses them explicitly
  (`misc/known_gaps.md` §4, `docs/browser-network-readiness.md`).
- `DIRECTX9`/`DIRECTX11` are validated under Wine (+DXVK) only; real-Windows hardware validation is
  not current. iOS has simulator smoke evidence only; `METAL` is validated by the macOS CI contract.
- `WEBGPU` is experimental; its open items are in the status summary of `plans/plan_webgpu.md`.
- `SDL_RENDERER` is 2D-only (3D calls throw), `SOFTWARE` is a bounded CPU renderer, and `HEADLESS`
  and `STUB` make no pixel claim (`docs/renderer-registry.md`, "Capability classes").
- Per-subsystem capability boundaries, by design rather than defect, are in the documents listed
  in §3.

## 6. Reserved for the human author

- **`BoundingFrustum::Intersects(Ray)`** (`modules/math/src/BoundingFrustum.cpp`). It answers a ray
  whose origin is outside (`nullopt`) or inside (`0`) the frustum, and throws
  `System::NotImplementedException` in the remaining case. The owner reserved the real
  implementation as hands-on C++ work: do not implement it, generate it, or remove the exception.
  `BoundingFrustumTests.cpp` covers only the two answered cases.

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

- GPU renderers are tested in their own trees (`cmake-build-vulkan`, `cmake-build-multi`, …) through
  the same private runner. Never run several renderers' full suites at the same time: GPU and driver
  contention produces false failures.
- `CMakePresets.json` has the focused unit, sanitizer, web, Apple and multi-renderer presets.
- System packages for a fresh Debian machine: [`programs.md`](programs.md).
