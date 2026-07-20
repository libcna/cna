#pragma once

#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"

// plan_metal.md: real bug found 2026-07-20 in the original metalMinFilter()/metalMagFilter()/
// metalMipFilter() case-set membership (three independently-maintained {..} sets, easy to
// transcribe wrong) -- 3 of the 9 real XNA TextureFilter values (3/6/7) produced the wrong
// min-or-mag filter. Reimplemented as a single per-filter switch that spells out each filter's
// Min/Mag/Mip components explicitly, matching the XNA enum's own self-documenting names exactly
// (e.g. `MinLinearMagPointMipPoint` -- the name IS the spec), which is inherently harder to get
// wrong than three separately-derived membership sets. Only reads a plain XNA `TextureFilter`
// ordinal and returns a plain C++ struct -- zero Objective-C dependency, so this is genuinely
// unit-tested on this Linux machine; only the final `MTLSamplerMinMagFilter`/`MTLSamplerMipFilter`
// enum translation stays in MetalGraphicsBackend.mm.
//
// plan_metal.md METAL-19: the switch below dispatches on the real `TextureFilter` enumerator
// names (cast once, at the top, from the plain int the .mm call sites still pass), not on magic
// integer literals -- unlike the version this replaced, which switched on raw case 1/3/4/5/6/7/8
// values with only a comment recording the assumed ordinals. A future reordering of
// TextureFilter's declaration is now a compile-time-irrelevant no-op here: the compiler resolves
// each `TextureFilter::Name` to whatever its current value actually is, so this function cannot
// silently drift out of sync with the enum the way the old literal-based version could have.
namespace CNA::Internal::Backends::Metal
{
    struct MetalSamplerFilterPlan
    {
        bool minIsPoint;
        bool magIsPoint;
        bool mipIsPoint;
    };

    inline bool operator==(const MetalSamplerFilterPlan& a, const MetalSamplerFilterPlan& b)
    {
        return a.minIsPoint == b.minIsPoint && a.magIsPoint == b.magIsPoint && a.mipIsPoint == b.mipIsPoint;
    }

    inline MetalSamplerFilterPlan DescribeMetalSamplerFilter(int xnaFilter)
    {
        using TF = Microsoft::Xna::Framework::Graphics::TextureFilter;
        switch (static_cast<TF>(xnaFilter)) {
            case TF::Point:                      return MetalSamplerFilterPlan{true,  true,  true};
            case TF::LinearMipPoint:              return MetalSamplerFilterPlan{false, false, true};
            case TF::PointMipLinear:              return MetalSamplerFilterPlan{true,  true,  false};
            case TF::MinLinearMagPointMipLinear:  return MetalSamplerFilterPlan{false, true,  false};
            case TF::MinLinearMagPointMipPoint:   return MetalSamplerFilterPlan{false, true,  true};
            case TF::MinPointMagLinearMipLinear:  return MetalSamplerFilterPlan{true,  false, false};
            case TF::MinPointMagLinearMipPoint:   return MetalSamplerFilterPlan{true,  false, true};
            case TF::Linear:
            case TF::Anisotropic:
            default:
                return MetalSamplerFilterPlan{false, false, false};
        }
    }
}
