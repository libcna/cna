# plan_bindings_upstream.md — defects the language bindings measured in CNA

Ten language bindings live in `../\_bindings`: `cna-cs`, `cna-common-lisp`, `cna-go`,
`cna-java`, `cna-python`, `cna-ruby`, `cna-rust`, `cna-swift`, `cna-ts`, and their
templates. Each one binds this repository's C ABI, and each one has been writing down
what it found wrong in CNA rather than working around it silently.

This plan is the CNA-side backlog derived from those records. It exists because the
evidence was scattered across roughly fifty documents in ten repositories, and because
none of those repositories can fix anything here: they are consumers of a read-only
dependency.

**Collected 2026-09-04** from the binding repositories' own documents. Their measurements
were taken against CNA C ABI 0.7.0 through 0.21.0, at commits between
`a09196a6477f69a7a57c8364f990658d31531a5b` and `e5ae0820e`. This repository's `next` is
several hundred commits past most of them and the ABI is now 0.22.0, so **every row must
be re-measured before it is worked on**. Several rows the bindings still carry as open
were closed by later commits, and the bindings say so themselves where they have
re-measured.

## What this plan is not

It is not a list of places where CNA differs from Microsoft XNA. Several of the strongest
binding findings are exactly that, and they are **decisions for the project owner rather
than defects**, because `CLAUDE.md` makes FNA the authoritative behavioural reference and
CNA matches FNA faithfully in each case:

- **`GraphicsAdapter::getIsWideScreenProperty()`** compares the aspect ratio against
  `4.0f / 3.0f`. `cna-ruby` read the pinned XNA IL and found `1.6f`. CNA's implementation
  is a byte-for-byte match of `FNA/src/Graphics/GraphicsAdapter.cs:71-83`, comment
  included. Every mode between 1.334 and 1.6 is reported widescreen by CNA and not by XNA,
  and 16:10 by CNA and not by XNA's strict `>`.
- **`Microphone::setBufferDurationProperty()`** reads `getMillisecondsProperty()`, the
  sub-second component, so the documented `> 1000` branch is unreachable and
  `set(get())` fails at 1000 ms. That is `FNA/src/Audio/Microphone.cs:57-60` exactly,
  `value.Milliseconds` and all. XNA's IL reads `get_TotalMilliseconds`. CNA's own comment
  already records the unreachable branch.
- **`SoundEffectInstance::Apply3D`** refusing more than one listener, which
  `_bindings/fixcna-analysis.md` §2 already identified as a scope decision rather than a
  bug for the same reason.
- **Display modes carrying a hardcoded `SurfaceFormat::Color`**, which `cna-ruby` and
  `cna-swift` both recorded. `SurfaceFormat.Color // FIXME: Assumption!` is what FNA writes,
  four times over, in `SDL3_FNAPlatform.cs` and `SDL2_FNAPlatform.cs`.
- **`SpriteFont::MeasureString`** adding the last glyph's right side bearing unclamped.
  `cna-ts` (finding 27) and `cna-go` (Foundation 69) measured this independently, with
  tables, against XNA's IL, which keeps the bearing pending and adds `Math.Max(pendingZ, 0)`
  at each line break. CNA's `curLineWidth += cKern.Y + cKern.Z` is
  `FNA/src/Graphics/SpriteFont.cs:201`, character for character, in both of FNA's measure
  paths.

Each of these is a real observable divergence from XNA that a binding has measured and
pinned. Fixing them means deciding that XNA's IL outranks FNA where the two disagree,
which is a change to the project's governing rule and not a change to a function.

That rule has since been decided: `CLAUDE.md` now makes XNA the tie-break where a measured
Microsoft XNA 4.0 behaviour and FNA's implementation disagree, and requires each divergence
taken on those grounds to be recorded here. The rows above are still owner decisions, because
none of them has been re-measured against the runtime itself; **XNAPACK-001** below is the first
one that has.

---

## XNAPACK-001 — every float channel XNA packs is rounded, ties to even, and CNA truncated

**Fixed 2026-09-05** in `modules/math/src/Color.cpp`, the fourteen headers under
`modules/graphics/include/Microsoft/Xna/Framework/Graphics/PackedVector/` that turn a float
channel into an integer one (the three half-float types have no rounding rule of their own),
and the new shared helper `modules/core/include/CNA/Internal/PackedRounding.hpp`.

Measured on the XNA 4.0 runtime itself, not read from anyone's source: the driver is
`tools/xna-pipeline-oracle/framework/FrameworkPackingOracle.cs`, run by
`run-framework-oracle.sh` under Wine against the installed XNA Game Studio 4.0 assemblies, and
its 68 measurements are committed as
`tests/reference/xna40/framework/framework-packing-oracle.json`. CNA reproduces all of them in
`modules/graphics/tests/Microsoft/Xna/Framework/Graphics/PackedVector/XnaFrameworkPackingTests.cpp`,
which also fails if a measured case gains no reproduction.

The rule XNA follows, everywhere a float channel becomes an integer one:

