# BC7 (BPTC) decoder: choice, license, and build impact

SKIA-141 promotes `Bc7EXT`/`Bc7SrgbEXT` for Skia `Texture2D`. This note records the decoder
evaluation and decision the task requires, separately from the surface-format matrix's byte
contract.

## Decoder choice

`CNA::Internal::Graphics::Bc7Util` (`include/CNA/Internal/Graphics/Bc7Util.hpp`,
`src/CNA/Internal/Graphics/Bc7Util.cpp`) is a from-scratch decoder implemented directly from the
public **Khronos Data Format Specification**'s BPTC section (the same normative text later
folded into `KhronosGroup/DataFormat`'s `bptc.txt`), not a vendored third-party library. It
covers:

- exact bit-layout extraction for all 8 BC7 modes (mode/partition/rotation/index-selection bits,
  colour/alpha endpoint fields, per-endpoint and shared P-bits, primary/secondary indices),
  transcribed from the specification's own `table-bptcmodebits` bit-range table;
- the 64-entry 2-subset and 3-subset partition tables (`bptcP2subset`/`bptcP3subset`) and their
  anchor-index tables (`bptcA2index`/`bptcA32index`/`bptcA33index`), transcribed verbatim from
  the specification's own numeric tables;
- the documented endpoint-precision expansion (P-bit insertion, MSB-replication to 8 bits) and
  interpolation formula (`((64-weight)*e0 + weight*e1 + 32) >> 6` with the 2/3/4-bit weight
  tables);
- the documented reserved-mode fallback (a block whose low byte is entirely zero) as a
  deterministic all-zero decode, matching the specification's "hardware decoders... should
  return 0 for all channels" guidance.

This mirrors the project's existing `DxtUtil` (DXT1/3/5), which is likewise a from-scratch
decoder against the public format description rather than a vendored library.

## Why not a third-party decoder

Several public-domain/permissively-licensed BC7 decoders exist (for example Microsoft's MIT-
licensed DirectXTex, or Rich Geldreich's public-domain `bc7decomp`). None were vendored:

- **No new build/dependency surface.** Every other compressed-format route in this backend
  (`DxtUtil`) is already a from-scratch, license-free implementation; adding a vendored BC7
  decoder would introduce this backend's first third-party codec dependency for a format that is
  fully and precisely specified in public documentation, when the specification alone is
  sufficient to implement it correctly.
- **Exact behavioural control.** CNA's contract requires exact byte-for-byte decode matching the
  documented algorithm (see `docs/skia-surface-format-matrix.md`'s "no other codec, zero fill, or
  stale RGBA8 content is used as a fallback" decision); a from-scratch implementation keeps that
  contract auditable against the cited specification text line-for-line, rather than trusting an
  external implementation's own (possibly undocumented) rounding/edge-case choices.
- **License-compatibility is moot.** A from-scratch implementation against a public technical
  specification (not copied source) carries no third-party license to reconcile with this
  project's MS-PL, unlike vendoring an external decoder would.

## Build impact

None. `Bc7Util.cpp`/`Bc7Util.hpp` are plain C++23 translation units alongside the existing
`DxtUtil.cpp`/`.hpp` in `src/CNA/Internal/Graphics/` and `include/CNA/Internal/Graphics/`; they
compile into the existing `CNA` static library with no new dependency, submodule, or vendored
directory. `SkiaTextureBackend.cpp` (in the separate `cna_backend_graphics_skia` static library)
calls into `Bc7Util::DecompressBc7`; making that cross-library symbol resolvable required adding
an explicit `target_link_libraries(${BACKEND_TARGET} PRIVATE CNA)` edge in
`cmake/BackendLibraries.cmake`'s `SKIA` branch (a mutual static-library dependency that
`CnaLibrary.cmake` did not previously declare in this direction) -- see that file's own comment
for the empirical link failure that surfaced it.

## Verification

Because BC7 is unusually easy to get subtly wrong (eight distinct modes with different
partition/endpoint/index bit layouts), the bit-layout table and all five numeric tables were
cross-checked field-by-field against the specification text for every mode before being wired
into the backend, and the parsed partition/anchor tables were validated against the
specification's own worked example (mode 2, partition 6, texel (1,2) resolves to subset 1) before
use. `Skia_Texture2D_Bc7` additionally exercises a single-subset mode (unique P-bits, exact
byte-for-byte round trip) and a two-subset mode (shared P-bits, partition-table texel assignment)
through the live `Bc7Util`/`SkiaTextureBackend` path, not only through the specification
cross-check.

## Conformance / regression coverage

`Skia_Texture2D_Bc7` (`examples/skia_texture2d_bc7_test.cpp`) covers exact compressed-block
transfer, sRGB colour-space handling (`Bc7SrgbEXT` decodes the identical bit pattern that stays
unchanged as `Bc7EXT`), block-alignment/NPOT-edge validation (reusing the same policy SKIA-140
established for Dxt1/3/5), malformed/undersized input rejection, the deterministic reserved-mode
fallback, decoded public `SpriteBatch` sampling, and continued `RenderTarget2D` refusal pending
SKIA-142.
