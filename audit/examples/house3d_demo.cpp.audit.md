# Audit: examples/house3d_demo.cpp

## Metadata

- Source file: `examples/house3d_demo.cpp`
- Audit status: AUDITED
- Subsystem: `examples-tests-generic` shard — a full interactive demo (not a CTest pixel-assertion
  file), built for **EasyGL and Vulkan only**
  (`cmake/Examples.cmake:279`: `if(CNA_BUILD_EXAMPLES AND (CNA_GRAPHICS_BACKEND STREQUAL "EASYGL"
  OR CNA_GRAPHICS_BACKEND STREQUAL "VULKAN"))`). Only EasyGL registers an automated smoke-test
  CTest (`EasyGL_House3D_SmokeTest`, `cmake/Tests/EasyGLTests.cmake:26`); no `Vulkan_House3D_*`
  CTest exists despite the demo compiling and linking against Vulkan too (see F3).
- File type: playable `Game`-subclass demo (1277 lines), also runnable headless via
  `--smoke N` (exits after N frames, used by the EasyGL CTest).
- XNA/FNA relevance: exercises `Vector3`/`Matrix`/`VertexPositionColor`/`VertexBuffer`/
  `IndexBuffer`/`BasicEffect`/`GraphicsDevice`/`SpriteBatch`/`Texture2D`/`Keyboard` — all real XNA
  4.0 API surface — plus several NOXNA `GraphicsDevice` convenience toggles.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`
  (`SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`, all confirmed `NOXNA`-tagged in
  `GraphicsDevice.hpp:674-678`), `RasterizerState.cpp` (`RasterizerState::CullNone`), backend
  implementations for the same 3 toggles in EasyGL/Vulkan/Bgfx.

## Purpose

A hand-built, texture-free 3D "walkable house" demo: axis-aligned colored boxes (walls, floors,
roof, fence, trees, stairs) assembled by demo-local `AddBox`/`AddGround`/`AddFence`/`AddTree`/
`AddWindow`/`AddDoor` helpers, a first-person camera with two modes (gravity+AABB-collision "Game"
mode, free-fly "Fly" mode), and a bitmap-font controls overlay drawn via `SpriteBatch`. Serves as
both a manual interactive demo and an automated 3-frame smoke test (`--smoke 3`) proving the whole
XNA 3D pipeline (buffers, indices, `BasicEffect`, `RasterizerState`, `SpriteBatch`) doesn't crash on
a non-trivial real scene. Correctly placed under `examples/` — this is exploratory/demo code, not
library surface.

## Executive Verdict

**Mostly healthy** — the demo runs a genuinely non-trivial 3D scene, its camera/movement math was
independently re-derived and confirmed correct, and its dependency on the current
`RasterizerState`/`GraphicsDevice` NOXNA toggle API was verified against production code. Three
real, previously-undetected issues found: an entire "transparent glass windows" feature (advertised
in a real commit message) that has never actually been wired up and is dead code from the commit
that introduced it (F1); a stale bit-order comment on the embedded bitmap font that contradicts the
font's own actual (correct) rendering logic (F2); and a testing-coverage gap where the demo's
Vulkan build path has no automated smoke test at all, unlike its EasyGL counterpart (F3).

## Checklist Results

### Purpose
Correctly scoped as a demo, not library code; namespace usage (`Microsoft::Xna::Framework[::Graphics]`
for real API, no `CNA`-namespace additions of its own) is appropriate.

### API / XNA / FNA parity
`GraphicsDevice::Indices(ib)` (line 414, "XNA: graphicsDevice.Indices = ...") and
`DrawIndexedPrimitives(type, baseVertex, minVertexIndex, numVertices, startIndex, primitiveCount)`
match FNA's real signature and argument order. `effect_->getCurrentTechniqueProperty()->getPassesProperty()`
+ `pass.Apply()` matches the real XNA 4.0 effect-pass draw idiom. `SetDepthTestEnabled`/
`SetBlendEnabled`/`SetDepthWriteEnabled`/`Texture2D::CreateFromPixels` are all confirmed `NOXNA`-
tagged in their production headers (`GraphicsDevice.hpp:674-678`, `Texture2D.hpp:228`) — correctly
used as CNA extensions, not misrepresented as XNA API.

### Behavioral correctness
Independently re-derived the camera-forward-vector math: `forward = (cos(pitch)*sin(yaw),
sin(pitch), -cos(pitch)*cos(yaw))`; at `yaw=pitch=0` this gives `(0,0,-1)`, matching the file's own
comment "0 = facing -Z (toward the house)". Cross-checked the horizontal movement basis
(`forwardH=(sin(yaw),0,-cos(yaw))`, `rightH=(cos(yaw),0,sin(yaw))`) against
`Vector3::Cross(forward, Vector3::Up)` at `yaw=0`: `cross((0,0,-1),(0,1,0)) = (1,0,0)`, matching
`rightH` at `yaw=0` = `(1,0,0)` exactly — the two independently-computed "right" vectors (one via
explicit horizontal formula, one via the real `Vector3::Cross` API) agree.

`CollidesWithSolid`/`GetBlockingColliderTopY`/`GetFloorEyeY` AABB overlap tests use strict `>`/`<`
(no touching-counts-as-overlap edge case), a standard and defensible choice for a player collider.

### Logic
`AddBox`'s `solid` parameter only controls whether a `Collider` is registered
(`colliders_.push_back(...)`); it does **not** route geometry into a different mesh batch — *every*
box, `solid` or not, is appended to `solid_builder_` and also gets black wireframe edges via
`AddBoxEdges` unconditionally. This is a sound design (decorative geometry still needs to be
visible and edge-outlined) and matches the class's own doc comment ("Decorative pieces … are
non-solid; the foundation slab and inner walls carry solid colliders" — describing collision, not
rendering). No bug here, but see F1 for the one place this shape breaks down.

### Robustness
`FontDrawText`'s glyph index bounds check (`if (idx < 0 || idx >= 95) { cx += 8; continue; }`)
correctly handles both control characters (`idx` goes negative since `unsigned char` promotion then
signed subtraction can yield negative `int`) and bytes above the printable ASCII range.

### Maintainability
`glass_builder_`/`glassMeshes_` and the "Transparent glass" block in `Draw()` are dead code — see
F1. `kFont8x8`'s header comment is stale — see F2.

### Testing
Only EasyGL has an automated CTest smoke test for this file; Vulkan does not, despite the demo
building for Vulkan too — see F3.

### Cross-file consistency
`RasterizerState::CullNone` (used in `Initialize()`, with an explicit "Task 896 finding" comment
about `AddBox()`'s per-face winding being back-facing under the real default `CullMode`) is
confirmed to exist as a real static preset (`RasterizerState.cpp:8`:
`const RasterizerState RasterizerState::CullNone{"RasterizerState.CullNone", CullMode::None};`).

## Detailed Findings

### F1 — `glass_builder_`/`glassMeshes_` are permanently empty; the advertised "semi-transparent glass windows" feature has never been wired up, and the per-frame blend-state toggle in `Draw()` is dead code

- Severity: MEDIUM
- Confidence: HIGH
- Category: dead-code / commit-message-vs-actual-behavior mismatch
- Location/symbol: `MeshBuilder glass_builder_;` / `std::vector<Mesh> glassMeshes_;` (lines 1209,
  1249); `FinalizeBuilders()`'s `upload(glass_builder_, glassMeshes_);` (line 1227); the
  "Transparent glass" block in `Draw()` (lines 439-451); `AddWindow()` (lines 817-870).
- Evidence: `git log -p --follow -- examples/house3d_demo.cpp` shows the introducing commit
  (`c38356c0`, "Controls overlay, glass windows, transparency and API improvements") explicitly
  claims *"house3d_demo: semi-transparent glass windows (alpha blending) with one open window on
  the ground floor; glassMeshes_ rendered last"* — but that same commit's own diff never adds any
  call that appends to `glass_builder_` anywhere. `AddWindow()`'s glass pane is built via
  `AddBox(device, c, glassSize, glass, /*solid=*/false)`, and `AddBox()` unconditionally appends
  **all** geometry (solid or not) to `solid_builder_`, never to `glass_builder_`
  (`grep -n "glass_builder_.append"` across the whole file returns zero matches — the only 2 uses
  of `glass_builder_` are its declaration and its `upload()` call in `FinalizeBuilders()`, which
  hits the `if (b.empty()) return;` early-out every single time). Consequently `glassMeshes_` stays
  an empty `std::vector<Mesh>` for the demo's entire lifetime, and the `Draw()` loop's
  `for (const auto& m : glassMeshes_) { … }` (lines 443-449) iterates zero times every frame —
  while the surrounding `device.SetBlendEnabled(true); device.SetDepthWriteEnabled(false); … 
  device.SetDepthWriteEnabled(true); device.SetBlendEnabled(false);` state toggles (lines 441-451)
  still execute unconditionally every frame regardless.
- Why it matters: the demo's windows (added via `AddWindow`, `Color(70,110,160,255)`, alpha=255)
  render as fully **opaque** colored boxes, not the "semi-transparent glass" the introducing
  commit's message and the `Draw()` loop's own comment ("Transparent glass — rendered last with
  blending on and depth writes off so the world behind glass shows through") both claim. A reader
  modifying this file to actually add transparent glass would reasonably expect `glass_builder_` to
  already be wired up (given the commit message and the dedicated `MeshBuilder`/`Draw()` scaffolding
  exist) and could waste time debugging why nothing appears, when in fact nothing was ever plumbed
  in. Functionally harmless today (visually, opaque "glass" still looks like a window), but it is a
  real, confirmed feature gap between what the codebase's own history/comments assert and what the
  code does, and it wastes two `SetBlendEnabled`/`SetDepthWriteEnabled` backend calls every frame
  for no effect.
- FNA/XNA comparison: N/A — pure demo-local design, no XNA API involved in the gap itself (the
  blend/depth-write toggles used are the NOXNA convenience methods, correctly tagged in production).
- Related files: none outside this demo file.
- Suggested action (not implemented by this audit): either wire `AddWindow()`'s glass pane through
  `glass_builder_` (and give it a `solid=false, glass=true`-style parameter distinct from
  `AddBox()`'s current "collider or not" `solid` flag) to deliver the originally-advertised
  transparent-glass effect, or remove the dead `glass_builder_`/`glassMeshes_`/"Transparent glass"
  scaffolding and its per-frame no-op state toggles if the feature is no longer intended.

### F2 — The embedded 8×8 bitmap font's header comment describes the wrong bit order; it contradicts the font's own (correct) rendering logic

- Severity: LOW
- Confidence: HIGH
- Category: documentation accuracy
- Location/symbol: comment at line 80 (`// kFont8x8[ch - 0x20][row]: bit 7 = leftmost pixel of
  that row.`) vs. `FontDrawText()`'s actual test `if (g8[row] & (1u << col))` with `px = cx + col`
  (lines 191-199).
- Evidence: this audit independently decoded the `/` (forward-slash, 0x2F) glyph
  (`{0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}`) under the code's *actual* convention
  (`1u << col`, i.e. bit0 = leftmost column): row 0 (top) has bits 5-6 set → columns 5-6 (right
  side); row 6 has bit 0 set → column 0 (left side) — top-right down to bottom-left, a correct
  forward slash. Re-decoding the same glyph under the comment's stated convention (bit 7 =
  leftmost, i.e. `col = 7 - bitIndex`) instead produces columns 1-2 at the top and column 7 at
  row 6 — top-left down to bottom-right, i.e. a **backslash**, which would be visibly wrong for a
  glyph literally named `/`. The backslash glyph (0x5C,
  `{0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}`) independently corroborates the same conclusion in
  the opposite direction. `git log -p` shows the introducing commit `c38356c0`'s own message states
  *"Fixed 8x8 font bit-order (bit0 = leftmost pixel)"* — i.e. the **code** was deliberately fixed to
  the bit0-leftmost convention that this audit independently confirmed is correct, but the
  standalone comment at line 80 (predating or surviving that fix) was never updated to match and
  still asserts the opposite, already-superseded convention.
- Why it matters: purely a documentation hazard — the demo currently renders all text correctly
  (confirmed via the `/` and `\` glyph analysis above), so there is no live rendering defect. A
  future contributor adding/editing glyphs by hand, relying on the stated "bit 7 = leftmost"
  comment, would author new glyph rows backwards.
- FNA/XNA comparison: N/A — demo-local embedded font, no XNA API involved.
- Related files: none.
- Suggested action (not implemented by this audit): correct the comment to state "bit 0 = leftmost
  pixel of that row" to match the actual, correct, already-fixed rendering logic.

### F3 — The Vulkan build of this demo has no automated CTest smoke test, unlike its EasyGL counterpart

- Severity: LOW
- Confidence: HIGH
- Category: testing gap
- Location/symbol: `cmake/Tests/EasyGLTests.cmake:26` registers
  `cna_register_backend_test(NAME EasyGL_House3D_SmokeTest COMMAND cna_house3d_demo --smoke 3 …)`;
  `cmake/Tests/VulkanTests.cmake` (grepped in full) contains no `House3D`/`house3d` reference at
  all, despite `cmake/Examples.cmake:279` building `cna_house3d_demo` for
  `CNA_GRAPHICS_BACKEND STREQUAL "VULKAN"` equally.
- Evidence: `grep -n "House3D\|house3d" cmake/Tests/VulkanTests.cmake cmake/Tests/EasyGLTests.cmake`
  returns only the EasyGL registration; the Vulkan build of the executable exists (compiles, links)
  but is never automatically exercised by CTest.
- Why it matters: a Vulkan-specific regression in this demo's 3D pipeline (buffer upload, indexed
  draw, `BasicEffect`, `SpriteBatch` overlay, the NOXNA state toggles) would not be caught by CI's
  Vulkan test run the way the equivalent EasyGL regression would be.
- FNA/XNA comparison: N/A — CI/test-registration gap, not an XNA behavior question.
- Related files: `cmake/Tests/VulkanTests.cmake`.
- Suggested action (not implemented by this audit): add a `Vulkan_House3D_SmokeTest` registration
  mirroring the EasyGL one.

## Cross-File Observations

- The file's own top-of-file doc comment states *"SDL_Renderer and bgfx still throw a clear '3D not
  supported' error"* for this API surface. Verified: `SdlGraphicsBackend.cpp:782` does throw
  `"SDL_Renderer does not support 3D: " + methodName`, so that half is accurate. For Bgfx
  specifically, `BgfxGraphicsBackend::SetDepthTestEnabled`/`SetBlendEnabled`/`SetDepthWriteEnabled`
  (lines 2002-2004) do indeed all call `ThrowNo3DState()` — so the literal claim about *these three
  specific NOXNA convenience methods* remains true today. However, this project's own
  `AUDIT_CROSS_CUTTING_FINDINGS.md` and dozens of `examples/bgfx_basiceffect_*`/
  `bgfx_skinnedeffect_*`/`bgfx_environmentmapeffect_*` test files confirm Bgfx has extensive,
  functioning 3D support via the standard XNA `BlendState`/`DepthStencilState` state-object path —
  so a reader could easily over-generalize this comment (as written) into believing Bgfx lacks 3D
  support altogether, when in fact only this demo's specific NOXNA toggle shortcut is unsupported
  there. Not raised as its own numbered finding (the literal sentence is still technically true and
  the demo is gated out of building for Bgfx entirely via `cmake/Examples.cmake`'s
  `EASYGL OR VULKAN` condition, so no reader would hit this claim while actually trying to build
  the file for Bgfx), but worth flagging as a phrasing risk consistent with this audit's broader
  "documentation rot" pattern seen elsewhere in the codebase.
- The comment about the AABB player collider ("Eye height ~1.7m … the eye is near the top, so
  player AABB extends from (eyeY-1.7..eyeY)") is imprecise: the actual constants
  (`kPlayerEyeToTop=0.10f`, `kPlayerEyeToBottom=1.60f`) place the AABB from `eyeY-1.60` to
  `eyeY+0.10`, a 1.70m total span but not the literal `(eyeY-1.7..eyeY)` range stated. Cosmetic only
  (LOW, not separately numbered) — the actual code and constants are internally consistent with
  each other, only the prose description is off by the 0.10m top margin.
- Confirmed `GraphicsDevice::SetDepthTestEnabled(true)` is called both in `Initialize()` and again
  at the top of every `Draw()` call — redundant (harmless) rather than incorrect, since the game
  never disables it elsewhere except transiently around the controls-overlay `SpriteBatch` draw
  (which correctly restores it with `SetDepthTestEnabled(true)` afterward).

## Missing or Weak Tests

- The only automated coverage is the 3-frame EasyGL smoke test (`--smoke 3`), which proves the demo
  doesn't crash on startup/first frames but does not pixel-verify any rendered content (no
  `GetBackBufferData()` assertion anywhere in this file) — appropriate for a demo, but worth noting
  it provides no regression protection against, e.g., the F1 dead-glass-path or a future winding
  regression in `AddBox()`. This is consistent with this shard's role (interactive demo, not a
  correctness oracle) and is not treated as a defect on its own.
- See F3 for the Vulkan-specific test-registration gap.

## Positive Findings

- The camera math (`forward`/`right`/horizontal movement vectors) was independently re-derived by
  this audit and found internally consistent with `Vector3::Cross`'s real behavior — a genuinely
  correct, non-trivial piece of hand-written trigonometry.
- `RasterizerState::CullNone`'s use in `Initialize()` is explicitly justified with a real,
  verifiable finding from Task 896 (per-face winding of `AddBox()` being back-facing under the
  actual current default `RasterizerState`), and this audit confirmed `RasterizerState::CullNone`
  is a real, correctly-defined static preset.
- `FinalizeBuilders()`'s CPU-side geometry batching (merging ~400 individual box meshes into 3 GPU
  buffers) is a legitimate, documented performance win (the commit history's own before/after GL
  call counts — ~700k calls/5s → ~15k calls/5s — are consistent with the ~46x reduction claimed in
  the code comment).
- `Texture2D::CreateFromPixels` and the NOXNA `GraphicsDevice` state-toggle methods used here are
  all correctly `NOXNA`-tagged in their production headers, so this demo does not misrepresent CNA
  extensions as real XNA API.

## Final Assessment

A functional, well-tested-by-hand interactive demo whose core 3D/camera/collision math holds up
under independent re-derivation. The dead "glass windows" feature (F1) is the most substantive
finding — a real, git-history-confirmed gap between an explicit commit-message claim and the actual
code, though visually harmless — alongside a stale font-bit-order comment (F2) and a Vulkan test-
registration gap (F3), both minor.
