# Input — Manual / Platform Verification Results

> **Related input docs (INP-0003):** [plan](../plans/plan_input.md) · [backend](input-backend.md) · [FNA fidelity + deviations](input-fna-fidelity.md) · [member-parity matrix](input-member-parity-matrix.md) · [frozen API + tier glossary](input-public-api-frozen.md) · [test coverage](input-test-coverage.md) · [build & test](input-build-and-test.md) · [platform notes](platform-input-notes.md) · [manual results](input-manual-verification-results.md) · [demo checklist](demo-input-checklist.md)

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

> **Superseded (2026-07-05):** the counts above are the historical record for **2026-07-04** (pre-Phase-I15).
> The current authoritative baseline — full suite **3269 / 2 skipped**, canonical input filter **280** — is
> in `docs/input-build-and-test.md` (§Test counts). This entry is retained as-is for that date, not rewritten.

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

---

## Entry — 2026-07-06

| | |
|---|---|
| **Date** | 2026-07-06 |
| **OS** | Linux 6.12.90+deb13-amd64 (Debian 13) |
| **Display server** | Xvfb + `SDL_VIDEODRIVER=x11` (headless CI-equivalent) |
| **Graphics backend** | EasyGL (OpenGL ES, Mesa) |
| **Toolchain / SDL** | g++ 14.2.0 · CMake 3.31.6 · Ninja 1.12.1 · `third_party/SDL` @ `cbe3fbe9f367…` |
| **Controller / touchscreen / IME** | **None available** (headless) |

### Automated baseline (this build)
- Input filter `ctest -L input`: **314 tests, 100% green** under `--gtest_shuffle --gtest_repeat=5`.
- Full `CnaTests`: **3303 passed / 2 skipped**.
- ASan+UBSan (`cmake-build-input-asan`, `detect_leaks=0:halt_on_error=1`): input subset **314 green, 0 sanitizer errors**
- `cna_demo_input` (extended with the INP-0219/0220/0221 EXT paths) builds + smoke-launches crash-free under Xvfb (`timeout 4`, window created, EasyGL/OpenGL ES 3.2 initialized). Visual/hardware correctness of the new indicators is **not** verified (no display observed, no hardware). (the only leaks are third-party `libGLX_mesa`, not CNA input code).

### Still hardware/human-gated (unchanged from 2026-07-04, not verified here)
Real controller actuation (rumble / trigger-haptics / light bar / sensors / hotplug / GUID), real
touchscreen multi-touch + gestures, live IME composition, and non-US physical keyboards remain
**hardware-gated** — see the matrix below.

---

## Hardware verification matrix (INP-0215)

Status: ✅ verified · ⬜ not yet verified · n/a. Fill a cell only from a real run on that family, and
add the dated row to the log. All cells are currently ⬜ (no hardware available in the audit environment).

| Controller family | Buttons/DPad | Analog sticks/triggers | Rumble | Trigger haptics | Light bar | Gyro/Accel | GUID | Hotplug |
|-------------------|:---:|:---:|:---:|:---:|:---:|:---:|:---:|:---:|
| Xbox (XInput) | ⬜ | ⬜ | ⬜ | ⬜ | n/a | n/a | ⬜ (`xinput`) | ⬜ |
| PlayStation (DS4/DualSense) | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ | ⬜ (hex) | ⬜ |
| Nintendo Switch Pro | ⬜ | ⬜ | ⬜ | n/a | n/a | ⬜ | ⬜ | ⬜ |
| Generic / DirectInput | ⬜ | ⬜ | ⬜ | n/a | n/a | n/a | ⬜ | ⬜ |
| Bluetooth (any) | ⬜ | ⬜ | ⬜ | — | — | — | ⬜ | ⬜ (pair) |

| Other hardware | Multi-touch | Gestures | IME composition | Non-US layout |
|----------------|:---:|:---:|:---:|:---:|
| Touchscreen | ⬜ | ⬜ | — | — |
| IME (JP/CN/KR) | — | — | ⬜ | — |
| Czech keyboard | — | — | — | ⬜ |

## Recording a result (template — INP-0222)

Copy a new `## Entry — YYYY-MM-DD` block and fill it. Required fields per entry:
**Date · OS (kernel/distro) · Display server · Graphics backend · Toolchain + SDL rev · Hardware present.**
For each hardware check, record **Check · Method · Result (Pass/Fail + notes)**, and update the matrix
cell(s). Never mark a cell ✅ without a dated row backing it.

## Supported-controllers checklist (INP-0223)

- [ ] Xbox One / Series (XInput) — verify all 15 buttons, DPad, 2 sticks, 2 triggers, rumble, `GetGUIDEXT`=`xinput`, hotplug.
- [ ] PlayStation DualShock 4 — + light bar (`SetLightBarEXT`), trigger haptics off, gyro/accel.
- [ ] PlayStation DualSense — + adaptive-trigger haptics (`SetTriggerVibrationEXT`), light bar, sensors.
- [ ] Nintendo Switch Pro — button remap (SDL mapping), gyro.
- [ ] Generic / DirectInput pad — mapping via SDL `gamecontrollerdb`; unmapped devices not reported.
- [ ] Bluetooth controller — pairing + hotplug add/remove.

## Supported-OS checklist (INP-0224)

- [ ] Linux / X11 (or Xvfb) — cursor warp landing, relative capture, global mouse position. *(automated subset green here)*
- [ ] Linux / Wayland — relative mode (pointer lock); `SDL_GetGlobalMouseState` returns (0,0); warp focus-gated.
- [ ] Windows — XInput `GetGUIDEXT`=`xinput`, IME composition window, rumble/trigger-rumble/light-bar.
- [ ] macOS — Cocoa warp + global position; relative mode.
- [ ] Android — touch primary (device seen after first touch); on-screen keyboard; attached-HW gamepad.
- [ ] iOS — touch-only, no cursor; on-screen keyboard; MFi/BT gamepad.
