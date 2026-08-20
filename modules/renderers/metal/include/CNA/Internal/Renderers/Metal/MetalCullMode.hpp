// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"

// plans/plan_metal.md METAL-19/METAL-5: same reasoning as MetalCompareFunction.hpp -- switches on the
// real `CullMode` enumerator names instead of the raw `c==1?...:(c==2?...:...)` ternary chain
// ApplyRasterizerState() previously used inline. METAL-5's own explicit mapping
// (CullClockwiseFace(1)->Front, CullCounterClockwiseFace(2)->Back) is preserved exactly, just made
// enum-reordering-safe and independently unit-testable.
namespace CNA::Internal::Renderers::Metal
{
    /** @brief Renderer-neutral Metal culling-mode classifications. */
    enum class MetalCullModeKind
    {
        /** @brief Disables face culling. */
        None,
        /** @brief Culls front-facing primitives. */
        Front,
        /** @brief Culls back-facing primitives. */
        Back
    };

    /**
     * @brief Translates an XNA culling ordinal to its Metal classification.
     *
     * @param xnaCull Raw Microsoft.Xna.Framework.Graphics.CullMode ordinal.
     * @return The corresponding renderer-neutral Metal culling mode.
     */
    [[nodiscard]] inline MetalCullModeKind DescribeMetalCullMode(int xnaCull)
    {
        using CM = Microsoft::Xna::Framework::Graphics::CullMode;
        switch (static_cast<CM>(xnaCull)) {
            case CM::CullClockwiseFace:        return MetalCullModeKind::Front;
            case CM::CullCounterClockwiseFace: return MetalCullModeKind::Back;
            case CM::None:
            default:                           return MetalCullModeKind::None;
        }
    }
}
