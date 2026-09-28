# DirectX9 fullscreen motion and presentation — FULLSCREEN-002

Status: **complete**, 2026-09-28.

Starting revision: `6f6100cd35df`. Follow-up to FULLSCREEN-001 after the owner reported
corrupt moving sprites and a small background with black remainder. No game or SharpRuntime
source changes are needed. No new dependencies, stubs or public XNA signatures are introduced.

## Confirmed causes before repair

1. A successful `DirectX9Renderer::EnsureDeviceSize` Reset silently discarded native render
   states. GraphicsDevice's object-identity caches retained AlphaBlend/DepthStencil.None and
   suppressed their repeated assignments. Native probing measured blending disabled and depth
   enabled after a successful reset. The earlier recovery fix covered loss/recovery resets but
   not this ordinary resize path.
2. DirectX9 ignored virtual resolution when reporting viewport dimensions and built SpriteBatch
   projection from the native physical viewport. The game's fixed logical background pieces and
   viewport-dependent sprite geometry consequently disagreed after fullscreen/resizing. The
   shared presentation probe failed seven assertions before repair.

## Repair

- Notify GraphicsDevice of native state invalidation without synthesizing an extra public
  DeviceReset event. Reapply retained blend, depth and rasterizer state objects after reset.
- Keep logical size and physical presentation rectangle separate, consistently with CNA's
  Letterbox/Stretch/FixedHeightDynamicWidth/NativeBackBuffer modes; map window input through
  the same rectangle and display density.
- Supply SpriteBatch with logical projection dimensions. Retain custom viewport and render
  target isolation. Clip physical viewports to legal D3D9 target bounds; adjust sprite projection
  to preserve cropped Overscan coordinates. Half-pixel correction stays half a physical pixel.
- Add DirectX9 registrations for the shared presentation probe and a successful-reset/motion
  probe. Draw a background and moving foreground over multiple Present calls without Clear;
  check old sprite positions are repainted, including a second resize.

## Verification

Evidence lives in the Mobile Eggbert consumer workspace's
`build/logs/directx9-fullscreen-followup/`. Graphical tests use the mandatory private
Weston/Xwayland runner and an isolated Wine prefix. The DirectX9 game is rebuilt;
five probe executables exit 0 with 110 passing assertions:

| Probe | Result |
|---|---|
| New successful-reset/state/input/multi-frame motion/Overscan probe | 25 assertions pass |
| Shared presentation/custom viewport/transform/render-target probe | 11 assertions pass |
| Existing injected-loss/held-backbuffer recovery probe | 2 assertions pass |
| Existing smoke fixture in temporary HiDef setup | 62 assertions pass |
| Existing SpriteBatch fixture in temporary HiDef setup | 10 assertions pass |

The older smoke/SpriteBatch fixtures select Reach while using GetBackBufferData, which is
correctly rejected by the current shared XNA profile boundary. The original fixtures remain
untouched; temporary copies selecting HiDef verify their behavior, including half-pixel
boundaries, sorting, blending, native resources and recovery. The new presentation/state
targets are registered in CNA's DirectX9 examples CMake file. They were compiled with the
consumer's exact toolchain and current libraries; full CNA-root CTest and other consumer
renderer builds are outside this follow-up.

The owner's provided screenshot is
`/home/robertvokac/Pictures/Screenshots/Screenshot From 2026-09-28 18-50-54.png`.
Baseline native-state failures are in `resize-state-before.log`; baseline presentation
failures are in `presentation-before.log`. The native-state probe measured blend=0/depth=1
before repair and blend=1/depth=0 afterward. The ordinary external-WM resize capture did not
reliably update SDL's native surface under private Wine, so large-game testing uses a
temporary launcher calling GameWindow.EndScreenDeviceChange during normal play. The launcher
links unchanged game objects and is not the delivered game executable.

The private Wine fullscreen surface is 800 x 500, so large-surface scaling is separately
checked with real 1600 x 900 resizing. The Wine held-backbuffer warning remains an observation;
the reference owner has not been established by this task.

Actual normal gameplay was checked while moving in both directions and after F11. The
unchanged game drawing code sees logical 800 x 480; the renderer records physical
`(50,0,1500,900)` at 1600 x 900, and `(0,10,800,480)` in the private 800 x 500 fullscreen
mode. Sprites/background have consistent scale and movement is visually clean. Wine moves
the large window back to `(560,300)`, cropping part of the desktop screenshot; full-target
coverage is independently verified by the pixel probes. The final consumer runner is
`game-large-motion-final-runner.log` and its native projection record is in
`game-controlled-runtime.log`.

Source reference: local FNA `src/Graphics/SpriteBatch.cs` PrepRenderState builds projection
from the public Viewport, independently of native presentation placement. All new hooks and
state notifications remain in CNA internals. `git diff --check` passes. Commit the completed
task without pushing, as required by AGENTS.md.
