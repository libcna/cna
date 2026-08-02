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
