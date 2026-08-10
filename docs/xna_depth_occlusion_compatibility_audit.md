# XNA 4.0 Depth Occlusion Compatibility Audit

**Status: RESOLVED.** A freshly-constructed `GraphicsDevice`'s own default `BlendState`/
`DepthStencilState` were never pushed to the renderer's real GPU state -- only `RasterizerState`
was (Task 896). On EasyGL this left OpenGL's raw depth-test state (disabled) in effect for any
game that never explicitly calls `GraphicsDevice.DepthStencilState = ...` itself -- exactly what
`../cna-samples`'s `SimpleAnimation` sample does (mirroring real XNA's own `Tank.cs`). Fixed by
syncing all 3 state objects at `GraphicsDevice` construction, matching FNA's own
`GraphicsDevice.cs` constructor line-for-line. No asset/data change and no sample-side change
were needed or made.

Date: 2026-07-11. Follow-up investigation to `docs/xna_culling_compatibility_audit.md` (the
turret-winding investigation on the same sample) -- this is a **separate, unrelated bug**, found
after that investigation's own asset fix was applied and verified. Do not conflate the two.

---

## 1. Reported symptom

After `docs/xna_culling_compatibility_audit.md`'s asset-level winding fix was applied and
verified (all 12 `tank_*_idx.bin` mesh parts), the project owner reported the tank was
"closer to the original" but still visibly wrong: **some tank parts render fully visible when
they should be occluded by other parts in front of them from the camera's point of view** --
placement and winding were no longer the issue; visible-surface (depth) occlusion was.

---

## 2. Static-pose verification

To remove animation as a confounder, `SimpleAnimationGame.hpp`/`Tank.hpp` were temporarily
patched (reverted before commit, confirmed via `git diff` showing zero net change) to force
`WheelRotation`/`SteerRotation`/`TurretRotation`/`CannonRotation`/`HatchRotation` = 0 and a fixed
identity world rotation, with the camera already fixed (SimpleAnimation's own hardcoded
`Matrix.CreateLookAt`).

**Before fix** (`docs/xna_depth_occlusion_compatibility_audit_images/before_static_pose.png`):
the turret dome shows dark rectangular "window" gaps letting the grey background show through
where it should be solid (the turret is a closed cylinder in the source mesh -- confirmed via
the same edge-adjacency check used in the culling audit), and the engine/exhaust tube connecting
the two sides of the hull incorrectly overlaps into the turret's own silhouette.

**After fix** (`docs/xna_depth_occlusion_compatibility_audit_images/after_static_pose.png`):
turret dome is solid and closed, matching the authoritative XNA reference; the connecting tube no
longer bleeds through the turret. 61,236 of 748,800 pixels (~8.2%) differ between the two
screenshots -- concentrated exactly in the turret windows, the engine tube/turret overlap region,
and parts of the wheel hub structure (see
`docs/xna_depth_occlusion_compatibility_audit_images/before_after_diff.png`).

This was **constant**, not animation-angle-dependent -- the root cause (below) is a
`GraphicsDevice`-construction-time default, unrelated to any per-frame animation state, so it
reproduces identically in the static pose and in normal animated play (confirmed separately by
also re-testing with animation restored -- same qualitative improvement, turret closed, hull
clearly visible as a distinct solid mass between the wheels).

---

## 3. Depth/blend/rasterizer state audit

