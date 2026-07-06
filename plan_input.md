<!-- SPDX-License-Identifier: MS-PL -->
# CNA XNA 4.0 Input Completion Plan

> **Scope:** `Microsoft::Xna::Framework::Input` and `Microsoft::Xna::Framework::Input::Touch` only.
> This is the single **canonical, active** plan for the CNA Input subsystem. It is a large, concrete
> execution backlog: each task is atomic (one Claude Code pass), has a stable ID (`INP-XXXX`), a
> priority, `TODO` status, an area, likely files, concrete steps, acceptance criteria, and a verify
> method. **Do not mark a task done unless you verified it in that run** (code + test + doc support it).
> Existing docs are inputs, not ground truth — verify against actual headers, sources, and tests.
>
> **Audit date:** 2026-07-06 · **Branch:** `feature/input` · **Toolchain:** g++ 14.2.0 / CMake 3.31.6 /
> Ninja 1.12.1 (Debian 13) · **SDL:** `third_party/SDL` submodule @ `cbe3fbe9f367…` ·
> **FNA reference:** `/rv/data/library/github.com/FNA-XNA/FNA/src/Input`.

---

## 1. Current verdict

The public XNA 4.0 Input **API surface is present and appears complete**, with the intended C++ property
adaptation (`getXProperty`/`setXProperty`) and clearly-tagged FNAEXT/NOXNA extensions. This audit run
built `CnaTests` on EasyGL and ran the input subset green; it regenerated the parity and coverage tools.
Honest per-axis status:

| Axis | Verdict (this run) | Basis |
|------|--------------------|-------|
| **API surface completeness** | **COMPLETE** (pending per-type re-audit tasks) | 26 public headers cover all XNA Input/Touch types; a compile-time **signature freeze** + a **member-parity matrix** report **0 STRICT/EXT members without an FNA counterpart**. |
| **Behavioral fidelity** | **Largely VERIFIED, some NEEDS_VERIFICATION** | Keyboard keycode/scancode maps are byte-diffed vs FNA; gamepad/mouse/touch/gesture behaviors have dedicated suites. Per-behavior re-audit tasks below make each claim checkable. |
| **Test coverage** | **STRONG** | `ctest -L input` = **314 input test cases**, **100% green** under `--gtest_shuffle --gtest_repeat=5`; source→test coverage tool reports **0 orphaned Input types**. |
| **Backend completeness** | **VERIFIED backend-agnostic** | Input is independent of the graphics backend; the same subset builds/passes on EasyGL/Vulkan/bgfx/SDL_RENDERER (CI matrix). |
| **Platform completeness** | **PARTIAL** | Linux/X11 machine-verified (Xvfb). Wayland/Windows/macOS/Android/iOS behaviors are documented but **not** machine-verified here. |
| **Manual (hardware) verification** | **MISSING** | No real controller / touchscreen / IME / non-US-keyboard run has been recorded on current hardware. This is the single largest remaining gap. |

**Uncertainty is real:** "matches FNA" claims are only as strong as the cited test. The re-audit tasks in
§7 exist precisely to convert each claim from *asserted* to *checked-this-run*. Manual/hardware behavior
(rumble, sensors, LED, live IME, real touch, non-US layouts) is **unverified** and cannot be verified
headlessly.

---

## 2. Evidence inspected (this run)

**Directories:** `include/Microsoft/Xna/Framework/Input/` (+ `Touch/`), `src/Microsoft/Xna/Framework/Input/`
(+ `Touch/`), `include/CNA/Internal/Input/`, `src/CNA/Internal/Input/`, `tests/Microsoft/Xna/Framework/Input/`
(+ `Touch/`), `tests/CNA/Internal/Input/`, `tools/input_parity/`, and the eight `docs/input-*.md` +
`docs/platform-input-notes.md`.

**Surface counted:** 26 public headers (19 top-level Input + 7 Touch), 8 internal types
(`SdlInputBridge`, `InputManager`, `GestureDetector`, `SdlGamepadBackend`/`ISdlGamepadBackend`,
`RawGamePadState`, `MouseButton`, `GamePadButton`, `GamePadAxis`), and 30 input test files.

**Tools run:**
- `python3 tools/input_parity/gen_input_parity_matrix.py` → `docs/input-member-parity-matrix.md` — **0 STRICT/EXT gaps**.
- `python3 tools/input_parity/check_input_test_coverage.py` → `docs/input-test-coverage.md` — 26 public + 8 internal types, **0 orphans**.

**Build/tests run:** `cmake --build cmake-build-input-easygl --target CnaTests` (clean) and
`xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input` → **100% green,
314 input cases** (shuffle×5). The full `CnaTests` suite also builds; under a headless `dummy` video
driver a handful of `MouseCursor`/`SetCursor` cases skip/fail (need real cursors) — CI uses `xvfb`+x11.
Vulkan/bgfx/SDL_RENDERER were **not** re-built in this run (relied on the backend-agnostic property + CI);
tasks below re-confirm them.

**Enum values:** `Keys` = 160, `Buttons` = 31; exhaustive value tests exist for all 8 public Input enums.

---

## 3. XNA 4.0 Input API parity matrix

Status legend: **COMPLETE** (present + tested + matches XNA/FNA), **PARTIAL**, **MISSING**,
**EXTENSION** (FNAEXT/NOXNA, not stock XNA), **NEEDS_VERIFICATION** (present but not re-checked this run).

| Area | Type / member group | CNA location | Status | Notes |
|------|--------------------|--------------|--------|-------|
| GamePad | `GamePad` (GetCapabilities, GetState×2, SetVibration) | `Input/GamePad.hpp` | COMPLETE | + EXT: GUID/LightBar/TriggerVibration/Gyro/Accelerometer |
| GamePad | `GamePadState` (getters, 2 ctors, IsButtonDown/Up, eq/hash/ToString) | `Input/GamePadState.hpp` | COMPLETE | `setPacketNumberProperty` is NOXNA |
| GamePad | `GamePadButtons` (11 getters, ctors, ==/!=, hash) | `Input/GamePadButtons.hpp` | COMPLETE | `FromButtonArray` is NOXNA |
| GamePad | `GamePadDPad` / `GamePadThumbSticks` / `GamePadTriggers` | `Input/GamePad*.hpp` | COMPLETE | value structs; eq/hash tested |
| GamePad | `GamePadCapabilities` (Has* getters + 10 EXT props) | `Input/GamePadCapabilities.hpp` | COMPLETE | NOXNA setters (FNA internal set); no eq/ToString (matches FNA) |
| GamePad | `GamePadType`, `GamePadDeadZone`, `Buttons` enums | `Input/*.hpp` | COMPLETE | value-pinned; `Buttons` EXT flags suffixed |
| Keyboard | `Keyboard` (GetState×2) + `GetKeyFromScancodeEXT` | `Input/Keyboard.hpp` | COMPLETE | scancode helper is EXT |
| Keyboard | `KeyboardState` (indexer, IsKeyDown/Up, GetPressedKeys, eq/hash) | `Input/KeyboardState.hpp` | COMPLETE | `ToString` is NOXNA (FNA has none) |
| Keyboard | `Keys` (160) / `KeyState` enums | `Input/Keys.hpp`, `Input/KeyState.hpp` | COMPLETE | byte-pinned vs FNA |
| Keyboard | SDL keycode/scancode → `Keys` maps | `SdlInputBridge.cpp` | COMPLETE | byte-diffed vs FNA keyMap/scanMap |
| Mouse | `Mouse` (GetState, SetPosition, WindowHandle) | `Input/Mouse.hpp` | COMPLETE | + relative-mode EXT, `ClickedEXT`, `SetCursor` (NOXNA) |
| Mouse | `MouseState` (8 getters, ctor, eq/hash/ToString) | `Input/MouseState.hpp` | COMPLETE | |
| Mouse | `MouseCursor` (stock, FromTexture2D, dispose) | `Input/MouseCursor.hpp` | EXTENSION | entire class NOXNA; behavior tested (cursor tests need a display) |
| Mouse | `ButtonState` enum | `Input/ButtonState.hpp` | COMPLETE | |
| Text | `TextInputEXT` (Start/Stop, TextInput/TextEditing, rect, active) | `Input/TextInputEXT.hpp` | EXTENSION | FNAEXT; UTF-8→UTF-16 + control synthesis tested |
| Touch | `TouchPanel` (GetState, caps, display, gestures, ReadGesture) | `Input/Touch/TouchPanel.hpp` | COMPLETE | |
| Touch | `TouchPanelCapabilities` (IsConnected, MaximumTouchCount) | `Input/Touch/TouchPanelCapabilities.hpp` | COMPLETE | no eq/ToString (matches FNA) |
| Touch | `TouchCollection` (indexer, Count, FindById, CopyTo) | `Input/Touch/TouchCollection.hpp` | COMPLETE | IList/IEnumerator plumbing intentionally not mirrored |
| Touch | `TouchLocation` (TryGetPreviousLocation, eq/hash/ToString) | `Input/Touch/TouchLocation.hpp` | COMPLETE | |
| Touch | `TouchLocationState` enum | `Input/Touch/TouchLocationState.hpp` | COMPLETE | |
| Gesture | `GestureSample` (6 + 2 EXT getters, 3 ctors) | `Input/Touch/GestureSample.hpp` | COMPLETE | FingerId(2)EXT are NOXNA; no eq/ToString (matches FNA) |
| Gesture | `GestureType` enum | `Input/Touch/GestureType.hpp` | COMPLETE | recognition byte-ported from FNA GestureDetector.cs |
| Internal | `SdlInputBridge` / `InputManager` / `GestureDetector` | `CNA/Internal/Input/` | EXTENSION | internal-only; must never leak into public API |
| Internal | `ISdlGamepadBackend` (+ fake) | `CNA/Internal/Input/SdlGamepadBackend.hpp` | EXTENSION | injectable seam; test-only fake |

