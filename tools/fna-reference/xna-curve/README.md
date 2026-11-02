# XNA Curve insertion and selected Smooth tangent evidence (MTH-CONF-003)

Local inspection on 2026-11-02 used the original Microsoft XNA 4.0 Windows assembly:

- File: `Microsoft.Xna.Framework.dll`.
- Version: `4.0.0.0`; public key token: `842cf8be1de50553`.
- SHA-256: `38e7093f52d7474bbc6256906519781a1210d7da50a1c667b52716fcf49ca130`.

The repository contains original prose summaries and regression inputs/outputs only. Microsoft
assembly disassembly is local evidence outside the repository and must not be redistributed here.
The types below belong to `Microsoft.Xna.Framework`.

## Inspection summary

- `CurveKey.CompareTo`, offsets `IL_0000`–`IL_0021`, returns zero for equal positions.
  `CurveKeyCollection.Add`, offsets `IL_000f`–`IL_005a`, finds the position with binary search and
  advances past equal-position keys before insertion. Additions `(0,0), (0,10), (1,20)` preserve
  values `[0,10,20]`. Equal-position keys with `(value,in,out)` of `(3,1,2)` and `(9,4,5)` preserve
  that whole-key arrival order. This settles eight observed scalar differences in CNA's favor.
- `Curve.ComputeTangent(int32, CurveTangent, CurveTangent)` checks the absolute **value difference**
  against `2^-23` for Smooth tangents: incoming offsets `IL_00b2`–`IL_00c3`, outgoing
  `IL_010e`–`IL_011f`. The outgoing calculation at `IL_012d`–`IL_013e` scales the value difference
  by the distance to the next key and divides by the neighboring position span. For positions
  `[0,1e-11]`, values `[7,9]`, the first outgoing tangent is exactly 2. The value difference is 2,
  so the guard does not fire. Neither Smooth guard tests the position span.
- `Curve.ComputeTangent(int32, CurveTangent)`, offsets `IL_0000`–`IL_0009`, delegates to the
  two-mode overload. `Curve.ComputeTangents(CurveTangent)`, offsets `IL_0000`–`IL_0008`, delegates
  to its two-mode overload. `Curve.ComputeTangents(CurveTangent, CurveTangent)`, offsets
  `IL_0000`–`IL_001f`, applies the same single-key calculation to each key. This settles twelve
  selected outgoing tangent fields in CNA's favor.

No Curve production change is required for these twenty fields. `CURVE_IL_RETAINED` freezes only
the exact observed paths and FNA/CNA values. Native regressions independently assert the two
insertion fixtures and the selected tiny-span outgoing result through all four overload forms.
This does not establish general tangent precision or agreement for other epsilon, zero-span or
invalid inputs; CNA and XNA implementations are not claimed identical.

## Regenerate locally

Use your own copy of the identified assembly and an output directory outside this repository:

```bash
XNA_ASSEMBLY="/path/to/your/Microsoft.Xna.Framework.dll"
LOCAL_EVIDENCE="/path/to/local/evidence/outside/cna"
sha256sum "$XNA_ASSEMBLY"
ikdasm "$XNA_ASSEMBLY" > "$LOCAL_EVIDENCE/framework.il"
```

Do not commit the resulting disassembly. Inspect the named types, methods and offsets locally.
The audit and evidence tests do not load disassembly files. This is a local assembly-inspection
decision, not a successful original-XNA runtime measurement: the earlier
[public-API half probe](../xna-half/README.md) could not initialize the mixed-mode assembly under
Mono before measurement.