| finding | source | repro |
|---|---|---|
| `Color(Vector4)` rounds; it does not truncate | `color/vector4_quarters` | `new Color(new Vector4(0.25f, 0.5f, 0.75f, 1))` is `{64, 128, 191, 255}`; CNA gave `{63, 127, 191, 255}` |
| the tie goes to the even neighbour, not away from zero | `color/vector4_tie_even`, `packed/Byte4/ties` | `126.5, 127.5, 128.5, 129.5` byte units pack as `126, 128, 128, 130`; `new Byte4(0.5f, 1.5f, 2.5f, 3.5f)` is `0x04020200` |
| the same rule holds for the normalized types, which CNA rounded away from zero (`std::lroundf`) | `packed/NormalizedByte4/ties`, `packed/NormalizedShort4/ties` | `0.5f/127, 1.5f/127, 2.5f/127, 3.5f/127` packs as `0, 2, 2, 4` |
| and for the colour layouts, which CNA rounded with `+ 0.5f` then truncated | `packed/Alpha8/ties`, `packed/Bgr565/ties`, `packed/Rg32/ties`, `packed/Rgba1010102/ties` | `new Alpha8(0.5f/255)` is `0x00`, not `0x01` |
| `Color.PackFromVector4` saturates and rounds exactly like the constructor | `color/packfromvector4_out_of_range` | `(2, -1, 0.5, 1)` packs as `{255, 0, 128, 255}`; CNA wrapped to `{254, 1, 127, 255}` |
| a NaN channel packs as 0, and the infinities saturate | `color/vector4_nan`, `packed/Byte4/nan_and_infinities` | `new Byte4(NaN, +inf, -inf, 1e30f)` is `0xFF00FF00` |
| `Color.Lerp` and `Color.Multiply` are the exception: they truncate, and `Lerp` clamps its amount | `color/lerp_half`, `color/multiply_odd_ties`, `color/lerp_amount_above_one` | `Lerp(black, white, 0.5f)` is `127`, not `128` |

FNA truncates in the constructors (`R = (byte) MathHelper.Clamp(color.X * 255, Byte.MinValue,
Byte.MaxValue);`, `Color.cs`) and neither clamps nor rounds in `PackFromVector4`, which is what
CNA reproduced and what `REMED-CORE-004` pinned. XNA wins, so those pins were rewritten against
the measurements rather than deleted.

Two consequences worth naming. The old `+ 0.5f then cast` and `std::clamp` paths let a NaN
channel reach an integer cast, which is undefined behaviour in C++ where C# merely leaves the
value unspecified; the shared helper closes that for every type at once. And the change is
visible in output, not only in edge cases: it is what made the content pipeline's
`VectorConverter` tables agree with XNA
(`plans/plan_xnapipeline_parity.md` XNAPP-090..092).

**Still open, measured in the same pass:** XNA's packed-vector structs all override `ToString()`
to print their packed value as hex (`packed/Byte4/tostring` is `04030201`,
`packed/Short2/tostring` is `00020001`). CNA implements `ToString()` on none of the seventeen
types. That is a missing member of the XNA surface rather than a packing difference, so it is
recorded here and left to the graphics module's own parity work; the two cases are listed as
unreproduced in the test above rather than silently skipped.

---

## BINDFIX-001 — `GraphicsAdapter` is enumerated before the video subsystem exists

**Reported by:** `cna-ruby`, in `docs/graphics-adapter-ordering-upstream-defect.md`,
measured 2026-09-01 against `cnanext` `e5ae0820e`, re-measured 2026-09-04 (Foundation 104
and 105) against artifact `c32bfbd307d695664f906ccf2834ec3f9ebc240fa388d544ac21ee3ebaeb731b`.
Corroborated by `cna-go` (`docs/foundation-68-graphics-adapter-evidence.md`) and
`cna-swift`.

**Status: fixed in `5726b1a828390e90e20d7973260069ea9697525a`.** Requalify against that SHA.

What was taken, and why it is two changes rather than one:

- `AdaptersChanged()` raises the video subsystem itself, so the enumeration no longer
  depends on the order someone else happens to call it in. A platform with no display
  server refuses the acquire, which is the no-display case and keeps the existing
  fallback.
- The reference is **kept** afterwards, not given straight back. A display id is only
  meaningful inside the video session that issued it, so an enumeration that raises
  video, reads the displays and drops it again caches ids that name nothing once the
  device's own acquire has restarted the session — measured: the name and the 22-mode
  list came out real while `CurrentDisplayMode` still answered 800x480.
- A `Game` installs its own platform (`modules/runtime/src/Game.cpp:197`), so a second
  `Game` in one process ends the session the cache was built from however long the pin
  is held. The adapter therefore **rebinds itself by display name, in place**, when its
  id stops naming anything. Fix shape C, and deliberately not B: rebuilding the cache is
  what `cna_graphics_adapters_refresh` already refuses to do, "because a live
  GraphicsDevice retains its adapter and replacing it would dangle" — the same
  constraint this row asked for, already enforced at the ABI.

Measured through the C ABI with nothing calling `SDL_Init` by hand, two sequential games
in one process:

| | before | after |
|---|---|---|
| `Description` | `Default Display` | `Dell Inc. 27"` |
| `DeviceName` | `\\.\DISPLAY1` | `\\.\DISPLAY1` (the synthetic XNA-convention name FNA also produces) |
| `CurrentDisplayMode` | 800x480 | 2048x1152 |
| `SupportedDisplayModes` | 1 | 22 |
| window vs adapter | contradict | agree, in the same frame |
| second `Game` | same fallback | identical to the first |