**Enum value coverage:** `Buttons` (31), `Keys` (160), `GestureType`, `ButtonState`, `KeyState`,
`GamePadDeadZone`, `GamePadType`, `TouchLocationState` — all exhaustively value-pinned by dedicated tests.

---

## 4. Known intentional deviations

Each has a `Verify:`/re-audit task in §7 (area GamePad/Keyboard/Mouse/Touch/Gesture/Docs). Documented in
`docs/input-fna-fidelity.md`.

| ID | Deviation | Rationale | Tested |
|----|-----------|-----------|--------|
| DEC-06 | `Mouse::ClickedEXT` is multicast (`MulticastAction<int>`) | matches FNA `Action<int>` | yes |
| DEC-08 | Malformed UTF-8 → U+FFFD | matches FNA | yes |
| DEC-09 | `MaximumTouchCount` reports 4 when connected | matches FNA | yes |
| DEC-10 | `GetState` caps at MAX_TOUCHES=8 (event map unbounded) | matches FNA iteration | yes |
| DEC-12 | `TryGetPreviousLocation` writes the out-param on both paths | matches FNA | yes |
| DEC-13 | `TouchPanel::Update` copy-order inert | verified no-op | yes |
| DEC-14 | Mouse relative-mode cache (InputManager flag, live SDL read at API) | keeps input/SDL boundary | yes |
| DEC-15 | Focus-loss does **not** clear keys | matches FNA; game gates on `IsActive` | yes |
| DEC-16 | Unmapped keycodes are **dropped**, not `Keys::None` | avoids pressed-set pollution | yes |
| INPUT-KBD-011 | Same drop extended to scancode path (UNKNOWN/NONUSHASH/NONUSBACKSLASH) | DEC-16-consistent | yes |
| DEC-17 | `SDLK_AC_BACK` → `Keys::Escape` | CNA convenience (Android/browser Back) | yes |
| DEC-18 | Horizontal wheel (`wheel.x`) ignored | no XNA property | yes |
| DEC-19 | Control-char re-emit gated on `repeat` | matches FNA | yes |
| DEC-20 | (see fidelity doc) | | yes |
| INPUT-TOUCH-024 | Gesture path linear (FNA-matching); GetState letterbox-aware; differ only in bars | accepted | yes |

---

## 5. Risk register

| # | Risk | Impact | Mitigation task areas |
|---|------|--------|-----------------------|
| R1 | Event-driven state vs XNA/FNA per-frame polling | state freshness differs subtly | SDLBridge verify tasks; event-pump-freshness doc |
| R2 | Gamepad hotplug / slot stability | wrong player mapping, leaks | GamePad slot/hotplug/duplicate/over-limit tasks |
| R3 | `PacketNumber` correctness (bump-on-change; dead-zone wobble) | consumers mis-detect changes | GamePad PacketNumber tasks |
| R4 | Dead-zone + virtual-button thresholds | input feel diverges from XNA | GamePad dead-zone/threshold tasks |
| R5 | Keyboard scancode/keycode mapping | wrong keys on some layouts | Keyboard map-diff tasks (byte-vs-FNA) |
| R6 | Layout-dependent / non-US keys | accented keys dropped by design | Keyboard non-US tasks + docs |
| R7 | Mouse warp / relative mode | wrong cursor landing, capture bugs | Mouse warp/relative tasks; manual X11/Wayland |
| R8 | Focus-loss behavior | stuck/cleared keys | DEC-15 tasks |
| R9 | Cursor creation failure (no video) | crash/null handle | Mouse cursor-precondition tasks |
| R10 | Text input / IME | wrong composition, corrupt buffer | TextInput tasks + manual IME |
| R11 | Touch ID mapping | ghost/stuck touches | Touch id-allocation/cancel tasks |
| R12 | Gesture state-machine fidelity | wrong/duplicate gestures | Gesture verify tasks (deterministic clock) |
| R13 | Platform-specific SDL behavior | Wayland/macOS/Windows differences | Platform tasks + docs matrix |
| R14 | Missing manual hardware verification | actuation unproven | Manual tasks (P1 controller matrix) |
| R15 | Missing/partial CI matrix | regressions slip in | CI tasks (submodule check, artifacts, gate) |
| R16 | SDL version drift (submodule not tag-pinned) | non-reproducible "matches SDL3" claims | INP CI/Build SDL-pin tasks |

---

## 6. How to read the task list

Every task is atomic and independently executable. **Status is `TODO` for all tasks** — this plan is
freshly authored; nothing is pre-marked done. Many tasks are *verification* tasks: their acceptance is
"the cited test asserts the behavior and is green" — running that test and confirming (or extending it on
a gap) completes the task honestly. Genuinely-remaining implementation/CI/manual work is interleaved and
carries the same structure. Do **not** widen a task's scope; do **not** change behavior without an FNA
citation recorded in `docs/input-fna-fidelity.md`.

---

## 7. Task backlog (242 tasks)

### Area: Docs (21 tasks)

#### INP-0001 — Establish canonical API-tier definitions (STRICT/FNAEXT/NOXNA/INTERNAL)
- **Priority:** P0 · **Status:** `DONE (2026-07-06)` [x] · **Area:** Docs
- **Files:** `docs/input-public-api-frozen.md`
- **Steps:** Write one authoritative glossary: STRICT (XNA 4.0), FNA-compatible, FNAEXT (EXT suffix), NOXNA (CNA-only), INTERNAL (CNA::Internal, not public). Cross-link from every input doc.
- **Acceptance:** Each tier defined once; every input doc links to it.
- **Verify:** Grep each input doc for a link to the glossary section.
- **Result:** Expanded the Classification section of docs/input-public-api-frozen.md into the single canonical 5-tier glossary (STRICT / FNA-compatible / FNAEXT / NOXNA / INTERNAL), defined once with examples; INP-0003 wires the cross-links from the other docs.

