# Vendored Microsoft XNA 4.0 Stock Effects (HLSL)

`plans/plan_dx9.md` Phase D9-7 (D9-70). These 10 files are copied **verbatim, byte-for-byte** from the
FNA reference tree:

```text
/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/HLSL/
```

which itself vendors them, essentially unchanged, from Microsoft's own **Stock Effects sample
project** (`http://xbox.create.msdn.com/en-US/education/catalog/sample/stock_effects`, per FNA's
own `StockEffects/README`).

**Not one line has been edited** (`plans/plan_dx9.md` design decision 3). `scripts/verify-d3d9-stock-effects-vendored.sh`
enforces this mechanically — it diffs every file here against the FNA tree and fails on any delta.
Run it after any refresh of the FNA reference tree, and before any change to this directory.

| File | Compiled entry points |
|---|---|
| `BasicEffect.fx` | `VSBasic`, `VSBasicNoFog`, `VSBasicVc`, `VSBasicVcNoFog`, `VSBasicTx`, `VSBasicTxNoFog`, `VSBasicTxVc`, `VSBasicTxVcNoFog`, `VSBasicVertexLighting`, `VSBasicVertexLightingVc`, `VSBasicVertexLightingTx`, `VSBasicVertexLightingTxVc`, `VSBasicOneLight`, `VSBasicOneLightVc`, `VSBasicOneLightTx`, `VSBasicOneLightTxVc`, `VSBasicPixelLighting`, `VSBasicPixelLightingVc`, `VSBasicPixelLightingTx`, `VSBasicPixelLightingTxVc`, `PSBasic`, `PSBasicNoFog`, `PSBasicTx`, `PSBasicTxNoFog`, `PSBasicVertexLighting`, `PSBasicVertexLightingNoFog`, `PSBasicVertexLightingTx`, `PSBasicVertexLightingTxNoFog`, `PSBasicPixelLighting`, `PSBasicPixelLightingTx` |
| `AlphaTestEffect.fx` | `VSAlphaTest`, `VSAlphaTestNoFog`, `VSAlphaTestVc`, `VSAlphaTestVcNoFog`, `PSAlphaTestLtGt`, `PSAlphaTestLtGtNoFog`, `PSAlphaTestEqNe`, `PSAlphaTestEqNeNoFog` |
| `DualTextureEffect.fx` | `VSDualTexture`, `VSDualTextureNoFog`, `VSDualTextureVc`, `VSDualTextureVcNoFog`, `PSDualTexture`, `PSDualTextureNoFog` |
| `EnvironmentMapEffect.fx` | `VSEnvMap`, `VSEnvMapFresnel`, `VSEnvMapOneLight`, `VSEnvMapOneLightFresnel`, `PSEnvMap`, `PSEnvMapNoFog`, `PSEnvMapSpecular`, `PSEnvMapSpecularNoFog` |
| `SkinnedEffect.fx` | `VSSkinnedOneLightOneBone`, `VSSkinnedOneLightTwoBones`, `VSSkinnedOneLightFourBones`, `VSSkinnedVertexLightingOneBone`, `VSSkinnedVertexLightingTwoBones`, `VSSkinnedVertexLightingFourBones`, `VSSkinnedPixelLightingOneBone`, `VSSkinnedPixelLightingTwoBones`, `VSSkinnedPixelLightingFourBones`, `PSSkinnedVertexLighting`, `PSSkinnedVertexLightingNoFog`, `PSSkinnedPixelLighting` |
| `SpriteEffect.fx` | `SpriteVertexShader`, `SpritePixelShader` |
| `Macros.fxh`/`Common.fxh`/`Lighting.fxh`/`Structures.fxh` | Shared `#include` headers, not compiled directly |

66 entry points total across the 6 `.fx` files (`D9-1`'s own verified count — the entry-point list
consumed by `D9-71`'s compile pipeline is *parsed* from each file's own `compile [vp]s_2_0 ...`
statements, not hand-copied from this table; this table is documentation, not a build input).

See `LICENSE` (Microsoft Permissive License, Ms-PL) and this repository's own
`THIRD_PARTY_NOTICES.md` for the applicable license terms.
