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
comments; every other line is `key=value`; `vertex=` may repeat (one `VertexPositionColor` per
line, in draw order). Unknown keys are a hard error on both sides (not silently ignored) — a typo
in a scene file should fail loudly, not quietly change what a "match" means. See
`scenes/colored3d.scene` for a fully-commented example.

| Key | Meaning |
|---|---|
| `width`, `height` | back buffer size |
| `profile` | `HiDef` or `Reach` |
| `clearcolor` | `r,g,b,a` (0-255) |
| `vertexcolor`, `lighting` | `BasicEffect.VertexColorEnabled`/`LightingEnabled` (`true`/`false`) |
| `primitive` | `TriangleList`, `TriangleStrip`, `LineList`, or `LineStrip` |
| `vertex` | `x,y,z,r,g,b,a` — repeats once per vertex, `World`/`View`/`Projection` are always `Matrix.Identity` |

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

## Diff

```bash
python3 scripts/xna-diff.py xna_out.png cna_out.png --diff-out diff.png
```

Requires Pillow (`pip install pillow`) — not previously a dependency of this project's other
`scripts/*.py` tools, but the standard library has no PNG decoder.

## Status

`colored3d` (`D9-A2`'s own original spike scene: a `BasicEffect` `VertexColorEnabled=true`/
`LightingEnabled=false` triangle over a `CornflowerBlue` clear) is the first scene in the corpus,
and it is **pixel-perfect**: `0/65536` pixels differ, `max per-channel delta=0`, across every
channel of every pixel, corner clear color through the triangle's own Gouraud-interpolated
interior. Confirmed both via 3 hand-picked sample points (corner `(100,149,237,255)`, centre
`(69,118,69,255)`, near-apex `(17,222,17,255)` — all three match `D9-A2`'s own original,
independently-recorded values from before DXVK was installed into the XNA prefix, confirming the
DXVK switch changed nothing about XNA's own rendered output) and via a full `scripts/xna-diff.py`
sweep of all 65,536 pixels.

`scripts/xna-diff.py` itself is mutation-verified: a deliberately 1-off-mutated copy of the CNA
PNG is correctly reported as `FAIL: 1/65536 pixels differ ... max per-channel delta=1` at the
default `--tolerance=0`, and correctly passes again at `--tolerance=1` — confirming the tool
genuinely discriminates, not just "always reports PASS".

The corpus has exactly one scene so far. `D9-A5` grows it "with the plan" — each subsequent
effect/feature this project verifies against the oracle adds its own scene(s) here, incrementally,
rather than attempting the full corpus (`BasicEffect` lighting/fog variants, `AlphaTestEffect`,
`DualTextureEffect`, `EnvironmentMapEffect`, `SkinnedEffect`, `SpriteBatch`, render targets, every
shared `SurfaceFormat`) in one sitting. `D9-84` (every draw path validated against the oracle) is
the task that consumes the finished corpus.
