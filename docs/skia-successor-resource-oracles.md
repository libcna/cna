# Skia successor resource and oracle policy

Status: active implementation guardrail for SKIA-118–170

This policy applies to every post-baseline mip, format, sampling, effect and accelerated route. It
does not promote any feature by itself. A later task may tighten a tolerance or limit, but may not
silently widen one to obtain a passing image.

## Resource and compiler budgets

`SkiaResourcePolicy.hpp` is the single code source for these boundaries:

| Resource | Limit | Accounting and failure rule |
|---|---:|---|
| Any texture/surface axis | 1..16,384 | Validate every logical dimension before allocation or signed-to-unsigned conversion. |
| CNA-owned stores for one resource | 256 MiB | Sum every retained mip, face, slice, conversion shadow and resolve store with checked `size_t` arithmetic. A transient store that overlaps retained data is part of peak accounting. |
| Tagged SkSL source | 64 KiB | Reject before invoking the runtime compiler. |
| Reflected SkSL uniform block | 16 KiB | Reject the compiled effect before publishing it. |
| Reflected uniforms / texture children | 64 / 8 | Names and types remain separately validated; these counts are ceilings, not ABI promises. |

`CheckedSizeMultiply`, `CheckedSizeAdd`, `CheckedTexelBytes2D/3D`, and
`CheckedSkiaResourceAccumulate` preserve their result argument on failure. Constructors must
preflight the complete resource before registering counters or exposing a backend. Transfer
validation must complete before modifying storage or caller memory. `SkiaResourceStats` reports
exact CNA-owned retained stores; opaque internal Skia/driver allocations are never guessed into
those counters and accelerated tasks must expose their own separate cache/budget evidence.

### Checked 2D mip-chain storage (SKIA-125)

`SkiaMipChain2D` is the common CNA-owned representation for the Texture2D/RenderTarget2D mip work
that follows. It owns one zero-initialized contiguous byte allocation and an immutable descriptor
per level: width, height, row bytes, byte offset, and byte count. Both dimensions use floor halving
with a lower bound of one until the chain reaches 1×1, so odd/NPOT and one-dimensional resources
have one deterministic layout. No vector is resized after construction, making every level address
stable for the lifetime of the chain.

`TryBuildSkiaMipChain2DLayout` performs the complete layout preflight without allocating texels.
It distinguishes invalid dimensions, zero bytes per texel, host-size overflow, and the resource
budget. Failure leaves the caller's descriptors and byte total unchanged. The owning constructor
allocates only after that preflight and publishes `SkiaResourceStats::mipChains2D` and
`mipChain2DStorageBytes` only after allocation succeeds; RAII removal returns both to baseline.
Consequently invalid, over-budget, overflow, and allocation failures cannot leak a partial resource
or accounting update.

`Skia_MipChain2D_Raster` proves odd 7×5 offsets, 1×9/9×1 chains, level-zero-only storage,
zero initialization and isolation, the exact 256-MiB descriptor boundary, descendant-over-budget
and row-overflow atomicity, axis errors, out-of-range level access, and live/released counters.
SKIA-126 uses this storage for public `Texture2D(..., mipMap=true, Color)` construction and exact
`LevelCount` reporting. SKIA-127 adds exact full/partial upload and readback for every level,
including result-preserving invalid requests and caller-memory isolation.

### Deterministic Texture2D generation (SKIA-128)

A changed level eagerly marks its unauthored descendants dirty and rebuilds them in ascending
order. Each target texel uses integer area partitions
`[floor(i*source/target), floor((i+1)*source/target))` on both axes, so an odd final row or column
contributes exactly once. Canonical straight RGBA8 channels are averaged independently and rounded
to nearest with `(sum + count/2) / count`; no Skia colour- or alpha-conversion path participates.

A full or partial public write makes that level caller-authored. It becomes an ownership barrier:
later changes to an ancestor stop before it, while its own changes may regenerate following
unauthored descendants until the next authored barrier. A partial first write to a generated level
seeds its public shadow from the backend's complete defined bytes before patching, preserving every
untouched texel. `Skia_Texture2D_MipGeneration` locks exact 7×5→3×2→1×1 bytes, odd-edge updates,
dirty-only rebuild counts, partial promotion, and two independent barriers.

### Deterministic Texture2D sampling (SKIA-129)

`SkiaMipSampling` decomposes each of the nine public filters into independent minification,
magnification and mip components. It measures rho from the inverse of the complete affine
local-source-pixel to device matrix, treats degenerate/non-finite transforms as level zero, clamps
lambda to the resource's real chain, resolves a nearest-mip half tie downward, and gives
mip-linear filters only the two adjacent levels and their fractional weight.