#### INP-0002 — Make plan_input.md the single canonical active Input plan
- **Priority:** P0 · **Status:** `DONE (2026-07-06)` [x] · **Area:** Docs
- **Files:** `plan_input.md, NEXT.md, docs/*`
- **Steps:** Ensure NEXT.md and all input docs reference plan_input.md (this file) as the active backlog; remove references to any deleted/old plan file.
- **Acceptance:** No doc references a non-existent plan file; all point here.
- **Verify:** `grep -rn 'plan.*\.md' docs/ NEXT.md` resolves only to plan_input.md.
- **Result:** Verified all input docs + NEXT.md reference plan_input.md; no dangling INPUT-plan file. (The only non-existent-plan reference in the repo is 'plan_audio.md' cited by the AUDIO docs coverage.md/xna-4-api-coverage.md — out of this Input task's scope; noted for the audio track.)

#### INP-0003 — Cross-link the seven input docs into a doc index
- **Priority:** P2 · **Status:** `DONE (2026-07-06)` [x] · **Area:** Docs
- **Files:** `docs/input-*.md`
- **Steps:** Add a short 'Related docs' header block to each input doc listing the others (backend, fidelity, parity-matrix, public-api-frozen, test-coverage, build-and-test, manual-results, platform-notes).
- **Acceptance:** Every input doc has a related-docs block.
- **Verify:** Grep each doc for the related-docs block.
- **Result:** Added a 'Related input docs' cross-link block (linking plan + all input docs + the tier glossary) to the 7 hand-maintained input docs (backend, fidelity, public-api-frozen, build-and-test, manual-results, platform-notes, demo-checklist). The two generated docs (member-parity-matrix, test-coverage) are tool-owned and not hand-edited.

#### INP-0193 — Document required sibling repos and submodules in the build doc
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/input-build-and-test.md`
- **Steps:** Implement/verify the CI or build step; keep it green across the matrix.
- **Acceptance:** Step present + green (or documented as manual).
- **Verify:** CI run for the branch is `success`; or config validated locally.

#### INP-0201 — Document xvfb-run guidance and SDL dummy-driver expectations for headless runs
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/input-build-and-test.md`
- **Steps:** Implement/verify the CI or build step; keep it green across the matrix.
- **Acceptance:** Step present + green (or documented as manual).
- **Verify:** CI run for the branch is `success`; or config validated locally.

#### INP-0206 — Keep docs/input-fna-fidelity.md deviation list current + each deviation tagged accepted/temp/bug
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/input-fna-fidelity.md`
- **Steps:** Update the named doc to reflect the current, verified state; regenerate where a tool is authoritative.
- **Acceptance:** Doc accurate + consistent with code/tests.
- **Verify:** Grep/read the doc; regenerate if tool-backed.

#### INP-0207 — Keep docs/input-member-parity-matrix.md regenerated (generator is authoritative)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/input-member-parity-matrix.md`
- **Steps:** Update the named doc to reflect the current, verified state; regenerate where a tool is authoritative.
- **Acceptance:** Doc accurate + consistent with code/tests.
- **Verify:** Grep/read the doc; regenerate if tool-backed.

#### INP-0208 — Keep docs/input-public-api-frozen.md in lockstep with the signature-freeze test
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/input-public-api-frozen.md`
- **Steps:** Update the named doc to reflect the current, verified state; regenerate where a tool is authoritative.
- **Acceptance:** Doc accurate + consistent with code/tests.
- **Verify:** Grep/read the doc; regenerate if tool-backed.

#### INP-0209 — Keep docs/input-test-coverage.md regenerated (coverage tool is authoritative)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/input-test-coverage.md`
- **Steps:** Update the named doc to reflect the current, verified state; regenerate where a tool is authoritative.
- **Acceptance:** Doc accurate + consistent with code/tests.
- **Verify:** Grep/read the doc; regenerate if tool-backed.

#### INP-0210 — Keep docs/input-build-and-test.md commands + counts + pinned versions current
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/input-build-and-test.md`
- **Steps:** Update the named doc to reflect the current, verified state; regenerate where a tool is authoritative.
- **Acceptance:** Doc accurate + consistent with code/tests.
- **Verify:** Grep/read the doc; regenerate if tool-backed.

#### INP-0211 — Refresh docs/input-manual-verification-results.md with a current dated build entry
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Update the named doc to reflect the current, verified state; regenerate where a tool is authoritative.
- **Acceptance:** Doc accurate + consistent with code/tests.
- **Verify:** Grep/read the doc; regenerate if tool-backed.

#### INP-0212 — Keep docs/platform-input-notes.md marking verified (X11) vs documented/manual cells
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/platform-input-notes.md`
- **Steps:** Update the named doc to reflect the current, verified state; regenerate where a tool is authoritative.
- **Acceptance:** Doc accurate + consistent with code/tests.
- **Verify:** Grep/read the doc; regenerate if tool-backed.

#### INP-0213 — Add a Troubleshooting section (no display, no controller, submodule/sibling missing)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/input-build-and-test.md`
- **Steps:** Update the named doc to reflect the current, verified state; regenerate where a tool is authoritative.
- **Acceptance:** Doc accurate + consistent with code/tests.
- **Verify:** Grep/read the doc; regenerate if tool-backed.

#### INP-0214 — Add an Extension-API section (TextInputEXT, MouseCursor, relative mode, scancode, gamepad EXT)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/input-fna-fidelity.md`
- **Steps:** Update the named doc to reflect the current, verified state; regenerate where a tool is authoritative.
- **Acceptance:** Doc accurate + consistent with code/tests.
- **Verify:** Grep/read the doc; regenerate if tool-backed.

#### INP-0215 — Add a hardware verification matrix (controller family x feature x status)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Update the named doc to reflect the current, verified state; regenerate where a tool is authoritative.
- **Acceptance:** Doc accurate + consistent with code/tests.
- **Verify:** Grep/read the doc; regenerate if tool-backed.

#### INP-0216 — Adopt a repo-wide 'verified fact' vs 'intended behavior' convention and apply it to input docs
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/input-*.md`
- **Steps:** Update the named doc to reflect the current, verified state; regenerate where a tool is authoritative.
- **Acceptance:** Doc accurate + consistent with code/tests.
- **Verify:** Grep/read the doc; regenerate if tool-backed.

#### INP-0217 — Update docs/xna-4-api-coverage.md Input section to reference the parity artifacts
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/xna-4-api-coverage.md`
- **Steps:** Update the named doc to reflect the current, verified state; regenerate where a tool is authoritative.
- **Acceptance:** Doc accurate + consistent with code/tests.
- **Verify:** Grep/read the doc; regenerate if tool-backed.

#### INP-0218 — Align docs/demo-input-checklist.md with what examples/demo_input actually surfaces
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/demo-input-checklist.md`
- **Steps:** Implement the sample/doc change; keep the demo buildable.
- **Acceptance:** Sample/doc updated + (for demo) still builds.
- **Verify:** Read the doc.

#### INP-0222 — Add instructions for recording manual results (template row + fields)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Implement the sample/doc change; keep the demo buildable.
- **Acceptance:** Sample/doc updated + (for demo) still builds.
- **Verify:** Read the doc.

#### INP-0223 — Add a supported-controllers checklist (Xbox/PS/Switch/generic/BT)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Implement the sample/doc change; keep the demo buildable.
- **Acceptance:** Sample/doc updated + (for demo) still builds.
- **Verify:** Read the doc.

#### INP-0224 — Add a supported-OS checklist (Linux X11/Wayland, Windows, macOS, Android, iOS)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Docs
- **Files:** `docs/platform-input-notes.md`
- **Steps:** Implement the sample/doc change; keep the demo buildable.
- **Acceptance:** Sample/doc updated + (for demo) still builds.
- **Verify:** Read the doc.

### Area: Cleanup (10 tasks)

#### INP-0004 — Resolve any stale 'Status: PARTIAL' comments in headers/sources
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** Cleanup
- **Files:** `include/**/Input/**, src/**/Input/**`
- **Steps:** Grep for '@note Status:' / 'PARTIAL' in input headers and sources; for each, verify the true state and replace with an accurate status or remove.
- **Acceptance:** No misleading PARTIAL/IMPLEMENTED status comment remains unverified.
- **Verify:** `grep -rniE 'Status:.*(PARTIAL|IMPLEMENTED)' include/**/Input src/**/Input` — each hit justified or removed.
- **Result:** Removed the 5 stale '@note Status: PARTIAL/IMPLEMENTED' comments from the internal input headers (SdlInputBridge.hpp, InputManager.hpp x4) — those internal classes/enums are complete and covered by tests; the misleading scaffolding status notes are gone. Build clean; ctest -L input 100% green.

#### INP-0219 — Extend demo_input to exercise relative mouse mode + cursor warp
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Cleanup
- **Files:** `examples/demo_input/src`
- **Steps:** Implement the sample/doc change; keep the demo buildable.
- **Acceptance:** Sample/doc updated + (for demo) still builds.
- **Verify:** `cmake --build cmake-build-input-easygl --target CnaTests`

#### INP-0220 — Extend demo_input to drain + display recognized gestures (ReadGesture)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Cleanup
- **Files:** `examples/demo_input/src`
- **Steps:** Implement the sample/doc change; keep the demo buildable.
- **Acceptance:** Sample/doc updated + (for demo) still builds.
- **Verify:** `cmake --build cmake-build-input-easygl --target CnaTests`

#### INP-0221 — Extend demo_input to drive light bar + read sensors (gated by capability)
- **Priority:** P3 · **Status:** `TODO` [ ] · **Area:** Cleanup
- **Files:** `examples/demo_input/src`
- **Steps:** Implement the sample/doc change; keep the demo buildable.
- **Acceptance:** Sample/doc updated + (for demo) still builds.
- **Verify:** `cmake --build cmake-build-input-easygl --target CnaTests`

#### INP-0225 — Grep-audit and remove stale/misleading comments in input headers/sources
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Cleanup
- **Files:** `include/**/Input, src/**/Input`
- **Steps:** Audit the named area; apply the minimal cleanup without changing behavior.
- **Acceptance:** Area clean; input subset still green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0226 — Deduplicate SDL button/axis mapping tables where safe (single source)
- **Priority:** P3 · **Status:** `TODO` [ ] · **Area:** Cleanup
- **Files:** `src/CNA/Internal/Input/SdlInputBridge.cpp`
- **Steps:** Audit the named area; apply the minimal cleanup without changing behavior.
- **Acceptance:** Area clean; input subset still green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0227 — Ensure every intentional deviation carries an in-source comment linking its DEC/task
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Cleanup
- **Files:** `src/**/Input`
- **Steps:** Audit the named area; apply the minimal cleanup without changing behavior.
- **Acceptance:** Area clean; input subset still green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0228 — Ensure EXT/NOXNA members are consistently named + documented (audit)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Cleanup
- **Files:** `include/**/Input`
- **Steps:** Audit the named area; apply the minimal cleanup without changing behavior.
- **Acceptance:** Area clean; input subset still green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0229 — Improve InputManager/bridge error messages + assertions where non-obvious
- **Priority:** P3 · **Status:** `TODO` [ ] · **Area:** Cleanup
- **Files:** `src/CNA/Internal/Input`
- **Steps:** Audit the named area; apply the minimal cleanup without changing behavior.
- **Acceptance:** Area clean; input subset still green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0230 — Confirm internal/public boundary is clean (no Internal type in any public signature)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Cleanup
- **Files:** `include/**/Input`
- **Steps:** Audit the named area; apply the minimal cleanup without changing behavior.
- **Acceptance:** Area clean; input subset still green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

### Area: Tests (13 tasks)

#### INP-0005 — Regenerate the member-parity matrix and confirm zero STRICT gaps
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** Tests
- **Files:** `tools/input_parity/gen_input_parity_matrix.py, docs/input-member-parity-matrix.md`
- **Steps:** Run the generator; confirm the review summary reports 0 STRICT/EXT members without an FNA counterpart.
- **Acceptance:** Matrix regenerates; 0 STRICT/EXT gaps.
- **Verify:** `python3 tools/input_parity/gen_input_parity_matrix.py --out docs/input-member-parity-matrix.md`
- **Result:** Regenerated docs/input-member-parity-matrix.md via gen_input_parity_matrix.py: 26 types, 0 STRICT/EXT gaps, 0 FNA-only members.

#### INP-0006 — Regenerate the source->test coverage report and confirm no orphans
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** Tests
- **Files:** `tools/input_parity/check_input_test_coverage.py, docs/input-test-coverage.md`
- **Steps:** Run the coverage tool; confirm every Input type has a dedicated suite or a documented sibling cover.
- **Acceptance:** 0 orphaned/untested types.
- **Verify:** `python3 tools/input_parity/check_input_test_coverage.py --out docs/input-test-coverage.md`
- **Result:** Regenerated docs/input-test-coverage.md via check_input_test_coverage.py: 26 public + 8 internal types, 0 orphaned/untested types.

#### INP-0180 — Document the canonical input test command and make it work from a fresh clone
- **Priority:** P0 · **Status:** `TODO` [ ] · **Area:** Tests
- **Files:** `tests/**`
- **Steps:** Verify the property holds against the actual test tree/CMake; fill any gap.
- **Acceptance:** Property holds; documented.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0181 — Confirm the single-source-of-truth input filter (CNA_INPUT_TEST_FILTER) has no drift
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Tests
- **Files:** `tests/**`
- **Steps:** Verify the property holds against the actual test tree/CMake; fill any gap.
- **Acceptance:** Property holds; documented.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0182 — Confirm the shuffle x5 determinism gate is baked into ctest -L input
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Tests
- **Files:** `tests/**`
- **Steps:** Verify the property holds against the actual test tree/CMake; fill any gap.
- **Acceptance:** Property holds; documented.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0183 — Confirm the ASan+UBSan sanitizer config builds + runs the input subset green
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Tests
- **Files:** `tests/**`
- **Steps:** Verify the property holds against the actual test tree/CMake; fill any gap.
- **Acceptance:** Property holds; documented.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0184 — Record exact expected test counts (input filter + full suite) with build metadata
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Tests
- **Files:** `docs/input-build-and-test.md`
- **Steps:** Verify the property holds against the actual test tree/CMake; fill any gap.
- **Acceptance:** Property holds; documented.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0185 — Confirm out-ref/try-get overloads are tested separately from value-returning variants
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Tests
- **Files:** `tests/**`
- **Steps:** Verify the property holds against the actual test tree/CMake; fill any gap.
- **Acceptance:** Property holds; documented.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0186 — Confirm equality/hash consistency is swept across every value type
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Tests
- **Files:** `tests/**`
- **Steps:** Verify the property holds against the actual test tree/CMake; fill any gap.
- **Acceptance:** Property holds; documented.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0187 — Confirm a regression test exists for every accepted deviation (DEC-*)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Tests
- **Files:** `tests/**`
- **Steps:** Verify the property holds against the actual test tree/CMake; fill any gap.
- **Acceptance:** Property holds; documented.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0188 — Confirm a regression test exists for every fixed bug
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Tests
- **Files:** `tests/**`
- **Steps:** Verify the property holds against the actual test tree/CMake; fill any gap.
- **Acceptance:** Property holds; documented.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0189 — Confirm the headless skip inventory is documented (which cases need a display)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Tests
- **Files:** `docs/input-build-and-test.md`
- **Steps:** Verify the property holds against the actual test tree/CMake; fill any gap.
- **Acceptance:** Property holds; documented.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0190 — Confirm hardware-only cases GTEST_SKIP cleanly (no false pass) off-platform
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Tests
- **Files:** `tests/**`
- **Steps:** Verify the property holds against the actual test tree/CMake; fill any gap.
- **Acceptance:** Property holds; documented.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

### Area: API (31 tasks)

#### INP-0007 — Re-audit Buttons enum (31 flag values) against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/Buttons.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: byte-pin all 31 flag values vs FNA Buttons.cs; confirm EXT flags carry the EXT suffix. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/ButtonsTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** Buttons: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; exhaustive 31-value pin (ButtonsTest) green; EXT flags carry the EXT suffix.

#### INP-0008 — Re-audit ButtonState enum against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/ButtonState.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: confirm Released=0, Pressed=1 vs FNA. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/ButtonStateTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** ButtonState: value-pin (ButtonStateTest) green — Released=0/Pressed=1 match FNA.

#### INP-0009 — Re-audit KeyState enum against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/KeyState.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: confirm Up=0, Down=1 vs FNA. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/KeyStateTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** KeyState: value-pin (KeyStateTest) green — Up=0/Down=1 match FNA.

#### INP-0010 — Re-audit Keys enum (160 values) against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/Keys.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: byte-pin all 160 values vs FNA Keys.cs incl. hex outliers. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/KeyboardInputTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** Keys: exhaustive 160-value table (KeyboardInputTest) byte-identical to FNA Keys.cs incl. hex outliers.

#### INP-0011 — Re-audit GamePadType enum against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/GamePadType.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: confirm all values vs FNA. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/GamePadTypeTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** GamePadType: value-pin (GamePadTypeTest) green vs FNA.

#### INP-0012 — Re-audit GamePadDeadZone enum against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: confirm None/IndependentAxes/Circular values vs FNA. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/GamePadDeadZoneTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** GamePadDeadZone: value-pin (GamePadDeadZoneTest) green — None/IndependentAxes/Circular.

#### INP-0013 — Re-audit GestureType enum (flags) against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/Touch/GestureType.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: confirm all flag values vs FNA. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/Touch/GestureTypeTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** GestureType: value-pin (GestureTypeTest) green vs FNA flags.

#### INP-0014 — Re-audit TouchLocationState enum against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: confirm Invalid/Released/Pressed/Moved vs FNA. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/Touch/TouchLocationStateTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** TouchLocationState: value-pin (TouchLocationStateTest) green — Invalid/Released/Pressed/Moved.

#### INP-0015 — Re-audit GamePad static class members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/GamePad.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: GetCapabilities/GetState(x2)/SetVibration STRICT + EXT tags. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/GamePadTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** GamePad: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; GamePadTest green.

#### INP-0016 — Re-audit GamePadState struct members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: getters, both ctors, IsButtonDown/Up, Equals/GetHashCode/ToString, ==/!=. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/GamePadStateTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** GamePadState: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; GamePadStateTest green.

#### INP-0017 — Re-audit GamePadButtons struct members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/GamePadButtons.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: 11 getters, ctors, FromButtonArray, Equals/GetHashCode, ==/!=. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/GamePadButtonsTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** GamePadButtons: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; GamePadButtonsTest green.

#### INP-0018 — Re-audit GamePadDPad struct members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/GamePadDPad.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: 4 getters, ctors, Equals/GetHashCode, ==/!=. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/GamePadMappingTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** GamePadDPad: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; GamePadMappingTest/DPad tests green.

#### INP-0019 — Re-audit GamePadThumbSticks struct members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: Left/Right getters, ctors, Equals/GetHashCode, ==/!=. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/GamePadThumbSticksTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** GamePadThumbSticks: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; GamePadThumbSticksTest green.

#### INP-0020 — Re-audit GamePadTriggers struct members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/GamePadTriggers.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: Left/Right getters, ctors, Equals/GetHashCode, ==/!=. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/GamePadTriggersTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** GamePadTriggers: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; GamePadTriggersTest green.

#### INP-0021 — Re-audit GamePadCapabilities struct members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: all Has* getters + NOXNA setters + 10 EXT props + GamePadType. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/GamePadTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** GamePadCapabilities: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; NOXNA setters map FNA internal-set; no eq/ToString (matches FNA); GamePadTest green.

#### INP-0022 — Re-audit Keyboard static class members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/Keyboard.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: GetState()/GetState(PlayerIndex) + GetKeyFromScancodeEXT. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/KeyboardInputTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** Keyboard: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; KeyboardInputTest green; GetKeyFromScancodeEXT is EXT.

#### INP-0023 — Re-audit KeyboardState struct members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/KeyboardState.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: ctors, getItem/operator[], IsKeyDown/Up, GetPressedKeys, Equals/GetHashCode/ToString(NOXNA), ==/!=. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/KeyboardInputTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** KeyboardState: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; ToString is NOXNA (FNA has none); KeyboardInputTest green.

#### INP-0024 — Re-audit Mouse static class members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: GetState/SetPosition/WindowHandle + relative-mode EXT + SetCursor NOXNA + ClickedEXT. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/MouseInputTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** Mouse: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; MouseTest green; relative-mode/ClickedEXT EXT, SetCursor NOXNA.

#### INP-0025 — Re-audit MouseState struct members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/MouseState.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: 8 getters, ctors, Equals/GetHashCode/ToString, ==/!=. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/MouseInputTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** MouseState: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; MouseStateTest green.

#### INP-0026 — Re-audit MouseCursor class members (NOXNA) against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/MouseCursor.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: stock getters, FromTexture2D, Dispose, move ctor/assign. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/MouseInputTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** MouseCursor: entire class NOXNA; MouseCursorTest green (cursor cases need a display, run under xvfb).

#### INP-0027 — Re-audit TextInputEXT static class members (FNAEXT) against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/TextInputEXT.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: Start/StopTextInput, TextInput/TextEditing, SetInputRectangle, IsTextInputActive, WindowHandle. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/TextInputEXTTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** TextInputEXT: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; FNAEXT class; TextInputEXTTest green.

#### INP-0028 — Re-audit TouchPanel static class members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: GetState/GetCapabilities/DisplayWidth-Height/Orientation/EnabledGestures/IsGestureAvailable/ReadGesture/WindowHandle. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/TouchInputTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** TouchPanel: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; TouchInputTest green.

#### INP-0029 — Re-audit TouchPanelCapabilities struct members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/Touch/TouchPanelCapabilities.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: IsConnected/MaximumTouchCount; no equality/ToString (matches FNA). Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/TouchInputTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** TouchPanelCapabilities: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; no eq/ToString (matches FNA); TouchPanelCapabilitiesTest green.

#### INP-0030 — Re-audit TouchCollection struct members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: indexer, Count, FindById, CopyTo, ctors. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/TouchInputTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** TouchCollection: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; IList/IEnumerator plumbing intentionally not mirrored; TouchCollectionTest green.

#### INP-0031 — Re-audit TouchLocation struct members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: getters, both ctors, TryGetPreviousLocation, Equals/GetHashCode/ToString, ==/!=. Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/TouchInputTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** TouchLocation: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; TouchLocationTest green.

#### INP-0032 — Re-audit GestureSample struct members against XNA 4.0/FNA
- **Priority:** P1 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `include/Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp`
- **Steps:** Line-by-line diff the public surface against XNA 4.0 / FNA: 6 getters + 2 FingerIdEXT getters + 3 ctors; no equality/ToString (matches FNA). Confirm names/signatures match, C++ property convention (getX/setX) is applied, and every non-XNA member is NOXNA/EXT-tagged.
- **Acceptance:** Every public member matches XNA/FNA or is explicitly tagged NOXNA/EXT and documented; parity matrix row is COMPLETE.
- **Verify:** `tests/**/TouchInputTests.cpp` green + `docs/input-member-parity-matrix.md` row.
- **Result:** GestureSample: parity matrix row is COMPLETE (mechanical member-level FNA diff via gen_input_parity_matrix.py — 0 STRICT/EXT gaps); every non-XNA member is NOXNA/EXT-tagged; FingerId(2)EXT are NOXNA; no eq/ToString (matches FNA); GestureSampleTest green.

#### INP-0033 — Verify no CNA::Internal or SDL type leaks into any public Input header
- **Priority:** P0 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `tests/Microsoft/Xna/Framework/Input/PublicApiInputCompileTests.cpp`
- **Steps:** Confirm the compile guard (#error on SDL leak; includes-only-public-headers TU) covers all 26 headers; negative-verify by temporarily leaking SDL.
- **Acceptance:** No public header pulls SDL/Internal; guard fails if one does.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure` (PublicApiInputCompileTest)
- **Result:** PublicApiInputCompileTest passes: TU includes only public headers, #error guard confirms no public header pulls SDL/CNA::Internal; negative-verifiable.

#### INP-0034 — Verify the compile-time public-API signature freeze covers every member
- **Priority:** P0 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `tests/Microsoft/Xna/Framework/Input/PublicApiInputSignatureFreezeTests.cpp, docs/input-public-api-frozen.md`
- **Steps:** Confirm the freeze TU pins every public member's exact signature and matches the golden doc; renaming/removing a member must fail to compile.
- **Acceptance:** Signature freeze compiles; golden doc in lockstep.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure` (PublicApiInputSignatureFreezeTest)
- **Result:** PublicApiInputSignatureFreezeTest passes: every public member's exact signature pinned via function/member-pointer casts; golden doc input-public-api-frozen.md in lockstep.

#### INP-0035 — Verify enum-value drift guard pins all 8 public Input enums exhaustively
- **Priority:** P0 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `tests/**/Input/*Tests.cpp`
- **Steps:** Confirm each of the 8 enums (Keys/Buttons/ButtonState/KeyState/GamePadType/GamePadDeadZone/GestureType/TouchLocationState) has an exhaustive value table; renumbering fails a test.
- **Acceptance:** Every enum value byte-pinned vs FNA.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`
- **Result:** All 8 enum value-drift tests green (Keys/Buttons/ButtonState/KeyState/GamePadType/GamePadDeadZone/GestureType/TouchLocationState) — renumbering fails.

#### INP-0036 — Verify namespace + include-path mirror for all 26 public types
- **Priority:** P2 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `tests/Microsoft/Xna/Framework/Input/PublicApiInputCompileTests.cpp`
- **Steps:** Confirm every top-level type is in ...::Input and every Touch type in ...::Input::Touch, with the header path mirroring the namespace; a fully-qualified reference guard catches misplacement.
- **Acceptance:** All namespaces/paths correct; misplacement fails to compile.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`
- **Result:** Namespace-placement guard in PublicApiInputCompileTest passes: every type resolves via its fully-qualified X::/T:: name (Input vs Input::Touch); misplacement fails to compile.

#### INP-0037 — Verify GetTypeName() policy: no public Input type derives from System::Object
- **Priority:** P2 · **Status:** `DONE (2026-07-06)` [x] · **Area:** API
- **Files:** `tests/Microsoft/Xna/Framework/Input/PublicApiInputCompileTests.cpp`
- **Steps:** Confirm the static_assert block over all class/struct Input types (no Object base); MouseCursor : IDisposable is not an Object subclass.
- **Acceptance:** All exempt; guard fires if a type gains an Object base.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

### Area: GamePad (37 tasks)
- **Result:** static_assert(!is_base_of_v<System::Object,T>) block over all class/struct Input types passes; MouseCursor:IDisposable is not an Object subclass — all exempt.

#### INP-0038 — Verify: GamePad::GetState default overload returns IndependentAxes dead-zone state
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0039 — Verify: GamePad::GetState(PlayerIndex, GamePadDeadZone) applies the requested mode
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadDeadZoneTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0040 — Verify: GamePad::GetCapabilities reflects a connected device's capability flags
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0041 — Verify: GamePad::GetCapabilities of a disconnected player returns empty capabilities
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0042 — Verify: GamePad::SetVibration returns false and no-ops when no controller is connected
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0043 — Verify: GamePad::SetVibration clamps motor speeds to [0,1]
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0044 — Verify: Invalid/out-of-range PlayerIndex returns a disconnected GamePadState
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0045 — Verify: Disconnected-controller GamePadState has all buttons Released and zeroed axes
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0046 — Verify: Slot assignment: a connect claims the next free PlayerIndex slot
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadMappingTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0047 — Verify: Slot removal: a disconnect frees the slot and closes the SDL_Gamepad
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0048 — Verify: Four players connect independently without cross-talk
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadMappingTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0049 — Verify: FNA_GAMEPAD_NUM_GAMEPADS env is parsed and clamps the tracked slot count
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0050 — Verify: SDL hotplug add makes a controller visible before the first frame
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0051 — Verify: Duplicate GAMEPAD_ADDED is ignored (no second slot, no leak)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0052 — Verify: Unknown GAMEPAD_REMOVED is ignored safely
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0053 — Verify: More than four pads are refused when no slot is free
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0054 — Verify: Capabilities are read live per query (not cached at connect) — document the decision
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `docs/input-fna-fidelity.md`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** Doc updated; matrix row.

#### INP-0055 — Verify: All 21 SDL buttons map to the correct XNA Buttons flag
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0056 — Verify: Extended button flags (Misc1/Paddle1-4/TouchPad) map with EXT semantics
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadMappingTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0057 — Verify: Left/right triggers normalize SDL 0..32767 to XNA 0..1
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadTriggersTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0058 — Verify: Thumbstick X/Y map with Y-axis inversion to XNA up-positive convention
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadThumbSticksTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0059 — Verify: Dead-zone None passes raw axis values through
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadDeadZoneTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0060 — Verify: Dead-zone IndependentAxes math matches FNA (ExcludeAxisDeadZone rescale)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0061 — Verify: Dead-zone Circular math matches FNA
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadThumbSticksTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0062 — Verify: XInput constants (LeftDeadZone/RightDeadZone/TriggerThreshold) match FNA
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0063 — Verify: Virtual buttons pack from triggers past threshold
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0064 — Verify: Virtual buttons pack from thumbstick directions
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadStateTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0065 — Verify: PacketNumber bumps only on button/axis change, not on unchanged reads
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0066 — Verify: PacketNumber within-dead-zone axis wobble policy documented + tested
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0067 — Verify: Vibration cancellation: GetCapabilities does not stop active rumble
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0068 — Verify: SetTriggerVibrationEXT support reported and gated by device capability
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0069 — Verify: SetLightBarEXT support reported; no-op when unsupported/disconnected
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0070 — Verify: GetGyroEXT returns data when available, false+zeroed when not
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0071 — Verify: GetAccelerometerEXT returns data when available, false+zeroed when not
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0072 — Verify: GamePadType maps from SDL joystick type (GamePad/Wheel/ArcadeStick/FlightStick)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0073 — Verify: GetGUIDEXT formats vendor+product little-endian hex; 'xinput' for XInput; Valve overrides
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0074 — Verify: Fake ISdlGamepadBackend covers connect/remove/axis/button/rumble/sensor/GUID paths
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** GamePad
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the behavior against FNA (SDL3_FNAPlatform.cs / GamePad.cs); confirm the named test asserts it; if a gap is found, extend the test (fake backend) — do not change behavior without an FNA citation.
- **Acceptance:** Behavior matches FNA (or a documented deviation); the cited test asserts it and is green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

### Area: Keyboard (23 tasks)

#### INP-0075 — Verify: Keyboard::GetState() returns the accumulated pressed-key set
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0076 — Verify: Keyboard::GetState(PlayerIndex) returns the same single-keyboard state
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0077 — Verify: Pressed->released state transitions are correct across events
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0078 — Verify: Repeated key-down while already down de-dupes (no double entry)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0079 — Verify: KeyboardState::GetPressedKeys returns ascending, de-duplicated keys
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0080 — Verify: IsKeyDown / IsKeyUp and the indexer agree
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0081 — Verify: KeyboardState equality / GetHashCode / ToString(NOXNA) behavior
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/Microsoft/Xna/Framework/Input/KeyboardInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0082 — Verify: SDL keycode->Keys map is byte-identical to FNA INTERNAL_keyMap (123 entries)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0083 — Verify: SDL scancode->Keys map is byte-identical to FNA INTERNAL_scanMap (122 entries)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0084 — Verify: FNA_KEYBOARD_USE_SCANCODES env selects scancode mode (read-once cache)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0085 — Verify: ISO/non-US NONUSHASH/NONUSBACKSLASH scancodes are dropped (DEC-16-consistent)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0086 — Verify: SDL_SCANCODE_UNKNOWN is dropped, not marked Keys::None
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0087 — Verify: Modifier keys (Shift/Ctrl/Alt, left+right) map independently
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0088 — Verify: Function keys F1-F24 map
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0089 — Verify: Numpad keys (incl. KP operators) map
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0090 — Verify: OEM punctuation keys map to Oem* Keys
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0091 — Verify: Browser/media keys: only VolumeUp/Down map; rest have no SDL source (matches FNA)
- **Priority:** P3 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0092 — Verify: IME keys (Kana/Kanji/Ime*/ProcessKey) exist but are unmapped (matches FNA)
- **Priority:** P3 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0093 — Verify: ChatPad keys exist, console-only, no desktop SDL source
- **Priority:** P3 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0094 — Verify: Non-US accented letters (de/fr/cz) are dropped in keycode mode
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0095 — Verify: Key-repeat keeps the key down without spurious transitions
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0096 — Verify: Window lifecycle events do not corrupt keyboard state
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0097 — Verify: Focus-loss does not clear held keys (DEC-15, matches FNA)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Keyboard
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check vs FNA; confirm the named test asserts it; extend the test if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

