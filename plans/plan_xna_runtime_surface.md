# Microsoft XNA 4.0 runtime public type surface

| Task | Status | Result |
| --- | --- | --- |
| XNA-ENUM-001 | Complete | Implement and test the four graphics, touch, and gamer nested collection enumerators; add a Microsoft XML type census and remove obsolete type-coverage claims. |

The original Microsoft runtime XML set contains 331 distinct documented
public types across ten runtime assemblies. The build-time Content.Pipeline
XML is excluded. The earlier 329 estimate collapsed two generic/non-generic
CLR name pairs. The starting HEAD had 326/331 declaration coverage: five
nested enumerators were absent, while the gamer nested struct already
existed but had an incompatible interface and cursor contract. The final
audit reports 331/331 represented and zero missing. See
`docs/xna-4-enumerator-reference.md` for metadata and behavior findings.

`tools/audit_xna_runtime_surface.py` exports the complete type inventory,
reference XML checksums, normalization, missing types, and documented
member inventory as JSON. The member inventory has 253 constructors, 1,518
methods, 1,040 properties, 753 fields, and 63 events. Their representation
classifications remain unreviewed; this task makes no member-level
percentage claim.

Validation used the HEADLESS renderer build. The six affected module and
dependency binaries passed: Graphics 2,318 pass/455 renderer-specific skips;
Input 474 pass with the unrelated `FakeHapticTest.*` fixture excluded;
GamerServices 368 pass with `XDG_DATA_HOME` in `/tmp`; Core 106 pass; Math
857 pass; Runtime 175 pass/2 skips. All twelve new enumerator cases pass.
The unfiltered Input binary has 25 failures confined to
`FakeHapticTest.*` because its canned platform forwards haptic subsystem
acquisition to the unavailable host backend. The aggregate `CnaTests`
build fails in an unrelated EasyGL test that includes unavailable
`metagl/metagl.hpp` in this HEADLESS configuration. No haptics or renderer
code was changed.
