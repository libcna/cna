# XNA 4.0 oracle diff harness

`plan_dx9.md` Phase D9-A (`D9-A3`/`D9-A4`). Renders the same declarative scene twice — once
through the real XNA 4.0 runtime (`Oracle.cs`, under Wine) and once through CNA's real public
`Game`/`GraphicsDeviceManager`/`GraphicsDevice`/`BasicEffect` API (`CnaOracleRender.cpp`, built
against whichever `CNA_GRAPHICS_BACKEND` this branch targets — `D3D9` on `feature/dx9`) — and
diffs the two resulting PNGs pixel-for-pixel (`scripts/xna-diff.py`).

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
| `effect` | `BasicEffect` (default) or `AlphaTestEffect` — which Stock Effect both sides construct |
| `vertexformat` | `PositionColor` (default), `PositionTexture`, or `PositionNormalTexture` — selects which `vertex=` shape below, and which `VertexPosition*` struct both sides draw |
| `vertexcolor`, `lighting`, `texture` | `VertexColorEnabled`/`LightingEnabled` (`BasicEffect` only)/`TextureEnabled` (`BasicEffect`) or texture-non-null (`AlphaTestEffect`) (`true`/`false`) |
| `texturewidth`, `textureheight` | size of an inline procedural texture (no content-pipeline asset file — matches `D9-A2`'s own "no content pipeline" constraint) |
| `texturefilter` | `Point` or `Linear` — `SamplerState.PointClamp`/`LinearClamp` on slot 0 |
| `texturepixel` | `r,g,b,a` — repeats `texturewidth*textureheight` times, row-major, only when `texture=true` |
| `ambientcolor` | `r,g,b` (0-1 floats) — `BasicEffect.AmbientLightColor`, only when `lighting=true` |
| `light0enabled`, `light0diffuse`, `light0direction` | `BasicEffect.DirectionalLight0.Enabled`/`DiffuseColor`/`Direction`, only when `lighting=true` |
| `alphafunction` | `Always`/`Never`/`Less`/`LessEqual`/`Equal`/`GreaterEqual`/`Greater`/`NotEqual` — `AlphaTestEffect.AlphaFunction`, only when `effect=AlphaTestEffect` |
| `referencealpha` | `0`-`255` int — `AlphaTestEffect.ReferenceAlpha`, only when `effect=AlphaTestEffect` |
| `primitive` | `TriangleList`, `TriangleStrip`, `LineList`, or `LineStrip` |
| `vertex` | `x,y,z,r,g,b,a` (`PositionColor`), `x,y,z,u,v` (`PositionTexture`), or `x,y,z,nx,ny,nz,u,v` (`PositionNormalTexture`) — repeats once per vertex, `World`/`View`/`Projection` are always `Matrix.Identity` |

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

Four scenes so far, **all pixel-perfect**:

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

`scripts/xna-diff.py` itself is mutation-verified: a deliberately 1-off-mutated copy of a passing
CNA PNG is correctly reported as `FAIL: 1/65536 pixels differ ... max per-channel delta=1` at the
default `--tolerance=0`, and correctly passes again at `--tolerance=1` — confirming the tool
genuinely discriminates, not just "always reports PASS".

`D9-A5` grows the corpus "with the plan" — each subsequent effect/feature this project verifies
against the oracle adds its own scene(s) here, incrementally, rather than attempting the full
corpus (`BasicEffect` lighting/fog variants, `AlphaTestEffect`, `DualTextureEffect`,
`EnvironmentMapEffect`, `SkinnedEffect`, `SpriteBatch`, render targets, every shared
`SurfaceFormat`) in one sitting. `D9-84` (every draw path validated against the oracle) is the
task that consumes the finished corpus.