The probe is `build-probe/bindfix_adapter_ordering.c`.

**Still open on this row:** the HEADLESS/no-display artifact check from the regression
list below has not been run — this build carries OPENGLES3 only, and the available
control was a video driver that cannot start, which degrades to the fallback and exits
cleanly. The four separate audits at the end of this row are also untouched.

### The measured behaviour

`GraphicsAdapter::getAdaptersProperty()` fills a static `adapters_` cache on first use.
`GraphicsDevice`'s default constructor evaluates `GraphicsAdapter::getDefaultAdapterProperty()`
as a **delegating-constructor argument**, which C++ sequences before the delegated
constructor's body — and the body is where `createOrAttachWindow()` and
`setVideoSubsystemAcquired(true)` run.

`Sdl3Displays::GetDisplays()` is `SDL_GetDisplays`, which answers nothing before
`SDL_INIT_VIDEO`. So the first enumeration happens with no video subsystem, the no-display
fallback is what gets cached, and it is process-global and stale for the rest of the
process:

| field | cached fallback | truth on the measured host |
|---|---|---|
| adapter count | 1 | 1 |
| `Description` | `Default Display` | `screen` |
| `DeviceName` | `\\.\DISPLAY1` | the real X11 display name |
| `CurrentDisplayMode` | 800x480, `SurfaceFormat.Color` | 1280x800, aspect 1.6 |
| `SupportedDisplayModes` | that one mode | the display's own modes |
| refresh | `CNA_RESULT_NOT_SUPPORTED` | — |

The binding's own control settles the cause: initialising SDL video before device
construction makes the **identical build** answer `screen`, 1280x800, aspect 1.6.
CNA's SDL3 display enumeration is correct. Only the moment it is first asked is wrong.

The clearest single symptom is a one-frame self-contradiction on a qualified
OPENGL33/X11 run with a real window (`cna_game_window_get_native_window_ext` answers
`CNA_NATIVE_WINDOW_SYSTEM_X11` with a non-null `Display*` and a real XID):

```
cna_game_window_copy_screen_device_name   -> "screen"
cna_graphics_adapter_copy_description     -> "Default Display"
```

Two sequential `Game`s in one process both read the fallback.

### What the fix must satisfy

1. On a display-capable platform and renderer, the adapter cache must not stay
   permanently populated from the pre-video fallback.
2. **Adapter object and reference stability is preserved.** Do not fix this by
   invalidating adapter objects a `GraphicsDevice` already holds or that are exposed
   through C API handles.
3. HEADLESS and genuinely displayless configurations keep their current, correct
   behaviour. Do not invent a physical display where there is none.
4. An ordinary consumer must not have to call `SDL_Init` or initialise video by hand.
5. Sequential `Game` creation must not retain a stale fallback adapter.
6. The existing C API adapter routes start answering truthfully after the core fix.
   Prefer fixing CNA's core over adding routes for one binding.

### Fix shapes, in preference order

- **A.** Acquire the video subsystem before the first evaluation of
  `getDefaultAdapterProperty()` that default `GraphicsDevice` construction performs.
  Note the trap: moving the acquisition into the delegated constructor's *body* is too
  late, because the adapter expression has already been evaluated by then.
- **B.** If A is architecturally awkward, let the cache record that it was populated from
  the no-video fallback, and refresh the **existing** adapter objects in place once
  display enumeration becomes valid.
- **C.** Otherwise make refresh safe while preserving object identity.

Do not simply replace the cached adapter objects: `GraphicsDevice` and C API consumers
may already hold them.

### Regression tests to add on the CNA side

- A real display is available and nothing initialises SDL externally.
- `DefaultAdapter` is the real adapter.
- `Description` and `DeviceName` are the real ones.
- `CurrentDisplayMode` is the real mode, not a hardcoded 800x480.
- `SupportedDisplayModes` come from the real display.
- The `GameWindow` screen-device identity and `GraphicsAdapter` no longer contradict
  each other.
- Create a `Game`, destroy it, create another one.
- A HEADLESS / no-display control.
- Retained adapter references and C API handles stay valid across the fix.

The acceptance criterion is **not** that the adapter routes answer `CNA_RESULT_SUCCESS`.
It is that on a real-display qualification artifact, the adapter describes the same real
display the window and device are actually using, with no cached fallback anywhere.

Commit the fix on its own and record its SHA here, so `cna-ruby` can requalify against a
named commit rather than against a description.

### What unblocks in `cna-ruby` when this lands

Missing types: `GraphicsAdapter`, `GraphicsDeviceInformation`,
`PreparingDeviceSettingsEventArgs`.
Missing `GraphicsDevice` members: the `GraphicsAdapter`-taking constructor, `Adapter`,
`DisplayMode`.
Missing `GraphicsDeviceManager` members: `FindBestDevice`, `CanResetDevice`,
`RankDevices`, `OnPreparingDeviceSettings`, `PreparingDeviceSettings`.
Five further Ruby overload-mapping diagnostics are derivative of those and are not
separate CNA problems.

`PreparingDeviceSettings` must eventually be able to expose **and mutate** truthful
device-selection information. An observer-only callback does not reproduce XNA's
semantics.

### The four separate audits — done, and none of them is a fix to take here

