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
- **`SpriteFont::MeasureString`** adding the last glyph's right side bearing unclamped.
  `cna-ts` (finding 27) and `cna-go` (Foundation 69) measured this independently, with
  tables, against XNA's IL, which keeps the bearing pending and adds `Math.Max(pendingZ, 0)`
  at each line break. CNA's `curLineWidth += cKern.Y + cKern.Z` is
  `FNA/src/Graphics/SpriteFont.cs:201`, character for character, in both of FNA's measure
  paths.

Each of these is a real observable divergence from XNA that a binding has measured and
pinned. Fixing them means deciding that XNA's IL outranks FNA where the two disagree,
which is a change to the project's governing rule and not a change to a function.

---

## BINDFIX-001 — `GraphicsAdapter` is enumerated before the video subsystem exists

**Reported by:** `cna-ruby`, in `docs/graphics-adapter-ordering-upstream-defect.md`,
measured 2026-09-01 against `cnanext` `e5ae0820e`, re-measured 2026-09-04 (Foundation 104
and 105) against artifact `c32bfbd307d695664f906ccf2834ec3f9ebc240fa388d544ac21ee3ebaeb731b`.
Corroborated by `cna-go` (`docs/foundation-68-graphics-adapter-evidence.md`) and
`cna-swift`.

**Status:** open, not started. This is the single highest-value row in this plan: it is
the only thing blocking `cna-ruby`'s entire `GraphicsAdapter` surface, and the binding
reports that all sixteen of its remaining strict diagnostics reduce to it.

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

### Audit separately, after enumeration is fixed

- **`MonitorHandle`** answers `CNA_RESULT_NOT_SUPPORTED` and zero today, and CNA's own
  value underneath is the display id cast to `uintptr_t` rather than an `HMONITOR`.
  Do not fabricate one. Decide whether CNA can now supply a truthful native monitor
  handle. Reported by `cna-ruby` and `cna-go`.
- **`Revision` and `SubSystemId`** are hardcoded zero, which `display.h` documents as
  "current CNA returns zero", beside a `vendor id`/`device id` pair that is read
  truthfully from sysfs. Decide whether these are truthful constants, platform data CNA
  cannot reach, or further defects. Reported by `cna-ruby` and `cna-go`.
- **Display modes are hardcoded to `SurfaceFormat::Color`.** `queryDisplayModes` and
  `queryCurrentDisplayMode` discard the platform's pixel format, and
  `CNA::Platform::DisplayMode` carries no format field at all even though SDL3's
  `SDL_DisplayMode` has one. `CNA_DisplayMode::format` already exists on the ABI and
  would simply start carrying truthful values. Reported by `cna-ruby`.
- **`IsProfileSupported` answers an unconditional `true`** on every renderer that supplies
  no descriptor hook, which is all of them but D3D9. CNA documents this as deliberate —
  the alternative was "a hardcoded table pretending to be a capability query" — so this
  is a decision to confirm rather than an obvious defect.
- **Every `cna_graphics_adapter_*` route is device-scoped**, validated through
  `GetBorrowedGraphicsDevice`, so it answers only inside a live graphics-device callback.
  XNA's `Adapters` and `DefaultAdapter` are statics usable before any `GraphicsDevice`
  exists, which is their purpose: an adapter is what a device is chosen *from*. This is a
  contract difference that survives the cache fix and needs its own answer.

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
