# Audit: include/Microsoft/Xna/Framework/Graphics/PackedVector/Short4.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/PackedVector/Short4.hpp`
- Audit status: AUDITED (full read, 90 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/PackedVector/Short4.cs`
- Main related tests: not independently located in this pass

## Purpose
Packed vector storing four signed 16-bit integers (range [-32768,32767], not normalized) in a
64-bit value.

## Executive Verdict
Bit-layout is correct (X@0, Y@16, Z@32, W@48, matching FNA exactly), but `Pack()` has the same
confirmed MEDIUM-severity rounding defect as `Byte4` and `Short2` in this same directory: it
truncates instead of rounding to the nearest integer.

## Checklist Results
- Bit-shift positions: correct, match FNA exactly.
- Rounding: **incorrect** — see Detailed Findings.

## Detailed Findings

### MEDIUM — `Short4::Pack()` truncates instead of rounding to the nearest integer
```cpp
static uint64_t Pack(float x, float y, float z, float w) {
    auto xi = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(x,-32768.f,32767.f)));
    ...
}
```
(lines 82-87). Identical defect shape to `Short2::Pack()` (see that file's own report for the full
severity discussion and suggested fix) and `Byte4::Pack()` — no rounding step before truncation,
where FNA's real `Pack()` uses `(long)Math.Round(MathHelper.Clamp(x, -32768, 32767))` for each
component.

This is the third and final confirmed instance of this defect pattern in this directory — all
three (`Byte4`, `Short2`, `Short4`) are the "plain integer, not normalized" `PackedVector`
sub-family, and all three share the identical missing-rounding-step omission, while every
"normalized" sibling type in this same directory correctly rounds.

## Cross-File Observations
See `Byte4.hpp.audit.md` and `Short2.hpp.audit.md` for the two other confirmed instances of this
identical defect pattern — together these three files establish a clear, systematic (not
one-off) pattern worth a project-wide fix pass covering all three at once.

## Missing or Weak Tests
A test asserting `Short4::Pack()`'s output for a non-integer input would have caught this; not
independently located in this pass.

## Positive Findings
Bit-layout and unpack (`ToVector4()`) are correct.

## Final Assessment
One MEDIUM finding: `Pack()` lacks a rounding step present in FNA and in the "normalized" sibling
types in this directory. This is the third of three confirmed instances of the same defect
pattern (alongside `Byte4` and `Short2`) in this shard.