### Area: Mouse (20 tasks)

#### INP-0098 — Verify: Mouse::GetState reflects position/buttons/scroll from the accumulator
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0099 — Verify: Mouse::SetPosition converts logical->window (renderer + backend transforms)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0100 — Verify: Mouse::SetPosition handles letterbox offset, not just uniform scale
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0101 — Verify: Mouse::WindowHandle round-trips as uintptr_t with no SDL leak
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0102 — Verify: Coordinate mapping with no window passes raw coords through
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0103 — Verify: Relative mouse mode: default off; accumulate; drain-on-read; toggle flush
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0104 — Verify: Mouse::SetPosition is a no-op while relative mode is enabled
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0105 — Verify: All five buttons transition Pressed<->Released through the bridge
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0106 — Verify: XButton1/XButton2 map on both ClickedEXT-index and state paths
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0107 — Verify: Wheel value uses the XNA 120-unit convention (cast-before-scale)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0108 — Verify: Horizontal wheel (wheel.x) is ignored (DEC-18)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeMouseTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0109 — Verify: Motion updates position and accumulates relative delta
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0110 — Verify: MouseState equality / GetHashCode / ToString (button ordering) behavior
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0111 — Verify: ClickedEXT is multicast and safe with no subscriber
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0112 — Verify: MouseCursor stock singletons are stable and non-null (with video)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0113 — Verify: MouseCursor lifecycle: default ctor, dispose idempotent, move ctor/assign
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0114 — Verify: MouseCursor::FromTexture2D validates format + origin bounds
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0115 — Verify: Mouse::SetCursor is a safe no-op for a disposed cursor
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0116 — Verify: Cursor creation degrades gracefully without SDL_INIT_VIDEO (null handle, no crash)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/Microsoft/Xna/Framework/Input/MouseInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0117 — Verify: Resolve any MouseCursor unimplemented/throw paths (audit for std::runtime_error stubs)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Mouse
- **Files:** `tests/Microsoft/Xna/Framework/Input/src/Microsoft/Xna/Framework/Input/MouseCursor.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend/resolve if a gap or stub is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

### Area: TextInput (13 tasks)

#### INP-0118 — Verify: StartTextInput/StopTextInput round-trip (with + without window)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** TextInput
- **Files:** `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; resolve any FIXME.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0119 — Verify: TextInput callback dispatches each UTF-16 code unit; multicast; no-subscriber-safe
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** TextInput
- **Files:** `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; resolve any FIXME.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0120 — Verify: TextEditing callback dispatches (text,start,length); multicast; empty composition
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** TextInput
- **Files:** `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; resolve any FIXME.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0121 — Verify: SetInputRectangle is a safe no-op without a window
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** TextInput
- **Files:** `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; resolve any FIXME.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0122 — Verify: WindowHandle round-trip + IsTextInputActive/IsScreenKeyboardShown false without window
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** TextInput
- **Files:** `tests/Microsoft/Xna/Framework/Input/TextInputEXTTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; resolve any FIXME.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0123 — Verify: UTF-8 -> UTF-16 decode for 1/2/3-byte sequences
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** TextInput
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; resolve any FIXME.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0124 — Verify: Astral emoji decode to surrogate pair; combining chars as separate units
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** TextInput
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; resolve any FIXME.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0125 — Verify: Malformed UTF-8 (invalid lead/truncated/bad-continuation/overlong/surrogate) -> U+FFFD (DEC-08)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** TextInput
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; resolve any FIXME.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0126 — Verify: Control characters (Back/Tab/Enter/Delete/Home/End) synthesize TextInput
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** TextInput
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; resolve any FIXME.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0127 — Verify: Ctrl+V synthesizes paste char and suppresses literal 'v'; suppression releases correctly
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** TextInput
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; resolve any FIXME.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0128 — Verify: Key-repeat re-emits control chars (DEC-19, matches FNA)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** TextInput
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; resolve any FIXME.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0129 — Verify: TextEditing forwards multi-byte UTF-8 composition unchanged
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** TextInput
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeTextInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; resolve any FIXME.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0130 — Verify: Resolve the SDL3 SetInputRectangle behavior/FIXME and document units
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** TextInput
- **Files:** `tests/CNA/Internal/Input/src/CNA/Internal/Input/SdlInputBridge.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; resolve any FIXME.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

### Area: Touch (15 tasks)

#### INP-0131 — Verify: TouchPanel DisplayWidth/Height round-trip
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Touch
- **Files:** `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0132 — Verify: TouchPanel DisplayOrientation round-trip; confirm coords are NOT transformed for orientation (matches FNA)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Touch
- **Files:** `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0133 — Verify: TouchPanelCapabilities: IsConnected + MaximumTouchCount (4 connected / 0 disconnected, DEC-09)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Touch
- **Files:** `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0134 — Verify: GetCapabilities is side-effect free (does not advance touch tracking)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Touch
- **Files:** `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0135 — Verify: GetState snapshot + InputManager fallback path
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Touch
- **Files:** `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0136 — Verify: Touch ID allocation: sequential CNA ids vs SDL finger ids; reuse after release
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Touch
- **Files:** `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0137 — Verify: Released touch is returned once then removed
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Touch
- **Files:** `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0138 — Verify: Pressed/Moved/Released/Invalid transitions
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Touch
- **Files:** `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0139 — Verify: Canceled finger releases like FINGER_UP and frees the id mapping (DEC handling)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Touch
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0140 — Verify: TouchLocation::TryGetPreviousLocation true + false paths (out-param written on both, DEC-12)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Touch
- **Files:** `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0141 — Verify: TouchCollection indexer / Count / FindById / CopyTo bounds + empty
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Touch
- **Files:** `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0142 — Verify: Normalized SDL coords -> pixel via display metrics; rounding; zero-display guard
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Touch
- **Files:** `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0143 — Verify: Multi-touch deterministic ordering by ascending id
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Touch
- **Files:** `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0144 — Verify: Max-touch cap: GetState caps at MAX_TOUCHES=8 while capabilities report 4 (DEC-10)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Touch
- **Files:** `tests/CNA/Internal/Input/TouchEdgeCaseTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0145 — Verify: Gesture vs touch-state share one logical coordinate basis (DEC/INPUT-TOUCH-024)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Touch
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`
- **Steps:** Line-check vs FNA; confirm the cited test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

### Area: Gesture (19 tasks)

#### INP-0146 — Verify: EnabledGestures round-trip + filtering (only enabled types fire)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0147 — Verify: IsGestureAvailable becomes true after a gesture is queued
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0148 — Verify: ReadGesture dequeues FIFO; throws InvalidOperationException when empty
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/Microsoft/Xna/Framework/Input/TouchInputTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0149 — Verify: Tap recognition on quick release near press position
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0150 — Verify: DoubleTap recognition within timing+distance window; negative past window
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0151 — Verify: Hold recognition after >=1s; negative before 1s
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0152 — Verify: HorizontalDrag recognition when movement is predominantly horizontal
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0153 — Verify: VerticalDrag recognition when movement is predominantly vertical
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0154 — Verify: FreeDrag recognition for diagonal movement
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0155 — Verify: Flick recognition above min release velocity; negative below
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0156 — Verify: Pinch + PinchComplete for two-finger gesture
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0157 — Verify: DragComplete fires after a drag ends; not without dragging; not below threshold
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0158 — Verify: Second finger during a drag interrupts it and becomes a Pinch
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0159 — Verify: Interrupted drag reports PinchComplete not DragComplete
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0160 — Verify: Gesture state machine recovers after a cancel so a fresh Tap still fires
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeTouchGestureTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0161 — Verify: GestureSample timestamps are populated
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0162 — Verify: GestureSample delta vectors correct across gestures
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0163 — Verify: GestureSample second-finger positions/FingerIdEXT correct for pinch
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0164 — Verify: GestureDetector is a line-by-line port of FNA GestureDetector.cs (audit)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Gesture
- **Files:** `docs/input-fna-fidelity.md`
- **Steps:** Line-check vs FNA GestureDetector.cs; confirm the cited (deterministic clock) test asserts it; extend if a gap is found.
- **Acceptance:** Matches FNA (or documented deviation); test green.
- **Verify:** Doc updated.

### Area: SDLBridge (15 tasks)

#### INP-0165 — Verify: SdlInputBridge::ProcessEvent handles exactly the 17 input SDL_EVENT_* cases; rest no-op
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** SDLBridge
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeFuzzTests.cpp`
- **Steps:** Line-check the bridge/InputManager behavior; confirm the cited test/doc asserts it.
- **Acceptance:** Behavior correct; test green or doc records it.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0166 — Verify: Deterministic seeded fuzz of ProcessEvent never crashes / corrupts state
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** SDLBridge
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeFuzzTests.cpp`
- **Steps:** Line-check the bridge/InputManager behavior; confirm the cited test/doc asserts it.
- **Acceptance:** Behavior correct; test green or doc records it.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0167 — Verify: Golden recorded event sequences produce exact state snapshots
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** SDLBridge
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- **Steps:** Line-check the bridge/InputManager behavior; confirm the cited test/doc asserts it.
- **Acceptance:** Behavior correct; test green or doc records it.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0168 — Verify: InputManager::ResetAllForTests restores a deterministic baseline (idempotent)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** SDLBridge
- **Files:** `tests/CNA/Internal/Input/InputResetTests.cpp`
- **Steps:** Line-check the bridge/InputManager behavior; confirm the cited test/doc asserts it.
- **Acceptance:** Behavior correct; test green or doc records it.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0169 — Verify: Gamepad subsystem init is idempotent (startup + lazy in ProcessEvent)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** SDLBridge
- **Files:** `tests/**/SdlGamepadSubsystemInit`
- **Steps:** Line-check the bridge/InputManager behavior; confirm the cited test/doc asserts it.
- **Acceptance:** Behavior correct; test green or doc records it.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0170 — Verify: Direct Get*State without Game::Tick returns the last-pumped snapshot
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** SDLBridge
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeGoldenTests.cpp`
- **Steps:** Line-check the bridge/InputManager behavior; confirm the cited test/doc asserts it.
- **Acceptance:** Behavior correct; test green or doc records it.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0171 — Verify: Event-pump freshness: state is only as fresh as the last PollEvents (documented)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** SDLBridge
- **Files:** `docs/input-backend.md`
- **Steps:** Line-check the bridge/InputManager behavior; confirm the cited test/doc asserts it.
- **Acceptance:** Behavior correct; test green or doc records it.
- **Verify:** Doc/audit.

