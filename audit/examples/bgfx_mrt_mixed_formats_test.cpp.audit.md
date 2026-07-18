# Audit: examples/bgfx_mrt_mixed_formats_test.cpp

## Metadata

- Source file: `examples/bgfx_mrt_mixed_formats_test.cpp` (119 lines)
- Audit status: AUDITED
- Subsystem: `examples-tests-bgfx` shard — MRT (multiple render target) mixed-format
  rejection/construction test (Task 774)
- File type: standalone `Game`-subclass executable, CTest-registered (Bgfx variant; CPU-side
  construction only, no actual rendering — `Draw()` override is an empty no-op, line 55).
- XNA/FNA relevance: direct but by way of a documented negative finding — `GraphicsDevice.
  SetRenderTargets`, `RenderTarget2D` construction, `SurfaceFormat` validation.
- FNA reference: `src/Graphics/GraphicsDevice.cs` (`SetRenderTargets`, confirmed by this audit to
  perform zero format-compatibility validation, matching the file's own header claim), `src/
  Graphics/RenderTarget2D.cs`.
- Related production code: `src/Microsoft/Xna/Framework/Graphics/Texture.cpp`
  (`ValidateFormat()`, lines 109-115), `RenderTarget2D.cpp` (delegates construction through
  `Texture2D`'s full constructor).

## Purpose

Per the file's own header (a genuine research finding, not an assumption): real FNA's
`GraphicsDevice.SetRenderTargets` performs **no** surface-format validation at bind time at all —
it forwards every `RenderTargetBinding` straight to FNA3D. The actual, already-shipped constraint
in this codebase lives one level earlier, in `Texture::ValidateFormat`, called unconditionally by
every format-taking texture/render-target constructor on all backends, which throws for every
`SurfaceFormat` except `Color` (Task 176's contract). This test proves, specifically for
`RenderTarget2D` on Bgfx: (1) two `SurfaceFormat::Color` render targets construct and bind
together via `SetRenderTargets` without throwing (baseline), and (2)
`RenderTarget2D(..., SurfaceFormat::Bgr565, ...)` throws `std::runtime_error` at **construction**,
before a second, differently-formatted target could ever reach `SetRenderTargets` — i.e. "mixed-
format MRT" is unreachable code, not merely rejected at the bind call.

## Executive Verdict

**Healthy** — the test's central claim was independently verified against the actual production
source (`Texture::ValidateFormat`, `Texture.cpp` lines 109-115): it throws `std::runtime_error`
for every `SurfaceFormat` value except `Color`, exactly as asserted. The test is honest about
testing a narrower, already-guaranteed-elsewhere property (construction-time validation) rather
than the nominally-implied one (bind-time MRT format-compatibility checking), and says so plainly
in its own header rather than mischaracterizing what it proves.

## Checklist Results

### API / XNA / FNA parity
`RenderTarget2D(dev, 4, 4, false, SurfaceFormat::Color, DepthFormat::None)` (lines 68-69, 86)
matches FNA's `RenderTarget2D` constructor signature shape (width, height, mipMap, format,
DepthFormat). `dev.SetRenderTargets({ RenderTargetBinding(&rtA), RenderTargetBinding(&rtB) })`
(line 70) and the null-restore `dev.SetRenderTargets({})` (line 71) both match FNA's
`GraphicsDevice.SetRenderTargets(params RenderTargetBinding[])` overload semantics (empty/no-args
restores the backbuffer).

### Behavioral correctness
Directly confirmed `Texture::ValidateFormat` (`Texture.cpp` lines 109-115):
```cpp
void Texture::ValidateFormat(SurfaceFormat fmt)
{
    if (fmt == SurfaceFormat::Color) return;
    throw std::runtime_error(... "only SurfaceFormat::Color is currently supported");
}
```
This is called unconditionally by the format-taking constructors this test exercises (per the
file's own citation of Task 176, cross-checked structurally: `RenderTarget2D`'s full constructor
delegates through `Texture2D`'s, which invokes `ValidateFormat`). The test's `catch (const
std::runtime_error&)` (line 92) matches the actual thrown exception type exactly — not a looser
`catch (...)` that could mask a different, unrelated exception type accidentally satisfying the
check.

### Logic
Two-part check design: baseline "same format binds fine" (lines 65-78) isolates *this test's own*
false-positive risk (if `SetRenderTargets` itself threw for any 2-target bind regardless of
format, the "throws" assertion below would pass for the wrong reason) from the actual "mixed
format throws at construction" claim (lines 83-96) — a deliberately sound test structure.

### C++ correctness
Both scoped `try`/`catch` blocks correctly scope `rtA`/`rtB`/`rtColor`/`rtMixed` as local stack
objects; the second block's `dev.SetRenderTargets(...)` call (line 89) is genuinely unreachable if
the constructor throws on the prior line (correctly commented as such, line 88) — the code reads
linearly as written, no `noexcept`/exception-safety concerns since nothing here manages raw
resources across the throw boundary.

### Robustness
`check()` (lines 49-53) records pass/fail without early-exiting, so both assertions always run and
are both reported even if the first fails — good for diagnosing which of the two distinct
properties broke, rather than aborting after the first failure.

### Testing
Covers exactly the two properties the header claims. Does **not** additionally test
`RenderTargetCube`/`RenderTarget3D`/plain `Texture2D`/`Texture3D` with a non-Color format on Bgfx
specifically — but the file's own header correctly notes the general `Texture2D` case is already
covered by `examples/easygl_surface_format_throws_test.cpp` (backend-agnostic, CPU-side-only, so
reused rather than re-proven per-backend) — a reasonable scope boundary, not a coverage gap.

## Detailed Findings

None — no correctness, parity, or test-validity defects found. The test's own framing (proving a
narrower, already-true property rather than the more ambitious "MRT format compatibility is
validated at bind time") is accurate and was independently confirmed, not merely asserted.

## Cross-File Observations

- This file and `examples/easygl_surface_format_throws_test.cpp` together fully account for the
  "mixed-format MRT" question across formats: the general per-`Texture2D` throw behavior is
  backend-agnostic (proven once, in EasyGL), while this file adds the `RenderTarget2D`-specific,
  Bgfx-specific confirmation that the same construction-time gate applies to render targets too
  and that a same-format two-target bind is not itself broken on this backend.
- `Texture::ValidateFormat`'s hard restriction to `SurfaceFormat::Color` only (i.e. this whole
  codebase currently cannot construct a non-`Color`-format texture or render target on **any**
  backend) is a real, significant XNA-compatibility gap by itself — genuine XNA/FNA content can
  and does use other `SurfaceFormat` values (e.g. `Bgr565`, `Rgba1010102`, depth/float formats for
  HDR render targets). This is out of scope to flag as a defect *of this test file* (the test
  correctly documents and exercises the current, real, intentional restriction rather than
  inventing false parity), but is worth surfacing as a subsystem-level observation: the day
  `Texture::ValidateFormat` is relaxed to support more formats, this specific test's "throws
  std::runtime_error" assertion for `Bgr565` (line 96) would need to flip to a genuine MRT-mixed-
  format bind-time check, since the premise that non-Color formats are simply unconstructible
  would no longer hold.

## Missing or Weak Tests

None for this file's own stated scope. The broader `Texture::ValidateFormat` restriction (noted
above) is a production-code gap, not a test-file gap, and outside this file's remit to fix.

## Positive Findings

- Rare example in this shard of a test file whose own header comment is *more* rigorous than a
  typical "verify X" task description — it independently investigated the FNA source to establish
  what the real constraint actually is and where it actually lives, then designed the test to
  match that reality rather than a naive reading of the task title ("MRT mixed formats").
- Correct, narrow `catch (const std::runtime_error&)` rather than a catch-all that could hide a
  wrong-exception-type regression.

## Final Assessment

A well-scoped, accurately-documented test whose central claim (construction-time format
validation makes mixed-format MRT unreachable) was independently confirmed against the current
`Texture::ValidateFormat` implementation. No defects found.
