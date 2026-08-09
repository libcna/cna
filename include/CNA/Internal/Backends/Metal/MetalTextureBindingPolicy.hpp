// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Internal::Backends::Metal
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
        PbrOcclusion
    };

    /** @brief Backend-owned neutral resource required by a missing stock texture slot. */
    enum class MetalNeutralTextureKind
    {
        /** @brief Opaque white RGBA 2D texture. */
        White2D,
        /** @brief Flat tangent-space normal RGBA 2D texture. */
        FlatNormal2D,
        /** @brief Opaque white RGBA cube texture. */
        WhiteCube
    };

    /** @brief Deterministic action for one shader texture slot. */
    enum class MetalTextureBindingDecision
    {
        /** @brief Bind the native texture supplied by the caller. */
        BindNative,
        /** @brief Bind the backend-owned neutral white 2D texture. */
        BindNeutralFallback,
        /** @brief Reject because no faithful native binding or neutral fallback exists. */
        Reject
    };

    /**
     * @brief Classifies a captured texture binding without consulting carried encoder state.
     *
     * @param callerProvided True when the draw captured a non-null backend texture pointer.
     * @param nativeResolved True when that pointer belongs to this Metal backend.
     * @param neutralFallbackAllowed True when the slot has a backend-owned neutral resource.
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
     * @brief Chooses the neutral resource for a missing built-in shader slot.
     *
     * @param slot Built-in shader texture slot.
     * @return The backend-owned neutral texture kind for that slot.
     */
    [[nodiscard]] constexpr MetalNeutralTextureKind MetalNeutralTextureForSlot(
        MetalStockTextureSlot slot) noexcept
    {
        if (slot == MetalStockTextureSlot::EnvironmentCube)
            return MetalNeutralTextureKind::WhiteCube;
        if (slot == MetalStockTextureSlot::PbrNormal)
            return MetalNeutralTextureKind::FlatNormal2D;
        return MetalNeutralTextureKind::White2D;
    }

    /**
     * @brief Classifies one built-in shader slot using its mandatory neutral fallback policy.
     *
     * @param slot Built-in shader texture slot.
     * @param callerProvided True when the captured draw state names a backend resource.
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