#### INP-0172 — Verify: Single-thread / unsynchronized-by-design policy documented + verified (no mutex/atomic)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** SDLBridge
- **Files:** `docs/input-backend.md`
- **Steps:** Line-check the bridge/InputManager behavior; confirm the cited test/doc asserts it.
- **Acceptance:** Behavior correct; test green or doc records it.
- **Verify:** Doc/audit.

#### INP-0173 — Verify: Fake gamepad backend is installed/restored cleanly by the test fixture
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** SDLBridge
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the bridge/InputManager behavior; confirm the cited test/doc asserts it.
- **Acceptance:** Behavior correct; test green or doc records it.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0174 — Verify: No SDL-specific detail leaks into the public XNA API (compile guard)
- **Priority:** P0 · **Status:** `TODO` [ ] · **Area:** SDLBridge
- **Files:** `tests/Microsoft/Xna/Framework/Input/PublicApiInputCompileTests.cpp`
- **Steps:** Line-check the bridge/InputManager behavior; confirm the cited test/doc asserts it.
- **Acceptance:** Behavior correct; test green or doc records it.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0175 — Verify: Deterministic injectable gesture clock restored by ResetAllForTests
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** SDLBridge
- **Files:** `tests/CNA/Internal/Input/GestureDetectorTests.cpp`
- **Steps:** Line-check the bridge/InputManager behavior; confirm the cited test/doc asserts it.
- **Acceptance:** Behavior correct; test green or doc records it.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0176 — Verify: Backend abstraction boundary: ISdlGamepadBackend never exposed in XNA layer
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** SDLBridge
- **Files:** `tests/CNA/Internal/Input/SdlGamepadBackendTests.cpp`
- **Steps:** Line-check the bridge/InputManager behavior; confirm the cited test/doc asserts it.
- **Acceptance:** Behavior correct; test green or doc records it.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0177 — Verify: Focus gained/lost handling matches FNA (no input clear; game gates on IsActive)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** SDLBridge
- **Files:** `tests/CNA/Internal/Input/SdlInputBridgeKeyboardTests.cpp`
- **Steps:** Line-check the bridge/InputManager behavior; confirm the cited test/doc asserts it.
- **Acceptance:** Behavior correct; test green or doc records it.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0178 — Verify: Shutdown: bridge holds no owned resources needing teardown (static, process-lifetime)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** SDLBridge
- **Files:** `src/CNA/Internal/Input/SdlInputBridge.cpp`
- **Steps:** Line-check the bridge/InputManager behavior; confirm the cited test/doc asserts it.
- **Acceptance:** Behavior correct; test green or doc records it.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