Asked after the enumeration was repaired, each against FNA, which `CLAUDE.md` makes the
authoritative behavioural reference. All four come back the same way.

- **`MonitorHandle`.** The question was whether CNA can now supply a truthful native
  monitor handle. It cannot without diverging twice. `cna-ruby` observed that CNA's own
  value underneath is the display id cast to `uintptr_t` rather than an `HMONITOR` — and
  that is exactly FNA: `SDL3_FNAPlatform.GetMonitorHandle` is
  `return new IntPtr(unchecked((int)displayIds[adapterIndex]));`. The route's
  `CNA_RESULT_NOT_SUPPORTED` is the second, separate decision: this ABI does not disclose
  native handles anywhere, which `display.h` states. Both halves are deliberate, and
  fabricating an `HMONITOR` is what the row asked not to do.
- **`Revision` and `SubSystemId`.** They answer a hardcoded zero and `display.h` says so.
  FNA's answer is worse: both properties `throw new NotImplementedException()`. Neither
  matches XNA, which reports real values; CNA's is the more useful of the two and is the
  only one that is honest at the point of use. Supplying real ones means per-adapter PCI
  data CNA does not read today.
- **Display modes hardcoded to `SurfaceFormat::Color`.** `CNA::Platform::DisplayMode`
  carries width, height and refresh rate and no format, so the graphics layer has nothing
  to map. That is FNA again, and FNA is not comfortable about it either: `SurfaceFormat.Color
  // FIXME: Assumption!` appears four times across `SDL3_FNAPlatform.cs` and
  `SDL2_FNAPlatform.cs`. A neutral format on the platform struct, mapped in the SDL backends
  and reported unknown by Headless, is the shape a fix would take — and it is a divergence
  from FNA, so it belongs to the owner.
- **Every `cna_graphics_adapter_*` route is device-scoped.** This one is a real contract
  gap rather than an FNA match: XNA's `Adapters` and `DefaultAdapter` are statics usable
  before any `GraphicsDevice` exists, because an adapter is what a device is chosen *from*.
  Closing it means routes that answer with no device handle, which is an ABI addition and a
  design decision, not a defect to repair. It survives the cache fix untouched.

---

## BINDFIX-008 — should `cna_game_launch_parameters_add` refuse a duplicate key?

`LaunchParameters::Add` calls `emplace`, so the first value for a key wins and a second add is a
silent no-op that still answers `CNA_RESULT_SUCCESS`. Three contracts existed for one operation:
the C header said "adds or replaces", the implementation kept the first value, and XNA's
`Dictionary<string, string>.Add` throws on a duplicate.

The header's claim was the demonstrably false one — it asserted the canonical add overwrites,
which it does not — so it now states the measured behaviour. What remains open is whether the
boundary should answer `CNA_RESULT_INVALID_STATE` for a key already present, which is what XNA's
throw becomes in a C ABI. That is a behaviour change to a published route, and `LaunchParameters`
also carries two related divergences `cna-ruby` measured and `cna-rust` restated:
`cna_game_launch_parameters_parse_ext` skips an argument shorter than three characters or without
a colon, where XNA keeps a colonless argument with an empty value; and
`cna_game_launch_parameters_get_key_size` enumerates by name rather than in container order.
Reported by `cna-rust` (`RUST-UPSTREAM-026`) and `cna-ruby`.

---

## BINDFIX-018 — a decal pass has no support query

`cna_decal_pass_create` succeeds on a renderer that cannot run the pass, which is the
engine layer's usual contract, and the caller is meant to ask before using it. There is
nothing to ask: a decal pass is its own handle kind, so `cna_post_process_pass_is_supported`
refuses it with `CNA_RESULT_INVALID_HANDLE`, and no `cna_decal_pass_is_supported` exists.

The header used to name that route anyway, along with `cna_post_process_pass_destroy`,
which refuses the handle too and so leaked every pass a caller released by the book. The
header is fixed; the missing query is not, because adding a route is an ABI addition.
Either give the family its own `cna_decal_pass_is_supported`, or let the
`cna_post_process_pass_*` routes accept the kind — the second is what the header assumed
all along and would close `copy_name` at the same time.

Measured by `cna-java`'s `engine_layer_families.c`, which asks a bloom pass the same
questions and gets SUCCESS, so the difference is the handle kind rather than the renderer.
Reported as `JAVA-UPSTREAM-008`.

---

## BINDFIX-019 — a test camera's birth state is one its own setter refuses

`cna_camera_set_test_state_ext` refuses `CNA_CAMERA_STATE_CLOSED` and
`CNA_CAMERA_STATE_NOT_SUPPORTED`, and its implementation gives a reason worth keeping: a
device that is not open is never handed out, so nothing reports closed, and "not supported"
is what an absent device answers rather than a state one can be put in. Every state it
accepts reads back unchanged, which is a property worth having.

Measured against `next`, with `cna-java`'s own probe:

```
state at birth        SUCCESS NOT_SUPPORTED
set NOT_SUPPORTED     INVALID_ARGUMENT  reads back SUCCESS OPENING
```

So the refusal and the birth reading disagree, though not for the reason
`JAVA-UPSTREAM-021` gives. `CameraTestState::state` defaults to `Opening`, not to
`NOT_SUPPORTED`; the `NOT_SUPPORTED` at birth is the `Camera` object's own answer, from a
layer above the test state, before anything has driven it. Two layers, two answers, and a
setter whose domain matches one of them.

