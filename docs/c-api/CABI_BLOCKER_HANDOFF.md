# Blocker handoff — cross-binding C-ABI milestone

`fixcnacs.md` Phase 10 asks for a per-blocker report against
`cna-cs/docs/native-behavior-blockers.md`. That file lives in a read-only repository and is not
edited from here; this is the upstream half of the handoff, in the shape Phase 10 specifies.

Measured on `feature/bindings`. Downstream rows not touched by this milestone are deliberately
absent rather than restated.

---

## `SoundEffectInstance.Apply3D(AudioListener[], AudioEmitter)`

- **BEFORE** — downstream reported the implementation rejecting every listener count but one.
- **ROOT CAUSE** — CNA followed FNA, which throws `NotSupportedException` for any count but one.
  The owner supplied decompiled XNA 4.0, which settles it the other way:
  `Microsoft.Xna.Framework.Audio.SoundEffectInstance.UnsafeApply3D` copies every listener into a
  native array and hands XACT the whole thing with `listeners.Length`. There is no count
  restriction anywhere in it. FNA's refusal is FNA's, not XNA's.
- **CNA CHANGE** — any positive count is accepted. What CNA cannot reproduce is XACT's per-listener
  output matrices — the mixer has a single stereo gain pair — so every listener is evaluated and the
  **dominant** one, the nearest to the emitter, decides the applied attenuation, pan and Doppler.
  Deliberately not "use `listeners[0]`": moving a second, closer listener changes the result. A
  count of zero is refused explicitly rather than guessed at, since XNA hands it to XACT and
  surfaces whatever XACT returns, which is not established here.
- **ABI CLASSIFICATION** — **D**, semantic change without shape change. A count this ABI refused
  now succeeds.
- **CNA TEST** — `SoundEffectInstanceTest.Apply3DArrayOverload` and
  `Apply3DMultiListenerNearestListenerDominates`. The second is geometric: the emitter sits hard
  left of the far listener and hard right of the near one, so "nearest wins" and "first wins" give
  opposite pans and the assertion can only pass for one of them.
- **CNA.NET TEST** — unchanged; the managed fallback stays until downstream re-reviews.
- **STATUS** — `RESOLVED`. **Downstream must re-review**: a count this ABI refused now succeeds.

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
- **ROOT CAUSE** — CNA **deliberately** refused them, in 13 places plus four more at the C
  boundary. A standing, documented departure predating this milestone: XNA's `SpriteBatch` contains
  no `IsNaN`, no `IsInfinity` and no finiteness check at all.
- **CNA CHANGE** — the refusals are gone and the values reach the vertex path. What made that
  possible was fixing the sort first: both depth comparators used a bare `<`, and NaN compares
  false in both directions, so the strict weak ordering `std::stable_sort` requires was violated —
  undefined behaviour, not a wrong order. `CompareOrdered` is now a total order matching **FNA's**
  own comparers (`SpriteBatch.cs:1602`, `depth.CompareTo`), which puts NaN below everything.
  `DrawString` also stopped building an integer `Rectangle`, since one cannot carry a NaN.
- **FNA and XNA differ here, and CNA follows FNA.** XNA's comparers are a bare `>` / `<` pair
  returning 0 when neither holds, so a NaN depth compares equal to every other depth. In C++ that
  is unusable: equivalence stops being transitive, which is exactly what `std::stable_sort` forbids,
  so copying XNA's shape would have kept the undefined behaviour.
- **ABI CLASSIFICATION** — **D**. The C boundary's own finiteness guards on the four XNA-shaped
  sprite routes are gone, so values this ABI refused now succeed.
  `cna_sprite_batch_draw_mesh_ext` keeps its guard, having no XNA counterpart and so its own
  contract.
- **STATUS** — `RESOLVED`. **Downstream must re-review**: values this ABI refused now succeed. The
  Int32 destination range check is unchanged and still refuses a finite value too large to be a
  representable destination.

## `DynamicVertexBuffer` / `DynamicIndexBuffer` / render-target `ContentLost`

