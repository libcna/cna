// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"

// plans/plan_metal.md METAL-19: mirrors MetalSamplerFilter.hpp's own established pattern -- the previous
// metalCompareFunction() in MetalRenderer.mm switched on raw `int` literals (case 1, 2, ...)
// with only a comment recording the assumed CompareFunction ordinals, so a future reordering of
// CompareFunction's declaration would silently desync from this table with no compiler error.
// Switching on the real `CompareFunction` enumerator names instead (cast once, at the top, from the
// plain int the .mm call site still passes) makes that class of drift compile-time-irrelevant: the
// compiler resolves each `CompareFunction::Name` to whatever its current value actually is. Only
// reads the XNA enum and returns a plain C++ enum describing the equivalent Metal semantic -- zero
// Objective-C dependency, genuinely unit-tested on this Linux machine; the final
// `MTLCompareFunction` translation (a trivial 1:1 name match) stays in MetalRenderer.mm,
// the one part of this that genuinely needs the Apple SDK.
namespace CNA::Internal::Renderers::Metal
{
    /** @brief Renderer-neutral Metal comparison-function classifications. */
    enum class MetalCompareFunctionKind
    {
        /** @brief Never passes. */
        Never,
        /** @brief Passes when the incoming value is less. */
        Less,
        /** @brief Passes when the incoming value is less than or equal. */
        LessEqual,
        /** @brief Passes when both values are equal. */
        Equal,
        /** @brief Passes when the incoming value is greater than or equal. */
        GreaterEqual,
        /** @brief Passes when the incoming value is greater. */
        Greater,
        /** @brief Passes when the values differ. */
        NotEqual,
        /** @brief Always passes. */
        Always
    };

    /**
     * @brief Translates an XNA comparison ordinal to its Metal classification.
     *
     * @param xnaCompare Raw Microsoft.Xna.Framework.Graphics.CompareFunction ordinal.
     * @return The corresponding renderer-neutral Metal comparison function.
     */
    [[nodiscard]] inline MetalCompareFunctionKind DescribeMetalCompareFunction(int xnaCompare)
    {
        using CF = Microsoft::Xna::Framework::Graphics::CompareFunction;
        switch (static_cast<CF>(xnaCompare)) {
            case CF::Never:         return MetalCompareFunctionKind::Never;
            case CF::Less:          return MetalCompareFunctionKind::Less;
            case CF::LessEqual:     return MetalCompareFunctionKind::LessEqual;
            case CF::Equal:         return MetalCompareFunctionKind::Equal;
            case CF::GreaterEqual:  return MetalCompareFunctionKind::GreaterEqual;
            case CF::Greater:       return MetalCompareFunctionKind::Greater;
            case CF::NotEqual:      return MetalCompareFunctionKind::NotEqual;
            case CF::Always:
            default:                return MetalCompareFunctionKind::Always;
        }
    }
}
