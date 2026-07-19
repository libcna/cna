# Audit: include/Microsoft/Xna/Framework/GamerServices/Guide.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/GamerServices/Guide.hpp`
- Audit status: AUDITED (full read, 561 lines)
- Subsystem: `xna-gamerservices` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA has no reference material for this namespace (confirmed
  via `find` — no `GamerServices` directory at all in the local FNA tree)
- Main related tests: not independently located in this pass

## Purpose
Static-only class providing access to the in-game Guide overlay: system message boxes, on-screen
keyboard text input, trial-mode state, notification position, and a large set of documented-no-op
"show UI" entry points (`ShowFriends`, `ShowMarketplace`, `ShowSignIn`, etc.).

## Executive Verdict
Correct and unusually well-engineered for a subsystem with zero platform reference to diff
against: `BeginShowMessageBox`/`BeginShowKeyboardInput` are genuinely functional (not FNA-style
stubs), with real async Begin*/End* pairs, real callback invocation (see Cross-File Observations —
this shard independently avoids the callback-never-invoked bug just found and fixed for
`NetworkSession` in the sibling `xna-net` shard this session), and a full NOXNA/EXT
render-and-simulate testing surface (`RenderPendingMessageBoxEXT`, `SimulateMessageBoxClickEXT`,
etc.) mirroring the same pattern established for `LocalNetworkGamer`. One MEDIUM finding: the
dedicated `GuideAlreadyVisibleException` type (audited separately) is never actually thrown by
this class's real "already pending" guards, which throw a generic `InvalidOperationException`
instead — see Detailed Findings.

## Checklist Results
- Doxygen coverage: complete; every public member has a full `@brief`/`@param`/`@return`/`@throws`
  block where applicable.
- `NOXNA`/`EXT` usage: correctly and consistently applied to every member that exists purely to
  support this port's real (non-FNA-stub) message-box/keyboard-input implementation
  (`GetHasPendingMessageBoxEXTProperty`, `RenderPendingMessageBoxEXT`,
  `SimulateMessageBoxClickEXT`, `ResetPendingMessageBoxForTestingEXT`,
  `GetPendingMessageBoxFocusButtonForTestingEXT`, and the keyboard-input equivalents).
  `ShowAchievementsEXT`'s doc comment additionally and correctly notes this specific member is
  "FNA's own addition, not part of real XNA 4.0's Guide API."
- Static-only design: `Guide() = delete;` correctly matches real XNA's `Guide` (all-static utility
  class, never instantiated).

## Detailed Findings

### MEDIUM — `GuideAlreadyVisibleException` exists, is tested, but is never actually thrown by
this class's real "message box/keyboard input already pending" checks
`BeginShowKeyboardInput` (`@throws System::InvalidOperationException if a keyboard input request
is already pending`, lines 135-136) and `BeginShowMessageBox` (`@throws
System::InvalidOperationException if another message box is already pending`, lines 313-314) both
document — and, confirmed in the paired `.cpp`, both actually throw —
`System::InvalidOperationException` for this exact scenario. Real XNA 4.0's documented behavior for
this precise case (attempting to show a Guide UI element while one is already visible) is to throw
the specific, dedicated `GuideAlreadyVisibleException` — a real, well-documented XNA type that
exists for exactly this purpose (confirmed: it is present in this codebase, fully implemented, and
has its own dedicated test file, `tests/.../GamerServicesExceptionsTests.cpp`) but is never
constructed or thrown anywhere in production code (confirmed via grep across `src/`/`include/` —
every match is either the type's own declaration/definition or its own test file). A real XNA game
ported to this platform that specifically catches `GuideAlreadyVisibleException` to gracefully
handle a double-show attempt (a documented, idiomatic real-XNA pattern) would not catch CNA's
`InvalidOperationException` here, silently breaking that compatibility path.

## Cross-File Observations
- Confirmed (via the paired `.cpp`) that both `CompletePendingMessageBox`/
  `CompletePendingKeyboardInput` correctly invoke `action->Callback(*action)` — this shard's own
  Begin*/End* async pattern does **not** share the "callback stored but never invoked" bug just
  found and fixed (Task 12) for `NetworkSession`'s Begin*/End* family in the sibling `xna-net`
  shard this session.
- `InviteAcceptedEventArgs` (audited separately, same shard) is a real, correctly-shaped type but
  confirmed genuinely dead on both sides project-wide (no construction site, no `Raise`/`Invoke`
  call site anywhere) — consistent with `NetworkSession::InviteAccepted`'s own doc comment
  ("declared for API parity; never raised upstream").
- `GetIsVisibleProperty()`'s doc comment (lines 64-75) is an excellent example of honestly
  documenting a deliberate divergence from FNA's own always-`false` stub: this port's `IsVisible`
  is a real, live signal derived from whether a message box or keyboard input is genuinely pending
  — consistent with "this project's decision 1a reasoning (real observable behavior over a PC
  no-op stub)," cited by name.

## Missing or Weak Tests
Not independently located in this pass; the extensive `*ForTestingEXT()`/`Simulate*EXT()` surface
strongly suggests a dedicated `GuideTests.cpp` (or similar) exists, but it was not read in this
pass. `GuideAlreadyVisibleExceptionTest` (in `GamerServicesExceptionsTests.cpp`) only exercises the
exception type's constructors directly — no test exists exercising `Guide`'s actual double-show
scenario, consistent with the exception never being thrown there in the first place.

## Positive Findings
- The keyboard-input and message-box overlays are both genuinely functional, real implementations
  (UTF-16-aware text capture with surrogate-pair-safe backspace, real mouse/keyboard-driven
  interaction) rather than a byte-for-byte port of FNA's own stubbed no-ops — a substantial, honest
  improvement over the platform this is nominally porting, clearly and extensively disclosed as
  such throughout.
- `EndShowKeyboardInput`'s doc comment (lines 178-192) is an excellent example of documenting a
  real, unavoidable C#-to-C++ representational gap: real XNA distinguishes "canceled" from
  "confirmed with nothing typed" via a nullable string return (`null` vs. `""`), which this port's
  non-nullable `std::string` cannot represent — correctly compensated for via the separate,
  clearly-marked NOXNA `WasKeyboardInputCanceledEXT` accessor.

## Final Assessment
One MEDIUM finding: `GuideAlreadyVisibleException` is fully implemented and tested but dead code in
production — the real "already pending" guard paths use a generic `InvalidOperationException`
instead.
