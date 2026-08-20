#pragma once

// plans/plan_dx9.md Phase D9-8 (D9-80): replicates XNA's real Stock Effects shader-permutation model
// exactly -- the `VSIndices[]`/`PSIndices[]` tables and the `ShaderIndex` computation, transcribed
// directly from Microsoft's own sources (design decision 7: a transcription task, not a design
// task -- every table and formula below is copied from a named, cited source, nothing invented).
//
// Two sources per effect:
//   - The `ShaderIndex` computation formula: ported line-for-line from FNA's `OnApply()` in
//     `<Effect>.cs` (`/rv/data/library/github.com/FNA-XNA/FNA/src/Graphics/Effect/StockEffects/`).
//   - The `VSIndices`/`PSIndices`/`VSArray`/`PSArray` tables: transcribed directly from the
//     vendored `.fx` files themselves (`shaders/xna/<Effect>.fx`) -- Microsoft's own comments on
//     each table row are preserved as this file's own comments, so a reader can diff by eye.
//
// The `Compute<Effect>ShaderIndex()` functions take the REAL XNA-shaped booleans as parameters
// (`preferPerPixelLighting`, `oneLight`, `isEqNe`, `specularEnabled`, ...), not `GpuDrawParams` --
// `GpuDrawParams` does not carry several of these today (D9-81's own audit), and extending it is a
// cross-cutting, project-owner-level decision this plan does not make unilaterally. D9-82 (the
// actual draw-call wiring) is responsible for SOURCING these booleans correctly -- from the real
// XNA effect classes' own tracked state, the same values FNA's own `OnApply()` already uses, not
// from any GpuDrawParams-shaped approximation -- and for escalating if a value genuinely cannot be
// obtained without modifying GpuDrawParams (D9-81's own two open cases: `PreferPerPixelLighting`,
// `specularEnabled`).
//
// The `Get<Effect>{Vertex,Pixel}ShaderNameEXT()` functions return the exact
// `"<EffectName>_<EntryPointName>"` string `D3D9ShaderCache::GetVertexShader()`/`GetPixelShader()`
// (D9-74) expects, so a caller can chain
// `cache.GetVertexShader(GetBasicEffectVertexShaderNameEXT(ComputeBasicEffectShaderIndex(...)))`
// directly.

namespace CNA::Internal::Renderers::DirectX9
{
    // -------------------------------------------------------------------------------------------
    // BasicEffect (BasicEffect.cs OnApply() / BasicEffect.fx VSIndices[32]/PSIndices[32])
    // -------------------------------------------------------------------------------------------

    /// Computes BasicEffect's real `ShaderIndex` (0..31). `lightingEnabled` gates whether
    /// `preferPerPixelLighting`/`oneLight` matter at all -- XNA adds nothing for either when
    /// lighting is off, exactly like `BasicEffect.cs`'s own `if (lightingEnabled) { ... }` guard.
    [[nodiscard]] int ComputeBasicEffectShaderIndex(bool fogEnabled, bool vertexColorEnabled,
                                                    bool textureEnabled, bool lightingEnabled,
                                                    bool preferPerPixelLighting, bool oneLight);
    /// Returns `"BasicEffect_<EntryPoint>"` for the vertex shader `VSArray[VSIndices[shaderIndex]]`
    /// selects. Throws `std::out_of_range` if `shaderIndex` is outside `0..31`.
    [[nodiscard]] const char* GetBasicEffectVertexShaderNameEXT(int shaderIndex);
    /// Returns `"BasicEffect_<EntryPoint>"` for the pixel shader `PSArray[PSIndices[shaderIndex]]`
    /// selects. Throws `std::out_of_range` if `shaderIndex` is outside `0..31`.
    [[nodiscard]] const char* GetBasicEffectPixelShaderNameEXT(int shaderIndex);

    // -------------------------------------------------------------------------------------------
    // AlphaTestEffect (AlphaTestEffect.cs OnApply() / AlphaTestEffect.fx VSIndices[8]/PSIndices[8])
    // -------------------------------------------------------------------------------------------

