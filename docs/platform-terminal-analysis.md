# Terminal platform feasibility analysis (PLAT-5b)

> **Question:** can CNA run a real game inside a terminal — rendering the frame and processing
> input through the TTY, with no display server and no window?
>
> **Verdict: yes for output, yes for mouse, *qualified* for keyboard, no for gamepad.** The
> qualification on keyboard is not a detail — it is the one place where a terminal cannot express
> XNA's input model faithfully on every terminal emulator, and it drives the whole design. Nothing
> here requires a new renderer, a new graphics API, or any change to the XNA-facing API.
>
> This analysis is written against the platform contract in `plans/plan_platform.md`. A terminal
> platform is only possible *because* of that separation: today every path to the screen goes
> through SDL3, so there is nowhere for a TTY implementation to attach.

---

## 1. Why this is worth more than it looks

A `TerminalPlatform` is not a novelty mode. It is the **strongest available proof that the
platform contract is not SDL-shaped**.

`plans/plan_platform.md` Phase 8 introduces `HeadlessPlatform` as a second implementation, precisely
because "a contract with one implementation is just indirection". But `HeadlessPlatform` proves
less than it appears to: it implements every method by doing nothing, so a contract shaped
entirely around SDL3's assumptions would still pass. A terminal implementation cannot cheat. It
has a real output device with a genuinely different model (character cells, not pixels), real
input with a genuinely different model (byte streams, not key state), and a capability profile
that is mostly *false*. If the contract survives it, the contract is real.

It also lands squarely on the plan's original motivation. The reason for separating SDL3 at all is
reaching targets where SDL3 is impractical. A terminal is such a target — over SSH, in a CI job,
in a `tmux` session, on a machine with no display server, and on genuinely old systems where a TTY
is the only thing guaranteed to exist.

---

## 2. What already exists (the reason this is cheap)

CNA already contains most of the output half, built for a different purpose.

| Asset | Location | What it gives the terminal platform |
|---|---|---|
| `QuantizeFrameToGrid()` | `modules/graphics-ext/src/AsciiQuantizer.cpp` | **RGBA8 frame → character grid**, the exact transform a TTY needs |
| `AsciiCell` | `.../Ascii/AsciiQuantizer.hpp` | `glyphIndex` + `foreground` + `background` + `hasBackground` — maps 1:1 onto an ANSI truecolor cell |
| `AsciiGrid` | same | `columns` × `rows`, row-major, with `At(col,row)` |
| `kAsciiGlyphRamp` | `.../Ascii/AsciiFontAtlas.hpp` | `" .:-=+*#%@"` — 10-step luminance ramp |
| `IGraphicsRenderer::ReadBackbuffer()` | `IGraphicsRenderer.hpp:1420` | Common-interface pixel readback, on every renderer |
| `SoftwareRenderer` | `modules/renderers/software/` | A complete CPU rasteriser holding `std::vector<uint8_t> color` (RGBA8); `ReadBackbuffer` fully implemented |

`QuantizeFrameToGrid` takes `cellWidth`/`cellHeight` **separately**, which matters more than it
sounds: terminal cells are roughly 1:2 (width:height), so a source block of 8×16 pixels per cell
is what keeps circles round. The existing signature already supports this; no change needed.

The history is directly on point. `docs/ascii-post-process-effect.md` records that the former
`ASCII` renderer identity was removed because it "was never a genuine terminal/TTY renderer" — it
was a decorator around `SDL_RENDERER` that drew glyphs *onto an SDL window*. The quantisation logic
survived as a renderer-neutral effect. **That logic is what a real terminal platform needs**; what
was missing was somewhere to send it that is not an SDL window. The platform contract is that
somewhere.

---

## 3. Architecture: it attaches to PLAT-127, not to a new renderer

The single most important design conclusion:

> A terminal platform needs **no new renderer**. It needs a `IPlatformSurfacePresenter`
> implementation.

`IPlatformSurfacePresenter` (PLAT-127) exists because PLAT-3's audit found `SKIA` and `BLEND2D`
rasterise on the CPU and use `SDL_Renderer` only to get finished pixels onto the window. Its
contract is "present this RGBA buffer to this window, with scaling and vsync". A terminal is
simply another device that can accept that buffer — it just quantises instead of blitting.

```text
Game
  ↓
GraphicsDevice
  ↓
SOFTWARE / SKIA / BLEND2D  (CPU rasteriser — unchanged, knows nothing about terminals)
  ↓  RGBA8 frame
IPlatformSurfacePresenter
  ├── Sdl3SurfacePresenter   → SDL_UpdateTexture / SDL_RenderPresent      (PLAT-128)
  └── TerminalSurfacePresenter → QuantizeFrameToGrid → ANSI to stdout
```