Left as measured. Widening the setter's domain for a test backend — where "closed" and "not
supported" are exactly the situations a test wants to reproduce — is defensible, and so is
keeping the guarantee that every accepted state reads back unchanged. That is a decision
about what the test surface is for.

---

## BINDFIX-028 — `JAVA-UPSTREAM-018` does not reproduce as reported

The row says creating a device resets three renderer-selection queries, "the count becomes
0 while `copy_available_ext` still answers 5". Asked directly, back to back, in a probe
that does nothing else:

```
get_available_count_ext  result=0 count=1
copy_available_ext       result=14 count=1
```

They agree, and both read the same `State().available`. The `0` in the binding's own probe
output comes from elsewhere in its sequence, after a `reset_for_tests`.

The row's second half does not reproduce either. Asked the same way:

```
get_current_type          result=0 type=3
copy_current_name         result=0 name="OPENGLES3"
```

Both name OPENGLES3. Both derive from `getCurrentGraphicsRendererType()`, which is a
compile-time constant, so they cannot disagree — and the `UNKNOWN` in the binding's output
is again a later line in a sequence that has called `reset_for_tests` by then.

Two lessons, and the second is the more useful. A binding's probe output is evidence about
the binding's sequence, not about a route: both halves of this row were read off an
aggregate that had moved on. Asking one route, twice, in a program that does nothing else
settled it in a minute. `get_active_ext` answering `UNKNOWN` before any device exists is
separate and correct — it is documented to, and `GetActive()` throws until a renderer has
been created.

---

## BINDFIX-031 — the GPU instance culler culls nothing, narrowed

`cna-ts` (finding 20) measured the culler running, reporting success and keeping every
instance it is given: one ten thousand units off the side of a hundred-unit frustum
survives. `is_supported` answers true and the unsupported reason is empty, so nothing is
announced.

What this pass established, without reproducing it on a GPU:

- CNA has its own test for exactly this, `ComputeCullingTest.TheGpuCullerAgreesWithTheCpuOneBoxForBox`
  (`plans/plan_modern.md` MOD-1551), and it passes. So the shader's arithmetic and the
  outward-normal convention it mirrors are not the fault on their own — the test runs the
  same test expression, character for character.
- The test and the shipped route differ in exactly one thing: **where the frustum comes
  from**. The test uploads `culler.getFrustum()`, the frustum the CPU culler already holds.
  `GpuInstanceCuller::cull` builds its own, `BoundingFrustum(view * projection)`, from the
  two matrices the caller passed.

So the hypothesis to test first is the product, not the shader: whether the matrices reach
`cull` in the order and convention `BoundingFrustum` expects. A frustum built from a
transposed or mis-ordered view-projection yields six planes that reject nothing, which is
the measured symptom exactly, and it would leave the existing test green because that test
never goes through this path.

The next step is a probe that culls with a known camera and compares against
`FrustumCullerEXT` on the CPU, then prints the six planes both produce. That is a GPU
measurement rather than a reading, which is why it is written down here instead of guessed
at.

---

## BINDFIX-035 — a `.cnb` model carries its skeleton and publishes no skins

Found while adding the skinned content asset the owner asked for. `cna_tool_gltf_to_cnb`
compiles `tests/assets/gltf/skin-four-weighted.gltf` into a model whose container holds an
`MSKL` chunk of 792 bytes -- `cna_tool_cnb_info` lists it beside `MBON`, `MMSH` and the
rest -- so the skeleton survives the format.

The loaded model's skin collection does not. Measured through the C ABI with the same
probe against both formats:

| asset | `cna_model_get_skin_count_ext` | skeleton route |
|---|---|---|
| `tests/assets/gltf/skin-four-weighted.gltf` | 1 | publishes, twice |
| `tests/assets/cnb/skinned/skinned_model.cnb` | 0 | nothing to ask |

`ContentManager.cpp:4103` populates `setSkinsEXTProperty` on the glTF import path and
nowhere else, and no CNB chunk carries the `ModelSkinEXT` triple -- a skin's name, its
skeleton, and the meshes its palette drives. So for any model delivered as `.cnb`,
`Model.SkinsEXT` is empty, `cna_model_get_skin_count_ext` answers zero and every skin
route below it is unreachable. That is a format gap rather than a defect in a route: the
data the collection describes is partly there (`MSKL`) and partly not (which meshes each
skin poses).

The asset is checked in as the fixture, so whoever closes it has the failing case and the
working one side by side.

---

## BINDFIX-036 — fractional global Doppler scale changes stationary pitch

Found through the native and browser ports of XNA Audio3D sample SAMPLE-059. Its normal dog
and cat recordings sounded like a motorcycle because the sample sets
`SoundEffect.DopplerScale = 0.1f`: CNA computed the physical Doppler factor with only the
emitter scale and then multiplied the completed playback ratio by `0.1`, so even zero
listener/emitter velocities produced a `0.1` frequency ratio.

Microsoft XNA 4.0's `KernelSoundEffectInstance.Apply3D` IL establishes the required ordering.
Before calling `X3DAudioCalculate`, it replaces the emitter's `_DopplerScale` with
`_DopplerScale * KernelSoundEffect::dopplerScale`; afterward it applies only
`dspSettings.DopplerFactor` to the pitch ratio. FNA's current `SoundEffectInstance.UpdatePitch`
diverges by multiplying the completed factor by the global scale.

