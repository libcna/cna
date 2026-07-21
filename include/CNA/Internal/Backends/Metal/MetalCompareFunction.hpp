#pragma once

#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"

// plan_metal.md METAL-19: mirrors MetalSamplerFilter.hpp's own established pattern -- the previous
// metalCompareFunction() in MetalGraphicsBackend.mm switched on raw `int` literals (case 1, 2, ...)
// with only a comment recording the assumed CompareFunction ordinals, so a future reordering of
// CompareFunction's declaration would silently desync from this table with no compiler error.
// Switching on the real `CompareFunction` enumerator names instead (cast once, at the top, from the
// plain int the .mm call site still passes) makes that class of drift compile-time-irrelevant: the
// compiler resolves each `CompareFunction::Name` to whatever its current value actually is. Only
// reads the XNA enum and returns a plain C++ enum describing the equivalent Metal semantic -- zero
// Objective-C dependency, genuinely unit-tested on this Linux machine; the final
// `MTLCompareFunction` translation (a trivial 1:1 name match) stays in MetalGraphicsBackend.mm,
// the one part of this that genuinely needs the Apple SDK.
namespace CNA::Internal::Backends::Metal
{
    enum class MetalCompareFunctionKind
    {
        Never, Less, LessEqual, Equal, GreaterEqual, Greater, NotEqual, Always
    };

    inline MetalCompareFunctionKind DescribeMetalCompareFunction(int xnaCompare)
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