This is the plan's design decision 5 holding up under a case it was never written for: the
platform layer hands the renderer a target and receives finished pixels; it never participates in
drawing. A terminal changes only the last step.

Consequences worth stating plainly:

- **No `NativeWindowHandle`.** Add `NativeWindowSystem::Terminal` (PLAT-12 already reserves the
  enum) with all three pointers null, and report `supportsNativeWindowHandle = false`. Every GPU
  renderer refuses at selection time, deterministically, rather than crashing on a null handle.
- **The "window" is the terminal viewport.** `IPlatformWindow::GetClientBounds()` returns
  `columns × cellWidth` by `rows × cellHeight` in virtual pixels. Resize arrives via `SIGWINCH`.
- **Only CPU renderers are selectable.** `SOFTWARE`, `SKIA`, `BLEND2D`, `PORTABLEGL`, `HEADLESS`,
  `STUB`. That is a capability answer, not a limitation to hide.

---

## 4. Output: solved, with one real engineering constraint

Mechanically straightforward: enter the alternate screen buffer (`CSI ?1049h`), hide the cursor,
emit one truecolor SGR pair plus one glyph per cell, restore on exit.

The real constraint is **bandwidth**, and it is the thing that decides whether this is pleasant or
unusable:

| Grid | Cells | Naive bytes/frame (~20 B/cell) | At 60 fps |
|---|---:|---:|---:|
| 80 × 24 | 1 920 | ~38 KB | ~2.3 MB/s |
| 120 × 40 | 4 800 | ~96 KB | ~5.8 MB/s |
| 200 × 60 | 12 000 | ~240 KB | ~14 MB/s |

Fine on a local pty; painful over SSH; hopeless on a slow link. Three mitigations, all standard
and all mandatory rather than optional:

1. **Damage tracking** — diff against the previous grid and emit only changed cells, with cursor
   positioning. Typical game frames change a small fraction of cells. CNA has precedent for this
   discipline in `gdi_dirty_damage_test.cpp`.
2. **Run-length SGR** — emit a colour change only when it differs from the previous cell, not per
   cell. Usually cuts output several-fold on its own.
3. **Colour degradation** — truecolor → 256-colour → 16-colour → monochrome, by terminal
   capability. `AsciiQuantizeMode` already has `BlackWhite` and `Color`.

A frame budget, not a frame rate, is the right contract here: cap bytes per frame and drop to a
lower refresh rate when the budget is exceeded.

---

## 5. Input: this is where the honest answer lives

### 5.1 Keyboard — the impedance mismatch

XNA's keyboard API is a **level** model:

```cpp
KeyboardState state = Keyboard::GetState();   // modules/input/.../Keyboard.hpp:36
state.IsKeyDown(Keys::Left);                  // "is Left held *right now*"
```

A terminal in raw mode delivers an **edge** model: bytes arrive when a key is pressed. Classic
terminals send **no key-release event at all**. Holding a key produces auto-repeat — a stream of
identical presses at the OS repeat rate, after an initial delay.

This breaks the most common thing a game does. "Move while the arrow key is held" becomes "move,
pause for the repeat delay, then move in bursts".

There are two paths, and the platform must choose at runtime:

**Path A — the Kitty keyboard protocol (correct).** Progressive enhancement (`CSI > 1 u`) with the
*report event types* flag makes the terminal send distinct press, repeat and release events. With
it, `KeyboardState` is exact — genuinely equivalent to SDL3. Support is real but partial across
emulators and evolving; **it must be detected at runtime and never assumed**. The capability model
already exists to express this, and this is precisely the kind of thing it is for.

**Path B — synthetic key-up on a timeout (approximation).** Without release events: mark a key down
on receipt, and clear it if no repeat arrives within a window slightly longer than the OS
auto-repeat interval. This is an approximation and must be documented as one:

- a key reads as held for a few tens of milliseconds after actual release;
- the initial repeat delay (typically ~250–500 ms) makes a held key appear to stutter unless the
  first repeat is bridged;
- the auto-repeat rate is a user/OS setting the application cannot query, so the timeout is a
  heuristic, not a derivation.

Report `supportsExactKeyboardState = false` under Path B. Do not silently paper over it: a game
that needs exact key state can then refuse or adapt, which is the whole point of the capability
model. This is the honest boundary of the terminal platform, and it should be the first line of
its documentation.

Further keyboard limits that hold on **both** paths, unless the Kitty protocol is active:
modifier keys are not independently observable (no "is Shift held" without another key);
several combinations are indistinguishable or swallowed (`Ctrl+C`, `Ctrl+Z`, `Ctrl+S` are signals
and flow control unless disabled); and `Keys` values with no terminal encoding simply never fire.

### 5.2 Mouse — works, at cell granularity