Fixed under `plans/plan_audio.md` **AUD-09-009** by passing the combined scale into
`ComputeDopplerFactor` exactly once. Two regressions cover both the stationary `0.1` case and
a moving-source `0.5 * 0.5` case whose XNA ratio is exactly `8/9`.

---

## Fixed in this pass

Three defects, each independently reported by two or more bindings, each one a place where
CNA contradicted its own documentation or its own siblings:

- **BINDFIX-002** — `cna_spot_shadow_map_destroy` did not check `activeBorrowCount`, so a
  spot shadow map destroyed itself with a lent handle still pointing at it where its three
  siblings refuse. The guard existed: it had been copy-pasted a second time into
  `cna_shadow_map_destroy`, where it was unreachable, still carrying the spot map's
  message. Moved to the function it was written for.
  Reported by `cna-ts` (finding 16) and `cna-java` (`JAVA-UPSTREAM-013`).
- **BINDFIX-003** — `CallWithExceptionBarrier` caught `std::out_of_range` and
  `std::invalid_argument` but not `std::logic_error` itself, so routes that throw a bare
  one answered `CNA_RESULT_INTERNAL` where their headers document
  `CNA_RESULT_INVALID_STATE`. A shim comment in the engine layer already asserted the arm
  existed. Reported by `cna-ts` (findings 14 and 25) and `cna-java`
  (`JAVA-UPSTREAM-006`, and `-012` for the compute-shader route).
- **BINDFIX-004** — the two `cna_cube_lut_*` shims caught `CNA::CNAException` while
  `CubeLut::parse` and `CubeLut::loadFromFile` throw `CNA::Graphics::EngineException`.
  The two are siblings, not parent and child, so the catch could never match and the
  barrier's `EngineException` arm answered `CNA_RESULT_NOT_SUPPORTED` for every failure.
  A typo in an artist's `.cube` file reached a game as "this renderer cannot do colour
  grading". Reported by `cna-ts` (finding 24) and `cna-java` (`JAVA-UPSTREAM-009`).
- **BINDFIX-005** — `cna_post_process_chain_add_owned_pass` consumed a pass handle without
  the `RemoveOwnedGraphicsResourceFor` its sibling destroy performs, so one transfer left
  `cna_game_destroy` refusing for the rest of the process. Reported by `cna-ts` (finding 1)
  and `cna-java` (`JAVA-UPSTREAM-011`).
- **BINDFIX-006** — `~MeshResource` moved an absent `detachedValue` over a content-loaded
  part's live borrow and `~PartResource` dereferenced the result. Destroying or tearing
  down any content-loaded `Model` faulted. Reported by `cna-rust` (`RUST-UPSTREAM-021`),
  `cna-java` (`JAVA-UPSTREAM-004`) and `cna-ruby`.
- **BINDFIX-007** — `ValidateMorphShape` restated a stride list that had gone stale against
  CNA's own canonical table, refusing both PBR layouts. Reported by `cna-rust`
  (`RUST-UPSTREAM-024`).
- **BINDFIX-009** — `cna_morph_target_data_ext_copy_tangent_deltas` bounded the target index
  against an array that is empty until the setter runs. Reported by `cna-java`
  (`JAVA-UPSTREAM-023`).
- **BINDFIX-010** — both `SpriteBatch` `Begin` routes refused the null state descriptors they
  document, and that FNA's own `Begin` substitutes. Reported by `cna-ruby`.
- **BINDFIX-011** — `cna_camera_destroy` freed the provider the process-wide platform override
  points at, without taking the pointer back out. Reported by `cna-rust`
  (`RUST-UPSTREAM-020`), `cna-ts` (finding 11) and `cna-java` (`JAVA-UPSTREAM-019`).
- **BINDFIX-012** — the four clustered constructors documented a game handle and resolve a
  graphics device. Header and parameter names corrected. Reported by `cna-python`
  (ENGINE-006), `cna-ts` (finding 10) and `cna-java` (`JAVA-UPSTREAM-005`).
- **BINDFIX-013** — `cna_pbr_effect_apply_material` wrote the C++ effect while
  `set_texture`/`get_texture` use the C API's retained-handle table, so a material lost every
  texture in both directions and could not clear a slot. Both routes now go through the table.
  Reported by `cna-python` (ENGINE-005), `cna-ts` (finding 19) and `cna-java`
  (`JAVA-UPSTREAM-010`).
- **BINDFIX-014** — the packed depth encoding used base 256 where an 8-bit UNORM channel
  stores `round(c * 255) / 255`, so the four channels delivered one channel's resolution.
  Reported by `cna-ts` (finding 13).
- **BINDFIX-015** — `cna_render_pipeline_get_skybox` echoes the handle that was set while
  `cna_skybox_get_environment` mints a counted one, and both carried the same borrow
  sentence: following it destroyed the caller's own skybox on one route and made the game
  undestroyable on the other. Both headers now say which they are. Reported by `cna-ts`
  (finding 15).
- **BINDFIX-016** — withdrawing a `Motion` or `Compass` test backend left `IsDataValid`
  true for a reading `getCurrentValueProperty()` would no longer hand over. Reported by
  `cna-ts` (finding 5).