- **BEFORE** — headers stated CNA never raises it; downstream could observe no notification.
- **ROOT CAUSE** — no bridge from a real device-loss event to the per-resource event. Note CNA was
  matching FNA, which hardcodes `IsContentLost` false and says so: *"We never lose data, but lol
  XNA4 compliance"*.
- **CNA CHANGE** — the four affected types carry real state and implement
  `CNA::Internal::Graphics::IContentLosable`; `GraphicsDevice` notifies them from the
  renderer-reported reset transition. `cna_render_target_subscribe_content_lost` /
  `_unsubscribe_content_lost` bind the render-target half that buffers already had.
- **ABI CLASSIFICATION** — **D** for the event becoming real; **C** for the two additive routes.
- **CNA TEST** — `ContentLostProbe`, `CApi_RenderTargetLifetimeSmoke`, and `ContentLostTests.cpp`.
- **STATUS** — `RESOLVED where loss is real`. Only `directx9`, `direct2d` and `skia` report a device
  reset; the other 44 renderer families never lose content and so never raise it. This is
  deliberate — firing on a caller-initiated `Reset` would make the event noise.
- **CORRECTION, and it was a real defect.** An earlier revision of this row claimed "a write clears
  the flag". That was true of the dynamic buffers and false of the render targets:
  `RenderTarget2D::ClearContentLostEXT()` and its cube equivalent were declared and never called
  from anywhere, so a render target that lost its content reported `IsContentLost == true` for the
  rest of its life. External review caught it. `GraphicsDevice::SetRenderTargets` now clears the
  flag on every target it binds, placed after the renderer has accepted the binding so a call that
  throws leaves the flag untouched — `ContentLostTest.ABindingThatThrowsDoesNotClearTheLostFlag`
  pins that placement. `ClearContentLostEXT()` was also promoted onto `IContentLosable`, which is
  what let the binding path call it without knowing the concrete type.

## `VideoPlayer.GetTexture()` identity and lifetime

- **BEFORE** — a borrowed transient texture with no stable identity or generation.
- **ROOT CAUSE** — `get_texture` creates a fresh handle per call, so "same frame again" and "the
  frame advanced" are indistinguishable.
- **CNA CHANGE** — `cna_video_player_get_frame_ext` returns the borrowed texture on the same
  lifetime terms plus `generation` and `presentation_time`. `VideoPlayer` counts decoded frames.
- **CORRECTION, and it was a real defect.** An earlier revision of this row said the count "resets
  on `Play`/`Stop`", and the implementation did exactly that under a comment stating the goal
  correctly: *a generation from a previous playback must never compare equal to one from this
  playback*. The reset is what makes them equal — every playback's first frame came back as
  generation 1, so a caller comparing generations across a Stop/Play boundary was told the frame had
  not changed. External review caught it. The counter is now monotonic for the lifetime of the
  player and is never restarted, which is what the contract required all along.
  `VideoPlayerTest.FrameGenerationNeverRestartsAcrossStopAndReplay` fails against the old
  behaviour.
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

    BLOCKERS_RESOLVED  = 7   (storage disposing, sort mode, video frame identity, cross-device,
                              Apply3D multi-listener, ContentLost where loss is real,
                              non-finite sprite values)
    BLOCKERS_PARTIAL   = 0
    BLOCKERS_REMAINING = 0

    CNA_ABI_VERSION 0.8.0 -> 0.9.0
    CNA_NEW_ABI_REQUIRES_DOWNSTREAM_REVIEW = true
    CNA_0_9_0_SEMANTICS_CHANGED = true  (four class-D rows above)

The version moved because an earlier revision of this file recorded `CNA_0_8_0_SEMANTICS_CHANGED =
true` beside `CNA_ABI_VERSION unchanged`, which contradict each other:
`docs/c-api/ABI_VERSIONING.md` requires a minor increment for an incompatible change under the
experimental `0.x` policy, and a class-D row is one. `cna-cs`'s reviewed policy names 0.6.0, 0.7.0
and 0.8.0, so it will refuse 0.9.0 until extended — that is the mechanism working, and it is the
re-review these four rows call for.

No downstream binding was modified and no downstream loader was weakened.
