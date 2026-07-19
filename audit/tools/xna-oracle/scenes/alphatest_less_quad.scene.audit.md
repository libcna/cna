# Audit: tools/xna-oracle/scenes/alphatest_less_quad.scene

## Metadata
- Source file: `tools/xna-oracle/scenes/alphatest_less_quad.scene`
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: documentation/data (declarative scene fixture)
- XNA/FNA relevance: `AlphaTestEffect.AlphaFunction = CompareFunction.Less` case; also the scene
  where the PNG-encoder `alpha==0` quirk was found and worked around
- Main related tests: consumed by `Oracle.cs`/`CnaOracleRender.cpp`

## Purpose
Exercises the `Less` compare function, one of the 8 `CompareFunction` values `README.md` claims
complete coverage for; targets the `PSAlphaTestLtGt` shader bucket.

## Executive Verdict
Correct fixture with a real, documented, already-worked-around encoder quirk: real XNA's
`Texture2D.SaveAsPng` writes `RGB=(0,0,0)` for any pixel with `alpha==0`, while CNA's own PNG writer
preserves the raw RGB regardless of alpha — a genuine cross-encoder difference confirmed specific
to exactly `alpha==0` (an adjacent `alpha=64` texel matched byte-for-byte, ruling out a general
premultiply step). Worked around by this scene using `alpha=1` instead of `alpha=0` in its source
texture data, sidestepping the encoder difference without touching either renderer.

## Checklist Results
- `alphafunction=Less` present, `referencealpha` set appropriately.
- Source texture alpha values confirmed in current content to avoid literal `0` (uses `alpha=1`),
  consistent with the documented workaround.
- Confirmed pixel-perfect per `README.md`'s status log.

## Detailed Findings
None currently open. (Historical: PNG-encoder `alpha==0` quirk — not a rendering bug, already
worked around at the fixture level, does not affect CNA production code.)

## Cross-File Observations
Part of the 8-scene `AlphaFunction` family. The `alpha=1`-instead-of-`0` workaround convention this
scene established should be kept in mind if any future scene needs a fully-transparent source
texel.

## Missing or Weak Tests
N/A — data fixture.

## Positive Findings
Correctly diagnosed a PNG-encoder-level difference as unrelated to rendering correctness (confirmed
via an adjacent alpha value matching byte-for-byte) rather than mistakenly treating it as a
rendering divergence.

## Final Assessment
No open findings. Historical PNG-encoder quirk already correctly diagnosed and worked around.
