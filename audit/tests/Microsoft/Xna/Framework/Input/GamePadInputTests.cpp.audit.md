# Audit: tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp

## Metadata
- Source file: `tests/Microsoft/Xna/Framework/Input/GamePadInputTests.cpp` (224 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tests-xna-input` shard
- File type: C++ test file (Google Test)
- XNA/FNA relevance: Tests for `Microsoft::Xna::Framework::Input::GamePad::GetState`
- Main related tests: N/A (this IS a test file)

## Purpose
Covers `GamePad::GetState`'s disconnected fallback, full button/axis mapping, snapshot immutability,
axis clamping (plus invalid-`PlayerIndex` handling), `PacketNumber` bump semantics (connect/button/
axis changes, including the subtle "raw axis wobble bumps the packet even though the dead-zoned
view stays at rest" case), and the 1-arg overload's forwarding to `IndependentAxes` dead-zone mode.

## Executive Verdict
No findings. `PacketNumberBumpsOnWithinDeadZoneAxisWobbleWhileDeadZonedStateStaysAtRest` is an
excellent, subtle test: it correctly distinguishes CNA's own synthesized `PacketNumber` semantics
(raw-axis-change-driven, matching XInput's `dwPacketNumber`) from the dead-zone-projected view,
explicitly noting FNA itself hardcodes `PacketNumber` to 0 (a CNA-specific enhancement, not an FNA
port) and pinning down that the dead zone is applied at `GetState`-read-time, not baked into stored
state.

## Checklist Results
- `GetStateReflectsMappedButtonsAndAxes` exercises a broad, simultaneous mix of buttons/axes in one
  state read, verifying the mapping doesn't cross-contaminate between fields.
- `SnapshotDoesNotChangeAfterInternalStateMutation` correctly proves `GamePadState` is a genuine
  value-type snapshot, not a live view — mutating `InputManager` after taking a snapshot must not
  retroactively change the already-returned state.
- `AxisValuesAreClampedAndInvalidPlayerReturnsDisconnectedState` correctly bundles two related but
  distinct assertions: axis clamping AND a bogus `PlayerIndex` safely returning a disconnected
  state rather than undefined behavior.
- `GetStateDefaultOverloadForwardsToIndependentAxesDeadZone`'s comment explicitly explains why the
  existing wobble test's stick value (which reads 0.0f either way) would not catch a
  default-dead-zone-mode forwarding bug, and deliberately chooses an above-dead-zone value to make
  the three dead-zone readings (`default`, explicit `IndependentAxes`, `None`) mutually
  distinguishable — a well-reasoned, specific regression design.

## Detailed Findings
None.

## Cross-File Observations
`PacketNumberBumpsOnConnectButtonAndAxisChangesOnly` is explicitly cross-referenced by
`GamePadMappingTests.cpp`'s `ConnectingBumpsPacketNumber` comment as the authoritative, complete
test for packet-number change-only semantics — a good example of avoiding duplicate coverage while
keeping cross-references legible.

## Missing or Weak Tests
None identified for `GetState`'s public surface.

## Positive Findings
The raw-vs-dead-zoned `PacketNumber` distinction test is a genuinely subtle, well-designed
regression test for a CNA-specific (not FNA-ported) design decision, explicitly documented as such.

## Final Assessment
No findings.
