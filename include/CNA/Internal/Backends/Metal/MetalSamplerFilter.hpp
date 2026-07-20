#pragma once

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

    // Microsoft::Xna::Framework::Graphics::TextureFilter ordinals: 0 Linear, 1 Point,
    // 2 Anisotropic, 3 LinearMipPoint, 4 PointMipLinear, 5 MinLinearMagPointMipLinear,
    // 6 MinLinearMagPointMipPoint, 7 MinPointMagLinearMipLinear, 8 MinPointMagLinearMipPoint.
    inline MetalSamplerFilterPlan DescribeMetalSamplerFilter(int xnaFilter)
    {
        switch (xnaFilter) {
            case 1: return MetalSamplerFilterPlan{true,  true,  true};   // Point
            case 3: return MetalSamplerFilterPlan{false, false, true};  // LinearMipPoint
            case 4: return MetalSamplerFilterPlan{true,  true,  false}; // PointMipLinear
            case 5: return MetalSamplerFilterPlan{false, true,  false}; // MinLinearMagPointMipLinear
            case 6: return MetalSamplerFilterPlan{false, true,  true};  // MinLinearMagPointMipPoint
            case 7: return MetalSamplerFilterPlan{true,  false, false}; // MinPointMagLinearMipLinear
            case 8: return MetalSamplerFilterPlan{true,  false, true};  // MinPointMagLinearMipPoint
            default: return MetalSamplerFilterPlan{false, false, false}; // 0 Linear, 2 Anisotropic
        }
    }
}
