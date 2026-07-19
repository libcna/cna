# Audit: src/CNA/Internal/Xnb/TextureCubeContentTypeReader.cpp

## Metadata
- Source file: `src/CNA/Internal/Xnb/TextureCubeContentTypeReader.cpp`
- Audit status: AUDITED (full read, 125 lines)
- Subsystem: `cna-internal-core` shard (Xnb)
- File type: C++ implementation
- XNA/FNA relevance: matches FNA's `TextureCubeReader`
- Main related tests: not independently located in this pass

## Purpose
Implements `TextureCubeReader::Read()`: same shape as `Texture2DReader`/`Texture3DReader`, iterating 6
cube-map faces each with its own mip chain.

## Executive Verdict
Needs attention -- **HIGH severity: missing the post-read byte-count validation its two sibling readers
both correctly have**, allowing a crafted `.xnb` TextureCube asset with an uncompressed `Color` face whose
declared per-level `byteCount` is smaller than `faceSize*faceSize*4` to trigger a genuine out-of-bounds heap
read.

## Checklist Results

### HIGH: missing byte-count-vs-pixelCount validation before uncompressed pixel unpacking
Compare this file's pixel-unpacking loop (lines 99-108) against the byte-for-byte equivalent point in
`Texture2DContentTypeReader.cpp` (lines 122-139) and `Texture3DContentTypeReader.cpp` (lines 115-129): both
siblings explicitly check `bytes.size() != <required size>` and throw `ContentLoadException` *before*
indexing into `bytes` to unpack `Color` values. This file has **no such check** -- after
`std::vector<uint8_t> bytes = input.ReadBytesExactOrThrow(byteCount, "TextureCubeReader");` (line 79), for
an uncompressed `SurfaceFormat::Color` face/level, `bytes` is used exactly as read (size == the file's own
claimed `byteCount`, entirely independent of `faceSize`) directly in:

```cpp
const int32_t pixelCount = faceSize * faceSize;
std::vector<Color> colors;
colors.reserve(static_cast<std::size_t>(pixelCount));
for (int32_t i = 0; i < pixelCount; ++i)
{
    const std::size_t o = static_cast<std::size_t>(i) * 4;
    colors.emplace_back(bytes[o], bytes[o + 1], bytes[o + 2], bytes[o + 3]);
}
```

`std::vector::operator[]` is **unchecked** access. If a malicious/corrupt `.xnb` file declares a `byteCount`
smaller than `faceSize*faceSize*4` for an uncompressed face/level (trivial to craft: the format is entirely
attacker-controlled, `byteCount` and `faceSize`/`levels` are independent fields read directly off the wire
with no cross-validation anywhere before this loop), `o` quickly exceeds `bytes.size()` and this reads
arbitrary heap memory past the vector's actual allocation -- a genuine out-of-bounds read (crash risk if it
walks into an unmapped page; otherwise, arbitrary heap bytes get unpacked as `Color` values and uploaded to
the GPU as texture pixels, a memory-disclosure vector: heap contents become visible on screen/in a
screenshot).

The compressed (`Dxt1`/`Dxt3`/`Dxt5`) path is **not** affected: `DxtUtil::DecompressDxtN()` (already
confirmed, earlier in this shard, to throw if its input `dataSize` is smaller than the block-count-derived
minimum for the claimed width/height) always either throws before producing output or produces exactly
`faceSize*faceSize*4` decompressed bytes -- the same reasoning `Texture2DContentTypeReader.cpp`'s own
comment gives for why only its uncompressed branch needs the explicit check. `TextureCubeContentTypeReader`
needs that same check for the identical reason and is simply missing it -- comparing the three files
side-by-side makes this read as an omission during porting/refactoring, not an intentional difference in
scope or threat model.

**Fix shape**: add the identical guard used in the sibling readers, immediately after the (optional)
decompression step and before the pixel-unpacking loop:
```cpp
if (bytes.size() != static_cast<std::size_t>(pixelCount) * 4)
{
    throw ContentLoadException(/* ... naming face/level/faceSize, matching sibling readers' message style */);
}
```

### Everything else: matches the sibling readers' correct patterns
`size <= 0` positivity check and `int64_t`-widened `size*size*4` decoded-byte-size check (lines 53-58)
correctly mirror `Texture2DReader`'s own dimension-overflow hardening. Mip-dimension halving
(`faceSize = max(faceSize>>1, 1)`, line 111) matches FNA. The `existingInstance`-move-or-construct pattern
matches the sibling readers.

## Detailed Findings

1. **[HIGH] Missing byte-count-vs-pixelCount validation before unpacking uncompressed `Color` pixel data**
   -- a genuine out-of-bounds heap read via a crafted `.xnb` TextureCube asset. Lines 99-108 (compare against
   `Texture2DContentTypeReader.cpp` lines 122-139 and `Texture3DContentTypeReader.cpp` lines 115-129, both
   of which have the missing check).

## Cross-File Observations
This is a clear, concrete "the sibling files got this right, this one didn't" finding -- `Texture2DReader`
and `Texture3DReader` (audited immediately before this file in this same shard) both explicitly implement
and comment on exactly the check this file lacks, for the exact same underlying reason (an adversarial file's
declared `byteCount` disagreeing with the dimensions it claims).

## Missing or Weak Tests
No test located exercising a `TextureCubeReader` face/level whose declared `byteCount` is smaller than
required -- this is exactly the test that would have caught the finding above, and (per the sibling files'
own apparent test coverage intent) should exist for `Texture2DReader`/`Texture3DReader` already.

## Positive Findings
Correctly extends the dimension-overflow hardening pattern and correctly matches FNA's own per-face mip
progression.

## Final Assessment
One HIGH-severity finding: missing byte-count validation allows an out-of-bounds heap read via a crafted
`.xnb` TextureCube asset with a mismatched uncompressed-format byte count.
