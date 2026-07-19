# Audit: include/Microsoft/Xna/Framework/Graphics/PackedVector/Alpha8.hpp

## Metadata
- Source file: `include/Microsoft/Xna/Framework/Graphics/PackedVector/Alpha8.hpp`
- Audit status: AUDITED (full read, 75 lines)
- Subsystem: `xna-graphics` shard
- File type: C++ header
- XNA/FNA relevance: Direct XNA type; FNA reference: `src/Graphics/PackedVector/Alpha8.cs`
- Main related tests: not independently located in this pass

## Purpose
Packed vector storing a single normalized alpha value as an 8-bit unsigned integer.

## Executive Verdict
Correct. `Pack()`'s formula (`clamp(v,0,1)*255+0.5, truncated`) and `ToVector4()`/`ToAlpha()`'s
unpack formula (`packedValue/255.0f`) match FNA's `Pack`/`ToAlpha`/`ToVector4` exactly in effect.

## Checklist Results
No issues found beyond the shard-wide rounding-tie note below.

## Detailed Findings

### Shard-wide note (canonical explanation — cross-referenced from every other `PackedVector`
report in this batch that shares the same pattern): rounding-tie divergence, LOW severity
CNA's `Pack()` here (and in `Bgr565`/`Bgra4444`/`Bgra5551`/`Rg32`/`Rgba1010102`/`Rgba64`) rounds via
`clamp(v,0,1)*N + 0.5f` truncated to an integer — "round half away from zero" for positive
inputs. FNA's real `Pack()` uses `(byte)Math.Round(Clamp(v,0,1)*255.0f)`, and .NET's
`Math.Round(double)` defaults to `MidpointRounding.ToEven` (banker's rounding). The two rounding
modes diverge ONLY at exact integer-plus-0.5 packed values (e.g. a channel value that lands
exactly on `127.5`): CNA's add-and-truncate always rounds such a tie up (e.g. to 128), while FNA's
banker's rounding rounds to the nearest *even* integer (128 in this case, but 126 for a tie at
`126.5`). This is a real, narrow, mostly-cosmetic divergence — affecting at most 1 packed value in
256/64/32/1024/etc. per channel, only when the input float lands exactly on a half-integer packed
boundary. Rated LOW (not MEDIUM) because it requires a precise, deliberately-crafted input to
trigger, unlike the MEDIUM `Byte4`/`Short2`/`Short4` findings below (which have NO rounding at all
and diverge for the much broader range of ALL non-integer inputs, not just exact-.5 ties).

## Cross-File Observations
This exact `+0.5f`-then-truncate pattern recurs in `Bgr565`, `Bgra4444`, `Bgra5551`, `Rg32`,
`Rgba1010102`, and `Rgba64` — all six share the identical LOW-severity rounding-tie note, recorded
here once as the canonical explanation. By contrast, `NormalizedByte2`/`NormalizedByte4`/
`NormalizedShort2`/`NormalizedShort4` use `std::lroundf` (round-half-away-from-zero, an even
closer match to `Math.Round`'s intent than the add-0.5 pattern), and `Byte4`/`Short2`/`Short4` use
NO rounding at all (a materially more severe divergence — see those files' own reports for the
confirmed MEDIUM findings).

## Missing or Weak Tests
A test asserting `Pack()`'s exact output at a deliberately-chosen half-integer boundary value
would surface the rounding-tie divergence; not independently located in this pass.

## Positive Findings
Core pack/unpack arithmetic is otherwise exactly FNA-equivalent.

## Final Assessment
One LOW finding (shared across 6 files in this batch): round-half-up vs. round-half-to-even
tie-breaking divergence from FNA's `Math.Round`, narrow in practical impact.
