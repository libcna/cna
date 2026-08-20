// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/PbrMaterial.hpp"
#include "CNA/Graphics/PbrMaterialExtensions.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureTransformEXT.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>

namespace Microsoft::Xna::Framework::Graphics { class Texture2D; }

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief The textures an imported material's slots resolve to, in `PbrTextureSlot` order.
     *
     * The importer decodes *images*; turning one into a `Texture2D` needs a device and is the
     * loader's job, so the two arrive separately and meet here. Any entry may be null.
     */
    struct GltfMaterialTexturesEXT
    {
        /** @brief One texture per slot: base colour, normal, metallic-roughness, emissive,
         *         occlusion, specular, specular colour. */
        std::array<Microsoft::Xna::Framework::Graphics::Texture2D*, kPbrTextureSlotCount>
            Slots{};
    };

    /**
     * @brief What `materialFromGltfEXT` needs of an imported glTF material.
     *
     * plans/plan_modern.md `MOD-1310`. The bridge is expressed against this concept rather than against
     * `CNA::Internal::GltfImport::MaterialOut` by name, and that is the whole point: the engine
     * layer would otherwise have to link the content module and see `cgltf`'s headers, for a
     * function that only reads fourteen plain values. The importer keeps exposing plain data; this
     * names exactly which of it the bridge reads, and a change on either side that breaks the
     * agreement is a compile error rather than a silent mismatch.
     */
    template<typename T>
    concept GltfMaterialSourceEXT = requires(const T& source) {
        { source.baseColorFactor } -> std::convertible_to<Microsoft::Xna::Framework::Vector4>;
        { source.metallicFactor } -> std::convertible_to<float>;
        { source.roughnessFactor } -> std::convertible_to<float>;
        { source.emissiveFactor } -> std::convertible_to<Microsoft::Xna::Framework::Vector3>;
        { source.normalScale } -> std::convertible_to<float>;
        { source.occlusionStrength } -> std::convertible_to<float>;
        { source.iorEXT } -> std::convertible_to<float>;
        { source.specularFactorEXT } -> std::convertible_to<float>;
        { source.specularColorFactorEXT } -> std::convertible_to<Microsoft::Xna::Framework::Vector3>;
        { source.alphaMode }
            -> std::convertible_to<Microsoft::Xna::Framework::Graphics::AlphaModeEXT>;
        { source.alphaCutoff } -> std::convertible_to<float>;
        { source.doubleSided } -> std::convertible_to<bool>;
        { source.textureCoordinateSetsEXT[0] } -> std::convertible_to<int>;
        { source.textureTransformsEXT[0] }
            -> std::convertible_to<Microsoft::Xna::Framework::Graphics::TextureTransformEXT>;
    };

    /**
     * @brief Builds a `PbrMaterial` from one imported glTF material and its resolved textures.
     *
     * plans/plan_modern.md `MOD-1309`. Nothing in the runtime import path changes: the importer keeps
     * producing the `PbrEffect` it always did, and this is a second, optional reading of the same
     * decoded record for an application that wants the material as a value it can store, compare
     * or serialize.
     *
     * **One value is not carried exactly.** glTF's `baseColorFactor` is four floats; a material's
     * albedo factor is a `Color`, so it is quantised to 8 bits per channel here. Everything else
     * -- including the emissive factor, which `KHR_materials_emissive_strength` may push above 1 --
     * arrives unchanged. Applying the result to a `PbrEffect` and reading it back is still exact
     * (`MOD-1305`); the quantisation happens once, on the way in.
     *
     * @param source   The imported material record.
     * @param textures The textures its slots resolve to; any may be null.
     * @return The material.
     */
    template<GltfMaterialSourceEXT TSource>
    [[nodiscard]] PbrMaterial materialFromGltfEXT(const TSource& source,
                                                  const GltfMaterialTexturesEXT& textures)
    {
        const auto toByte = [](const float value) {
            return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
        };

        PbrMaterial material;
        material.setAlbedoTexture(textures.Slots[0]);
        material.setNormalTexture(textures.Slots[1]);
        material.setMetallicRoughnessTexture(textures.Slots[2]);
        material.setEmissiveTexture(textures.Slots[3]);
        material.setAmbientOcclusionTexture(textures.Slots[4]);
        material.setSpecularTexture(textures.Slots[5]);
        material.setSpecularColorTexture(textures.Slots[6]);

        material.setAlbedoColor(Microsoft::Xna::Framework::Color(
            toByte(source.baseColorFactor.X), toByte(source.baseColorFactor.Y),
            toByte(source.baseColorFactor.Z), toByte(source.baseColorFactor.W)));
        material.setMetallicFactor(source.metallicFactor);
        material.setRoughnessFactor(source.roughnessFactor);
        material.setEmissiveFactor(source.emissiveFactor);
        material.setNormalScale(source.normalScale);
        material.setOcclusionStrength(source.occlusionStrength);
        material.setIor(source.iorEXT);
        material.setSpecularFactor(source.specularFactorEXT);
        material.setSpecularColorFactor(source.specularColorFactorEXT);

        material.setAlphaMode(source.alphaMode);
        material.setAlphaCutoff(source.alphaCutoff);
        material.setDoubleSided(source.doubleSided);

        for (int slot = 0; slot < kPbrTextureSlotCount; ++slot)
        {
            const auto named = static_cast<PbrTextureSlot>(slot);
            material.setTextureCoordinateSet(
                named, static_cast<int>(source.textureCoordinateSetsEXT[
                    static_cast<std::size_t>(slot)]));
            material.setTextureTransform(
                named, source.textureTransformsEXT[static_cast<std::size_t>(slot)]);
        }
        return material;
    }

    /**
     * @brief What `materialExtensionsFromGltfEXT` needs of an imported glTF material.
     *
     * plans/plan_modern.md `MOD-2076`. Separate from `GltfMaterialSourceEXT` rather than folded into
     * it, so that a caller with an older importer record still satisfies the first concept: the
     * extension fields arrived later, and a single widened concept would have turned that into a
     * compile error at every existing call site.
     */
    template<typename T>
    concept GltfMaterialExtensionSourceEXT = requires(const T& source) {
        { source.clearcoatFactorEXT } -> std::convertible_to<float>;
        { source.clearcoatRoughnessFactorEXT } -> std::convertible_to<float>;
        { source.sheenColorFactorEXT } -> std::convertible_to<Microsoft::Xna::Framework::Vector3>;
        { source.sheenRoughnessFactorEXT } -> std::convertible_to<float>;
        { source.transmissionFactorEXT } -> std::convertible_to<float>;
        { source.thicknessFactorEXT } -> std::convertible_to<float>;
        { source.attenuationDistanceEXT } -> std::convertible_to<float>;
        { source.attenuationColorEXT } -> std::convertible_to<Microsoft::Xna::Framework::Vector3>;
        { source.iridescenceFactorEXT } -> std::convertible_to<float>;
        { source.iridescenceIorEXT } -> std::convertible_to<float>;
        { source.iridescenceThicknessMinimumEXT } -> std::convertible_to<float>;
        { source.iridescenceThicknessMaximumEXT } -> std::convertible_to<float>;
    };

    /**
     * @brief The textures an imported material's extension slots resolve to.
     *
     * As with @ref GltfMaterialTexturesEXT, the importer decodes images and the loader turns them
     * into textures; the two meet here. Any entry may be null.
     */
    struct GltfMaterialExtensionTexturesEXT
    {
        /** @brief `KHR_materials_clearcoat.clearcoatTexture`. */
        Microsoft::Xna::Framework::Graphics::Texture2D* Clearcoat = nullptr;
        /** @brief `KHR_materials_clearcoat.clearcoatRoughnessTexture`. */
        Microsoft::Xna::Framework::Graphics::Texture2D* ClearcoatRoughness = nullptr;
        /** @brief `KHR_materials_clearcoat.clearcoatNormalTexture`. */
        Microsoft::Xna::Framework::Graphics::Texture2D* ClearcoatNormal = nullptr;
        /** @brief `KHR_materials_sheen.sheenColorTexture`. */
        Microsoft::Xna::Framework::Graphics::Texture2D* SheenColor = nullptr;
        /** @brief `KHR_materials_sheen.sheenRoughnessTexture`. */
        Microsoft::Xna::Framework::Graphics::Texture2D* SheenRoughness = nullptr;
        /** @brief `KHR_materials_transmission.transmissionTexture`. */
        Microsoft::Xna::Framework::Graphics::Texture2D* Transmission = nullptr;
        /** @brief `KHR_materials_volume.thicknessTexture`. */
        Microsoft::Xna::Framework::Graphics::Texture2D* Thickness = nullptr;
        /** @brief `KHR_materials_iridescence.iridescenceTexture`. */
        Microsoft::Xna::Framework::Graphics::Texture2D* Iridescence = nullptr;
        /** @brief `KHR_materials_iridescence.iridescenceThicknessTexture`. */
        Microsoft::Xna::Framework::Graphics::Texture2D* IridescenceThickness = nullptr;
    };

    /**
     * @brief Builds a `PbrMaterialExtensions` from one imported glTF material.
     *
     * plans/plan_modern.md `MOD-2076`. A file declaring none of these extensions produces a **neutral**
     * set, because the importer carries each extension's own default -- so reading the extensions
     * of a material that has none changes nothing, and a loader can call this unconditionally.
     *
     * Subsurface scattering is not among them: it is a CNA addition rather than a glTF extension
     * (`MOD-2074`), so nothing in a file names it and nothing here invents it.
     *
     * @param source   The imported material record.
     * @param textures The textures its extension slots resolve to; any may be null.
     * @return The extension set.
     */
    template<GltfMaterialExtensionSourceEXT TSource>
    [[nodiscard]] PbrMaterialExtensions materialExtensionsFromGltfEXT(
        const TSource& source, const GltfMaterialExtensionTexturesEXT& textures = {})
    {
        PbrMaterialExtensions extensions;

        extensions.setClearcoatFactor(source.clearcoatFactorEXT);
        extensions.setClearcoatRoughness(source.clearcoatRoughnessFactorEXT);
        extensions.setClearcoatTexture(textures.Clearcoat);
        extensions.setClearcoatRoughnessTexture(textures.ClearcoatRoughness);
        extensions.setClearcoatNormalTexture(textures.ClearcoatNormal);

        extensions.setSheenColorFactor(source.sheenColorFactorEXT);
        extensions.setSheenRoughness(source.sheenRoughnessFactorEXT);
        extensions.setSheenColorTexture(textures.SheenColor);
        extensions.setSheenRoughnessTexture(textures.SheenRoughness);

        extensions.setTransmissionFactor(source.transmissionFactorEXT);
        extensions.setTransmissionTexture(textures.Transmission);
        extensions.setThicknessFactor(source.thicknessFactorEXT);
        extensions.setThicknessTexture(textures.Thickness);
        extensions.setAttenuationDistance(source.attenuationDistanceEXT);
        extensions.setAttenuationColor(source.attenuationColorEXT);

        extensions.setIridescenceFactor(source.iridescenceFactorEXT);
        extensions.setIridescenceIor(source.iridescenceIorEXT);
        extensions.setIridescenceThicknessMinimum(source.iridescenceThicknessMinimumEXT);
        extensions.setIridescenceThicknessMaximum(source.iridescenceThicknessMaximumEXT);
        extensions.setIridescenceTexture(textures.Iridescence);
        extensions.setIridescenceThicknessTexture(textures.IridescenceThickness);

        return extensions;
    }

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
