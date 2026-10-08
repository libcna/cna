// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Internal::Renderers::Metal
{
    /** @brief One built-in shader slot that must receive a fresh per-draw binding. */
    enum class MetalStockTextureSlot
    {
        /** @brief BasicEffect diffuse texture slot. */
        BasicDiffuse,
        /** @brief AlphaTestEffect diffuse texture slot. */
        AlphaTestDiffuse,
        /** @brief DualTextureEffect first texture slot. */
        DualFirst,
        /** @brief DualTextureEffect second texture slot. */
        DualSecond,
        /** @brief EnvironmentMapEffect diffuse texture slot. */
        EnvironmentDiffuse,
        /** @brief EnvironmentMapEffect cube texture slot. */
        EnvironmentCube,
        /** @brief SkinnedEffect diffuse texture slot. */
        SkinnedDiffuse,
        /** @brief PBR base-color texture slot. */
        PbrBaseColor,
        /** @brief PBR normal-map texture slot. */
        PbrNormal,
        /** @brief PBR metallic-roughness texture slot. */
        PbrMetallicRoughness,
        /** @brief PBR emissive texture slot. */
        PbrEmissive,
        /** @brief PBR occlusion texture slot. */
        PbrOcclusion,
        /** @brief KHR_materials_specular strength texture slot (alpha). */
        PbrSpecular,
        /** @brief KHR_materials_specular colour texture slot. */
        PbrSpecularColor
    };

    /** @brief Renderer-owned neutral resource required by a missing stock texture slot. */
    enum class MetalNeutralTextureKind
    {
        /** @brief Opaque white RGBA 2D texture. */
        White2D,
        /** @brief Flat tangent-space normal RGBA 2D texture. */
        FlatNormal2D,
        /** @brief Opaque white RGBA cube texture. */
        WhiteCube,
        /** @brief Opaque black RGBA 2D texture -- what XNA samples from an unbound classic slot. */
        Black2D,
        /** @brief Opaque black RGBA cube texture -- XNA's unbound EnvironmentMap. */
        BlackCube
    };

    /** @brief Deterministic action for one shader texture slot. */
    enum class MetalTextureBindingDecision
    {
        /** @brief Bind the native texture supplied by the caller. */
        BindNative,
        /** @brief Bind the renderer-owned neutral white 2D texture. */
        BindNeutralFallback,
        /** @brief Reject because no faithful native binding or neutral fallback exists. */
        Reject
    };

    /**
     * @brief Classifies a captured texture binding without consulting carried encoder state.
     *
     * @param callerProvided True when the draw captured a non-null renderer texture pointer.
     * @param nativeResolved True when that pointer belongs to this Metal renderer.
     * @param neutralFallbackAllowed True when the slot has a renderer-owned neutral resource.
     * @return Native binding, neutral fallback, or deterministic rejection.
     */
    [[nodiscard]] constexpr MetalTextureBindingDecision DescribeMetalTextureBinding(
        bool callerProvided,
        bool nativeResolved,
        bool neutralFallbackAllowed) noexcept
    {
        if (callerProvided)
            return nativeResolved ? MetalTextureBindingDecision::BindNative
                                  : MetalTextureBindingDecision::Reject;
        return neutralFallbackAllowed ? MetalTextureBindingDecision::BindNeutralFallback
                                      : MetalTextureBindingDecision::Reject;
    }

    /**
     * @brief Names the renderer-owned resource a slot binds when the draw supplies no texture.
     *
     * plans/plan_apple_m4.md AM4-140: XNA measures an unbound texture in a classic stock effect as
     * opaque black (plans/plan_graphics_shared_cleanup.md GSC-0004), which Vulkan, WebGPU, SDL_GPU
     * and the GL renderers bind. Metal bound neutral white, the glTF default its PBR slots keep. A
     * classic slot the effect does not sample -- a BasicEffect with TextureEnabled off, whose Metal
     * functions still read it -- keeps white, the identity of `texel * colour`.
     *
     * @param slot Built-in shader texture slot.
     * @param xnaSampled True when the effect samples the slot: its TextureEnabled, or an effect
     *                   without that switch (AlphaTest, DualTexture, EnvironmentMap, Skinned).
     * @return The renderer-owned neutral texture kind for that slot.
     */
    [[nodiscard]] constexpr MetalNeutralTextureKind MetalNeutralTextureForSlot(
        MetalStockTextureSlot slot, bool xnaSampled) noexcept
    {
        if (slot == MetalStockTextureSlot::PbrNormal)
            return MetalNeutralTextureKind::FlatNormal2D;
        const bool classic = slot == MetalStockTextureSlot::BasicDiffuse ||
                             slot == MetalStockTextureSlot::AlphaTestDiffuse ||
                             slot == MetalStockTextureSlot::DualFirst ||
                             slot == MetalStockTextureSlot::DualSecond ||
                             slot == MetalStockTextureSlot::EnvironmentDiffuse ||
                             slot == MetalStockTextureSlot::EnvironmentCube ||
                             slot == MetalStockTextureSlot::SkinnedDiffuse;
        const bool black = classic && xnaSampled;
        if (slot == MetalStockTextureSlot::EnvironmentCube)
            return black ? MetalNeutralTextureKind::BlackCube : MetalNeutralTextureKind::WhiteCube;
        return black ? MetalNeutralTextureKind::Black2D : MetalNeutralTextureKind::White2D;
    }

    /**
     * @brief Classifies one built-in shader slot using its mandatory neutral fallback policy.
     *
     * @param slot Built-in shader texture slot.
     * @param callerProvided True when the captured draw state names a renderer resource.
     * @param nativeResolved True when that resource belongs to Metal and has native storage.
     * @return Native binding, the slot's neutral fallback, or deterministic foreign rejection.
     */
    [[nodiscard]] constexpr MetalTextureBindingDecision DescribeMetalStockTextureBinding(
        MetalStockTextureSlot slot,
        bool callerProvided,
        bool nativeResolved) noexcept
    {
        (void)slot;
        return DescribeMetalTextureBinding(callerProvided, nativeResolved, true);
    }
}