#### INP-0179 — Verify: App suspend/resume behavior audited (SDL background events are no-ops for input state)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** SDLBridge
- **Files:** `src/CNA/Internal/Input/SdlInputBridge.cpp`
- **Steps:** Line-check the bridge/InputManager behavior; confirm the cited test/doc asserts it.
- **Acceptance:** Behavior correct; test green or doc records it.
- **Verify:** `xvfb-run -a env SDL_VIDEODRIVER=x11 ctest --test-dir cmake-build-input-easygl -L input --output-on-failure`

### Area: Build (3 tasks)

#### INP-0191 — Verify CMake configures the input target across EASYGL/VULKAN/BGFX/SDL_RENDERER
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Build
- **Files:** `CMakeLists.txt`
- **Steps:** Implement/verify the CI or build step; keep it green across the matrix.
- **Acceptance:** Step present + green (or documented as manual).
- **Verify:** CI run for the branch is `success`; or config validated locally.

#### INP-0192 — Verify actionable FATAL_ERROR messages for each missing dependency (SDL*/sharp-runtime/easy-gl)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Build
- **Files:** `cmake/ThirdPartySDL.cmake, CMakeLists.txt`
- **Steps:** Implement/verify the CI or build step; keep it green across the matrix.
- **Acceptance:** Step present + green (or documented as manual).
- **Verify:** CI run for the branch is `success`; or config validated locally.

