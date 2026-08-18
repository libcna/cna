// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/MaterialBinding.hpp"

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"

#include <algorithm>
#include <cmath>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PbrEffect;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect;
using Microsoft::Xna::Framework::Graphics::TextureTransformEXT;

namespace CNA::Graphics {

    namespace {

        /// The five slots an effect addresses by index; the last two are named properties there.
        constexpr int kIndexedSlots = 5;

        [[nodiscard]] float ToLinearComponent(const int byteValue)
        {
            return static_cast<float>(byteValue) / 255.0f;
        }

        /// The inverse of ToLinearComponent, rounding rather than truncating: truncation would
        /// lose one step on almost every channel and break the round trip this pair exists for.
        [[nodiscard]] int ToByteComponent(const float value)
        {
            const float clamped = std::clamp(value, 0.0f, 1.0f);
            return static_cast<int>(std::lround(clamped * 255.0f));
        }

        template<typename TEffect>
        void ApplyTo(const PbrMaterial& material, TEffect& effect)
        {
            effect.setTextureProperty(material.getAlbedoTexture());
            effect.setNormalMapProperty(material.getNormalTexture());
            effect.setMetallicRoughnessMapProperty(material.getMetallicRoughnessTexture());
            effect.setEmissiveMapProperty(material.getEmissiveTexture());
            effect.setOcclusionMapProperty(material.getAmbientOcclusionTexture());
            effect.setSpecularMapEXTProperty(material.getSpecularTexture());
            effect.setSpecularColorMapEXTProperty(material.getSpecularColorTexture());

            const Color albedo = material.getAlbedoColor();
            effect.setDiffuseColorProperty(Vector3(ToLinearComponent(albedo.getRProperty()),
                                                   ToLinearComponent(albedo.getGProperty()),
                                                   ToLinearComponent(albedo.getBProperty())));
            effect.setAlphaProperty(ToLinearComponent(albedo.getAProperty()));

            effect.setMetallicFactorProperty(material.getMetallicFactor());
            effect.setRoughnessFactorProperty(material.getRoughnessFactor());
            effect.setEmissiveFactorProperty(material.getEmissiveFactor());
            effect.setNormalScaleEXTProperty(material.getNormalScale());
            effect.setOcclusionStrengthEXTProperty(material.getOcclusionStrength());
            effect.setIorEXTProperty(material.getIor());
            effect.setSpecularFactorEXTProperty(material.getSpecularFactor());
            effect.setSpecularColorFactorEXTProperty(material.getSpecularColorFactor());

            effect.setAlphaModeEXTProperty(material.getAlphaMode());
            effect.setAlphaCutoffEXTProperty(material.getAlphaCutoff());
            effect.setDoubleSidedEXTProperty(material.isDoubleSided());

            for (int slot = 0; slot < kIndexedSlots; ++slot)
            {
                const auto named = static_cast<PbrTextureSlot>(slot);
                effect.setTextureCoordinateSetEXTProperty(
                    slot, material.getTextureCoordinateSet(named));
                effect.setTextureTransformEXTProperty(slot, material.getTextureTransform(named));
            }
            effect.setSpecularTextureCoordinateSetEXTProperty(
                material.getTextureCoordinateSet(PbrTextureSlot::Specular));
            effect.setSpecularColorTextureCoordinateSetEXTProperty(
                material.getTextureCoordinateSet(PbrTextureSlot::SpecularColor));
            effect.setSpecularTextureTransformEXTProperty(
                material.getTextureTransform(PbrTextureSlot::Specular));
            effect.setSpecularColorTextureTransformEXTProperty(
                material.getTextureTransform(PbrTextureSlot::SpecularColor));

            effect.setBaseColorTextureIsSrgbEXTProperty(material.isBaseColorTextureSrgb());
            effect.setEmissiveTextureIsSrgbEXTProperty(material.isEmissiveTextureSrgb());
            effect.setSpecularColorTextureIsSrgbEXTProperty(material.isSpecularColorTextureSrgb());
            effect.setEncodeOutputToSrgbEXTProperty(material.isOutputEncodedToSrgb());
        }

