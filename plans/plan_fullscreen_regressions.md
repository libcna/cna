# Fullscreen presentation and DirectX9 recovery — FULLSCREEN-001

Status: **complete**, 2026-09-28. Starting CNA revision: `a62c40b09e86`.
The owner explicitly authorized fixing these CNA defects after diagnosis in Mobile Eggbert.

## Scope and causes

- Vulkan used a physical letterbox rectangle as the logical SpriteBatch projection divisor.
  Enlargement was cancelled whenever the default viewport differed from the physical target.
- Canvas enlarged the backing canvas without applying presentation scaling or letterbox
  offsets to SpriteBatch rendering or input coordinates.
- DirectX9 allowed Game.Draw on a lost device, so an exception could escape before the
  Present-based recovery poll. A failed fullscreen Reset also left recovery using obsolete
  backbuffer dimensions, and cached XNA blend/depth states survived native state invalidation.

## Completed implementation

- [x] Forward public GraphicsDevice.Viewport dimensions to the internal SpriteBatch renderer
  at the render-state preparation boundary: End for Deferred, Begin for Immediate. This matches
  the projection boundary in the local FNA `src/Graphics/SpriteBatch.cs` PrepRenderState.
- [x] Capture the logical projection separately in each Vulkan batch snapshot. Retain physical
  viewport placement, custom viewports, user transforms and render-target isolation.
- [x] Compute Canvas presentation rectangles; compose the user transform before the physical
  viewport transform; clip to that viewport. Map input with letterbox offsets, independent
  axis scales and display density. RenderTarget2D uses its own viewport.
- [x] Add an internal mutable TryBeginDrawEXT gate, defaulting to existing CanBeginDrawEXT.
  DirectX9 polls cooperative level and pending reset/resize before Draw. Update continues
  while Draw is skipped. Explicit debug loss remains pending until debug restoration.
- [x] Retry DirectX9 recovery against the current drawable dimensions, clear pending resize
  after successful reset, and invalidate GraphicsDevice blend/depth caches on native reset.
- [x] Add shared Vulkan/Canvas presentation pixel probes, Canvas host tests, and a DirectX9
  Game-loop probe that deliberately holds a native backbuffer during fullscreen Reset.
- [x] Rebuild the three affected Mobile Eggbert variants, run private-display regression and
  consumer checks, update task/audit notes, and commit this task without pushing.

The changes are framework internals; public XNA API signatures and the game's drawing
sequences are preserved. No game source, SharpRuntime, stubs or new dependencies were needed.
The internal projection hook is a no-op for unaffected renderers. The mutable draw gate
preserves their existing readiness behavior. Native recovery gating is a documented
renderer/platform adaptation rather than a game workaround.

## Verification

All commands below completed successfully. GPU/window tests used
`tools/platform/run_gpu_tests_private.sh`, never the owner's live desktop. Wine and Chrome
used dedicated test prefixes/profiles. Evidence is in the consumer workspace
`/rv/data/development/github.com/openeggbert/mobile-eggbert/build/logs/fullscreen-fixed/`.

| Check | Result |
|---|---|
| Mobile Eggbert Linux Vulkan build | pass, `linux-vulkan-final-build.log` |
| Mobile Eggbert web Canvas build | pass, `canvas-final-build.log` |
| Mobile Eggbert MinGW DirectX9 build | pass, `directx9-final-build.log` |
| Vulkan shared SpriteBatch presentation probe | 11 pixel assertions pass |
| Canvas shared probe in real Chrome | 11 assertions pass; console/output capture duplicates them |
| Canvas host unit tests | 19/19 pass, including two new presentation/input cases |
| Existing Vulkan presentation-mode and 3D viewport-subregion probes | both pass |
| DirectX9 Game-loop recovery probe under Wine | injected loss skips Draw; Update continues; restoration resumes Draw |
| Same DirectX9 probe with deliberately retained native backbuffer | Reset fails while held, Draw waits, then resumes after release; fullscreen exit passes |
| DirectX9 cached-state restoration | native AlphaBlend enabled and depth disabled on every resumed Draw |
| Actual Vulkan game, resized 1600 x 900 | image bounds `(50,0)-(1550,900)`, correctly enlarged to 1500 x 900 |
| Actual Canvas game, page Fullscreen | 1920 x 1024 backing canvas; centered 1707 x 1024 image, with Resize canvas both on and off |
| Actual DirectX9 game, F11 under Wine | remains running; fullscreen image fills private 800 x 500 surface |
| Source whitespace | `git diff --check` passes |

Test executables were compiled with the consumer's exact toolchain and current CNA libraries
and run directly. Their targets are also registered in the corresponding CNA example CMake
files. Full CNA-root CTest and the other nineteen consumer renderer builds were not rerun.
Enabling CNA examples through this consumer's add_subdirectory exposes an existing unrelated
CMAKE_SOURCE_DIR path assumption; the normal consumer configuration retains CNA tests off.

## Limits and remaining observations

- Wine still emits its held-backbuffer warning in the actual game test. The specific native
  reference owner is unconfirmed; this task repairs unsafe loss/recovery and reset-state
  handling, without claiming to eliminate that warning or to prove a particular leak.
- The private Vulkan F11 run changes the display mode to the game's 800 x 480. The larger
  physical-surface scaling defect is independently verified by the resize and pixel probes.
- Web F11 is explicitly excluded by the game's existing InputPad implementation. Page-button
  DOM fullscreen is repaired; no new game keyboard behavior was introduced.
- OpenGLES rendering and the earlier content-alpha discrepancy remain separate deferred work.
