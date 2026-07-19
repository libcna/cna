# Audit: examples/demo_guide_overlay_console/src/Main.cpp

## Metadata
- Source file: `examples/demo_guide_overlay_console/src/Main.cpp` (247 lines)
- Audit status: AUDITED
- Subsystem: `examples-demo_guide_overlay_console` shard
- File type: standalone console demo executable (Task 15.11)
- XNA/FNA relevance: exercises `Microsoft::Xna::Framework::GamerServices::Guide`'s full static API
  surface (`ShowSignIn`, `BeginShowKeyboardInput`/`EndShowKeyboardInput`,
  `BeginShowMessageBox`/`EndShowMessageBox`, `IsTrialMode`, `SimulateTrialMode`,
  `IsScreenSaverEnabled`, `NotificationPosition`, `DelayNotifications`)
- Related production code: `Guide.hpp`/`Guide.cpp` (audited in parallel this session as part of the
  `xna-gamerservices` shard), `Microsoft::Xna::Framework::Input::TextInputEXT`

## Purpose
A numbered console menu exercising every `Guide` static member without needing a real window —
each entry triggers one real `Guide` call and prints its result. `--auto` runs every item once for
unattended/CI use.

## Executive Verdict
Correct and well-designed. `MenuKeyboardInput`/`MenuMessageBox` correctly drive `Guide`'s real
async Begin/End keyboard-input and message-box paths through headless stand-ins
(`TextInputEXT::INTERNAL_OnTextInput`, `Guide::SimulateMessageBoxClickEXT`) rather than needing an
actual window — a legitimate, well-reasoned way to exercise a real (not stubbed) async completion
path from a console-only demo. `MenuMessageBox`'s negative-path check
(`EndShowMessageBox(nullptr)` expected to throw `System::ArgumentException`) is a genuine
input-validation test, not just a happy-path smoke test.

## Checklist Results
- Correctly uses `System::ArgumentException` (not a raw `std::` exception) to catch the expected
  negative-path throw — consistent with this project's established exception-type convention.
- `result`/`IAsyncResult*` returned by `BeginShowKeyboardInput`/`BeginShowMessageBox` is `delete`d
  after use in both success paths — no leak.
- `MenuScreenSaver`'s own inline comment correctly anticipates and explains the "before == after"
  outcome (SDL's screensaver toggle needs an initialized video subsystem this console-only demo
  intentionally lacks) rather than treating it as an unexplained no-op.

## Detailed Findings
None.

## Cross-File Observations
Directly corroborates two claims made independently while auditing `Guide.hpp`/`Guide.cpp` in the
parallel `xna-gamerservices` fork this session: that `BeginShowKeyboardInput` is a real
(not-synchronously-complete) async operation consuming `TextInputEXT`'s real code-unit stream, and
that `BeginShowMessageBox`/`EndShowMessageBox` is a real (not `NotSupportedException`-stub)
implementation with a dedicated headless completion path
(`SimulateMessageBoxClickEXT`) — this demo is effectively a working integration test for both.

## Missing or Weak Tests
This is itself a demo, not a test — no CTest registration was checked for in this pass. The
`--auto` mode gives it a real unattended-run capability, which is a positive if it is wired into
CI (not verified in this pass).

## Positive Findings
Every menu item's expected behavior is explained inline for a case that could otherwise look like
an unexplained no-op (trial mode, screensaver, notification position), making this genuinely useful
as both a demo and an ad-hoc manual/automated Guide regression check.

## Final Assessment
No findings.
