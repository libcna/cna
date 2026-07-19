# Audit: include/Microsoft/Xna/Framework/Input/Mouse.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Input/Mouse.hpp`
- Audit status: AUDITED (full read, 111 lines)
- Subsystem: `xna-input` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Input/Mouse.cs`
- Main related tests: not independently located in this pass

## Purpose
Static entry point for mouse state/position/cursor control and 6 FNA/NOXNA extension methods
(relative mode, capture, global position/warp).

## Executive Verdict
Correct. `ClickedEXT`'s doc comment correctly identifies it as multicast (`System::MulticastAction<int>`,
matching FNA's real `public static Action<int>` — a C# delegate field is inherently multicast via
`+=`, so this is a faithful semantic match, not an arbitrary design choice).

## Checklist Results
No issues found.

## Detailed Findings
None.

## Cross-File Observations
`SetPosition`/`GetState`/coordinate-transform logic verified correct in the paired `.cpp`.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
Correctly matches FNA's multicast-delegate semantics for `ClickedEXT`.

## Final Assessment
No findings.
