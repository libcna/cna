# Audit: include/Microsoft/Xna/Framework/Graphics/PackedVector/Byte4.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/PackedVector/Byte4.hpp`
- Audit status: AUDITED (full read, 96 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/PackedVector/Byte4.cs`
- Main related tests: not independently located in this pass

## Purpose
Packed vector storing four 8-bit unsigned integer values (range [0,255], not normalized) in a
32-bit value.

## Executive Verdict
Bit-layout is correct (byte-1@0, byte-2@8, byte-3@16, byte-4@24, matching FNA exactly), but
`Pack()` has a genuine, confirmed MEDIUM-severity rounding defect: it truncates instead of
rounding to the nearest integer, unlike FNA and unlike every other type in this same directory.

## Checklist Results
- Bit-shift positions: correct, match FNA exactly.
- Rounding: **incorrect** — see Detailed Findings.

## Detailed Findings

### MEDIUM — `Byte4::Pack()` truncates instead of rounding to the nearest integer
```cpp
static uint32_t Pack(float x, float y, float z, float w) {
    auto xi = static_cast<uint32_t>(std::clamp(x, 0.0f, 255.0f));
    ...
}
```
(lines 87-93). FNA's real `Pack()` is `(uint)Math.Round(MathHelper.Clamp(x, 0, 255))` — it rounds
to the nearest integer before truncating to `uint`. CNA's version clamps but performs **no
rounding step at all**: `static_cast<uint32_t>(254.9f)` truncates to `254`, whereas FNA's
`Math.Round(254.9f)` correctly produces `255`.

This is categorically more severe than the LOW rounding-*tie* divergence documented in
`Alpha8.hpp.audit.md` (which only affects exact half-integer boundary values): this divergence
affects **every non-integer input value**, not just values landing precisely on a `.5` boundary —
a much broader and more easily-triggered defect. For a typical caller passing e.g. an
interpolated/computed float (not a hand-picked integer), the packed result will systematically be
off by 1 relative to FNA's real behavior whenever the fractional part is ≥ 0.5.

**Cross-reference**: this exact same defect pattern (clamp-then-truncate with no rounding) recurs
identically in `Short2::Pack()` and `Short4::Pack()` (both audited in this same batch) — three
confirmed instances in this directory, all sharing the same missing-rounding-step shape. Every
OTHER packed type in this directory (`Alpha8`, `Bgr565`, `Bgra4444`, `Bgra5551`, `Rg32`,
`Rgba1010102`, `Rgba64` via `+0.5f`-then-truncate; `NormalizedByte2`, `NormalizedByte4`,
`NormalizedShort2`, `NormalizedShort4` via `std::lroundf`) correctly performs SOME form of
rounding — making `Byte4`/`Short2`/`Short4` a clearly identifiable, isolated sub-family sharing
this specific omission.

**Failure scenario**: `Byte4(254.9f, 0.0f, 0.0f, 0.0f).getPackedValueProperty()` would return a
packed value with byte 0 = `254` in this port, vs. `255` in real FNA/XNA — a silent, systematic
off-by-one error for any non-integer-valued input, in either direction of channel data flowing
through this packed format (e.g. vertex-attribute data authored as floats and packed for GPU
upload).

**Suggested fix** (report-only; no source changes made per this audit's scope): change each
`static_cast<uint32_t>(std::clamp(...))` to include an explicit round-to-nearest step, e.g.
`static_cast<uint32_t>(std::lroundf(std::clamp(x, 0.0f, 255.0f)))`, matching the pattern already
used correctly by `NormalizedByte2`/`NormalizedByte4`/`NormalizedShort2`/`NormalizedShort4` in
this same directory.

## Cross-File Observations
See `Short2.hpp.audit.md` and `Short4.hpp.audit.md` for the two other confirmed instances of this
identical defect pattern.

## Missing or Weak Tests
A test asserting `Byte4::Pack()`'s output for a non-integer input (e.g. `254.9f` → expect `255`,
not `254`) would have caught this; not independently located in this pass.

## Positive Findings
Bit-layout and unpack (`ToVector4()`) are correct.

## Final Assessment
One MEDIUM finding: `Pack()` lacks a rounding step present in FNA and in every sibling
`PackedVector` type in this directory.