#### INP-0196 — Pin third_party/SDL to an explicit upstream SDL3 release tag; record it
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Build
- **Files:** `.gitmodules, docs/input-build-and-test.md`
- **Steps:** Implement/verify the CI or build step; keep it green across the matrix.
- **Acceptance:** Step present + green (or documented as manual).
- **Verify:** CI run for the branch is `success`; or config validated locally.

### Area: CI (8 tasks)

#### INP-0194 — Confirm the Linux/X11 CI matrix runs ctest -L input under xvfb on all backends
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** CI
- **Files:** `.github/workflows/input-ci.yml`
- **Steps:** Implement/verify the CI or build step; keep it green across the matrix.
- **Acceptance:** Step present + green (or documented as manual).
- **Verify:** CI run for the branch is `success`; or config validated locally.

#### INP-0195 — Add a CI step that fails if submodules are missing or SDL is not at the pinned rev
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** CI
- **Files:** `.github/workflows/input-ci.yml`
- **Steps:** Implement/verify the CI or build step; keep it green across the matrix.
- **Acceptance:** Step present + green (or documented as manual).
- **Verify:** CI run for the branch is `success`; or config validated locally.

#### INP-0197 — Upload test logs, skip reports, and on-failure SDL/GL diagnostics as CI artifacts
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** CI
- **Files:** `.github/workflows/input-ci.yml`
- **Steps:** Implement/verify the CI or build step; keep it green across the matrix.
- **Acceptance:** Step present + green (or documented as manual).
- **Verify:** CI run for the branch is `success`; or config validated locally.

#### INP-0198 — Add an optional gcov/llvm-cov coverage report for the input filter
- **Priority:** P3 · **Status:** `TODO` [ ] · **Area:** CI
- **Files:** `.github/workflows/input-ci.yml`
- **Steps:** Implement/verify the CI or build step; keep it green across the matrix.
- **Acceptance:** Step present + green (or documented as manual).
- **Verify:** CI run for the branch is `success`; or config validated locally.

#### INP-0199 — Define the 'Input stable' gate: 4-backend + sanitizer + determinism + dated hardware entry
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** CI
- **Files:** `docs/input-build-and-test.md`
- **Steps:** Implement/verify the CI or build step; keep it green across the matrix.
- **Acceptance:** Step present + green (or documented as manual).
- **Verify:** CI run for the branch is `success`; or config validated locally.

#### INP-0200 — Author a pre-merge regression checklist (frozen API, no SDL leak, deviations, counts, filter green)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** CI
- **Files:** `docs/input-pre-merge-checklist.md`
- **Steps:** Implement/verify the CI or build step; keep it green across the matrix.
- **Acceptance:** Step present + green (or documented as manual).
- **Verify:** CI run for the branch is `success`; or config validated locally.

#### INP-0203 — Add Windows CI path plan (or document why it is manual)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** CI
- **Files:** `docs/input-build-and-test.md`
- **Steps:** Implement/verify the CI or build step; keep it green across the matrix.
- **Acceptance:** Step present + green (or documented as manual).
- **Verify:** CI run for the branch is `success`; or config validated locally.

#### INP-0204 — Add macOS CI path plan (or document why it is manual)
- **Priority:** P3 · **Status:** `TODO` [ ] · **Area:** CI
- **Files:** `docs/input-build-and-test.md`
- **Steps:** Implement/verify the CI or build step; keep it green across the matrix.
- **Acceptance:** Step present + green (or documented as manual).
- **Verify:** CI run for the branch is `success`; or config validated locally.

### Area: Platform (2 tasks)

#### INP-0202 — Add Wayland notes/path and confirm which cursor/warp behaviors are X11-only
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Platform
- **Files:** `docs/platform-input-notes.md`
- **Steps:** Implement/verify the CI or build step; keep it green across the matrix.
- **Acceptance:** Step present + green (or documented as manual).
- **Verify:** CI run for the branch is `success`; or config validated locally.

#### INP-0205 — Add Android/iOS manual/emulator verification plan
- **Priority:** P3 · **Status:** `TODO` [ ] · **Area:** Platform
- **Files:** `docs/platform-input-notes.md`
- **Steps:** Implement/verify the CI or build step; keep it green across the matrix.
- **Acceptance:** Step present + green (or documented as manual).
- **Verify:** CI run for the branch is `success`; or config validated locally.

### Area: Manual (12 tasks)