    /// Computes AlphaTestEffect's real `ShaderIndex` (0..7). `isEqNe` is real XNA state
    /// (`AlphaFunction == CompareFunction.Equal || == CompareFunction.NotEqual`) -- D9-81's own
    /// finding confirms the existing `GpuDrawParams::alphaTest[1]` (tolerance) encoding already
    /// recovers this LOSSLESSLY (`tolerance > 0` iff `Equal`/`NotEqual`, verified against
    /// `AlphaTestEffect.cs`'s own `alphaTest.Y = threshold` assignment, which fires in exactly
    /// those two `CompareFunction` cases and nowhere else) -- a caller may derive `isEqNe` that way
    /// with full confidence, not as a guess.
    [[nodiscard]] int ComputeAlphaTestEffectShaderIndex(bool fogEnabled, bool vertexColorEnabled, bool isEqNe);
    /// Throws `std::out_of_range` if `shaderIndex` is outside `0..7`.
    [[nodiscard]] const char* GetAlphaTestEffectVertexShaderNameEXT(int shaderIndex);
    /// Throws `std::out_of_range` if `shaderIndex` is outside `0..7`.
    [[nodiscard]] const char* GetAlphaTestEffectPixelShaderNameEXT(int shaderIndex);

    // -------------------------------------------------------------------------------------------
    // DualTextureEffect (DualTextureEffect.cs OnApply() / DualTextureEffect.fx, PSIndices[4] only
    // -- its own VSArray[4] has no VSIndices table, `VSArray[ShaderIndex]` is used directly since
    // the 4-entry range needs no compression)
    // -------------------------------------------------------------------------------------------

    /// Computes DualTextureEffect's real `ShaderIndex` (0..3). Fully transcribable today with no
    /// `GpuDrawParams` gap (D9-80's own plan note) -- both inputs already exist on that struct.
    [[nodiscard]] int ComputeDualTextureEffectShaderIndex(bool fogEnabled, bool vertexColorEnabled);
    /// Throws `std::out_of_range` if `shaderIndex` is outside `0..3`.
    [[nodiscard]] const char* GetDualTextureEffectVertexShaderNameEXT(int shaderIndex);
    /// Throws `std::out_of_range` if `shaderIndex` is outside `0..3`.
    [[nodiscard]] const char* GetDualTextureEffectPixelShaderNameEXT(int shaderIndex);

    // -------------------------------------------------------------------------------------------
    // EnvironmentMapEffect (EnvironmentMapEffect.cs OnApply() / EnvironmentMapEffect.fx
    // VSIndices[16]/PSIndices[16])
    // -------------------------------------------------------------------------------------------

    /// Computes EnvironmentMapEffect's real `ShaderIndex` (0..15). `specularEnabled` is real,
    /// tracked XNA state (D9-81's own finding: CNA already computes it internally, at
    /// `EnvironmentMapEffect.cpp`'s own `specularEnabled_` field) but is not forwarded through
    /// `GpuDrawParams` today -- callers needing this value must read the real effect-class field,
    /// not infer it from `envMapSpecular != 0` (a legitimately-zero-but-enabled state exists).
    [[nodiscard]] int ComputeEnvironmentMapEffectShaderIndex(bool fogEnabled, bool fresnelEnabled,
                                                             bool specularEnabled, bool oneLight);
    /// Throws `std::out_of_range` if `shaderIndex` is outside `0..15`.
    [[nodiscard]] const char* GetEnvironmentMapEffectVertexShaderNameEXT(int shaderIndex);
    /// Throws `std::out_of_range` if `shaderIndex` is outside `0..15`.
    [[nodiscard]] const char* GetEnvironmentMapEffectPixelShaderNameEXT(int shaderIndex);

    // -------------------------------------------------------------------------------------------
    // SkinnedEffect (SkinnedEffect.cs OnApply() / SkinnedEffect.fx VSIndices[18]/PSIndices[18])
    // -------------------------------------------------------------------------------------------

    /// Computes SkinnedEffect's real `ShaderIndex` (0..17). `weightsPerVertex` must be 1, 2, or 4
    /// (XNA's own `SkinningEffect.WeightsPerVertex` domain -- matches `GpuDrawParams::weightsPerVertex`
    /// exactly already, D9-80's own "happy accident" note: Task 895 added that field for an
    /// unrelated reason and it turns out to already be XNA's own model). SkinnedEffect has no
    /// `lightingEnabled` toggle -- skinned meshes are always lit in XNA, matching the absence of
    /// any such guard in `SkinnedEffect.cs`'s own `OnApply()`.
    [[nodiscard]] int ComputeSkinnedEffectShaderIndex(bool fogEnabled, int weightsPerVertex,
                                                      bool preferPerPixelLighting, bool oneLight);
    /// Throws `std::out_of_range` if `shaderIndex` is outside `0..17`.
    [[nodiscard]] const char* GetSkinnedEffectVertexShaderNameEXT(int shaderIndex);
    /// Throws `std::out_of_range` if `shaderIndex` is outside `0..17`.
    [[nodiscard]] const char* GetSkinnedEffectPixelShaderNameEXT(int shaderIndex);
}