- **BINDFIX-017** — `cna_area_light_brdf_table_get_texture` mints a handle counted against
  the game and said only that it borrows. Reported by `cna-rust` (`RUST-UPSTREAM-025`).
- **BINDFIX-020** — `SDL_INIT_CAMERA` appeared nowhere in the tree, so `SDL_GetCameras`
  always answered nothing while `IsSupported` answered true. Reported by `cna-ts` (34).
- **BINDFIX-021** — every GPU timer's first sample was `4294.967295` ms, the value a 32-bit
  query saturates at and what a driver returns for a disjoint result. Reported by
  `cna-python` (ENGINE-003).
- **BINDFIX-022** — `PacketWriter::Write(Color)` wrote four bytes and `PacketReader::ReadColor`
  read four floats, so a colour could not be read back out of a packet. Reported by
  `cna-python`.
- **BINDFIX-023** — the XACT demo's own Z and X keys wrote to a variable its generator had
  marked read-only, under a comment saying "settable". Reported by `cna-ts` (31).
- **BINDFIX-024** — three `_init` routes documented identity transforms and fill zero
  matrices; the code is right and `default(Matrix)` is why. Reported by `cna-ts` (23) and
  `cna-python` (ENGINE-008).
- **BINDFIX-025** — EasyGL's uniform setters wrote through `glUniform` without making their
  own program current, so a uniform set before an effect was applied went to whichever
  program was bound. Reported by `cna-java` (`JAVA-UPSTREAM-016`).
- **BINDFIX-026** — `cna_weighted_blended_transparency_begin` documented the behaviour
  MOD-1697 corrected, and the defensive code it asked for leaves the bracket open forever.
  Reported by `cna-ts` (21).
- **BINDFIX-027** — `importance_sample_ggx` needs a unit normal and said nothing about it.
  Reported by `cna-python` (ENGINE-007).
- **BINDFIX-029** — `cna_post_process_chain_is_gpu_timing_enabled`, the route the header
  sends a caller to, answered `CNA_FALSE` until the chain had applied once, because it
  asked timers that are made lazily. Reported by `cna-python` (ENGINE-004).
- **BINDFIX-030** — a glTF-imported skin's skeleton was refused as "not created through the
  C API" though `skin->Data` held it; it is now published as an aliasing borrow of the
  Model. Reported by `cna-rust` (`RUST-UPSTREAM-022`).

---

## The rest of the backlog

Roughly 160 further open findings are recorded in the binding repositories. They are not
transcribed here, because each one's evidence — the probe source, the measured table, the
artifact SHA — lives with the binding that took it, and a copy here would go stale. What
follows is the index: where to look, and which rows carry a reproducer.

### Process-fatal

| what | where reported | reproducer |
|---|---|---|
| Content-loaded `Model` destroy faults; `~MeshResource` moves an absent `detachedValue` over a good pointer and `~PartResource` then calls `setTagProperty` on it with no null check | `cna-rust` `RUST-UPSTREAM-021`, `cna-java` `JAVA-UPSTREAM-004`, `cna-ruby` | `cna-rust/tools/reproducers/ext015g_load_model_destroy.c` (+ hand-built control), `cna-java/tools/native-abi/probes/content_manager_model_teardown.c` |
| `cna_camera_destroy` leaves a dangling process-wide platform override; the next camera route dereferences freed memory | `cna-rust` `RUST-UPSTREAM-020`, `cna-ts` (11), `cna-java` `JAVA-UPSTREAM-019` | `cna-java/tools/native-abi/probes/camera_test_backend.c` |
| Concurrent `cna_graphics_device_create` corrupts the heap; `Sdl3GlContext::CreateContext`'s mutex does not cover the whole construction | `cna-rust` `RUST-UPSTREAM-023` | `cna-rust/tools/reproducers/ext015h_concurrent_device_create.c` (13 aborts / 70) |
| A process exiting with a live vertex buffer aborts on the EasyGL renderers when the creating thread has ended — which every JVM satisfies | `cna-java` `JAVA-UPSTREAM-014` | `cna-java/tools/native-abi/probes/exit_with_live_graph.c` |
| An unavailable `CNA_GRAPHICS_RENDERER` aborts the process during library load, before any consumer frame exists | `cna-java` `JAVA-UPSTREAM-017` | `cna-java/tools/native-abi/probes/renderer_selection.c` |
| `cna_shader_effect_create` aborts on EasyGL/OPENGLES3 when handed desktop GLSL instead of the dialect the renderer declares | `cna-cs` | — |
| A second `Game` create/destroy cycle segfaults on OPENGL33 | `cna-cs` | two-line reproducer named in `docs/native-behavior-blockers.md` |
| A refused `cna_game_destroy` is not side-effect-free: the process then segfaults on the way out | `cna-ts` (18) | — |
| Shutdown on `SIGTERM` calls a pure virtual during native teardown | `cna-cs` | `scripts/Reproduce-SigtermShutdown.sh` |
| A virtual call into a `GraphicsDevice` whose derived destructor has run (UBSan + ASan agree) | `cna-cs` | reproduced on this repository's own `build-ubsan` and `build-asan` |

### Owned-handle accounting