xterm SGR extended mouse reporting (`CSI ?1006h` with `?1002h`/`?1003h`) gives press, release,
motion and wheel, with coordinates beyond 223 columns. Widely supported, including over SSH.

The catch is resolution: **coordinates arrive in character cells, not pixels.** With 8×16-pixel
cells, `Mouse::GetState()` reports positions quantised to 8 px horizontally and 16 px vertically.
Fine for tile/grid games, unusable for anything needing pixel-accurate pointing. Report it via a
capability rather than pretending to a precision that is not there.

### 5.3 Gamepad — no path

A terminal is a character device. There is no gamepad, joystick, haptic or sensor channel through
it, and no escape sequence will invent one. `GamePad::GetState()` must report "not connected".

Note this is a *platform* boundary, not a hardware one: on Linux the devices are readable via
evdev/`/dev/input/js*` independently of any display server. A future `TerminalPlatform` could
compose an evdev input backend, but that is a separate implementation with its own permissions
story, and it must not be assumed to be present.

---

## 6. Capability profile

Answering `PlatformCapabilities` (PLAT-16) honestly for a terminal — note how much is `false`,
which is exactly what makes it a good test of the contract:

| Capability | Terminal | Note |
|---|---|---|
| `supportsMultipleWindows` | ✗ | One TTY, one viewport |
| `supportsHighDpi` | ✗ | Cells, not pixels |
| `supportsClipboard` | ~ | **OSC 52** gives write, sometimes read; detect, do not assume |
| `supportsTextInput` | ✓ | Native to the medium |
| `supportsIme` | ✗ | Terminal's own concern, not observable |
| `supportsExactKeyboardState` | ~ | Kitty protocol only; see §5.1 |
| `supportsGamepadRumble` / `Sensors` | ✗ | No channel |
| `supportsNativeFileDialog` / `MessageBox` | ✗ | Would have to be drawn in-app |
| `supportsVulkanSurface` / `OpenGLContext` | ✗ | No native handle at all |
| `supportsSurfacePresentation` | ✓ | Via `TerminalSurfacePresenter` |
| `supportsPixelAccurateMouse` | ✗ | Cell granularity, §5.2 |

---

## 7. Things that must not be forgotten

These are the details that make the difference between a demo and something usable:

1. **Terminal state restoration is not optional.** Raw mode via `tcsetattr`, the alternate screen
   buffer, a hidden cursor and enabled mouse reporting all leave the user's shell broken if not
   restored. Restore on normal exit, on `SIGINT`/`SIGTERM`/`SIGHUP`, **and** on unhandled
   exception and on abort. A crash that leaves an invisible cursor and no echo is a hostile bug.
2. **`SIGWINCH` → resize event**, mapped onto the existing `PlatformEvent` window-resized path, so
   `GameWindow` and `GraphicsDeviceManager` need no terminal-specific code.
3. **Aspect ratio.** Cells are ~1:2. Default to `cellWidth:cellHeight = 1:2` or the game's output
   is vertically squashed by half.
4. **`stdout` is the display; `stderr` is not.** Logging must never write to `stdout` — it would
   corrupt the frame. PLAT-53 already moves logging to CNA's own sink; this makes the sink's
   destination a correctness matter, not a preference.
5. **Not every "terminal" is a TTY.** If `stdout` is a pipe or file, refuse cleanly rather than
   emitting escape sequences into a log.
6. **Windows.** Requires the Virtual Terminal processing mode
   (`ENABLE_VIRTUAL_TERMINAL_PROCESSING`) and a different input path entirely
   (`ReadConsoleInput`) — which, notably, *does* deliver key-release events, so Windows may be
   better placed for exact keyboard state than most Unix terminals. Treat it as a separate
   implementation of the same contract, not an afterthought.

---

## 8. What this does *not* solve

- **It does not make CNA C++23-independent.** `cnaplatform.md` §"C++23 remains a separate problem"
  applies unchanged: reaching an old system needs an old toolchain, not just a TTY.
- **It is not a fast path.** It is a portability and accessibility path.
- **It does not replace `HeadlessPlatform`.** Headless is for CI runs that want no I/O at all;
  terminal is for a human watching. Both are useful, and they validate different things.

---

## 9. Recommendation

**Feasible and worth doing, but after Phase 8, not instead of it.**

Sequencing matters. A terminal platform written *before* the contract has been proven against
`Sdl3Platform` and the conformance suite would end up shaping the contract around terminal
quirks — the mirror image of the SDL3-shaped mistake `cnaplatform.md` warns against. Written
*after*, it is the test that shows the contract holds.

The cost is genuinely low, because the hard part — turning a rendered frame into a character grid
— is already written, tested, and in the tree. What is new is a presenter, a termios/ANSI I/O
layer, and an honest keyboard story.

Task breakdown: `plans/plan_platform.md` Phase 10 (PLAT-129 … PLAT-140).
