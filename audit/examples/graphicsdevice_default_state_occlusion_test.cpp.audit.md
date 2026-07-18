# Audit: examples/graphicsdevice_default_state_occlusion_test.cpp

## Metadata

- Source file: `examples/graphicsdevice_default_state_occlusion_test.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — genuinely backend-agnostic (no `easygl_`/`vulkan_`/
  `bgfx_` prefix, no backend-specific include). Registered per-backend only through whichever
  `cmake/Tests/<Backend>Tests.cmake` happens to compile it against `${BACKEND_TARGET}`; the
  `.cpp` itself has zero backend-specific code.
- File type: standalone `Game`-subclass executable, CTest-registered per backend (confirmed
  EasyGL: `cna_register_backend_test(NAME EasyGL_...)` pattern used project-wide for this shape).
- XNA/FNA relevance: direct — `GraphicsDevice`'s constructor-time default
  `BlendState`/`DepthStencilState`/`RasterizerState` application (`GraphicsDevice.cs`).
- FNA reference: `GraphicsDevice.cs` constructor (`BlendState = BlendState.Opaque;
  DepthStencilState = DepthStencilState.Default; RasterizerState =
  RasterizerState.CullCounterClockwise;`).
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (constructor,
  lines 142-208), `src/CNA/Internal/Backends/EasyGL/EasyGLGraphicsBackend.cpp`
  (`ApplyDepthStencilState()`, lines 1924+).

## Purpose

Regression test for Task 955: proves that a freshly-constructed `GraphicsDevice`, with **no** game
code ever calling `setDepthStencilStateProperty()`/`setBlendStateProperty()`/
`setRasterizerStateProperty()` itself (mirroring the un-instrumented `Tank.hpp`-style draw loop of
the `cna-samples` `SimpleAnimation` sample), produces a real, working default depth test. Two flat
quads (near green z=0.2, far red z=0.8) are drawn in reversed depth order (far *after* near); if the
device's own default `DepthStencilState.Default` (depth test+write ON, `LessEqual`) is genuinely
reaching the backend, the far quad's later fragment is rejected and the sampled centre pixel stays
green. Check B is a same-order positive control that passes regardless of depth-test correctness,
isolating the draw/clear/readback pipeline itself as a suspect.

## Executive Verdict

**Healthy** — this is a well-constructed regression test whose premise this audit independently
verified against the current `GraphicsDevice` constructor and EasyGL's `ApplyDepthStencilState()`:
the Task 955 fix (`setBlendStateProperty`/`setDepthStencilStateProperty`/
`setRasterizerStateProperty` all called unconditionally in the ctor, `GraphicsDevice.cpp` lines
205-207) is real, present, and matches the file's own claim almost word-for-word, and the quad
winding under the untouched default `RasterizerState` was independently re-derived and confirmed
correct (see Behavioral correctness). No functional defects found; only a minor style note (F1).

## Checklist Results

### Purpose
Correctly placed under `examples/` (backend-agnostic regression executable, not library source).

### API / XNA / FNA parity
`GraphicsDevice::Clear(Color)` single-arg overload (defaults to `Target|DepthBuffer`, matches FNA's
own `Clear(Color)` overload semantics), `DrawUserPrimitives`, `BasicEffect.VertexColorEnabled`,
`GetBackBufferData` all used per their real XNA signatures. No NOXNA extensions used beyond
standard test-harness calls (`Exit()`).

### Behavioral correctness
Independently re-derived the NDC signed area of `DrawQuad`'s hardcoded 6-vertex quad under identity
World/View/Projection: triangle `{(-1,1),(1,-1),(-1,-1)}` has shoelace area = -2 (negative) —
matching this project's own already-established rule (see the sibling
`rasterizerstate_cullmode_camera_test.cpp` audit) that a negative NDC signed area survives the
default `CullCounterClockwiseFace`. This confirms the file's own comment ("Winding is front-facing
under CNA's real default RasterizerState … deliberately so this test never needs to touch
RasterizerState") is correct, not just asserted.

Confirmed via direct source read that `GraphicsDevice`'s constructor
(`GraphicsDevice.cpp:205-207`) really does call all three of
`setBlendStateProperty(blendState_)` / `setDepthStencilStateProperty(depthStencilState_)` /
`setRasterizerStateProperty(rasterizerState_)` unconditionally, with an inline comment crediting
"Task 896/955" — this matches the test file's own header narrative essentially verbatim (down to
citing the same task numbers), and independently confirms it is not stale historical framing.

Further confirmed `EasyGLGraphicsBackend::ApplyDepthStencilState()` (lines 1924-1938) really calls
`device.set_depth_test_enabled(depthEnable)` and `device.set_depth_mask(depthWriteEnable)` from the
passed-through state — i.e. the depth state genuinely reaches OpenGL, not just a C++-level field —
so Check A is testing a real, currently-wired code path end-to-end, not a tautology.

### Logic
Two-step frame state machine (`step_` 0→1) matches the project's own established convention
(referenced explicitly in the file's own comment) for backends whose `GetBackBufferData()` only
reliably reflects the first read of a rendered frame (Task 406) — each check gets its own
Clear+2-draws+1-read cycle in its own `Draw()` invocation.

### C++ correctness
`ApplyBasicEffect()`'s function-local `static BasicEffect* fx = nullptr;` lazily `new`s a
`BasicEffect` once and never deletes it — see F1.

### Memory/resource lifetime
The leaked `BasicEffect` (F1) is harmless in practice: the process exits shortly after `Exit()` is
called from `step_==1`, and the pointer is never dereferenced after the object would otherwise need
tearing down relative to `GraphicsDevice`'s own destruction order.

### Testing
`IsGreen()`'s threshsolds (`G>=200 && R<=60 && B<=60`) were checked against all three test colors
(`kBackground(20,20,20)`, `kGreen(0,255,0)`, `kRed(255,0,0)`) and correctly discriminate all three
with margin — no risk of `kBackground` or `kRed` being misclassified as green.

### Cross-file consistency
Consistent with the sibling `rasterizerstate_cullmode_camera_test.cpp`/
`rasterizerstate_cullmode_indexed_basiceffect_test.cpp` files' shared NDC-signed-area convention
and with `occlusion_query_test.cpp`'s shared multi-frame-state-machine idiom for Bgfx's Task-406
readback quirk.

## Detailed Findings

### F1 — `ApplyBasicEffect()`'s lazily-constructed `BasicEffect` is never freed

- Severity: LOW
- Confidence: HIGH
- Category: maintainability / resource-lifetime style
- Location/symbol: `ApplyBasicEffect()`, `static BasicEffect* fx = nullptr; if (fx == nullptr) fx =
  new BasicEffect(dev);` (lines 84-85)
- Evidence: the pointer is a raw `BasicEffect*` function-local `static`; nothing in the file ever
  calls `delete fx`, and no smart pointer manages it.
- Why it matters: purely a maintainability nit for this specific file — the process exits (via
  `Exit()`/`return game.getResult();`) shortly after the object's last use, so this is not a
  reachable use-after-free or a leak that accumulates across iterations (the `if (fx == nullptr)`
  guard means exactly one instance is ever created). Flagged for consistency with the project's
  general RAII discipline elsewhere, not because it causes an observable defect here.
- Suggested action (not implemented by this audit): use a function-local
  `static std::unique_ptr<BasicEffect>` (or hoist `fx` into the `Game` subclass as a member) if this
  pattern is copied into a longer-lived test.

## Cross-File Observations

- Shares the "each check is its own real `Draw()`/frame" state-machine shape with
  `occlusion_query_test.cpp`'s own multi-frame convention and with the sibling rasterizer-cull-mode
  tests, all explicitly citing the same Task 406 (Bgfx `GetBackBufferData()`-reflects-only-first-
  read-of-frame) rationale.
- The header comment's claim that this test "guards against" Task 896 having ported only the
  `RasterizerState` line of `GraphicsDevice`'s 3-state constructor sync was independently verified
  against the actual current constructor body, which now carries all 3 lines with an explicit
  "Task 896/955" comment — the historical narrative in this test file is accurate, not stale.

## Missing or Weak Tests

- No test in this file (or, as far as this audit's scope covers, in this shard) exercises the
  *reverse* discriminating direction — i.e. a scenario where the far quad is drawn first and the
  near quad second under a **broken** depth state would still (by chance) look correct; Check B
  already covers this by design (it is the intentional "passes regardless" control), so this is not
  a gap, just worth noting that Check A alone (not Check B) is what actually exercises Task 955's
  fix.

## Positive Findings

- The two-check design (discriminating check + same-order sanity control) is a genuinely good test
  structure that isolates "is the pipeline itself sound" from "is the depth-test default correct" —
  a naive single-check test could not distinguish those two failure modes.
- This audit's own independent NDC-signed-area re-derivation and direct read of both
  `GraphicsDevice`'s constructor and `EasyGLGraphicsBackend::ApplyDepthStencilState()` confirm the
  test's premises are currently true, not just asserted by a stale comment.

## Final Assessment

A solid, currently-accurate regression test with real evidence backing every claim in its own
header comment; the only note is a cosmetic, inconsequential resource-lifetime style nit (F1) that
does not affect correctness.
