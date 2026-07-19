# Audit: include/Microsoft/Xna/Framework/Graphics/PackedVector/Short2.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/PackedVector/Short2.hpp`
- Audit status: AUDITED (full read, 86 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/PackedVector/Short2.cs`
- Main related tests: not independently located in this pass

## Purpose
Packed vector storing two signed 16-bit integers (range [-32768,32767], not normalized) in a
32-bit value.

## Executive Verdict
Bit-layout is correct (X@0, Y@16, matching FNA exactly), but `Pack()` has the same confirmed
MEDIUM-severity rounding defect as `Byte4` and `Short4` in this same directory: it truncates
instead of rounding to the nearest integer.

## Checklist Results
- Bit-shift positions: correct, match FNA exactly.
- Rounding: **incorrect** — see Detailed Findings.

## Detailed Findings

### MEDIUM — `Short2::Pack()` truncates instead of rounding to the nearest integer
```cpp
static uint32_t Pack(float x, float y) {
    auto xi = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(x,-32768.f,32767.f)));
    auto yi = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(y,-32768.f,32767.f)));
    return static_cast<uint32_t>(xi) | (static_cast<uint32_t>(yi) << 16);
}
```
(lines 79-84). FNA's real `Pack()` is `(int)Math.Round(MathHelper.Clamp(x, -32768, 32767))` —
rounds to the nearest integer, THEN clamps. CNA's version clamps then truncates via
`static_cast<int16_t>` with no rounding step: `static_cast<int16_t>(100.7f)` truncates to `100`,
where FNA's `Math.Round(100.7f)` correctly produces `101`.

This is the same defect pattern confirmed in `Byte4::Pack()` (see that file's own report for the
full severity discussion) and in `Short4::Pack()` (this same batch) — three confirmed instances
sharing an identical missing-rounding-step shape, all in the "plain integer, not normalized"
sub-family of `PackedVector` types (as opposed to the "normalized [0,1]/[-1,1]" sub-family, which
correctly rounds throughout this directory).

**Practical caveat**: `Short2`/`Short4` typically carry integer-valued mesh-attribute data (e.g.
bone indices, discrete texel offsets) where callers may already only ever pass whole-number
floats — in which case this defect would never trigger in practice. However, any caller
constructing a `Short2` from a genuinely fractional float (e.g. a computed/interpolated value)
will silently get a systematically different result than real FNA/XNA would for the same input.

**Suggested fix** (report-only; no source changes made per this audit's scope): add
`std::lroundf` before the clamp-and-cast, matching the pattern `NormalizedShort2`/
`NormalizedShort4` already use correctly in this same directory, e.g.
`static_cast<int16_t>(std::clamp(std::lroundf(x), -32768.f, 32767.f))`.

## Cross-File Observations
See `Byte4.hpp.audit.md` and `Short4.hpp.audit.md` for the two other confirmed instances of this
identical defect pattern.

## Missing or Weak Tests
A test asserting `Short2::Pack()`'s output for a non-integer input would have caught this; not
independently located in this pass.

## Positive Findings
Bit-layout and unpack (`ToVector4()`) are correct.

## Final Assessment
One MEDIUM finding: `Pack()` lacks a rounding step present in FNA and in the "normalized"
sibling types in this directory.
