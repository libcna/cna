# Microsoft XNA 4.0 runtime public type surface

| Task | Status | Result |
| --- | --- | --- |
| XNA-ENUM-001 | Complete | Implement and test the four graphics, touch, and gamer nested collection enumerators; add a Microsoft XML type census and remove obsolete type-coverage claims. |
| XNA-MEMBER-001 | Baseline complete | Inventory and classify all 3,627 documented runtime members before production API fixes; preserve the 331/331 type result. See `docs/xna-4-runtime-member-coverage-baseline.md` and its per-entry JSON. |

The original Microsoft runtime XML set contains 331 distinct documented
public types across ten runtime assemblies. The build-time Content.Pipeline
XML is excluded. The earlier 329 estimate collapsed two generic/non-generic
CLR name pairs. The starting HEAD had 326/331 declaration coverage: five
nested enumerators were absent, while the gamer nested struct already
existed but had an incompatible interface and cursor contract. The final
audit reports 331/331 represented and zero missing. See
`docs/xna-4-enumerator-reference.md` for metadata and behavior findings.

`tools/audit_xna_runtime_surface.py` exports the complete type inventory,
reference XML and DLL checksums, normalization, missing types, and a
signature-aware member inventory as JSON. The runtime member inventory has
253 constructors, 1,518 methods, 1,040 properties, 753 fields, and 63 events.
The pre-fix XNA-MEMBER-001 baseline is **3,458/3,627 documented members
represented (95.34%)**: 1,658 exact, 1,742 semantic, 58 host-language
substitutions, 169 missing, zero not applicable, and zero needing review.
These numbers measure API representation, not behavioral parity. The
Content Pipeline XML is excluded and remains separately measured. Run
`python3 tools/audit_xna_runtime_surface.py --write-reports --baseline` and
`python3 -m unittest discover -s tools -p test_audit_xna_runtime_surface.py`.

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