`Tank.hpp::Draw()` (mirroring real XNA's own `Tank.cs`) never calls
`GraphicsDevice.setBlendStateProperty()`/`setDepthStencilStateProperty()`/
`setRasterizerStateProperty()` anywhere -- it only ever assigns `BasicEffect.World`/`View`/
`Projection` and calls `EnableDefaultLighting()`, then `mesh->Draw()`. This is not a sample bug:
real XNA's `Tank.cs` does exactly the same thing, correctly relying on `GraphicsDevice`'s own
already-correct constructed defaults (`BlendState.Opaque`, `DepthStencilState.Default`,
`RasterizerState.CullCounterClockwise`) never needing to be re-stated by ordinary game code.

Reading `GraphicsDevice`'s own constructor (`src/Microsoft/Xna/Framework/Graphics/
GraphicsDevice.cpp`) found the actual gap:

```cpp
// Before this fix:
setRasterizerStateProperty(rasterizerState_);   // Task 896 -- RasterizerState only
```

`blendState_`/`depthStencilState_` were initialized as C++ fields (`BlendState::Opaque`,
`DepthStencilState::Default`) but **never applied to the renderer** -- `GraphicsDevice::
setBlendStateProperty()`/`setDepthStencilStateProperty()` are the only call sites for
`IGraphicsRenderer::ApplyBlendState()`/`ApplyDepthStencilState()`, and neither is called from the
constructor. Task 896 (2026-07-10ish) found and fixed this exact class of bug for
`RasterizerState` alone; `BlendState`/`DepthStencilState` were left with the same gap.

**Per-renderer own hardcoded defaults, audited directly against XNA's real defaults**
(`BlendState.Opaque`: no blending; `DepthStencilState.Default`: depth test ON, depth write ON,
`LessEqual`):

| Renderer | Depth test default (own member/GL state) | Blend default (own member/GL state) | Matches XNA before this fix? |
|---|---|---|---|
| EasyGL | Plain OpenGL raw default: **disabled** (`easygl::Device::initialize()` never calls `set_depth_test_enabled(true)`) | Plain OpenGL raw default: **disabled** (behaviourally equivalent to Opaque, since Opaque = One/Zero = no-op blend) | **Depth: NO. Blend: coincidentally yes** (raw-disabled happens to look like Opaque). |
| Vulkan | `depthTestEnabled_ = true`, `depthWriteEnabled_ = true` (own C++ member defaults) | `blendEnabled_ = false` (own C++ member default) | **Yes, by coincidence of its own member initializers** -- unaffected by this bug either way. |
| Bgfx | `depthFlags_ = BGFX_STATE_DEPTH_TEST_LESS \| BGFX_STATE_WRITE_Z` (own member default) | `blendFlags_ = BGFX_STATE_BLEND_ALPHA` (own member default) | **Depth: yes (coincidence). Blend: NO** -- alpha blending was silently ON by default. |

Real FNA's own `GraphicsDevice.cs` constructor (authoritative reference, confirmed by direct
source read):

```csharp
BlendState = BlendState.Opaque;
DepthStencilState = DepthStencilState.Default;
RasterizerState = RasterizerState.CullCounterClockwise;
```

All three assigned unconditionally, every time. CNA's constructor had ported only the third line
(Task 896). **This audit's fix ports the other two, matching FNA exactly.**

Net effect of the bug, confirmed empirically (§5): **EasyGL** had depth testing genuinely
disabled by default -- any two overlapping opaque `Model`/`ModelMesh` draws (like SimpleAnimation's
12 tank mesh parts, drawn in whatever order `Model.Meshes` iterates them, not sorted by depth)
simply painted in draw order regardless of actual 3D depth, which is exactly the reported
symptom. **Bgfx** had a real, independent, second bug from the same root cause: alpha blending
silently enabled by default -- did not affect SimpleAnimation specifically (its opaque texture
data blends indistinguishably from opaque when alpha=255 throughout), but is a real correctness
gap fixed as a direct side effect of this same constructor fix. **Vulkan** was never actually
affected (its own hardcoded member defaults already happened to match XNA).

---

## 4. Opaque vs. transparent path

Checked whether any tank mesh/part enters an unintended blended path: `Tank.hpp` never sets
`BasicEffect.Alpha`/`DiffuseColor`'s alpha channel away from 1.0, `VertexColorEnabled` is never
enabled for this sample (no per-vertex color data in `tank.model.json`), and there is no
automatic "texture has alpha => switch to a blended pass" logic anywhere in `Model`/`ModelMesh`/
`BasicEffect` (confirmed via source read -- `ModelMeshPart`/`BasicEffect::Apply()` have no such
branch; XNA/FNA don't either). **No tank mesh or part was found entering a transparent/
alpha-blended path.** The bug was purely a depth-test/write default gap, not a
blend-classification gap -- confirmed directly: isolating the diagnostic fix to *only*
`setDepthStencilStateProperty(DepthStencilState::Default)` (no `BlendState`/`RasterizerState`
touched at all) reproduced a **pixel-identical** result to fixing all 3 (`compare -metric AE`:
`0` differing pixels) -- see §5.

---

## 5. Diagnostic isolation (proving root cause before writing the fix)

1. Captured a static-pose screenshot of the pre-fix build (`before_static_pose.png`).
2. Temporarily added `device.setDepthStencilStateProperty(DepthStencilState::Default);
   device.setBlendStateProperty(BlendState::Opaque);
   device.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);` directly in
   `SimpleAnimationGame::Draw()` before `tank_.Draw(...)` (diagnostic only, reverted before
   commit) -- rebuilt, re-screenshotted: turret closed, engine tube no longer overlaps it. Real,
   substantial change (61,236 pixels, concentrated in exactly the previously-wrong regions).
3. Removed the `BlendState`/`RasterizerState` lines, kept only `DepthStencilState::Default` --
   rebuilt, re-screenshotted: **pixel-identical to step 2** (`compare -metric AE`: `0`). This
   isolates the entire visible fix to `DepthStencilState` alone.
4. Implemented the real fix at `GraphicsDevice`'s own constructor (§6) -- removed the diagnostic
   override from `SimpleAnimationGame.hpp` entirely (confirmed via `git diff` showing zero net
   change to that file), rebuilt with **zero `cna-samples` changes**, re-screenshotted:
   **pixel-identical to steps 2 and 3** (`compare -metric AE`: `0`). Proves the constructor-level
   fix, with no sample-side code at all, reproduces the exact same corrected rendering that
   explicitly setting state in the sample did -- confirming the fix belongs in `GraphicsDevice`,
   not in `cna-samples`.

---

## 6. The fix

**File:** `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp` (`GraphicsDevice`'s
3-argument constructor).

```cpp
setBlendStateProperty(blendState_);
setDepthStencilStateProperty(depthStencilState_);
setRasterizerStateProperty(rasterizerState_);   // Task 896, already present
```

Mirrors FNA's own `GraphicsDevice.cs` constructor line-for-line (§3). No renderer-specific patch,
no `cna-samples` change, no `Tank.hpp`/`SimpleAnimationGame.hpp` change of any kind -- the fix is
entirely at the true ownership boundary (`GraphicsDevice` construction), matching how real XNA
itself guarantees every renderer starts in the documented default state regardless of whether game
code ever explicitly sets it.

---

## 7. Regression testing

New shared (3-renderer) regression test: `examples/graphicsdevice_default_state_occlusion_test.cpp`,
registered as `EasyGL_GraphicsDevice_DefaultStateOcclusion` / `Vulkan_GraphicsDevice_
DefaultStateOcclusion` / `Bgfx_GraphicsDevice_DefaultStateOcclusion`.

**Deliberately unlike every other depth/blend test in this project** (which all explicitly call
`setDepthStencilStateProperty()`/`setBlendStateProperty()`/`setRasterizerStateProperty()` before
drawing, per the established test-authoring convention) -- this is exactly why the bug was never
caught by the existing suite. This test never touches any of the 3 state setters at all,
exercising only `GraphicsDevice`'s own untouched, just-constructed defaults, matching
`Tank.hpp`'s own draw loop exactly.

**Method:** two fully-overlapping opaque quads at different Z (green, near, z=0.2; red, far,
z=0.8). Check A (discriminating): green drawn FIRST, red drawn SECOND -- draw order deliberately
defies depth order, so only a genuinely working default depth test produces the correct result
(GREEN). A depth-test-disabled bug would show RED (last-drawn wins). Check B (sanity/positive
control): red drawn first, green second -- draw order matches depth order, passes regardless of
depth-test correctness, proving the quad/colour/pixel-readback pipeline itself is sound.

**Discriminating power verified via `git stash`** of the 1-line-added-to-3-lines production fix:
reverted state reproduced the exact predicted failure (Check A: FAIL, centre=(255,0,0)
instead of (0,255,0); Check B: PASS, as expected) on EasyGL. Restored and reconfirmed 2/2 PASS.

**Full regression, 0 new failures on any renderer:**
- EasyGL: `ctest -R "^EasyGL_"` 188/190 (2 already-documented pre-existing: `EasyGL_MRT_
  TwoAttachments` Task 145, `EasyGL_GraphicsDevice_ReferenceStencil` Task 872) + `CnaTests`
  4371/4373 (2 hardware skips, exact baseline).
- Bgfx: `ctest -R "^Bgfx_"` 103/105 (2 already-documented pre-existing: `Bgfx_RenderTarget2D_
  MsaaResolve` Task 878/879, `Bgfx_RenderTargetCube_DepthFormat` Task 952 deferred).
- Vulkan: `ctest -R "Vulkan_"` 126/127 (1 already-documented pre-existing: `Vulkan_DepthBias`).

---

## 8. Related, NOT fixed here -- a second, separate state-leak gap found while investigating

While confirming `DrawHelpOverlay()`'s `SpriteBatch` couldn't be the cause of the reported bug
(it early-returns and was never invoked in either the static-pose or animated repro), a real,
separate bug was found: `EasyGLSpriteBatchRenderer::Begin()` unconditionally enabled blending
(`set_blend_enabled(true)` + `SrcAlpha`/`OneMinusSrcAlpha`), but `End()`/`FlushBatch()` never
restored the prior blend state. Any 3D draw issued after a `SpriteBatch.Begin()`/`End()` pair in
the same or a later frame -- without that game explicitly resetting `BlendState` itself -- inherited
`SpriteBatch`'s own hardcoded blend state instead of whatever `GraphicsDevice.BlendState` actually
claimed was active. Confirmed via direct source read (`EasyGLRenderer.cpp`), not observed
in SimpleAnimation itself (its `DrawHelpOverlay()` never actually runs in the automated repro), but
a real, XNA-incompatible gap in the same "state doesn't reset the way a game implicitly expects"
family as this document's own fix. **Deliberately not fixed here** -- out of this task's own scope
(a `SpriteBatch`-to-3D state transition issue, not a `GraphicsDevice`-construction-default issue)
and not implicated in the reported bug. Tracked as a new follow-up, **Task 956 — fixed 2026-07-11**
by removing the hardcoded blend call entirely (`SpriteBatch::Begin()` already applies the real
requested `BlendState` correctly before the renderer's own `Begin()` runs); see `NEXT.md` §3 for
the full write-up and the new `EasyGL_SpriteBatch_BlendStateLeak` regression test.

---

## 9. Files changed

### `cna_graphics`

- `src/Microsoft/Xna/Framework/Graphics/GraphicsDevice.cpp`: the 2-line constructor fix (§6).
- **New**: `examples/graphicsdevice_default_state_occlusion_test.cpp` (§7), registered on all 3
  renderers.
- `CMakeLists.txt`: 3 new test registrations.
- **New**: this document + `docs/xna_depth_occlusion_compatibility_audit_images/`.

### `cna-samples`

- **No changes.** `Tank.hpp`/`SimpleAnimationGame.hpp` are byte-identical to their state after the
  culling investigation's own fix (confirmed via `git diff` at every diagnostic step, §5) -- the
  fix is entirely in `cna_graphics`, at the true ownership boundary.

---

## 10. Summary for future readers

- `GraphicsDevice`'s constructor must sync **all 3** of `BlendState`/`DepthStencilState`/
  `RasterizerState` to the renderer, not just `RasterizerState` (Task 896's own original fix was
  incomplete in scope, not wrong). If a 4th state object is ever added to `GraphicsDevice`
  (unlikely -- XNA only has these 3), check whether FNA's own constructor sets it too.
- Any game/sample that never explicitly sets one of these 3 properties (correct, idiomatic XNA
  code, matching real `Tank.cs`) is exercising the CONSTRUCTOR's defaults, not a per-draw-call
  default -- this project's existing test suite almost universally sets state explicitly before
  drawing (a reasonable test-isolation habit) and so structurally cannot catch this class of bug.
  `graphicsdevice_default_state_occlusion_test.cpp` (§7) is deliberately the one exception -- keep
  it that way if it's ever "cleaned up" to match the other tests' style.
- Bgfx's own default blend state was ALSO wrong (`BGFX_STATE_BLEND_ALPHA` instead of no-blend) --
  fixed as a side effect of the same constructor change, not a coincidence: same root cause,
  different renderer, different one of the 3 states.
- A second, unrelated, still-open state-leak bug (`SpriteBatch`'s blend state not restored after
  `End()`, EasyGL) was found and documented (§8) but deliberately not fixed here -- Task 956.
- This is unrelated to `docs/xna_culling_compatibility_audit.md`'s own winding/CullMode
  investigation on the same sample -- that one was a `cna-samples` asset data bug; this one is a
  `cna_graphics` framework bug. Both were real, both are now fixed, independently.
