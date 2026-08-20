# Audit: tools/xna-oracle/Oracle.cs

## Metadata
- Source file: `tools/xna-oracle/Oracle.cs` (807 lines)
- Audit status: AUDITED (full read)
- Subsystem: `tools-xna-oracle` shard
- File type: C# tool (real XNA 4.0 renderer, the "oracle" half of the diff harness)
- XNA/FNA relevance: this IS the project's own real-XNA reference renderer — the single most
  trust-critical file in this entire audit's FNA-comparison methodology, since every "confirmed
  against FNA" claim this project makes ultimately traces back to either direct FNA source reading
  or this tool's rendered output
- Main related tests: consumed by `plans/plan_dx9.md` Phase D9-A/D9-9x workflow; not a unit test itself

## Purpose
Parses a `.scene` file and renders it using the **real XNA 4.0 runtime** (compiled against real
GAC assemblies under Wine via the real `csc.exe`, per the shard's own README build instructions —
not FNA, not a reimplementation), saving the result as a PNG for comparison against CNA's own
rendered output.

## Executive Verdict
**Confirmed genuinely trustworthy as an authoritative XNA reference.** This is real
`Microsoft.Xna.Framework`/`Microsoft.Xna.Framework.Graphics` code (`using Microsoft.Xna.Framework;`
at line 14, real `Game`/`GraphicsDeviceManager`/`BasicEffect`/`AlphaTestEffect`/
`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`/`SpriteBatch`/`Texture2D`/`TextureCube`/
`RenderTarget2D` throughout), and per the shard's own README, compiled with the real .NET
Framework 4.0-era `csc.exe` against real XNA 4.0 GAC assemblies under Wine+DXVK — not a hand-rolled
guess at what XNA "should" do. This resolves the single most important trust question for this
entire shard: the oracle really is the authoritative implementation, not another CNA-adjacent
approximation.

## Checklist Results
- `Scene.Load()`'s parser (lines 187-329) treats an unrecognized key as a hard error
  (`throw new InvalidDataException`, line 325) rather than silently ignoring it — correctly matches
  the README's own stated design principle ("a typo in a scene file should fail loudly, not quietly
  change what a 'match' means").
- `VertexPositionDualTexture`/`VertexPositionNormalTextureWeights` (lines 49-120) both use
  `[StructLayout(LayoutKind.Sequential)]` with an explicit, correct rationale in their own comments:
  C#'s default "auto" layout does not formally guarantee field-declaration-order matches memory
  order, and `DrawUserPrimitives<T>` marshals this struct's raw bytes directly against an explicit
  byte-offset `VertexDeclaration` — an auto-reordered layout would silently corrupt every attribute
  after the first. A genuinely correct, non-obvious C# interop concern, correctly identified and
  addressed.
- The `LightingEnabled` explicit-interface-implementation carve-out for `EnvironmentMapEffect`/
  `SkinnedEffect` (lines 660-664, 706-708) is confirmed via a real, cited compile error
  (`CS1061`) against the real `csc.exe` — not an assumption.
- `Draw()` (lines 525-794) renders to an explicit `RenderTarget2D` (not the back buffer directly),
  matching real XNA's own supported readback pattern; `CnaOracleRender.cpp`'s own comment (audited
  alongside this file) explains why the CNA side deliberately does NOT mirror this (its own
  `RenderTarget2D::GetData()` CPU readback path wasn't proven yet on that backend), and confirms the
  two approaches are pixel-equivalent for what this tool needs — a reasoned, disclosed asymmetry,
  not an inconsistency.
- `Main()` (lines 796-806) is a minimal 2-argument CLI wrapper (`<scene-file> <output-png>`),
  correctly validates `args.Length` before use.

## Detailed Findings
None.

## Cross-File Observations
Byte-for-byte parallel structure with `CnaOracleRender.cpp` in this same shard, confirmed via
direct side-by-side comparison of every scene key, default value, and effect-construction branch —
see that file's own audit report for the C++-side confirmation. This symmetry, combined with this
file's confirmed use of the real XNA runtime, is the foundation of the whole shard's
trustworthiness.

## Missing or Weak Tests
Not applicable — this file IS the reference-generation tool, not a test.

## Positive Findings
This tool, combined with the extensively-documented scene corpus in this same shard (see the
`README.md` audit report), has already found and fixed multiple real bugs in CNA's own D3D9
backend (a Z-clipping bug in `D3D9SpriteBatchBackend`'s sprite-depth handling, a multi-texture
`FlushBatch()` rebind bug) — genuine, working, value-delivering trust infrastructure, not
theoretical tooling that has never actually caught anything.

## Final Assessment
No findings. Confirmed as a genuinely authoritative, real-XNA-backed reference implementation —
the foundation this entire shard's (and by extension, several other shards' FNA-comparison claims
throughout this audit) trustworthiness rests on is sound.