Higher mip images are synchronous no-copy raster views over stable chain bytes, so later uploads
cannot leave a retained stale snapshot or duplicate the chain against the resource budget. A fixed
two-child SkSL shader evaluates both levels at normalized odd/NPOT coordinates and interpolates
them before tint, custom effect, or blending is applied. Its strict-source bounds prevent adjacent
atlas texels from entering a crop; non-strict Clamp/Wrap/Mirror remain independent per axis.
`Skia_MipSampling_Raster` fixes the scalar/matrix decisions and
`Skia_Sampler_MipmapFilterPolicy` fixes public pixels, including generated bytes. RenderTarget2D
wiring is supplied by SKIA-131.

### Stable RenderTarget2D mip storage and resolve (SKIA-131–132)

A mipmapped target owns the same checked canonical `SkiaMipChain2D` plus one stable premultiplied
`SkSurface` for every descriptor. The complete surface-plus-shadow sum must fit 256 MiB before any
allocation or counter registration. Only level zero can become the active raster destination;
higher levels remain independently uploadable, readable and sampleable. A single snapshot cache
records its level and switches entries across the chain, so a large target cannot retain one cache
image per mip. Canvas writes synchronize level zero back into exact canonical readback bytes at the
pass boundary. A write to level zero dirties every descendant; a parent SetData dirties its suffix.
Valid readback, sampling and unbind are identical resolve barriers and materialize each dirty level
once with the same complete odd/NPOT area partition as Texture2D. Unlike ordinary texture-authored
ownership barriers, target descendants intentionally follow EasyGL resolve semantics and are
replaced by a later parent write. Common `Texture2D` target staging is therefore never authoritative.

## Pixel and precision rules

The reusable fixture is `examples/common/SkiaSuccessorOracle.hpp`.

| Operation | Accepted comparison |
|---|---|
| Public `SetData`/`GetData`, compressed blocks, packed formats, point sampling, integer clears and unfiltered copies | Exact bytes/bits; tolerance zero. |
| Bilinear or mip-linear sampling | RGB error at most 1 byte, alpha exact, and only inside an analytically declared filtered footprint. Pixels outside it remain exact. |
| Antialiased coverage/MSAA resolve | RGBA error at most 1 byte and only on an explicitly enumerated geometric edge. Interiors and exteriors remain exact. |
| Half/float transfer and readback | Exact IEEE bits, including signed zero, infinities and chosen NaN payloads. |
| Finite shader arithmetic | `abs(error) <= max(1e-6, abs(reference) * 1e-5)` for explicitly named arithmetic outputs. NaN/infinity never pass this tolerance implicitly. |

There is no global changed-pixel percentage, whole-image fuzz, or tolerance inferred after seeing a
failure. Each test declares its comparison class and variance region before rendering. EasyGL or
real XNA output remains the external semantic reference where the route exists; scalar CPU math is
used to discriminate selectors, not to redefine the public contract.

## Cross-feature oracle scenes

Each row is a minimal scene template. The owning feature tasks add real resources and public draws
to the shared template; they do not remove the other feature leg to make a regression disappear.

| Scene ID | Required interaction | Default comparison | Owning tasks |
|---|---|---|---|
| `odd-mip-format-filter` | odd/NPOT mip chain, non-Color format, target upload/readback and scaled sampling | localized bilinear RGB ±1 | SKIA-125–143 |
| `translucent-blend-target-format` | translucent source and target, arbitrary independent RGB/alpha blend, packed/HDR format | exact outside a declared filtered edge | SKIA-119–143 |
| `effect-mip-format` | custom vertex/fragment ABI, uniform, typed texture, mip and non-Color format | localized bilinear RGB ±1; bounded float math | SKIA-129–158 |
| `cube-seam-format-mip` | direction mapping across two cube faces, address mode, format conversion and mip choice | localized bilinear RGB ±1 | SKIA-144–151 |
| `volume-slice-format-mip` | adjacent volume slices, Z interpolation/addressing, format conversion and mip choice | localized bilinear RGB ±1 | SKIA-144–151 |
| `ganesh-msaa-target-parity` | raster/Ganesh target lifecycle, MSAA edge resolve, sampling and readback | enumerated edge RGBA ±1 | SKIA-159–164 |
| `ganesh-anisotropy-mips` | oblique minification, mip chain, anisotropy clamp and raster fallback control | localized filtered RGB ±1 | SKIA-159–165 |
| `mrt-effect-blend-target` | distinct effect outputs, independent masks/blends, target readback or exact refusal | exact bytes | SKIA-166–169 |

`Skia_SuccessorResource_Policy` is display-free and verifies the constants, arithmetic failure
atomicity, every tolerance discriminator, scene uniqueness, at least two feature legs per scene,
and complete feature-family coverage. It is intended to run in Debug, Release and sanitizer builds
before later tasks consume the helpers.
