# Audit: include/Microsoft/Xna/Framework/Input/GamePadState.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Input/GamePadState.hpp`
- Audit status: AUDITED (full read, 157 lines)
- Subsystem: `xna-input` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/GamePadState.cs`
- Main related tests: not independently located in this pass

## Purpose
Composite snapshot of a gamepad's full state: buttons, D-pad, thumbsticks, triggers,
connected/packet-number metadata.

## Executive Verdict
Needs a minor documentation-accuracy note (not a functional defect). All three constructors,
`IsButtonDown`/`IsButtonUp`, `Equals`, and `ToString()` are verified correct in the paired `.cpp`
(including a genuinely obscure, correctly-replicated FNA quirk: `ToString()` is never overridden by
real FNA `GamePadState`, so it returns the CLR's default `ValueType.ToString()` — the fully-qualified
type name regardless of field values — and this port reproduces that literal string). `GetHashCode()`'s
implementation comment, however, reads as if preserving an original FNA formula ("avoids
signed-overflow UB in `packetNumber_ * 31`... INPUT-BUILD-006"), but FNA's real `GamePadState.GetHashCode()`
is simply `return base.GetHashCode();` — there is no original formula to preserve, since .NET's
`ValueType.GetHashCode()` default is CLR-implementation-internal and has no portable equivalent. The
custom formula here (`buttons_.GetHashCode() ^ (packetNumber_ * 31)`) is therefore a necessary CNA
invention, not a preserved port — a legitimate and expected deviation given `GetHashCode()`'s
contract only requires internal `Equals`-consistency, not exact FNA numeric parity, but the comment
framing is misleading about which case this is.

## Checklist Results

### LOW (documentation accuracy only, not functional): `GetHashCode()`'s comment implies a preserved FNA formula that doesn't exist
See Executive Verdict. FNA's real `GamePadState.GetHashCode()` (`GamePadState.cs` line 265-268) is
`base.GetHashCode()`. No functional consequence — `GetHashCode()`'s only real contract requirement
(equal objects hash equally) is satisfied by the custom formula either way.

## Detailed Findings
1. **[LOW, documentation-only] `GetHashCode()`'s comment framing implies FNA has a portable formula
   being faithfully preserved, when FNA's real implementation is an opaque `base.GetHashCode()`** —
   implementation in the paired `.cpp`; cf. FNA `GamePadState.cs` lines 265-268.

## Cross-File Observations
The identical documentation-framing situation recurs in `MouseState.hpp`/`.cpp` (audited
separately) — both `GamePadState` and `MouseState` have real FNA types whose `GetHashCode()` is
`base.GetHashCode()`, unlike e.g. `GamePadDPad`/`GamePadThumbSticks`/`GamePadTriggers`/
`KeyboardState`/`TouchLocation` (also in this shard), which all have genuine, portable FNA formulas
correctly preserved via the same `INPUT-BUILD-006` overflow-safe rewrite pattern.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`ToString()`'s reproduction of FNA's un-overridden `ValueType.ToString()` behavior (returning the
literal type name, not a field-value summary) is a genuinely obscure, easy-to-miss XNA quirk,
correctly identified and replicated.

## Final Assessment
One LOW, documentation-accuracy-only finding with no functional consequence.
