# Blocker handoff — cross-binding C-ABI milestone

`fixcnacs.md` Phase 10 asks for a per-blocker report against
`cna-cs/docs/native-behavior-blockers.md`. That file lives in a read-only repository and is not
edited from here; this is the upstream half of the handoff, in the shape Phase 10 specifies.

Measured on `feature/bindings`. Downstream rows not touched by this milestone are deliberately
absent rather than restated.

---

## `SoundEffectInstance.Apply3D(AudioListener[], AudioEmitter)`

- **BEFORE** — downstream reported the implementation rejecting every listener count but one.
- **ROOT CAUSE** — not a defect. `SoundEffectInstance.cpp:1082-1094` is a line-for-line match of
  FNA `SoundEffectInstance.cs:266-278`, which throws `NotSupportedException` for any count but one.
  The C wrapper deliberately routes even an empty array through the canonical overload so the
  refusal is FNA's, not one invented at the C boundary.
- **CNA CHANGE** — none.
- **ABI CLASSIFICATION** — none.
- **CNA TEST** — unchanged.
- **CNA.NET TEST** — unchanged; the managed fallback stays.
- **STATUS** — `STILL_BLOCKED`. FNA throws, MonoGame loops over the listeners, and the official XNA
  sample archive never uses the array overload in 17.5 MB of C#. Needs XNA IL or a captured Windows
  runtime to settle. Everything needed to implement either answer is in place and the ABI shape is
  sufficient for both.

## `StorageContainer` disposing callback

- **BEFORE** — downstream measured zero callbacks.
- **ROOT CAUSE** — not reproducible at this HEAD. `StorageContainer.cpp:71` raises `Disposing`, and
  `StorageSmoke.c` already asserted exactly-once delivery.
- **CNA CHANGE** — coverage only, for the cases the order enumerates and nothing reached: multiple
  subscribers, a subscriber removed before disposal, the destroy-if-needed path, ordering against
  native invalidation observed from inside the callback, and repeated create/destroy.
- **ABI CLASSIFICATION** — none.
- **CNA TEST** — `CApi_StorageSmoke`, extended.
- **STATUS** — `RESOLVED`. One contract worth downstream's attention: the registration is a
  separately owned handle and **survives its container**; the caller still releases it, once.

## SpriteBatch invalid sort modes and non-finite draw values

Two halves with different answers.

**Unknown sort mode**

- **BEFORE** — CNA refused values XNA stores.
- **ROOT CAUSE** — `TryMapSpriteSortMode`'s `default: return false` at the C boundary. CNA's own
  `SpriteBatch` was already built like FNA's: `==`/`!=` chains, no switch with a throwing default.
- **CNA CHANGE** — `SpriteSortMode` gets a fixed underlying type; unnamed values pass through and
  run as Deferred.
- **ABI CLASSIFICATION** — **D**, semantic change without shape change.
- **STATUS** — `RESOLVED`. **Downstream must re-review**: a value this ABI refused now succeeds.

**Non-finite sprite values**

- **BEFORE** — CNA refused values XNA carries into the vertex path.
- **ROOT CAUSE** — CNA **deliberately** refuses them. `SpriteBatch` validates finiteness in 16
  places, throws `ArgumentOutOfRangeException`, documents it with `@throws`, and has tests
  asserting it. A standing, documented departure from XNA predating this milestone.
- **CNA CHANGE** — the hole in that decision: `DrawString` never validated `layerDepth`, which is
  what `flushBatch`'s depth comparators order by, and a NaN there breaks the strict weak ordering
  `std::stable_sort` requires — undefined behaviour, not a wrong sort. Now validated and
  documented.
- **ABI CLASSIFICATION** — none at the C boundary; the C guard already refused it.
- **STATUS** — `PARTIALLY_RESOLVED`. The crash path is closed. The divergence from XNA remains and
  is CNA's deliberate position, not an oversight.

## `DynamicVertexBuffer` / `DynamicIndexBuffer` / render-target `ContentLost`

- **BEFORE** — headers stated CNA never raises it; downstream could observe no notification.
- **ROOT CAUSE** — no bridge from a real device-loss event to the per-resource event. Note CNA was
  matching FNA, which hardcodes `IsContentLost` false and says so: *"We never lose data, but lol
  XNA4 compliance"*.
- **CNA CHANGE** — the four affected types carry real state and implement
  `CNA::Internal::Graphics::IContentLosable`; `GraphicsDevice` notifies them from the
  renderer-reported reset transition, and a write clears the flag.
- **ABI CLASSIFICATION** — **D**. An event that never fired now can.
- **CNA TEST** — `ContentLostProbe`.
- **STATUS** — `RESOLVED where loss is real`. Only `directx9`, `direct2d` and `skia` report a device
  reset; the other 44 renderer families never lose content and so never raise it. This is
  deliberate — firing on a caller-initiated `Reset` would make the event noise. **Still missing**:
  `cna_render_target_subscribe_content_lost` (additive, class C); buffers already have theirs.

## `VideoPlayer.GetTexture()` identity and lifetime

- **BEFORE** — a borrowed transient texture with no stable identity or generation.
- **ROOT CAUSE** — `get_texture` creates a fresh handle per call, so "same frame again" and "the
  frame advanced" are indistinguishable.
- **CNA CHANGE** — `cna_video_player_get_frame_ext` returns the borrowed texture on the same
  lifetime terms plus `generation` and `presentation_time`. `VideoPlayer` counts decoded frames and
  resets on `Play`/`Stop`.
- **ABI CLASSIFICATION** — **C**, additive. `cna_video_player_get_texture` is untouched.
- **STATUS** — `RESOLVED, with one XNA behaviour deliberately not emulated`. There is no `slot`
  field: CNA decodes into **one** texture in place, XNA alternates between two. A slot token would
  report an alternation that does not happen. A binding modelling XNA's two slots must map both
  onto this frame and use `generation` for change detection.

## Cross-device graphics resource/state validation

- **BEFORE** — the corpus emitted `not-run(CNA-ABI-has-one-game-owned-device)`; no supported route
  to a second independent device.
- **ROOT CAUSE** — the C ABI only ever exposed the Game's device, borrowed for a callback. The C++
  constructor was XNA-shaped and worked; nothing bound it.
- **CNA CHANGE** — `cna_graphics_device_create`/`_destroy`. Resources carry an **owner token** —
  the game handle for a Game's device, the device's own handle for a caller-created one — so
  cross-device refusal works for both kinds.
- **ABI CLASSIFICATION** — **C**, additive.
- **CNA TEST** — `CApi_OwnedGraphicsDeviceSmoke`, including the cross-device draw refusal, and
  `StandaloneGraphicsDeviceProbe` for two live devices.
- **STATUS** — `RESOLVED`. Those corpus rows are now expressible.

---

## Summary

    BLOCKERS_RESOLVED  = 4   (storage disposing, sort mode, video frame identity, cross-device)
    BLOCKERS_PARTIAL   = 2   (non-finite sprite values, ContentLost)
    BLOCKERS_REMAINING = 1   (Apply3D multi-listener)

    CNA_ABI_VERSION unchanged at 0.8.0
    CNA_NEW_ABI_REQUIRES_DOWNSTREAM_REVIEW = false
    CNA_0_8_0_SEMANTICS_CHANGED = true  (two class-D rows above)

No downstream binding was modified and no downstream loader was weakened.
