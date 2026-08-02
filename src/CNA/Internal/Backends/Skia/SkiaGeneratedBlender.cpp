#include "CNA/Internal/Backends/Skia/SkiaGeneratedBlender.hpp"

#include "include/core/SkString.h"
#include "include/effects/SkRuntimeEffect.h"

#include <cmath>
#include <string>

namespace CNA::Internal::Backends::Skia
{
    namespace
    {
        struct CachedGeneratedBlendEffect final
        {
            sk_sp<SkRuntimeEffect> effect;
            std::string error;
        };

        [[nodiscard]] const CachedGeneratedBlendEffect& GeneratedBlendEffect()
        {
            static const CachedGeneratedBlendEffect cached = []
            {
                CachedGeneratedBlendEffect result;
                const auto compiled = SkRuntimeEffect::MakeForBlender(SkString(R"(
                    uniform int colorSourceFactor;
                    uniform int alphaSourceFactor;
                    uniform int colorDestinationFactor;
                    uniform int alphaDestinationFactor;
                    uniform int colorFunction;
                    uniform int alphaFunction;
                    uniform float4 blendFactor;

                    half4 factor(int selector, half4 src, half4 dst) {
                        if (selector == 0) return half4(1.0);                    // One
                        if (selector == 1) return half4(0.0);                    // Zero
                        if (selector == 2) return src;                           // SourceColor
                        if (selector == 3) return half4(1.0) - src;              // InverseSourceColor
                        if (selector == 4) return src.aaaa;                      // SourceAlpha
                        if (selector == 5) return half4(1.0 - src.a);            // InverseSourceAlpha
                        if (selector == 6) return dst;                           // DestinationColor
                        if (selector == 7) return half4(1.0) - dst;              // InverseDestinationColor
                        if (selector == 8) return dst.aaaa;                      // DestinationAlpha
                        if (selector == 9) return half4(1.0 - dst.a);            // InverseDestinationAlpha
                        if (selector == 10) return half4(blendFactor);           // BlendFactor
                        if (selector == 11) return half4(1.0) - blendFactor;      // InverseBlendFactor
                        half saturation = min(src.a, 1.0 - dst.a);               // SourceAlphaSaturation
                        return half4(saturation, saturation, saturation, 1.0);
                    }

                    half4 equation(int selector, half4 src, half4 dst,
                                   half4 weightedSrc, half4 weightedDst) {
                        if (selector == 0) return saturate(weightedSrc + weightedDst);
                        if (selector == 1) return saturate(weightedSrc - weightedDst);
                        if (selector == 2) return saturate(weightedDst - weightedSrc);
                        // EasyGL maps Max/Min to OpenGL GL_MAX/GL_MIN. OpenGL specifies that
                        // source/destination factors are ignored for these two equations.
                        if (selector == 3) return max(src, dst);
                        return min(src, dst);
                    }

                    half4 main(half4 src, half4 storedDst) {
                        // SkSurface stores the destination premultiplied. Recover the logical
                        // XNA/EasyGL destination components on which blend factors operate.
                        half4 dst = half4(storedDst.a > 0.0
                                             ? storedDst.rgb / storedDst.a
                                             : half3(0.0),
                                         storedDst.a);
                        half4 colorSrc = src * factor(colorSourceFactor, src, dst);
                        half4 colorDst = dst * factor(colorDestinationFactor, src, dst);
                        half4 alphaSrc = src * factor(alphaSourceFactor, src, dst);
                        half4 alphaDst = dst * factor(alphaDestinationFactor, src, dst);
                        half4 color = equation(colorFunction, src, dst, colorSrc, colorDst);
                        half4 alpha = equation(alphaFunction, src, dst, alphaSrc, alphaDst);
                        half4 logical = half4(saturate(color.rgb), saturate(alpha.a));
                        // Encode logical components for the premultiplied SkSurface. Public
                        // readback reverses this when output alpha is nonzero.
                        return half4(logical.rgb * logical.a, logical.a);
                    }
                )"));
                result.effect = compiled.effect;
                if (!compiled.effect)
                {
                    result.error = std::string("Skia generated blender SkSL compilation failed: ")
                        + compiled.errorText.c_str();
                }
                return result;
            }();
            return cached;
        }

        [[nodiscard]] bool ValidFactor(int factor) noexcept
        {
            return factor >= 0 && factor < kSkiaBlendFactorCount;
        }

        [[nodiscard]] bool ValidFunction(int function) noexcept
        {
            return function >= 0 && function < kSkiaBlendFunctionCount;
        }

        [[nodiscard]] bool Validate(const SkiaGeneratedBlendSelectors& selectors,
                                    const std::array<float, 4>& blendFactor,
                                    std::string& error)
        {
            const struct
            {
                const char* name;
                int value;
                bool factor;
            } fields[] = {
                {"color source factor", selectors.colorSourceFactor, true},
                {"alpha source factor", selectors.alphaSourceFactor, true},
                {"color destination factor", selectors.colorDestinationFactor, true},
                {"alpha destination factor", selectors.alphaDestinationFactor, true},
                {"color function", selectors.colorFunction, false},
                {"alpha function", selectors.alphaFunction, false},
            };
            for (const auto& field : fields)
            {
                if ((field.factor && !ValidFactor(field.value))
                    || (!field.factor && !ValidFunction(field.value)))
                {
                    error = std::string("Skia generated blender received invalid ") + field.name
                        + " ordinal " + std::to_string(field.value) + ".";
                    return false;
                }
            }
            for (std::size_t channel = 0; channel < blendFactor.size(); ++channel)
            {
                if (!std::isfinite(blendFactor[channel]) || blendFactor[channel] < 0.0f
                    || blendFactor[channel] > 1.0f)
                {
                    error = "Skia generated blender constant channel " + std::to_string(channel)
                        + " must be finite and within [0, 1].";
                    return false;
                }
            }
            return true;
        }
    }

    sk_sp<SkBlender> TryMakeSkiaGeneratedBlender(
        const SkiaGeneratedBlendSelectors& selectors,
        const std::array<float, 4>& blendFactor,
        std::string& error)
    {
        error.clear();
        if (!Validate(selectors, blendFactor, error))
            return nullptr;

        const CachedGeneratedBlendEffect& cached = GeneratedBlendEffect();
        if (!cached.effect)
        {
            error = cached.error;
            return nullptr;
        }

        SkRuntimeBlendBuilder builder(cached.effect);
        builder.uniform("colorSourceFactor") = selectors.colorSourceFactor;
        builder.uniform("alphaSourceFactor") = selectors.alphaSourceFactor;
        builder.uniform("colorDestinationFactor") = selectors.colorDestinationFactor;
        builder.uniform("alphaDestinationFactor") = selectors.alphaDestinationFactor;
        builder.uniform("colorFunction") = selectors.colorFunction;
        builder.uniform("alphaFunction") = selectors.alphaFunction;
        builder.uniform("blendFactor") = blendFactor;
        sk_sp<SkBlender> blender = builder.makeBlender();
        if (!blender)
            error = "Skia generated blender failed to instantiate the validated selector tuple.";
        return blender;
    }

    std::size_t SkiaGeneratedBlenderCompileCountEXT() noexcept
    {
        (void)GeneratedBlendEffect();
        return 1u;
    }
} // namespace CNA::Internal::Backends::Skia
