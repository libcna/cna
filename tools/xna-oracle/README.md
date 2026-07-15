# XNA 4.0 oracle diff harness

`plan_dx9.md` Phase D9-A (`D9-A3`/`D9-A4`). Renders the same declarative scene twice — once
through the real XNA 4.0 runtime (`Oracle.cs`, under Wine) and once through CNA's real public
`Game`/`GraphicsDeviceManager`/`GraphicsDevice` API, using any of the 5 real XNA Stock Effects
(`BasicEffect`/`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`)
(`CnaOracleRender.cpp`, built against whichever `CNA_GRAPHICS_BACKEND` this branch targets —
`D3D9` on `feature/dx9`) — and diffs the two resulting PNGs pixel-for-pixel (`scripts/xna-diff.py`).

This is what makes "indistinguishable from real XNA" a testable claim rather than an aspiration
(`plan_dx9.md`'s own framing for Phase D9-A). Moved here from `dx9-spike/xna-oracle/Oracle.cs`
(`D9-A1`/`D9-A2`'s original spike) and rewritten to be scene-driven, per `D9-A3`'s own instruction:
"the scenes must be authored once, in a shared, declarative form, and rendered by both — not
hand-transcribed twice, or the harness will drift."

## Scene format

`scenes/*.scene` — a minimal, line-oriented, dependency-free text format both `Oracle.cs` and
`CnaOracleRender.cpp` parse identically (no JSON library needed on either side — the XNA-side
build environment is GAC-only .NET 4.0 with no NuGet). `#`-prefixed lines and blank lines are
comments; every other line is `key=value`; `vertex=`/`texturepixel=` may repeat. Unknown keys are
a hard error on both sides (not silently ignored) — a typo in a scene file should fail loudly, not
quietly change what a "match" means. See `scenes/colored3d.scene`/`scenes/textured_quad.scene` for
fully-commented examples.

| Key | Meaning |
|---|---|
| `width`, `height` | back buffer size |
| `profile` | `HiDef` or `Reach` |
| `clearcolor` | `r,g,b,a` (0-255) |
| `spritebatchmode` | `true`/`false` (default `false`) — when `true`, bypasses the entire effect/vertex-format draw path below and instead drives the real public `SpriteBatch`/`Texture2D` API. Uses the same `texture`/`texturewidth`/`textureheight`/`texturepixel` keys as the effect path for the sprite's own source texture, plus the `sprite*` keys below. No `effect=`/`vertexformat=`/`vertex=` lines are used in this mode |
| `spritedestrect` | `x,y,w,h` — the destination rectangle passed to `SpriteBatch.Draw()`, only when `spritebatchmode=true` |
| `spritecolor` | `r,g,b,a` (0-255) — tint color, defaults to opaque white, only when `spritebatchmode=true` |
| `spriterotation` | float radians, only when `spritebatchmode=true`. Defaults to `0` |
| `spriteorigin` | `x,y` in source-texture pixel space, only when `spritebatchmode=true`. Defaults to `(0,0)` |
| `spriteeffects` | `None` (default), `FlipHorizontally`, or `FlipVertically`, only when `spritebatchmode=true` |
| `spritesourcerect` | `x,y,w,h`, only when `spritebatchmode=true`. Optional — omitted means the whole texture (real XNA's own `sourceRectangle=null` semantics) |
| `spritesampler` | `LinearClamp` (default, matches `SpriteBatch.Begin()`'s own real default), `PointClamp`, `PointWrap`, `LinearWrap`, or `PointMirror` (manually constructed — real XNA has no named `PointMirror` preset), only when `spritebatchmode=true` |
| `effect` | `BasicEffect` (default), `AlphaTestEffect`, `DualTextureEffect`, `EnvironmentMapEffect`, or `SkinnedEffect` — which Stock Effect both sides construct (all 5 real XNA Stock Effects), only meaningful when `spritebatchmode` is unset/`false` |
| `vertexformat` | `PositionColor` (default), `PositionTexture`, `PositionNormalTexture`, `PositionDualTexture`, or `PositionNormalTextureWeights` — selects which `vertex=` shape below, and which vertex struct both sides draw. `PositionDualTexture`/`PositionNormalTextureWeights` have no XNA-built-in equivalent (real XNA has no dual-UV or skinned vertex type either) — both sides define their own custom `IVertexType`/`VertexDeclaration`, exactly as a real game using `DualTextureEffect`/`SkinnedEffect` would have to |
| `vertexcolor`, `lighting`, `texture` | `VertexColorEnabled`/`LightingEnabled` (`BasicEffect` only — see the `LightingEnabled` carve-out below)/`TextureEnabled` (`BasicEffect`) or texture-non-null (`AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`) (`true`/`false`) |
| `texturewidth`, `textureheight` | size of an inline procedural texture (no content-pipeline asset file — matches `D9-A2`'s own "no content pipeline" constraint) |
| `texturefilter` | `Point` or `Linear` — `SamplerState.PointClamp`/`LinearClamp` on slot 0 |
| `texturepixel` | `r,g,b,a` — repeats `texturewidth*textureheight` times, row-major, only when `texture=true` |
| `texture2`, `texture2width`, `texture2height`, `texture2pixel` | same shape as `texture`/`texturewidth`/`textureheight`/`texturepixel`, for `DualTextureEffect.Texture2` (the second sampler), only when `effect=DualTextureEffect` |
| `diffusecolor` | `r,g,b` (0-1 floats) — `DualTextureEffect.DiffuseColor`, only when `effect=DualTextureEffect` |
| `environmentmap`, `environmentmapsize`, `environmentmappixel` | a `TextureCube.EnvironmentMap`, only when `effect=EnvironmentMapEffect` — a single `environmentmappixel=` sets ALL 6 faces to the same color (sidesteps needing to hand-compute `reflect()` geometry, same trick `D9-82e`'s own CTest used) |
| `environmentmapamount` | `0`-`1` float — `EnvironmentMapEffect.EnvironmentMapAmount`, only when `effect=EnvironmentMapEffect` |
| `fresnelfactor` | float — `EnvironmentMapEffect.FresnelFactor`, only when `effect=EnvironmentMapEffect`. Real XNA defaults this to `1` (fresnel ENABLED) in the constructor — a scene that wants the non-fresnel bucket must set `fresnelfactor=0` explicitly, it is not the implicit default of leaving the key unset (`envmap_quad.scene` got this wrong once — see its own comment) |
| `ambientcolor` | `r,g,b` (0-1 floats) — `AmbientLightColor`, only when `lighting=true` (`BasicEffect`/`EnvironmentMapEffect`/`SkinnedEffect`) |
| `light0enabled`/`light1enabled`/`light2enabled`, `light0diffuse`/`light1diffuse`/`light2diffuse`, `light0direction`/`light1direction`/`light2direction` | `DirectionalLight0`/`DirectionalLight1`/`DirectionalLight2`'s own `.Enabled`/`.DiffuseColor`/`.Direction`, only when `lighting=true` (`BasicEffect`/`EnvironmentMapEffect`/`SkinnedEffect`) — all three lights are always applied once `lighting=true` (a scene that only cares about `Light0` simply never sets `light1*`/`light2*`, which default to disabled/zero) |
| `alphafunction` | `Always`/`Never`/`Less`/`LessEqual`/`Equal`/`GreaterEqual`/`Greater`/`NotEqual` — `AlphaTestEffect.AlphaFunction`, only when `effect=AlphaTestEffect` |
| `referencealpha` | `0`-`255` int — `AlphaTestEffect.ReferenceAlpha`, only when `effect=AlphaTestEffect` |
| `fogenabled` | `true`/`false` — `IEffectFog.FogEnabled`, applied uniformly to all 5 Stock Effects (`IEffectFog` is shared by all of them) |
| `fogcolor` | `r,g,b` (0-1 floats) — `IEffectFog.FogColor`, only meaningful when `fogenabled=true` |
| `fogstart`, `fogend` | floats — `IEffectFog.FogStart`/`FogEnd`. **Not simple world-space distances**: with `World`/`View` both `Identity` (this corpus's own convention), the fog factor is `saturate(z*scale + fogStart*scale)` where `scale=1/(fogStart-fogEnd)` — see `fog_gradient_quad.scene`'s own extensive comment for the two false starts this produced (a saturated-to-zero uniform result, then a near-plane-clipped empty result) before landing on `FogStart=0`/`FogEnd=-1` (negative) to get a genuine `z=0`→unfogged, `z=1`→fogged gradient without leaving the safe `[0,1]` clip-space z range |
| `primitive` | `TriangleList`, `TriangleStrip`, `LineList`, or `LineStrip` |
| `vertex` | `x,y,z,r,g,b,a` (`PositionColor`), `x,y,z,u,v` (`PositionTexture`), `x,y,z,nx,ny,nz,u,v` (`PositionNormalTexture`), `x,y,z,u0,v0,u1,v1` (`PositionDualTexture`), or `x,y,z,nx,ny,nz,u,v,boneindex0,boneweight0[,boneindex1,boneweight1[,boneindex2,boneweight2,boneindex3,boneweight3]]` (`PositionNormalTextureWeights` — the 2nd pair is optional (defaults to `index=0/weight=0`); the 3rd/4th pair are also optional but must appear together (16 columns total) when present) — repeats once per vertex, `World`/`View`/`Projection` are always `Matrix.Identity` |
| `weightspervertex` | `1`, `2`, or `4` — `SkinnedEffect.WeightsPerVertex`, only when `effect=SkinnedEffect`. Real XNA defaults this to `4` (confirmed in FNA's own `SkinnedEffect.cs`) — a scene that wants the `OneBone` bucket must set `weightspervertex=1` explicitly, it is not the implicit default of leaving the key unset (`skinned_quad.scene` got this wrong once — see its own comment) |
| `bone1translate`, `bone2translate`, `bone3translate` | `x,y,z` float — pure-translation bones (`Bones[1]`/`Bones[2]`/`Bones[3]`), only when `effect=SkinnedEffect`. `Bones[0]` is always `Matrix.Identity`; each defaults to `(0,0,0)` (also Identity) when unset |

**`LightingEnabled` carve-out for `EnvironmentMapEffect`/`SkinnedEffect`**: real XNA/FNA's
`EnvironmentMapEffect` **and** `SkinnedEffect` both implement `IEffectLights.LightingEnabled` via
**explicit interface implementation** — not a public member of either concrete class (confirmed
live for both: a plain `emfx.LightingEnabled = ...`/`skfx.LightingEnabled = ...` is a real
`CS1061` compile error against the real `csc.exe`), and the setter throws if given `false` anyway
(lighting is always on for these two effects). Neither side calls it for either effect;
`ambientcolor`/`light0*` are still applied (unconditionally, once `lighting=true`), since
`AmbientLightColor`/`DirectionalLight0` genuinely are ordinary public members on both.

`SkinnedEffect` always uses a **single Identity bone at 100% vertex weight** (`WeightsPerVertex=1`)
— skinning becomes a mathematical no-op, so the expected pixel math reduces to exactly the same
lit-textured formula `lit_textured_quad.scene` already established, while still genuinely
exercising the real per-vertex `BLENDWEIGHT0`/`BLENDINDICES0` upload and the full bone-matrix-array
path end to end. This is not scene-configurable (yet) — every `SkinnedEffect` scene in this corpus
calls `SetBoneTransforms(new[] { Matrix.Identity })` on both sides, hardcoded.

## Build and run — XNA side (the oracle)

Needs the real XNA 4.0 runtime under Wine (`plan_dx9.md` `D9-A1`), **with DXVK installed into
that prefix too** (`D9-A4`'s own critical methodological requirement — otherwise real XNA runs on
WineD3D while CNA/D3D9 runs on DXVK, and any diff would silently measure a *driver* difference,
not a CNA one):

```bash
# One-time, if not already done: WINEPREFIX=~/.wine-cna-xna40 WINEARCH=win32 dxvk-setup install -y

export WINEPREFIX=$HOME/.wine-cna-xna40 WINEARCH=win32
GAC=$WINEPREFIX/drive_c/windows/Microsoft.NET/assembly
CSC=$WINEPREFIX/drive_c/windows/Microsoft.NET/Framework/v4.0.30319/csc.exe

wine "$CSC" /nologo /target:exe /platform:x86 /out:Oracle.exe \
  /r:"$(winepath -w $(find $GAC -name Microsoft.Xna.Framework.dll          | head -1))" \
  /r:"$(winepath -w $(find $GAC -name Microsoft.Xna.Framework.Game.dll     | head -1))" \
  /r:"$(winepath -w $(find $GAC -name Microsoft.Xna.Framework.Graphics.dll | head -1))" \
  Oracle.cs

wine Oracle.exe scenes/colored3d.scene xna_out.png
```

**This `csc.exe` targets .NET Framework 4.0-era C# — no expression-bodied members (`=>` on a
method), no string interpolation, no null-conditional operators, none of C# 6+.** Nothing about
writing ordinary-looking modern C# signals which language version an old compiler accepts; this
project's own `Oracle.cs` shipped with an expression-bodied `ParseBool` briefly and the real
compiler rejected it (`CS1002`/`CS1519`) — write plain, old-style C# here.

## Build and run — CNA side

```bash
cmake --build cmake-build-d3d9 --target cna_oracle_render
cd cmake-build-d3d9
CNA_D3D9_WINEPREFIX=~/.wine-cna-d3d9-spike ../scripts/run-wine-dxvk9.sh \
    ./cna_oracle_render.exe ../tools/xna-oracle/scenes/colored3d.scene cna_out.png
```

Not registered as a CTest (`add_test`) — it has no pass/fail assertion of its own, it just
produces a PNG; `scripts/xna-diff.py` is what judges it. `D9-120` is the task that promotes this
corpus into a real, checked-in-reference-image CTest that doesn't need the XNA prefix to run.

**`GraphicsDevice::DrawUserPrimitives()` reads its `GpuDrawParams` from `currentEffect_`, a raw
pointer `Effect::Apply()` sets — the constructed `BasicEffect`/`AlphaTestEffect` object must
outlive every `DrawUserPrimitives()` call that follows its `Apply()`.** A real bug found live while
adding a second effect type here: the effect object was originally scoped inside an `if`/`else`
block selecting which Stock Effect to construct, and was destroyed at that block's closing brace —
before the (deliberately effect-agnostic, shared) `DrawUserPrimitives()` call further down read the
now-dangling `currentEffect_` pointer, reporting stale stack garbage instead of the flags actually
just set. Fixed by declaring both possible effect objects as `std::unique_ptr` at `Draw()`'s own
top level so whichever one gets constructed survives to the end of the function.

## Diff

```bash
python3 scripts/xna-diff.py xna_out.png cna_out.png --diff-out diff.png
```

Requires Pillow (`pip install pillow`) — not previously a dependency of this project's other
`scripts/*.py` tools, but the standard library has no PNG decoder.

## Status

Twenty-four scenes so far, **all pixel-perfect**, and every one of XNA's 5 Stock Effects plus
`IEffectFog`, ALL 8 `AlphaTestEffect.AlphaFunction` values (`AlphaTestEffect` compare-function
coverage is COMPLETE), `EnvironmentMapEffect.FresnelFactor`, ALL 3
`SkinnedEffect.WeightsPerVertex` values (`SkinnedEffect` weighting coverage is COMPLETE), and
`SpriteBatch`'s core draw path AND sampler address modes (basic draw, rotation/origin,
`SpriteEffects` flip, `Wrap`/`Mirror` — `D9-90`/`D9-91`/`D9-92` all COMPLETE) is now represented
in the corpus:

- `colored3d` (`D9-A2`'s own original spike scene: a `BasicEffect` `VertexColorEnabled=true`/
  `LightingEnabled=false` triangle over a `CornflowerBlue` clear) — `0/65536` pixels differ,
  `max per-channel delta=0`, across every channel of every pixel, corner clear color through the
  triangle's own Gouraud-interpolated interior. Confirmed both via 3 hand-picked sample points
  (corner `(100,149,237,255)`, centre `(69,118,69,255)`, near-apex `(17,222,17,255)`) and via a
  full `scripts/xna-diff.py` sweep of all 65,536 pixels.
- `textured_quad` (`BasicEffect.TextureEnabled=true`, a tiny 2×2 point-filtered checkerboard
  texture, `SamplerState.PointClamp`) — also `0/65536` pixels differ, including at the exact
  UV=(0.5,0.5) point-filter texel-boundary pixel (a genuine tie-break case: both sides independently
  pick the identical texel there, not merely "close").
- `lit_textured_quad` (`BasicEffect.LightingEnabled=true` + `TextureEnabled=true`, `VSInputNmTx`'s
  Position+Normal+TexCoord vertex shape, one dim `DirectionalLight0` — `0/65536` pixels differ.
  Deliberately dimmed (`diffuse=0.5`, no ambient) so the lit result is visibly darker than the raw
  texture (`(255,0,0)` → `(128,0,0)`, `(0,0,255)` → `(0,0,128)`) rather than saturating to full
  brightness, which would have made this scene indistinguishable from an unlit one and proven
  nothing about whether the lighting math is genuinely applied.
- `alphatest_quad` — the first scene to use a non-`BasicEffect` Stock Effect (`AlphaTestEffect`,
  `AlphaFunction=Greater`, `ReferenceAlpha=128`) on the existing `VertexPositionTexture` shape. A
  2×2 texture whose 4 texels deliberately straddle the threshold (alpha `255`/`0`/`255`/`64`) —
  `0/65536` pixels differ, and the passing/failing texels are visibly, correctly different (passing
  texels show their own color; failing texels show the `CornflowerBlue` clear color through
  `clip()`'s real discard, not some blended/wrong value).
- `dualtexture_quad` — the second non-`BasicEffect` Stock Effect (`DualTextureEffect`), and the
  first scene needing a brand-new vertex shape neither side had a built-in type for
  (`PositionDualTexture`: Position+TexCoord0+TexCoord1, stride 28 — real XNA has no dual-UV vertex
  struct either, so both `Oracle.cs` and `CnaOracleRender.cpp` define their own custom
  `IVertexType`/`VertexDeclaration`). Two 1×1 solid-color textures (white, `(100,60,20)`) and
  `DiffuseColor=(0.5,0.5,0.5)` — the real doubling-blend formula (`texture0 * texture1 * 2 *
  DiffuseColor`) makes the `*2*0.5` cancel out, so the expected result is exactly `(100,60,20,255)`,
  confirmed independently by hand before running the oracle and then confirmed pixel-for-pixel —
  `0/65536` pixels differ. `DualTextureEffect` was added as a THIRD `std::unique_ptr` alongside
  `alphaFx`/`basicFx` (see "Build and run — CNA side" above for why that pattern exists), correctly
  avoiding a repeat of the dangling-pointer bug `AlphaTestEffect` found — no new bug this time.
- `envmap_quad` — the third non-`BasicEffect` Stock Effect (`EnvironmentMapEffect`), reusing the
  existing `PositionNormalTexture` shape (`VSInputNmTx`, `D9-82e`'s own "no new vertex declaration
  needed" finding). A 1×1 white base texture, a 1×1 environment (cube) map (all 6 faces the same
  color — the same trick `D9-82e`'s own CTest used to sidestep hand-computing `reflect()`
  geometry), one dim `DirectionalLight0`, `EnvironmentMapAmount=0.5` — real formula
  (`lerp(texture*diffuseSum, environmentMap, environmentMapAmount)`) — `0/65536` pixels differ,
  exact `(164,114,89,255)` on both sides. **Real, genuine API-surface finding, not a bug**: real
  XNA/FNA's `EnvironmentMapEffect` implements `IEffectLights.LightingEnabled` via *explicit
  interface implementation*, invisible on the concrete class (`emfx.LightingEnabled = ...` is a
  real `CS1061` against the real `csc.exe`) — lighting is always on for this effect, and the
  setter throws given `false` anyway. CNA's own equivalent is directly callable (C++ has no
  explicit-interface-implementation hiding) but was deliberately left unset here, matching what a
  real game using this effect actually can (and cannot) do. Sets `fresnelfactor=0` explicitly
  (see the next bullet for why this matters).
- `envmap_fresnel_quad` — the first scene to genuinely exercise `EnvironmentMapEffect.FresnelFactor`
  with a real per-vertex gradient. **Real documentation-accuracy finding, not a rendering bug**:
  `envmap_quad.scene` had always claimed to test the "non-fresnel bucket", but neither side ever
  actually set `FresnelFactor`, and real XNA's `EnvironmentMapEffect` constructor defaults
  `FresnelFactor=1` (confirmed in FNA's own source, matched by CNA's own constructor) — so that
  scene had ACTUALLY been running the fresnel-ENABLED bucket the whole time. Undetected because
  its geometry is coincidentally degenerate for Fresnel: the quad sits in the same `z=0` plane as
  `EyePosition=(0,0,0)` (`View` is always `Identity` in this corpus), so `viewAngle=0` at every
  vertex with `normal=(0,0,1)`, and `pow(max(1-abs(0),0), anything)=1` regardless of the exponent
  — Fresnel enabled/disabled produce the identical result there. Fixed with an explicit
  `fresnelfactor=0` on `envmap_quad.scene`. This new scene proves the real formula
  (`pow(max(1-abs(dot(eyeVector,worldNormal)),0), FresnelFactor) * EnvironmentMapAmount`, computed
  per-vertex then Gouraud-interpolated, not recomputed per-pixel). A second trap surfaced while
  designing it: any single normal shared by all 4 corners of the symmetric origin-centered quad
  gives an identical fresnelFactor everywhere (no gradient at all) — fixed by deliberately
  assigning DIFFERENT per-vertex normals to the top vs. bottom edge (`(0,0,1)` top →
  `fresnelFactor=1` exactly; `(1,0,0)` bottom → hand-derived `fresnelFactor≈0.29289`). With
  lighting forced to `diffuseSum=0`, the result reduces to exactly
  `fresnelFactor * environmentMapColor` — sampled at the exact vertical center, the hand-derived
  prediction `≈(129.3,64.6,32.3)` matched the observed `(129,65,32)` exactly on both real XNA and
  CNA, `0/65536` pixels differ overall.
- `skinned_quad` — the fourth non-`BasicEffect` Stock Effect (`SkinnedEffect`), and the fifth (last)
  of the 5 XNA Stock Effects in the corpus, on another brand-new custom vertex shape
  (`PositionNormalTextureWeights`: Position+Normal+TexCoord+BlendWeight+BlendIndices, stride 52 —
  `VSInputNmTxWeights`, matches the existing stride-52 CNA vertex declaration byte-for-byte,
  `D9-82f`'s own "no new vertex declaration needed" finding). A single Identity bone at 100%
  vertex weight (skinning is a mathematical no-op) plus the same dim light/white texture
  `lit_textured_quad.scene` uses — exact `(128,128,128,255)` on both sides, `0/65536` pixels
  differ. Same `LightingEnabled` explicit-interface-implementation carve-out as
  `EnvironmentMapEffect` (confirmed against FNA's own `SkinnedEffect.cs` source too). Sets
  `weightspervertex=1` explicitly (see the next bullet for why this matters).
- `skinned_twobone_quad` — the first scene to exercise a REAL, non-degenerate skinning blend
  (`WeightsPerVertex=2`, two different bones both bound with nonzero weight), not
  `skinned_quad.scene`'s own single-Identity-bone no-op. **Real documentation-accuracy finding,
  not a rendering bug**: `skinned_quad.scene` had always claimed `WeightsPerVertex=1`, but that
  property was never actually set on either side, and real XNA's `SkinnedEffect` constructor
  defaults `WeightsPerVertex=4` (confirmed `int weightsPerVertex = 4;` in FNA's own
  `SkinnedEffect.cs`, matched by CNA's own constructor) — so that scene had ACTUALLY been running
  the `FourBones` shader bucket the whole time, harmless only because its single `(boneindex,
  boneweight)` pair leaves weights `[1..3]=0`, making the extra bone terms structurally zero
  regardless of how many the shader sums. Fixed with an explicit `weightspervertex=1` on
  `skinned_quad.scene`, now genuinely exercising the distinct `OneBone` bucket. This new scene
  also extends the shared vertex line format from 10 columns to an optional 12 (a second
  `boneindex,boneweight` pair) — backward compatible, existing 10-column lines default the pair
  to `index=0/weight=0`. Real formula (confirmed against FNA's own `SkinnedEffect.fx`'s `Skin()`):
  `skinning = Σ Bones[Indices[i]] * Weights[i]`, a literal weighted SUM of the raw bone matrices
  (not a composition of transforms). Bone 0 = Identity, Bone 1 = `Translate(0.4,0,0)` (pure
  translation, same rotation/scale part as Bone 0), weights `0.5/0.5` — since both bones share the
  same Identity rotation/scale part, the blended rotation/scale is also exactly Identity, and only
  the translation blends: `0.5*(0,0,0) + 0.5*(0.4,0,0) = (0.2,0,0)` exactly, giving a pure,
  hand-derivable rightward shift of the whole quad by `0.2` NDC units (`25.6px` at 256 wide), with
  the normal (and therefore lighting) completely unaffected. Sampled at the predicted shifted
  boundaries: the original left edge correctly shows clear color, the lit `(128,128,128,255)`
  color begins exactly at the shifted position, and clear color resumes exactly past the shifted
  right edge — confirmed identical on both real XNA and CNA, `0/65536` pixels differ.
- `skinned_fourbone_quad` — the first scene to exercise a REAL, non-degenerate 4-bone skinning
  blend (`WeightsPerVertex=4`, all four bones bound with distinct nonzero weights), completing
  coverage of all 3 real `WeightsPerVertex` values (`1`: `skinned_quad.scene`, `2`:
  `skinned_twobone_quad.scene`, `4`: this scene). Extends the vertex line format again, from the
  12-column pair to an optional 16 (a THIRD and FOURTH `boneindex,boneweight` pair) — backward
  compatible. New `bone2translate`/`bone3translate` scene keys extend `bone1translate`'s pattern
  to bones 2 and 3. All four bones are pure translations (same Identity rotation/scale trick, so
  the normal/lighting stay unaffected): `Bone 0=Identity` (weight `0.4`), `Bone
  1=Translate(0.4,0,0)` (weight `0.3`), `Bone 2=Translate(0,0.2,0)` (weight `0.2`), `Bone
  3=Translate(0,-0.1,0)` (weight `0.1`). Hand-derived blend:
  `0.4*(0,0,0)+0.3*(0.4,0,0)+0.2*(0,0.2,0)+0.1*(0,-0.1,0) = (0.12,0.03,0)` exactly — a genuine
  TWO-AXIS shift (unlike the 2-bone scene's pure-X shift), proving all four weighted terms are
  summed correctly, not just the first two. Sampled at the predicted shifted boundaries in both X
  and Y, confirmed identical on both real XNA and CNA, `0/65536` pixels differ.
- `multilight_textured_quad` — the first scene to genuinely exercise `BasicEffect`'s **multi-light
  summation** formula (`D9-82b`'s own "2-light-sum" `ShaderIndex` bucket, a structurally different
  dispatch path from the "`OneLight`" bucket every earlier lit scene exercises). Two active lights
  (`DirectionalLight0` diffuse `0.3`, `DirectionalLight1` diffuse `0.2`, same direction) sum to the
  exact same total dimming `lit_textured_quad.scene`'s own single `0.5` light already produces —
  exact `(128,128,128,255)` on both sides, matching that scene's own result byte-for-byte, proving
  the two lights are genuinely summed rather than one silently overwriting the other's constant
  register. `DirectionalLight2` is explicitly present but **disabled**, with a deliberately large
  nonzero diffuse color (`0.9`) that must NOT contribute — confirmed it doesn't (the result would
  be far brighter than `128` if it leaked in). Extended `light1*`/`light2*` scene keys and applied
  them to all three lit effects (`BasicEffect`/`EnvironmentMapEffect`/`SkinnedEffect`) uniformly,
  even though only this scene currently sets them — `0/65536` pixels differ.
- `fog_gradient_quad` — the first scene to exercise `IEffectFog` (`FogEnabled`/`FogColor`/
  `FogStart`/`FogEnd`, shared by all 5 Stock Effects; fog wiring was added to all 5 effect
  branches on both sides even though this scene itself only uses `BasicEffect`). A solid-white
  `VertexColorEnabled` quad spanning `z=0` (near/top edge) to `z=1` (far/bottom edge) in clip
  space, `FogColor=black`. **Took two false starts to get a genuinely non-trivial gradient**,
  confirmed against FNA's own `EffectHelpers.SetFogVector`/`Common.fxh`'s `ComputeFogFactor`
  (`saturate(dot(position, FogVector))`): with `World=View=Identity`, `FogStart=0`/`FogEnd=1` (the
  "obvious" reading) gives `fogFactor=saturate(-z)`, which is `<=0` for all `z>=0` — every pixel
  clamps to 0% fog, rendering **uniformly white** on both real XNA and CNA (an exact `0/65536`
  match that proved nothing, since fog was never actually applied on either side). Flipping the
  far vertices to `z=-1` for a "correct" negative view-space Z instead near-plane-clipped the
  whole quad away on both sides (`Projection` is also `Identity`, so clip-space z is the raw
  vertex z, and `-1` is outside D3D's valid `[0,w]` depth range) — also an exact but empty match.
  The working fix keeps vertex z in the safe `[0,1]` range and uses `FogStart=0`/`FogEnd=-1`
  (**negative**), which reduces `fogFactor` to exactly `z` — a genuine monotonic white
  (`(249,249,249)`) → grey (`(127,127,127)` at centre) → black (`(8,8,8)` at the far edge)
  gradient, confirmed pixel-for-pixel identical on both sides, `0/65536` pixels differ. See
  `scenes/fog_gradient_quad.scene`'s own comment for the full derivation.
- `alphatest_less_quad` — the first scene to exercise a SECOND `AlphaTestEffect.AlphaFunction`
  value (`Less`), not just the single `Greater` value `alphatest_quad.scene` covers. Reuses the
  exact same 2×2 texture and `ReferenceAlpha=128` threshold, only `AlphaFunction` changes — this
  deliberately flips which texels pass vs. get discarded relative to the `Greater` scene, proving
  the compare function itself is genuinely honored (a backend that silently ignored
  `AlphaFunction` would still pass `alphatest_quad.scene` but fail this one). No code changes were
  needed on either side (`Less` was already a supported `CompareFunction` value in both parsers).
  **Real finding — a PNG-encoder quirk in the oracle tooling, not a rendering bug**: a first draft
  used a texel with `alpha=0` for the passing top-right texel. The actual shader output was
  byte-identical on both sides (`RGBA=(255,255,255,0)`), yet the SAVED PNG differed: real XNA's
  `Texture2D.SaveAsPng` wrote `RGB=(0,0,0)` for that exact-`alpha=0` pixel, while CNA's own PNG
  writer preserved the raw `RGB=(255,255,255)` — confirmed specific to `alpha==0` (not a general
  premultiply-before-encode step) since the adjacent `alpha=64` texel matched byte-for-byte on
  both sides in the same run. Fixed by using `alpha=1` instead of `0` for that texel (still
  exercises the identical `Less` code path) — re-verified `0/65536` pixels differ.
- `alphatest_equal_quad` — the first scene to exercise `AlphaFunction=Equal`, a STRUCTURALLY
  different pixel shader bucket from `Greater`/`Less`. Confirmed against FNA's own
  `AlphaTestEffect.cs` source: `Less`/`LessEqual`/`GreaterEqual`/`Greater`/`Never`/`Always` all
  compile to the shared `PSAlphaTestLtGt` shader (`clip((a < x) ? z : w)`), while
  `Equal`/`NotEqual` compile to the entirely separate `PSAlphaTestEqNe` shader
  (`clip((abs(a - x) < y) ? z : w)`) — genuinely different comparison logic, not just a
  differently-signed threshold on the same one. FNA's source also gives the exact tolerance:
  `threshold = 0.5f / 255f` (half of one 8-bit integer step). The scene straddles that boundary
  with 4 texels: `alpha=128` (exact match to `ReferenceAlpha=128`, `abs diff=0`, PASSES),
  `alpha=127`/`alpha=129` (off by exactly `1/255 ≈ 0.0039`, both `> 0.5/255`, both FAIL), and
  `alpha=1` (far off, FAILS) — the pass/fail pattern was predicted before running either side,
  then confirmed pixel-for-pixel identical, `0/65536` pixels differ. No code changes needed on
  either side (`Equal` was already a supported `CompareFunction` value in both parsers).
- `alphatest_notequal_quad` — the first scene to exercise `AlphaFunction=NotEqual`, the negation
  of `alphatest_equal_quad.scene` within the SAME `PSAlphaTestEqNe` shader bucket. Confirmed
  against FNA's own `AlphaTestEffect.cs` source: `NotEqual` uses the identical `abs(a - x) < y`
  comparison as `Equal`, only the pass/fail branch targets (`AlphaTest.z`/`AlphaTest.w`) are
  swapped. Reuses `alphatest_equal_quad.scene`'s exact texture and threshold, only `AlphaFunction`
  changes — deliberately flips every texel's pass/fail (the exact-match `alpha=128` texel now
  FAILS; the three near/far-miss texels now PASS), confirmed pixel-for-pixel identical,
  `0/65536` pixels differ. No code changes needed (`NotEqual` already supported). This completes
  coverage of both compare-function directions on both real pixel shader buckets (`Greater`/`Less`
  on `PSAlphaTestLtGt`, `Equal`/`NotEqual` on `PSAlphaTestEqNe`).
- `alphatest_greaterequal_quad`/`alphatest_lessequal_quad` — exercise `GreaterEqual`/`LessEqual`,
  which share the `PSAlphaTestLtGt` bucket with `Greater`/`Less` but differ from them specifically
  at the EXACT boundary value. Confirmed against FNA's own `AlphaTestEffect.cs`: `GreaterEqual`
  sets `alphaTest.X = reference - threshold` (vs. `Greater`'s `reference + threshold`);
  `LessEqual` sets `reference + threshold` (vs. `Less`'s `reference - threshold`) — a texel whose
  alpha exactly equals `ReferenceAlpha` PASSES under the `-Equal` variant but would be DISCARDED
  under the plain variant. Both scenes reuse `alphatest_equal_quad.scene`'s own texture
  (`alpha=128,127,129,1`) specifically because it already has a texel at the exact `128` boundary
  — `alphatest_quad.scene`'s own texture (`alpha=255,1,255,64`) never lands exactly on `128`, so
  it could not distinguish either pair at all. Both scenes' pass/fail patterns were predicted
  before running either side, then confirmed pixel-for-pixel identical, `0/65536` pixels differ
  each. No code changes needed (both values already supported). Together these complete coverage
  of all 4 alpha-value-dependent `PSAlphaTestLtGt` values (`Less`/`LessEqual`/`GreaterEqual`/
  `Greater`).
- `alphatest_never_quad`/`alphatest_always_quad` — COMPLETE ALL 8 REAL XNA
  `AlphaTestEffect.AlphaFunction` VALUES IN THE CORPUS. Confirmed against FNA's own
  `AlphaTestEffect.cs` source: `Never` sets both branch targets negative — `clip((a < x) ? z : w)`
  evaluates to `clip(-1)` unconditionally, discarding every fragment regardless of alpha; `Always`
  sets both targets positive, `clip(1)` unconditionally, never discarding anything. Both scenes
  reuse `alphatest_quad.scene`'s exact texture (`alpha=255,1,255,64`) unchanged — under `Never`,
  all 4 texels (including the `alpha=255` ones that would normally pass `Greater`) are discarded,
  rendering pure clear color everywhere; under `Always`, all 4 texels (including the
  `alpha=1`/`alpha=64` ones that would normally fail `Greater`) survive and show their own raw
  color. Confirmed pixel-for-pixel identical, `0/65536` pixels differ each. No code changes
  needed. `AlphaTestEffect`'s entire compare-function surface is now independently verified:
  `Less`/`LessEqual`/`GreaterEqual`/`Greater`/`Never`/`Always` on `PSAlphaTestLtGt`,
  `Equal`/`NotEqual` on `PSAlphaTestEqNe`.
- `sprite_basic_quad` — the first scene to exercise `SpriteBatch` at all (Phase D9-9,
  `D9-90`/`D9-91`), a genuinely different draw model from every earlier scene: no `Effect`, no
  vertex declaration, no explicit `World`/`View`/`Projection` — just `SpriteBatch.Begin()`/
  `Draw(texture, destRect, color)`/`End()` through the real public API. Also the first empirical
  measurement of `D9-91`'s own half-pixel offset (design decision 10): a 1×1 solid-color texture
  stretched into a clean integer destination rectangle (`x=50,y=60,w=80,h=40`), with sample
  points just inside/outside each of the 4 edges. Pixel-perfect on the first attempt, boundaries
  landing exactly at the requested rectangle on both sides.
- `sprite_rotated_quad` — proves `D9-90`'s rotation/origin geometry with a 2×2 four-color texture
  (not a 1×1 solid color, so a 90-degree rotation is independently verifiable: `cos(90°)=0`,
  `sin(90°)=1`, no fractional trig) rotated exactly `π/2` around the texture's own center, on a
  SQUARE destination so the bounding box doesn't change — only which quadrant color ends up
  where. Pixel-perfect on the first attempt.
- `sprite_flipped_quad` — proves `D9-90`'s `SpriteEffects.FlipHorizontally` handling, reusing
  `sprite_rotated_quad.scene`'s exact four-color texture with no rotation instead: the horizontal
  texel order swaps (TL↔TR, BL↔BR) while top/bottom stays put, confirmed pixel-for-pixel exactly
  as predicted before running either side.
- `sprite_wrap_quad` — the first scene to exercise `D9-92`: `SpriteBatch.Begin()` with an
  explicit `SamplerState.PointWrap` (every earlier scene used `Begin()`'s own default
  `LinearClamp`). A 2×1 RED/GREEN texture sampled with a `sourceRectangle` DOUBLE the texture's
  own width tiles the pattern across the destination: `RED,GREEN,RED,GREEN` in 4 clean bands.
  Predicted before running either side, then confirmed pixel-for-pixel identical.
- `sprite_mirror_quad` — the second `D9-92` scene, proving `TextureAddressMode.Mirror` is
  genuinely DIFFERENT from `Wrap`, not just "some non-Clamp behavior". Identical texture,
  `sourceRectangle`, and destination geometry as `sprite_wrap_quad.scene`, only the sampler
  changes to a manually-constructed Point/Mirror `SamplerState` (real XNA has no named
  `PointMirror` preset — only `PointClamp`/`PointWrap`/`LinearClamp`/`LinearWrap`/
  `AnisotropicClamp`/`AnisotropicWrap`). Standard GPU mirror addressing folds U outside `[0,1]`
  as a triangle wave (`u_effective = 2-u` for `u` in `[1,2]`), giving a SYMMETRIC pattern around
  the `U=1` boundary instead of `Wrap`'s repeating one: `RED,GREEN,GREEN,RED`. Confirmed
  pixel-for-pixel identical to the independently-predicted pattern.

**Real, non-obvious finding surfaced while mutation-testing the half-pixel offset (not caught by
the oracle diffs alone)**: `sprite_basic_quad.scene`'s own 1×1 texture is structurally incapable
of detecting the classic D3D9 half-pixel bug — it shifts which TEXTURE CONTENT a screen pixel
samples, not where a rectangle's geometric edges land, and a single-texel texture has nothing to
shift between. Discovered by commenting out the offset in `D3D9SpriteBatchBackend` and re-running
every scene: `sprite_basic_quad.scene` stayed pixel-perfect even with the offset entirely removed
(a false-positive "this proves it" trap), while `sprite_rotated_quad.scene`/
`sprite_flipped_quad.scene` (both using the four-color texture) diverged from real XNA by
`4800/65536` pixels — confirming the offset is genuinely necessary, and that a multi-texel,
crisp-content-boundary scene is required to actually observe its effect. See
`src/CNA/Internal/Backends/D3D9/D3D9SpriteBatch.cpp`'s own `BuildMatrixTransformEXT()` comment
and the new `D3D9_SpriteBatch` CTest for the full record.

`scripts/xna-diff.py` itself is mutation-verified: a deliberately 1-off-mutated copy of a passing
CNA PNG is correctly reported as `FAIL: 1/65536 pixels differ ... max per-channel delta=1` at the
default `--tolerance=0`, and correctly passes again at `--tolerance=1` — confirming the tool
genuinely discriminates, not just "always reports PASS".

**Every one of XNA's 5 Stock Effects (`BasicEffect`, `AlphaTestEffect`, `DualTextureEffect`,
`EnvironmentMapEffect`, `SkinnedEffect`) plus `IEffectFog`, ALL 8 `AlphaTestEffect.AlphaFunction`
values (`Less`/`LessEqual`/`GreaterEqual`/`Greater`/`Never`/`Always` on the `PSAlphaTestLtGt`
bucket, `Equal`/`NotEqual` on the separate `PSAlphaTestEqNe` bucket — compare-function coverage is
now COMPLETE), `EnvironmentMapEffect.FresnelFactor`, ALL 3 `SkinnedEffect.WeightsPerVertex`
values (`SkinnedEffect` weighting coverage is now COMPLETE), and `SpriteBatch`'s core draw path
AND sampler address modes (basic draw, rotation/origin, `SpriteEffects` flip, `Wrap`/`Mirror` —
`D9-90`/`D9-91`/`D9-92`, all now COMPLETE) are now represented in the corpus, at least once, and
every single comparison so far is pixel-perfect.** `D9-A5` keeps growing "with the plan" — each
subsequent effect/feature combination this project verifies against the oracle adds its own
scene(s) here, incrementally, rather than attempting the full corpus (multi-sprite
`SpriteSortMode` sweep — `D9-93`, the only remaining open row in Phase D9-9 — render targets,
every shared `SurfaceFormat`) in one sitting. `D9-84` (every draw path validated against the
oracle) is the task that consumes the finished corpus.
