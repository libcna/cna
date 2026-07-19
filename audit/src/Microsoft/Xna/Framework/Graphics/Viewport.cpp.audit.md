# Audit: src/Microsoft/Xna/Framework/Graphics/Viewport.cpp

## Metadata
- Source file: `src/Microsoft/Xna/Framework/Graphics/Viewport.cpp` (118 lines)
- Audit status: AUDITED (full read, `Project`/`Unproject` independently re-derived against FNA's
  real algorithm)
- Subsystem: `xna-graphics` shard
- File type: C++ implementation
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/Viewport.cs`
- Main related tests: not independently located in this pass

## Purpose
Implements both constructors, every property, `Project`/`Unproject`, and `ToString()`.

## Executive Verdict
Correct, faithful port. `Project()`/`Unproject()` match FNA's real algorithm term-for-term,
including the perspective-divide epsilon guard (`MathHelper::WithinEpsilon(a, 1.0f)`) and the
Y-axis flip/unflip (`vector.Y = ((-vector.Y + 1.0f) * 0.5f) * Height + Y` in `Project`;
`source.Y = -(((source.Y - Y) / Height) * 2.0f - 1.0f)` in `Unproject`) — both confirmed matching
FNA's `Viewport.cs` lines 218-277 exactly, field-for-field and operation-for-operation.

## Checklist Results
- `getAspectRatioProperty()` (lines 35-40): `if (Height_ != 0 && Width_ != 0) return w/h; return
  0.0f;` — matches FNA's real `AspectRatio` getter exactly (`Viewport.cs` lines 118-128), unlike the
  superficially similar `DisplayMode::getAspectRatioProperty()` (audited separately), whose real
  FNA equivalent has no such guard.
- `ToString()` (lines 108-116): produces `"{X:x Y:y Width:w Height:h MinDepth:n MaxDepth:x}"` —
  matches FNA's real format string exactly (`Viewport.cs` lines 284-296).
- Both constructors correctly default `MinDepth=0.0f`/`MaxDepth=1.0f` — matches FNA.

## Detailed Findings
None.

## Cross-File Observations
See the paired `.hpp` report and the `DisplayMode` contrast noted there.

## Missing or Weak Tests
Not independently located in this pass.

## Positive Findings
`Project`/`Unproject`'s exact term-for-term fidelity to FNA's real matrix-math implementation
(including the shared perspective-divide epsilon guard) is a strong result for a type this
error-prone to port correctly.

## Final Assessment
No findings.