        template<typename TEffect>
        [[nodiscard]] PbrMaterial ExtractFrom(const TEffect& effect)
        {
            PbrMaterial material;
            material.setAlbedoTexture(effect.getTextureProperty());
            material.setNormalTexture(effect.getNormalMapProperty());
            material.setMetallicRoughnessTexture(effect.getMetallicRoughnessMapProperty());
            material.setEmissiveTexture(effect.getEmissiveMapProperty());
            material.setAmbientOcclusionTexture(effect.getOcclusionMapProperty());
            material.setSpecularTexture(effect.getSpecularMapEXTProperty());
            material.setSpecularColorTexture(effect.getSpecularColorMapEXTProperty());

            const Vector3 diffuse = effect.getDiffuseColorProperty();
            material.setAlbedoColor(Color(ToByteComponent(diffuse.X), ToByteComponent(diffuse.Y),
                                          ToByteComponent(diffuse.Z),
                                          ToByteComponent(effect.getAlphaProperty())));

            material.setMetallicFactor(effect.getMetallicFactorProperty());
            material.setRoughnessFactor(effect.getRoughnessFactorProperty());
            material.setEmissiveFactor(effect.getEmissiveFactorProperty());
            material.setNormalScale(effect.getNormalScaleEXTProperty());
            material.setOcclusionStrength(effect.getOcclusionStrengthEXTProperty());
            material.setIor(effect.getIorEXTProperty());
            material.setSpecularFactor(effect.getSpecularFactorEXTProperty());
            material.setSpecularColorFactor(effect.getSpecularColorFactorEXTProperty());

            material.setAlphaMode(effect.getAlphaModeEXTProperty());
            material.setAlphaCutoff(effect.getAlphaCutoffEXTProperty());
            material.setDoubleSided(effect.getDoubleSidedEXTProperty());

            const auto& sets = effect.getTextureCoordinateSetsEXTProperty();
            const auto& transforms = effect.getTextureTransformsEXTProperty();
            for (int slot = 0; slot < kIndexedSlots; ++slot)
            {
                const auto named = static_cast<PbrTextureSlot>(slot);
                material.setTextureCoordinateSet(named, sets[static_cast<std::size_t>(slot)]);
                material.setTextureTransform(named, transforms[static_cast<std::size_t>(slot)]);
            }
            material.setTextureCoordinateSet(PbrTextureSlot::Specular,
                                             effect.getSpecularTextureCoordinateSetEXTProperty());
            material.setTextureCoordinateSet(
                PbrTextureSlot::SpecularColor,
                effect.getSpecularColorTextureCoordinateSetEXTProperty());
            material.setTextureTransform(PbrTextureSlot::Specular,
                                         effect.getSpecularTextureTransformEXTProperty());
            material.setTextureTransform(PbrTextureSlot::SpecularColor,
                                         effect.getSpecularColorTextureTransformEXTProperty());

            material.setBaseColorTextureSrgb(effect.getBaseColorTextureIsSrgbEXTProperty());
            material.setEmissiveTextureSrgb(effect.getEmissiveTextureIsSrgbEXTProperty());
            material.setSpecularColorTextureSrgb(
                effect.getSpecularColorTextureIsSrgbEXTProperty());
            material.setOutputEncodedToSrgb(effect.getEncodeOutputToSrgbEXTProperty());
            return material;
        }

    } // namespace

    void applyMaterial(const PbrMaterial& material, PbrEffect& effect)
    {
        ApplyTo(material, effect);
    }

    void applyMaterial(const PbrMaterial& material, SkinnedPbrEffect& effect)
    {
        ApplyTo(material, effect);
    }

    PbrMaterial extractMaterial(const PbrEffect& effect) { return ExtractFrom(effect); }

    PbrMaterial extractMaterial(const SkinnedPbrEffect& effect) { return ExtractFrom(effect); }

    void applyMaterialState(const PbrMaterial& material, GraphicsDevice& device)
    {
        device.setBlendStateProperty(material.getAlphaMode() == AlphaModeEXT::Blend
                                         ? BlendState::NonPremultiplied
                                         : BlendState::Opaque);
        device.setRasterizerStateProperty(material.isDoubleSided()
                                              ? RasterizerState::CullNone
                                              : RasterizerState::CullCounterClockwise);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
