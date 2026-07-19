# Audit: include/Microsoft/Xna/Framework/Graphics/DisplayMode.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/DisplayMode.hpp` (63 lines)
- Audit status: AUDITED (full read)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/DisplayMode.cs` (118 lines)
- Main related tests: not independently located in this pass

## Purpose
Describes a supported display mode: width, height, pixel format, aspect ratio.

## Executive Verdict
Several real gaps relative to FNA's actual public API surface: a missing `TitleSafeArea` property,
missing `GetHashCode()`/`ToString()` overrides, an undocumented behavioral deviation in
`AspectRatio`'s zero-height handling, and public constructors where FNA's are `internal`
(construction-only-by-the-framework).

## Checklist Results
- Doxygen coverage: complete for the members that exist.
- Equality: `operator==`/`operator!=` present and correct in shape.

## Detailed Findings

### MEDIUM — missing `TitleSafeArea` property
Real FNA's `DisplayMode.TitleSafeArea` (`DisplayMode.cs` lines 52-58) is a real, documented public
property: `return new Rectangle(0, 0, Width, Height);`. This header has no equivalent member at
all. A game porting real XNA code that reads `someDisplayMode.TitleSafeArea` has no member to call.

### MEDIUM — missing `GetHashCode()` override
Real FNA's `DisplayMode.GetHashCode()` (`DisplayMode.cs` lines 100-103) is
`Width.GetHashCode() ^ Height.GetHashCode() ^ Format.GetHashCode()`. This type has no equivalent —
this project's own `CHECKLIST.md` explicitly requires `GetHashCode()` to be tested for
Equals-consistency on every ported type; here it's not just untested, it doesn't exist.

### MEDIUM — missing `ToString()` override
Real FNA's `DisplayMode.ToString()` produces `"{{Width:w Height:h Format:f}}"`. This type has no
equivalent — again, a required member per this project's own per-file checklist, entirely absent.

### LOW — `AspectRatio`'s zero-height guard is an undocumented deviation from FNA
Real FNA's `AspectRatio` getter (`DisplayMode.cs` lines 26-32) is `(float)Width / (float)Height`
with no guard at all — for `Height==0` this produces `Infinity`/`NaN` in real .NET (float division
by zero doesn't throw), not an exception. This port's doc comment states "0 if either dimension is
zero," a real, deliberate behavioral difference from FNA's actual (crash-free, Infinity/NaN-valued)
result, undocumented as a *deviation* (i.e. no comment explains this was a conscious choice rather
than an assumption about FNA's behavior). Low severity since a `DisplayMode` with `Height==0` is an
unlikely real-world case (would require a degenerate display enumeration), and neither behavior
crashes — but it is a real, silent divergence from FNA worth flagging. Contrast with
`Viewport::getAspectRatioProperty()` (audited separately, same shard), whose real FNA equivalent
DOES have this exact guard — so the guard is correct there and merely undocumented-as-a-deviation
here.

### LOW — public constructors where FNA's are `internal`
Real FNA's only constructor is `internal DisplayMode(int width, int height, SurfaceFormat format)`
— games can never construct a `DisplayMode` directly; every instance comes from
`GraphicsAdapter`'s already-populated lists. This port's constructors (including a parameterless
default constructor with no FNA equivalent at all) are both `public`, letting any caller construct
an arbitrary `DisplayMode` value. This is the same class of C#-`internal`-to-C++-visibility mapping
gap this project's own `CLAUDE.md` explicitly calls out ("map C# `internal` to `private`/
`protected`/detail-namespace... not public"). Low severity: a public constructor here is unlikely
to cause incorrect behavior (no invariant is actually violated by constructing an arbitrary
`DisplayMode`), but it is a real, confirmed visibility-mapping deviation from this project's own
documented policy.

## Cross-File Observations
See the paired `.cpp` report and the `Viewport`/`DisplayMode` `AspectRatio` contrast noted above.

## Missing or Weak Tests
Given the missing members, no test can exist for `TitleSafeArea`/`GetHashCode`/`ToString` on this
type — a real, confirmed test-coverage gap tracing directly to the missing API surface.

## Positive Findings
`operator==`/`operator!=` are present and, per the paired `.cpp` report, correctly compare all
three fields, matching FNA's own equality semantics for this type.

## Final Assessment
Three MEDIUM findings (missing `TitleSafeArea`, `GetHashCode()`, `ToString()`) and two LOW findings
(undocumented `AspectRatio` deviation, public constructors vs. FNA's `internal`).
