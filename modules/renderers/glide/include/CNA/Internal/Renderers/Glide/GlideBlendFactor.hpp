#pragma once

#include <stdexcept>

#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"

namespace CNA::Internal::Renderers::Glide
{
    /** Historical Glide 3.x GrAlphaBlendFnc_t numeric codes (Glide 3.0 Reference). */
    inline constexpr int kGlideBlendZero = 0x0;
    inline constexpr int kGlideBlendSourceAlpha = 0x1;
    inline constexpr int kGlideBlendSourceColor = 0x2;
    inline constexpr int kGlideBlendDestinationAlpha = 0x3;
    inline constexpr int kGlideBlendOne = 0x4;
    inline constexpr int kGlideBlendInverseSourceAlpha = 0x5;
    inline constexpr int kGlideBlendInverseSourceColor = 0x6;
    inline constexpr int kGlideBlendInverseDestinationAlpha = 0x7;
    inline constexpr int kGlideBlendAlphaSaturate = 0xf;

    /**
     * grAlphaBlendFunction(rgb_sf, rgb_df, alpha_sf, alpha_df) takes four independent arguments,
     * but the *_sf ("source factor") and *_df ("destination factor") slots accept different,
     * argument-position-dependent legal value sets.
     */
    enum class GlideBlendSlot { RgbSource, RgbDestination, AlphaSource, AlphaDestination };

    /**
     * Maps an XNA Blend value into the native Glide code for one specific argument slot of
     * grAlphaBlendFunction.
     *
     * A *_sf slot may reference GR_BLEND_DST_COLOR/GR_BLEND_ONE_MINUS_DST_COLOR and
     * GR_BLEND_ALPHA_SATURATE, never GR_BLEND_SRC_COLOR. A *_df slot may reference
     * GR_BLEND_SRC_COLOR/GR_BLEND_ONE_MINUS_SRC_COLOR, never GR_BLEND_DST_COLOR or
     * ALPHA_SATURATE. GR_BLEND_SRC_COLOR and GR_BLEND_DST_COLOR (and their inverses) even share
     * the same native numeric code, so which XNA Blend value is representable depends entirely on
     * which of the four slots it is going into. Throws for a Blend value that slot cannot express.
     */
    [[nodiscard]] inline int ToGlideBlendFactor(Microsoft::Xna::Framework::Graphics::Blend blend, GlideBlendSlot slot)
    {
        using Microsoft::Xna::Framework::Graphics::Blend;
        const bool isSourceSlot = slot == GlideBlendSlot::RgbSource || slot == GlideBlendSlot::AlphaSource;
        switch (blend)
        {
            case Blend::One:  return kGlideBlendOne;
            case Blend::Zero: return kGlideBlendZero;
            case Blend::SourceAlpha:             return kGlideBlendSourceAlpha;
            case Blend::InverseSourceAlpha:      return kGlideBlendInverseSourceAlpha;
            case Blend::DestinationAlpha:        return kGlideBlendDestinationAlpha;
            case Blend::InverseDestinationAlpha: return kGlideBlendInverseDestinationAlpha;
            case Blend::SourceColor:
                if (isSourceSlot)
                {
                    throw std::runtime_error(
                        "GLIDE renderer cannot use Blend::SourceColor as a source blend factor: "
                        "Glide's source-factor argument slot can only reference the destination colour");
                }
                return kGlideBlendSourceColor;
            case Blend::InverseSourceColor:
                if (isSourceSlot)
                {
                    throw std::runtime_error(
                        "GLIDE renderer cannot use Blend::InverseSourceColor as a source blend factor: "
                        "Glide's source-factor argument slot can only reference the destination colour");
                }
                return kGlideBlendInverseSourceColor;
            case Blend::DestinationColor:
                if (!isSourceSlot)
                {
                    throw std::runtime_error(
                        "GLIDE renderer cannot use Blend::DestinationColor as a destination blend factor: "
                        "Glide's destination-factor argument slot can only reference the source colour");
                }
                return kGlideBlendSourceColor; // native code 0x2 means GR_BLEND_DST_COLOR in a *_sf slot
            case Blend::InverseDestinationColor:
                if (!isSourceSlot)
                {
                    throw std::runtime_error(
                        "GLIDE renderer cannot use Blend::InverseDestinationColor as a destination blend "
                        "factor: Glide's destination-factor argument slot can only reference the source "
                        "colour");
                }
                return kGlideBlendInverseSourceColor; // native code 0x6 means ONE_MINUS_DST_COLOR here
            case Blend::SourceAlphaSaturation:
                if (!isSourceSlot)
                {
                    throw std::runtime_error(
                        "GLIDE renderer cannot use Blend::SourceAlphaSaturation as a destination blend "
                        "factor: GR_BLEND_ALPHA_SATURATE is a source-factor-only value in Glide");
                }
                return kGlideBlendAlphaSaturate;
            case Blend::BlendFactor:
            case Blend::InverseBlendFactor:
                throw std::runtime_error(
                    "GLIDE renderer does not support BlendFactor/InverseBlendFactor: Glide 3.x "
                    "has no programmable constant blend color in this 2D scope.");
        }
        throw std::runtime_error("GLIDE renderer received an unknown XNA Blend value");
    }

    /**
     * The Glide 3.0 Reference documents destination-alpha blend factors (GR_BLEND_DST_ALPHA,
     * GR_BLEND_ONE_MINUS_DST_ALPHA) and GR_BLEND_ALPHA_SATURATE as producing undefined results
     * whenever depth buffering (or triple buffering) is enabled, because they contend for the
     * same auxiliary buffer Glide uses for the Z plane.
     */
    [[nodiscard]] inline bool GlideBlendFactorNeedsAuxiliaryAlpha(int factor)
    {
        return factor == kGlideBlendDestinationAlpha || factor == kGlideBlendInverseDestinationAlpha ||
               factor == kGlideBlendAlphaSaturate;
    }

    /** True if any of the four resolved native blend factors needs the auxiliary alpha buffer. */
    [[nodiscard]] inline bool GlideBlendFactorsNeedAuxiliaryAlpha(
        int colorSrc, int colorDst, int alphaSrc, int alphaDst)
    {
        return GlideBlendFactorNeedsAuxiliaryAlpha(colorSrc) || GlideBlendFactorNeedsAuxiliaryAlpha(colorDst) ||
               GlideBlendFactorNeedsAuxiliaryAlpha(alphaSrc) || GlideBlendFactorNeedsAuxiliaryAlpha(alphaDst);
    }
} // namespace CNA::Internal::Renderers::Glide
