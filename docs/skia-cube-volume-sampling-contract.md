# Skia cube and volume sampling contract

Status: normative SKIA-144 contract for Phase S15 (SKIA-144–151). Fixes every ABI, orientation,
addressing, filtering, mip/LOD, precision, and resource-limit decision before any sampling code is
written. SKIA-145/147 implement the two formulas below; SKIA-149 wires them into the public
`ShaderEffect`/`SetTexture` surface. Nothing here changes `TextureCube`/`Texture3D` CPU storage
(`docs/skia-texture-storage.md`, SKIA-80–86), which remains exact and unaffected.

## Non-goals

- This does **not** promote stock 3D effects, `EnvironmentMapEffect`, real vertex processing, or
  general `samplerCUBE`/`sampler3D` GLSL translation. It is a bounded 2D-fragment-only extension of
  the existing explicit `CNA_SKIA_SKSL_V1` ABI (`docs/skia-effects.md`).
- At most **one** bound cube map and **one** bound volume texture per compiled effect. An effect
  needing two cube maps (e.g. environment + irradiance) is out of scope for this phase; SKIA-151
  records this as a documented capability limit, not a bug.
- No native Skia cube/volume image type is used (none exists for a CPU raster surface). Both are
  emulated entirely from ordinary 2D child shaders, matching `skia-texture-storage.md`'s existing
  "no `BindGL`, no native handle" policy for the plain storage backends.

## Why this needs its own reserved child namespace

`SkiaEffectBackend`'s existing ABI (SKIA-91/92) already fixes `cnaTexture0`–`cnaTexture7`: exactly
8 total `uniform shader` children, `cnaTexture0` reserved for the primary SpriteBatch texture and
`cnaTexture1`–`cnaTexture7` for `SetTexture(unit, Texture2D)`. Cube sampling alone needs six
simultaneous child images (SKIA-145's "six 2D child shaders"), which would consume 6 of the
existing 8 slots and leave essentially nothing for ordinary 2D textures in the same effect.

Cube and volume children therefore get their **own reserved names**, orthogonal to and independent
of the `cnaTexture0`–`cnaTexture7` budget:

| Name(s) | Count | Bound by | Declared type |
|---|---:|---|---|
| `cnaCubeFace0` … `cnaCubeFace5` | 6 | `SetTexture(unit, TextureCube)` | `uniform shader` |
| `cnaVolumeAtlas0` | 1 | `SetTexture(unit, Texture3D)` | `uniform shader` |
| `cnaVolumeAtlasMeta0` | 1 | supplied automatically, not by the caller | `uniform float4` (see below) |

`CompileProgram` accepts an effect that declares all six `cnaCubeFace*` names (all-or-nothing; a
partial declaration is a compile error, matching the existing "unique, exact name" `cnaTexture*`
policy), and/or `cnaVolumeAtlas0` plus its paired `cnaVolumeAtlasMeta0` uniform, in addition to
`cnaTexture0`–`cnaTexture7`. A future `unit` index beyond 0 for cube/volume (a second bound cube,
for instance) is unsupported and out of scope, matching the non-goals above; `SetTexture` accepts
only `unit == 1` for a `TextureCube`/`Texture3D` binding (the first and only non-primary unit,
reusing the same numbering space `SetTexture(unit, Texture2D)` already uses, but a cube/volume and
an ordinary `Texture2D` bound to the same numeric `unit` are mutually exclusive within one effect
compile).

Total possible declared children in one compiled effect is therefore at most 8 (`cnaTexture*`) + 6
(`cnaCubeFace*`) + 1 (`cnaVolumeAtlas0`) = 15, comfortably inside Skia's own runtime-effect child
limits and consistent with this codebase's existing pattern of small, explicit, checked constants
(`kSkiaSkslMaxTextureUnitEXT` and friends in `SkiaResourcePolicy.hpp`; SKIA-145/147 add matching
`kSkiaSkslMaxCubeFaceChildrenEXT = 6` / `kSkiaSkslMaxVolumeAtlasChildrenEXT = 1`).

## CNA-provided sampling helper functions, not author-written math

Effect authors do not write the dominant-axis or atlas-lookup math themselves. `CompileProgram`
prepends a fixed, versioned SkSL preamble (source-controlled, not user input) to the author's
fragment source before compilation, declaring:

```
half4 cnaSampleCubeEXT(float3 dir);       // present only when all six cnaCubeFace* are declared
half4 cnaSampleVolumeEXT(float3 uvw);     // present only when cnaVolumeAtlas0 is declared
```

The author's own SkSL computes whichever `dir`/`uvw` value their effect needs (from `coords`, their
own uniforms, and ordinary SkSL arithmetic -- exactly as any other bounded 2D fragment value is
computed today) and calls the helper. This keeps the face-selection and atlas-lookup formulas in
one CNA-controlled, auditable, testable place rather than delegated to and duplicated by every
effect author, and lets SKIA-150's public sampling oracles exercise the real preamble instead of a
per-test reimplementation.