`cna_post_process_chain_add_owned_pass` releases the handle without the matching
`RemoveOwnedGraphicsResourceFor`, so the game's owned-child count never falls and
`cna_game_destroy` refuses for the rest of the process — reported by `cna-ts` (1) and
`cna-java` (`JAVA-UPSTREAM-011`), with a precise fix and a reproducer
(`chain_owned_pass.c`). Several engine getters mint a fresh counted handle per read while
their headers say the handle is borrowed and must not be destroyed:
`cna_post_process_effect_pass_get_effect` is the one every binding names
(`cna-python` ENGINE-002, `cna-ts` 17, `cna-java`), and `cna_skybox_get_environment`
versus `cna_render_pipeline_get_skybox` document the same contract while only one of them
mints (`cna-ts` 15).

### Documentation that contradicts the code

The four clustered-lighting constructors name their first parameter `game` and resolve it
with `GetBorrowedGraphicsDevice`, so a real game handle is refused (`cna-python`
ENGINE-006, `cna-ts` 10, `cna-java` `JAVA-UPSTREAM-005`). `cna_compute_shader_create` is
documented to succeed for source that does not compile, and fails instead — which makes
`is_valid` and `copy_compile_error` unreachable for the case they exist for
(`cna-python` ENGINE-001, `cna-ts` 8, `cna-java` `JAVA-UPSTREAM-012`). Three `_init`
routes document identity transforms and write zero matrices (`cna-python` ENGINE-008,
`cna-ts` 23). `cna_sprite_batch_begin_with_effect` and `_with_states` document "or null
for AlphaBlend" and refuse null, one parameter at a time (`cna-ruby`). The weighted-blended
bracket's header describes behaviour the code deliberately changed (`cna-ts` 21).

### Two sources of truth

`cna_pbr_effect_apply_material` sets textures through the C++ object while
`set_texture`/`get_texture` use the C API's retained-handle table, so a material applied
through the ABI carries every scalar and no texture, `extract_material` never reports a
texture at all, and a read-modify-write silently unbinds every map. Reported by
`cna-python` (ENGINE-005), `cna-ts` (19) and `cna-java` (`JAVA-UPSTREAM-010`), all three
independently.

### Silent wrong answers

An upload into a render target reports success and is dropped (`cna-ruby`). A `Color`
written into a packet cannot be read back out of it — the writer writes four bytes and the
reader consumes sixteen (`cna-python`). A packet larger than the caller's buffer is cut
and reported as a success, and the `PacketReader` overload delivers every byte while
reporting none (`cna-rust` `RUST-UPSTREAM-028`). The GPU instance culler runs, reports
success and culls nothing (`cna-ts` 20). A `ShaderEffect`'s first draw through
`SpriteBatch` produces nothing (`cna-ts` 22). A uniform set before its shader effect is
applied is written to whichever program is current (`cna-java` `JAVA-UPSTREAM-016`). The
packed depth encoding divides by 256 where an 8-bit UNORM target quantises by 255, losing
every bit the packing bought (`cna-ts` 13). `cna_texture2d_create_from_encoded_memory`'s
cover-and-crop path refuses targets wider than tall while accepting taller than wide
(`cna-ruby`, `cna-swift`).

### Capability queries that do not predict behaviour

`cna-java` counts five instances by itself (`JAVA-UPSTREAM-005`, `-007`, `-015`, `-021`,
`-022`); `cna-cs` adds render-target readback and cube-face storage having no capability
identity at all, and `cna-ts` adds WEBGL2 advertising `MultipleRenderTargets` while a draw
into two bound targets reaches neither. `cna_graphics_device_get_shader_dialect_ext`
answers `UNKNOWN` on every renderer but WebGPU, while GLSL ES sources compile and draw.

### Arithmetic that disagrees with XNA

`SpriteFont::MeasureString` adds the last glyph's right side bearing unclamped, where
XNA's IL adds `Math.Max(pendingZ, 0)` — measured independently by `cna-ts` (27) and
`cna-go` (Foundation 69), both with tables. `GetSampleDuration` truncates the fractional
millisecond before `TimeSpan::FromMilliseconds` ever sees it, and `GetSampleSizeInBytes`
drops XNA's frame alignment and binary32 division (`cna-rust` `RUST-UPSTREAM-027`,
`cna-ruby`). The two stencil masks default to `Int32.MaxValue` where XNA's IL writes `-1`
(`cna-ruby`, `cna-common-lisp`, `cna-cs`). `cna_game_launch_parameters_add` documents
"adds or replaces", calls `emplace`, and so does neither (`cna-rust`
`RUST-UPSTREAM-026`, `cna-ruby`).

### Build and environment

`_bindings/fixcna-analysis.md` (2026-08-26, against `6319f30c5`) is the earlier
cross-binding analysis and still holds two prerequisites: the C-API test suite had six
red tests, and several rows in it were already stale when it was written. `cna-java`
`JAVA-UPSTREAM-003` reports five of this repository's own `CApi_*` smoke tests failing in
the HEADLESS/NULL-audio configuration. `cna-common-lisp` records that `next` at
`822d3b960` does not build against published `sharp-runtime:next`, because
`StorageDevice.cpp` calls `SetIsolatedStorageRootOverride`, which exists only on the
`next` branch of that repository — the same breakage this session hit, and the reason
`CMakeLists.txt`'s default of `../sharp-runtime` is worth revisiting.
