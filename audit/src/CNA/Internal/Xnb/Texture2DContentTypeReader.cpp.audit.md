# Audit: src/CNA/Internal/Xnb/Texture2DContentTypeReader.cpp

## Metadata
- Source file: `src/CNA/Internal/Xnb/Texture2DContentTypeReader.cpp`
- Audit status: AUDITED (full read, 160 lines)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ implementation
- XNA/FNA relevance: matches FNA's `Texture2DReader`, plus FNA's own legacy (pre-4.0) SurfaceFormat-ordinal
  remapping for older `.xnb` versions
- Main related tests: not independently located in this pass

## Purpose
Implements `Texture2DReader::Read()`: surface format (with legacy-version remapping), width/height/level
count, per-level byte-count-prefixed pixel data (DXT1/3/5 software-decompressed via the existing
`DxtUtil`), uploaded via `Texture2D::SetData()`.

## Executive Verdict
Healthy -- explicit, correctly-reasoned adversarial-input hardening (cited plans/plan_xnb.md XNB-43) against the
classic "negative-times-negative dimension overflow" trick, plus a genuine post-decompress byte-count
cross-check before ever indexing into decoded pixel data.

## Checklist Results

### Dimension-overflow hardening: correctly implemented
`width <= 0 || height <= 0` is checked *individually* (line 64) before the multiplicative byte-size check
-- correctly closing the classic bug where two negative dimensions multiply to a small, deceptively
"in-range" positive product that would otherwise slip past a naive `width*height*4 <= limit` check. The
byte-size product itself is computed in `int64_t` (line 68-69) specifically so a large-but-still-`int32_t`-
representable width/height pair can't wrap the multiplication itself before the comparison happens.

### Post-decompress bounds check before pixel indexing: correct
After DXT decompression (or for the uncompressed `Color` path), `bytes.size() !=
pixelCount*4` is explicitly checked (line 132) before the per-pixel `bytes[o]/[o+1]/[o+2]/[o+3]` indexing
loop -- correctly catches a truncated/adversarial file whose declared per-level `byteCount` doesn't actually
match `levelWidth`/`levelHeight`'s required size, rather than reading past `bytes`' actual allocation.

### `levelCount`-driven loop: naturally bounded (consistent with this project's established pattern)
No explicit upper-bound check on `levelCount` before the mip-level loop, but each iteration requires a real
`ReadBytesExactOrThrow()` call that throws on stream exhaustion -- the same "naturally bounded by the file's
own real size" pattern already established safe elsewhere in this codebase (`CurveContentTypeReader`,
`NetDiscoveryProtocol`).

### Legacy SurfaceFormat remapping: correctly scoped
`ReadSurfaceFormat()`'s pre-version-5 branch remaps the historical pre-XNA-4.0 ordinal values (1/28/30/32)
to their modern `SurfaceFormat` equivalents, matching FNA's own documented legacy-content compatibility
path, with an explicit `ContentLoadException` for any other legacy ordinal rather than silently
misinterpreting it.

### Cross-file dependency worth confirming later (not independently verified in this pass)
`texture.SetData(level, nullptr, colors.data(), 0, pixelCount)` trusts `Texture2D::SetData()` (Task #4,
`Microsoft::Xna::Framework::Graphics` area, not yet audited) to itself validate `level` against the
texture's actual constructed mip-level count. If the file's own claimed `levelCount` were larger than what
`Texture2D`'s constructor (given only a `bool useMipmaps` derived from `levelCount > 1`, not the literal
count) actually allocates internally, an unvalidated `SetData(level, ...)` call for an out-of-range `level`
could misbehave. Flagging as a cross-reference to confirm when `Texture2D.cpp` is audited under Task #4,
not asserting a confirmed bug here (FNA's own real `Texture2D.SetData` does validate `level` against
`LevelCount`, so the CNA port would be expected to match).

## Detailed Findings
None confirmed in this file; see the cross-file dependency note above for a Task #4 follow-up.

## Cross-File Observations
Correctly reuses `CNA::Internal::Graphics::DxtUtil` (already audited, confirmed bit-for-bit correct) rather
than reimplementing DXT decompression.

## Missing or Weak Tests
Not independently located in this pass; a test with mismatched `byteCount` vs. `levelWidth`/`levelHeight`
would directly exercise the line-132 guard.

## Positive Findings
Genuinely careful, explicitly-cited adversarial-input hardening against a real, non-obvious integer-overflow
class (negative-dimension multiplication) -- one of the more rigorously defended parsers in this shard.

## Final Assessment
No issues found in this file; one cross-file dependency (`Texture2D::SetData()`'s own level-bounds
validation) flagged for confirmation when Task #4 reaches that file.