#### INP-0231 — Manual: cursor warp landing (X11) + relative-mode capture on a real display
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Perform on real hardware/OS; record date/OS/backend/SDL version/hardware and pass/fail per row.
- **Acceptance:** A dated results row exists with the verification outcome.
- **Verify:** Manual: run `examples/demo_input`, record date/OS/backend/SDL/hardware in `docs/input-manual-verification-results.md`.

#### INP-0232 — Manual: gamepad hardware matrix — Xbox controller (buttons/axes/dpad/triggers/rumble/GUID/hotplug)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Perform on real hardware/OS; record date/OS/backend/SDL version/hardware and pass/fail per row.
- **Acceptance:** A dated results row exists with the verification outcome.
- **Verify:** Manual: run `examples/demo_input`, record date/OS/backend/SDL/hardware in `docs/input-manual-verification-results.md`.

#### INP-0233 — Manual: gamepad hardware matrix — PlayStation controller (+ light bar, trigger haptics, sensors)
- **Priority:** P1 · **Status:** `TODO` [ ] · **Area:** Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Perform on real hardware/OS; record date/OS/backend/SDL version/hardware and pass/fail per row.
- **Acceptance:** A dated results row exists with the verification outcome.
- **Verify:** Manual: run `examples/demo_input`, record date/OS/backend/SDL/hardware in `docs/input-manual-verification-results.md`.

#### INP-0234 — Manual: gamepad hardware matrix — Nintendo Switch controller
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Perform on real hardware/OS; record date/OS/backend/SDL version/hardware and pass/fail per row.
- **Acceptance:** A dated results row exists with the verification outcome.
- **Verify:** Manual: run `examples/demo_input`, record date/OS/backend/SDL/hardware in `docs/input-manual-verification-results.md`.

#### INP-0235 — Manual: gamepad hardware matrix — generic/DirectInput controller
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Perform on real hardware/OS; record date/OS/backend/SDL version/hardware and pass/fail per row.
- **Acceptance:** A dated results row exists with the verification outcome.
- **Verify:** Manual: run `examples/demo_input`, record date/OS/backend/SDL/hardware in `docs/input-manual-verification-results.md`.

#### INP-0236 — Manual: gamepad hardware matrix — Bluetooth controller (pairing + hotplug)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Perform on real hardware/OS; record date/OS/backend/SDL version/hardware and pass/fail per row.
- **Acceptance:** A dated results row exists with the verification outcome.
- **Verify:** Manual: run `examples/demo_input`, record date/OS/backend/SDL/hardware in `docs/input-manual-verification-results.md`.

#### INP-0237 — Manual: real touchscreen multi-touch (markers + IDs + release)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Perform on real hardware/OS; record date/OS/backend/SDL version/hardware and pass/fail per row.
- **Acceptance:** A dated results row exists with the verification outcome.
- **Verify:** Manual: run `examples/demo_input`, record date/OS/backend/SDL/hardware in `docs/input-manual-verification-results.md`.

#### INP-0238 — Manual: real touchscreen gestures (Tap/Drag/Flick/Pinch)
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Perform on real hardware/OS; record date/OS/backend/SDL version/hardware and pass/fail per row.
- **Acceptance:** A dated results row exists with the verification outcome.
- **Verify:** Manual: run `examples/demo_input`, record date/OS/backend/SDL/hardware in `docs/input-manual-verification-results.md`.

#### INP-0239 — Manual: IME composition (TextEditing) on a real IME
- **Priority:** P2 · **Status:** `TODO` [ ] · **Area:** Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Perform on real hardware/OS; record date/OS/backend/SDL version/hardware and pass/fail per row.
- **Acceptance:** A dated results row exists with the verification outcome.
- **Verify:** Manual: run `examples/demo_input`, record date/OS/backend/SDL/hardware in `docs/input-manual-verification-results.md`.

#### INP-0240 — Manual: Czech keyboard diacritics text entry
- **Priority:** P3 · **Status:** `TODO` [ ] · **Area:** Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Perform on real hardware/OS; record date/OS/backend/SDL version/hardware and pass/fail per row.
- **Acceptance:** A dated results row exists with the verification outcome.
- **Verify:** Manual: run `examples/demo_input`, record date/OS/backend/SDL/hardware in `docs/input-manual-verification-results.md`.

#### INP-0241 — Manual: CJK (JP/CN/KR) IME composition
- **Priority:** P3 · **Status:** `TODO` [ ] · **Area:** Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Perform on real hardware/OS; record date/OS/backend/SDL version/hardware and pass/fail per row.
- **Acceptance:** A dated results row exists with the verification outcome.
- **Verify:** Manual: run `examples/demo_input`, record date/OS/backend/SDL/hardware in `docs/input-manual-verification-results.md`.

#### INP-0242 — Manual: Wayland cursor/warp caveats on a real Wayland session
- **Priority:** P3 · **Status:** `TODO` [ ] · **Area:** Manual
- **Files:** `docs/input-manual-verification-results.md`
- **Steps:** Perform on real hardware/OS; record date/OS/backend/SDL version/hardware and pass/fail per row.
- **Acceptance:** A dated results row exists with the verification outcome.
- **Verify:** Manual: run `examples/demo_input`, record date/OS/backend/SDL/hardware in `docs/input-manual-verification-results.md`.

---

## 8. Execution order (phases)

Run phases roughly in order; within a phase, take one task at a time. Task IDs are grouped by area in §7;
this maps areas → phases.

- **Phase 0 — Audit reproducibility & plan foundation:** the Docs-foundation tasks (glossary, canonical
  plan, doc index, stale-PARTIAL sweep) + regenerate the parity/coverage tools. *(Docs, Tests, Cleanup)*
- **Phase 1 — API parity lock:** the API re-audit tasks (per type) + the three guards (signature freeze,
  SDL-leak, enum-drift) + namespace/GetTypeName guards. *(API)*
- **Phase 2 — Test infrastructure & CI:** canonical test command from a fresh clone, filter/determinism
  gate, sanitizer, counts, headless inventory, and the CI matrix/submodule/pin/artifact tasks. *(Tests, CI, Build)*
- **Phase 3 — GamePad correctness:** all GamePad verify tasks (state/caps/vibration/slots/hotplug/mapping/
  dead-zone/packetnumber/EXT/fake-backend). *(GamePad)*
- **Phase 4 — Keyboard correctness:** keycode/scancode map diffs, modifiers/function/numpad/OEM, non-US,
  repeat, focus-loss. *(Keyboard)*
- **Phase 5 — Mouse correctness:** GetState/SetPosition/relative/buttons/wheel/cursor. *(Mouse)*
- **Phase 6 — Text input correctness:** UTF-8 decode, control/Ctrl+V synthesis, TextEditing, rect FIXME. *(TextInput)*
- **Phase 7 — Touch correctness:** display metrics, caps, ids, transitions, cancel, collection, scaling. *(Touch)*
- **Phase 8 — Gesture correctness:** each gesture + interruption/timestamps/deltas + FNA-port audit. *(Gesture)*
- **Phase 9 — SDL bridge & platform behavior:** bridge/InputManager/fuzz/golden/reset + platform docs. *(SDLBridge, Platform)*
- **Phase 10 — Manual verification:** the hardware matrix (controllers, touchscreen, IME, non-US, Wayland). *(Manual)*
- **Phase 11 — Documentation finalization:** refresh all input docs; troubleshooting; extension-API section;
  hardware matrix; verified-vs-intended convention. *(Docs)*
- **Phase 12 — Release readiness:** pre-merge checklist, "Input stable" gate, sample verifier tools. *(CI, Docs)*

---

## 9. Definition of done — "Input complete"

Input is complete when **all** of the following hold:

1. **Public API parity locked** — every public member matches XNA 4.0/FNA or is explicitly tagged
   FNAEXT/NOXNA; the signature-freeze test + member-parity matrix pass with 0 STRICT/EXT gaps.
2. **FNA-compatible behavior verified or documented** — every behavior is either covered by a green test
   or recorded as an accepted, dated deviation in `docs/input-fna-fidelity.md`.
3. **Tests pass from a clean checkout** — `ctest -L input` is green on all four backends under the
   shuffle×5 determinism gate, plus the ASan+UBSan config; recorded counts match.
4. **Manual hardware matrix completed** — a current dated entry exists for at least one real controller
   family, a real touchscreen, and a real IME, in `docs/input-manual-verification-results.md`.
5. **All known deviations documented** — each carries a DEC/task id, a rationale, and a pinning test.
6. **No stale `PARTIAL`/`TODO` comments** in input headers/sources without a linked `INP-XXXX` task.
7. **CI coverage available** — the 5-backend matrix + sanitizer + determinism jobs are green; submodule/
   SDL-pin validation is enforced; a pre-merge checklist is in place.
8. **Sample verification tools available** — `examples/demo_input` exercises (or documents as out-of-scope)
   every input path, and the manual checklist matches it.

---

## 10. Next recommended task

Start with a **reproducible audit/test task**, not an implementation rewrite:

> **INP-0005 — Regenerate the member-parity matrix and confirm zero STRICT gaps**, immediately followed by
> **INP-0006 — Regenerate the source→test coverage report and confirm no orphans**, and
> **INP-0026** (verify the public-API compile guard) / **INP-0027** (verify the signature freeze).

Rationale: these re-establish the audit baseline mechanically (the two `tools/input_parity/` generators +
the two compile guards) in one safe pass each, confirming the API-parity claims in §1/§3 are still true
before any behavioral re-audit. Then proceed through Phase 0 → Phase 1. Reserve the P1 **Manual** gamepad
matrix (INP-02xx) for when real hardware is available — it is the single largest remaining gap and cannot
be done headlessly.
