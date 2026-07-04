# Input — Manual / Platform Verification Results

Records hardware- and platform-gated input checks that the headless `CnaTests` suite cannot cover
(Phase I10 tasks 850–853). **This log is deliberately honest:** items that could not be verified in
a given environment are marked *not verified*, not silently passed. Add a new dated entry per
environment/session.

---

## Entry — 2026-07-04

| | |
|---|---|
| **Date** | 2026-07-04 |
| **OS** | Linux 6.12.90+deb13-amd64 (Debian 13) |
| **Display server** | Wayland (`XDG_SESSION_TYPE=wayland`, `WAYLAND_DISPLAY=wayland-0`); XWayland available (`DISPLAY=:0`, usable via `SDL_VIDEODRIVER=x11`) |
| **Graphics backend** | EasyGL (OpenGL ES 3.2, Mesa 25.0.7) |
| **Controller** | **None available** |
| **IME / keyboard layout** | No IME configured; no interactive human at the keyboard |
| **Screenshot tool** | None working (`import -window root` fails under Wayland) |

### Automated (headless) baseline
- `CnaTests` full suite (clean builds, task 862, incl. the task-858 offset test): **1964/1964**
  (EasyGL, Vulkan); **1968/1968** (bgfx, +4 bgfx-specific).
- Input filter: **217** tests, identical on all three backends.

### Verified in this environment

| Check | Method | Result |
|-------|--------|--------|
| `Mouse::SetPosition` logical→window conversion (a-0001) | Unit test `SetPositionConvertsLogicalToWindowForLetterboxedRenderer` (real window + `SDL_Renderer`, 100×100 logical on 200×200 window) | **Pass** under ambient Wayland **and** `SDL_VIDEODRIVER=x11` |
| `SetPosition` letterbox **offset** (not just scale) | Unit test `SetPositionHandlesLetterboxOffsetNotJustScale` (100×100 logical LETTERBOXed into a non-square 200×100 window → logical `(50,50)`→window `(100,50)`) | **Pass** (Wayland + X11); the +50px centering offset is applied (task 858) |
| Basic `SetPosition` OS-cursor warp (window == render res) | Prior manual X11 harness (task 783): `SetPosition(50,60)` → global cursor at `windowPos+(50,60)` | **Pass** (pixel-exact, X11) |
| `TextInputEXT` Start/Stop/`IsTextInputActive` | Real hidden-window round-trip test (task 809) | **Pass** |
| `demo_input` builds + runs crash-free | `timeout 4 ./cna_demo_input` — window created, EasyGL/OpenGL ES 3.2 initialized, no crash | **Pass** (no crash; layout **not** visually verified — no screenshot tool) |
| Czech/multi-byte + astral text decoding | Bridge decode tests incl. a Czech string (`žluťoučký`) and an astral emoji surrogate pair (task 807/852) | **Pass** (UTF-8→UTF-16 decode) |

### Not verified — hardware-gated (task 851)

These are **implemented and FNA-faithful in code, but hardware-unverified** (no controller was
available). They are **not** claimed as fully verified:
- Gamepad **rumble** (`SetVibration`, `SetTriggerVibrationEXT`) — real haptic output.
- Gamepad **sensors** (`GetGyroEXT`, `GetAccelerometerEXT`) — live sensor values/orientation/units.
- Gamepad **light bar** (`SetLightBarEXT`).
- Gamepad **hotplug** with a real device (add / duplicate-add / remove / no-free-slots), analog
  triggers/sticks, button mapping on real hardware, `GetGUIDEXT` on a real controller.

### Not verified — platform/human-gated

- **Real IME composition** (task 852): the UTF-8→UTF-16 decode path (Czech diacritics, astral
  emoji) is unit-tested, and text-input activation is verified with a real window — but **actual
  IME composition and physical typing of Czech characters were not verified** (no IME, no human).
- **Wayland-specific** (task 853): `SDL_GetGlobalMouseState` returns `(0,0)` under this Wayland
  session (compositor security policy, confirmed task 783), so the OS-cursor *landing* pixel for
  `SetPosition` on a letterboxed window could not be read back here; the conversion itself is
  unit-tested and the basic warp was pixel-exact under X11/XWayland. Absolute cursor warp on native
  Wayland is compositor-constrained; **relative mouse mode (pointer lock) is the supported path**.
- **Touch** hardware, **four simultaneous controllers**, and the demo's **interactive** input
  (key highlighting, live gamepad panels) — no touch device / controllers / human present.

**Summary:** everything reachable headlessly or via an automated real-window check passed on all
three backends. The unverified items are strictly hardware- or human-gated, not gaps in the
implementation. Re-run this checklist on a machine with a controller, an IME, and (ideally) an X11
session to close the hardware/platform gaps.