## Cube: direction-to-face/UV formula

Face indexing matches `CubeMapFace`/`ITextureCubeBackend::SetData`'s existing order exactly:
`0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z` (`cnaCubeFace0`=+X … `cnaCubeFace5`=-Z).

Given a (not necessarily normalized) direction `dir = (x, y, z)`, dominant-axis selection picks the
component with the largest absolute value; that determines face and sign, matching every major
graphics API's cube-map hardware (D3D, GL, Vulkan, Metal all agree on *which* axis dominates --
this part carries no real ambiguity). The per-face `(u, v)` derivation below is the classic D3D
cube-map table (Microsoft's legacy `D3DCUBEMAP_FACES` documentation), chosen because CNA must
reproduce *XNA's* observable cube sampling, and XNA's public behavior was defined against D3D9:

| Face | Condition | `ma` | `uc` | `vc` |
|---|---|---|---|---|
| `+X` (0) | `|x| >= |y|, |x| >= |z|, x >= 0` | `x` | `-z` | `-y` |
| `-X` (1) | `|x| >= |y|, |x| >= |z|, x < 0` | `-x` | `z` | `-y` |
| `+Y` (2) | `|y| >= |x|, |y| >= |z|, y >= 0` | `y` | `x` | `z` |
| `-Y` (3) | `|y| >= |x|, |y| >= |z|, y < 0` | `-y` | `x` | `-z` |
| `+Z` (4) | `|z| >= |x|, |z| >= |y|, z >= 0` | `z` | `x` | `-y` |
| `-Z` (5) | `|z| >= |x|, |z| >= |y|, z < 0` | `-z` | `-x` | `-y` |

Ties (e.g. `|x| == |y| == |z|`, a cube corner) resolve to the first matching row in the table above
(X before Y before Z, positive before negative within an axis) -- deterministic, not
implementation-defined, so a corner-direction sample is reproducible across runs and platforms.

```
u = 0.5 * (uc / ma + 1.0)
v = 0.5 * (vc / ma + 1.0)
```

Because every `vc` above uses `-y` or `-z` (never `+y`/`+z`), this table already expresses `v` with
`v=0` at the visual **top** of each face in D3D's native top-down texture convention -- which is
also CNA's own established top-row-first storage convention for every 2D image, including cube
faces (`ITextureCubeBackend::SetData`'s own doc comment: "top row first"). No additional V-flip is
applied when sampling a `cnaCubeFace*` child with this `(u, v)`.

**This table is the design hypothesis, not yet proven.** Per this project's established rule that
an orientation/winding decision needs pixel evidence rather than derivation alone (see
`feedback_sampling_quad_winding_must_be_clockwise.md`-class precedent elsewhere in this codebase),
SKIA-145 must render six distinct known solid-colour faces and sample a set of known reference
directions (face centers, edge midpoints, and at least one corner) through the real compiled
`cnaSampleCubeEXT` preamble before this table is considered confirmed rather than hypothesized. If
empirical results disagree with this table, SKIA-145 corrects the table here rather than silently
patching around a wrong formula at the call site.

## Volume: 3D-coordinate-to-atlas formula

`Texture3D` CPU storage is one contiguous `width * height * depth` RGBA8 buffer per level
(`SkiaTexture3DBackend::Level::voxels`); there is no native Skia 3D image type, so `SetTexture(1,
Texture3D)` packs every depth slice of the bound level into one 2D grid atlas image and binds it as
the single `cnaVolumeAtlas0` child (SKIA-147's "bounded slice atlas").

**Grid layout.** For a volume level of size `(w, h, d)`:

```
cols = ceil(sqrt(d))
rows = ceil(d / cols)
atlasWidth  = w * cols
atlasHeight = h * rows
```

favoring a roughly square atlas (minimizes the larger of the two axes for a given `d`, keeping the
existing 16384-axis and 256 MiB per-resource ceilings reachable for realistic volume sizes; a naive
single-column vertical strip (`atlasHeight = h * d`) blows the 16384-axis ceiling for even modest
`h`/`d`, e.g. `h=256, d=256`). Slice index `s` (`0 <= s < d`) occupies grid cell `(s % cols, s /
cols)`; its tile origin in unpadded atlas pixels is `((s % cols) * w, (s / cols) * h)`.

**Padding.** Trilinear interpolation samples two adjacent slices with ordinary bilinear (hardware)
filtering within each; if two slices land in different, non-adjacent grid cells, a naive bilinear
sample near a tile edge would blend with an *unrelated* neighboring slice's texels ("atlas
bleeding", SKIA-147's explicit "atlas padding cannot bleed between slices" requirement). Every tile
is therefore packed with a **1-texel border on every edge**, replicated from that tile's own edge
texels (matching CNA's `Clamp` addressing semantics, not zero/black), giving each tile true size
`(w + 2, h + 2)` and revised layout:

```
atlasWidth  = (w + 2) * cols
atlasHeight = (h + 2) * rows
tileOriginX(s) = (s % cols) * (w + 2) + 1   // +1 skips this tile's own left border
tileOriginY(s) = (s / cols) * (h + 2) + 1   // +1 skips this tile's own top border
```

A bilinear sample using per-tile-clamped UV (clamped to the tile's own `[0, w] x [0, h]` interior,
*not* the whole atlas) can never read outside its `(w+2, h+2)` bordered cell, so it can never blend
with a different slice's texels regardless of `Wrap`/`Mirror`/`Clamp` addressing requested by the
caller (address mode applies only to how CNA computes the incoming `(u, v, w)` coordinate before
this per-tile clamp, never to the atlas sampling itself, which always effectively clamps at the
tile boundary).

**W-axis (slice) selection.** Given normalized volume coordinate `(u, v, w)` in `[0, 1]^3` and
`sampleF = w * d - 0.5`, the two slices to blend are `s0 = clamp(floor(sampleF), 0, d - 1)` and `s1
= clamp(s0 + 1, 0, d - 1)`, with blend weight `wf = clamp(sampleF - floor(sampleF), 0, 1)`. This is
the standard half-texel-centered mip/slice convention already used elsewhere in this backend
(`docs/skia-successor-resource-oracles.md`'s mip-selection precedent), so `w=0` samples exactly
slice 0's centre and `w=1` samples exactly slice `d-1`'s centre with no half-slice bias. Address
mode determines how `w` itself is derived from a caller value outside `[0, 1]` (`Clamp` clamps `w`
before this formula; `Wrap` takes `frac(w)`; `Mirror` folds `w` into `[0, 1]` by triangle-wave
reflection) -- `u`/`v` addressing is independent per axis and applied identically before each of
the two per-slice bilinear samples.

`cnaSampleVolumeEXT(uvw)` therefore performs: compute `(s0, s1, wf)` from `uvw.z`; compute the
per-tile-clamped `(u, v)` from `uvw.xy` under the requested `u`/`v` address mode; sample
`cnaVolumeAtlas0` once inside tile `s0` and once inside tile `s1` (both ordinary hardware-bilinear
2D samples, giving the "eight-voxel interpolation" SKIA-148 requires: 4 texels from each of 2
slices); linearly blend the two results by `wf`.

**Metadata uniform.** `cols`, `rows`, `w`, `h`, `d` (five integers) do not fit the existing
`uniform float4 cnaTint`-style reserved-uniform pattern in one vector, and must vary per bound
`Texture3D` (unlike the fixed cube-face table, which needs no runtime parameter). `SetTexture(1,
Texture3D)` therefore also writes a CNA-reserved, non-author-settable `uniform float4
cnaVolumeAtlasMeta0` = `(cols, rows, 1.0 / atlasWidth, 1.0 / atlasHeight)` alongside the atlas
image binding; `cnaSampleVolumeEXT` reads it to locate tiles. Like `cnaTint`, an author's
`SetUniformFloat4("cnaVolumeAtlasMeta0", ...)` call is rejected as reserved.

## Addressing

| Axis | Cube | Volume |
|---|---|---|
| Per-face `(u, v)` | Always effectively `Clamp` at each face's own edge (the dominant-axis formula already keeps `(u, v)` inside `[0, 1]` except at exact corner/edge directions, and D3D/XNA hardware cube samplers ignore the declared `AddressU/V/W` for cube maps -- CNA matches this rather than inventing seam-wrapping behavior XNA never had). | Independent `Clamp`/`Wrap`/`Mirror` per `u`/`v`/`w` axis, matching ordinary `Texture3D`/`SamplerState` semantics; each is applied to the *volume-normalized* coordinate before the per-tile atlas clamp described above. |

## Filtering and mip/LOD

- **Point**: nearest-face (cube) or nearest-slice-pair-collapsed-to-nearest (volume: `wf` rounds to
  0 or 1 instead of blending) sampling, with `SkFilterMode::kNearest` for the underlying 2D atlas
  sample, matching `TextureFilter::Point`'s existing meaning elsewhere in this backend.
- **Linear**: the full formulas above (hardware-bilinear per slice/face, manual `w`-blend for
  volume). This is the default and the only mode SKIA-145/147's initial pixel oracles need to prove
  exactly; `Point` reuses the same face/tile-selection math with interpolation disabled.
  matching `TextureFilter::Point`'s existing meaning elsewhere in this backend.
- **Mip/LOD**: reuses SKIA-129's existing affine-rho LOD selection unchanged for the *face-local* or
  *tile-local* 2D sample -- each mip level of a cube face or volume slice is an ordinary 2D image,
  so within one already-selected face/slice pair, mip selection is not a new formula. Selecting
  *which mip level's* atlas/face set to sample from is a new per-effect LOD scalar (SKIA-146/148),
  computed the same affine-rho way from the destination draw's screen-space derivative of `dir`/
  `uvw}`, then rounded/clamped to `[0, levelCount - 1]` and used to pick that level's already-built
  atlas/face children -- CNA does not implement continuous inter-mip blending (`GL_LINEAR_MIPMAP_
  LINEAR`-style trilinear-across-levels) for cube/volume in this phase; only inter-mip *selection*
  is in scope, matching `TextureFilter`'s existing documented mip granularity for ordinary
  `Texture2D` in this backend.

## Precision

Matches the established Skia adapter convention (`docs/skia-effects.md`'s "Runtime arithmetic uses
Skia's float/half pipeline, while input/output surfaces remain premultiplied RGBA8"): all
direction/coordinate/weight math above runs in SkSL `float`; sampled colour values are `half4`
(SkSL's default runtime-effect precision); cube faces and volume atlases are `Color`-only RGBA8
storage (`SurfaceFormat::Color`, matching the existing `docs/skia-texture-storage.md` storage
restriction -- SKIA-144 does not extend cube/volume *storage* to the SKIA-135–142 promoted format
set, only adds a sampling path for the one format already supported).

## Resource limits

- Cube: 6 face `SkImage`s snapshotted directly from the existing exact per-face CPU storage, no
  additional shadow copy -- identical bytes to what `docs/skia-texture-storage.md` already accounts
  for `(sum(levelWidth * levelHeight * 4 * 6))`.
- Volume: the packed grid atlas is a **new, additional** allocation distinct from the existing exact
  linear voxel storage (the atlas layout does not match the linear buffer's memory order, so it
  cannot alias it). Atlas bytes = `atlasWidth * atlasHeight * 4` per bound level using the padded
  formula above. This is accounted as its own `SkiaResourceStats` category (`volumeAtlases` /
  `volumeAtlasBytes`, SKIA-147) under the same checked-arithmetic, 256 MiB-per-resource policy as
  every other Skia CPU allocation (`SkiaResourcePolicy.hpp`), and is retained only while a
  `Texture3D` is actually bound to an effect -- unbinding or rebinding a different `Texture3D`
  releases the previous atlas rather than caching one atlas per ever-bound volume.
- A `Texture3D` whose *unpadded* size already sits within the 256 MiB storage limit can still have a
  padded atlas that does not (padding overhead approaches `((w+2)(h+2))/(wh) - 1`, negligible for
  large slices but proportionally large for tiny ones e.g. `w=h=2`). `SetTexture(1, Texture3D)`
  checks the padded atlas size independently and throws `NotSupportedException` before allocating
  if it would exceed budget, even though the same volume's plain storage already fit -- matching
  this codebase's established transactional-refusal pattern (no partial state change on rejection).

## What SKIA-145–151 each still owe

This document fixes the contract; none of the following exist yet and are unimplemented until their
own task lands:

- `cnaSampleCubeEXT`/`cnaSampleVolumeEXT` preamble source and `CompileProgram` child-name parsing
  (SKIA-145 cube, SKIA-147 volume).
- Empirical pixel confirmation (or correction) of the cube face/UV table above (SKIA-145) and the
  volume padding/blend formulas above (SKIA-147/148).
- `RenderTargetCube`/mip/seam/snapshot-invalidation policy for a cube bound as a *sampling* source
  rather than a draw destination (SKIA-146).
- `BindTextureCube`/`BindTexture3D` no longer throwing `ThrowSkiaUnsupported3D`, with weak lifetime
  tracking matching the existing `BindTexture` pattern (SKIA-149).
- Public sampling oracles, `SkiaResourceStats` accounting fields, and cross-backend/Release/
  sanitizer evidence (SKIA-150).
- Final capability wording distinguishing this bounded sampling subset from general cube/volume/3D
  support (SKIA-151); `GraphicsCapability::Texture3D`/`ThreeD`/`CustomEffects` reporting is
  unaffected by this document alone.
