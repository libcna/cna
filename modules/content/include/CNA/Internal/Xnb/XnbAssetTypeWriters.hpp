// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "CNA/Internal/Xnb/XnbFileOptions.hpp"
#include "CNA/Internal/Xnb/XnbTypeWriter.hpp"

namespace CNA::Internal::Xnb
{
    /**
     * @brief A `Texture2D` asset ready for serialization
     *        (plans/plan_xnapipeline.md `XNAP-23`).
     *
     * `XnbTextureData` already carries a @ref XnbTextureKind discriminant, but a registry keyed by
     * C++ type needs three distinct types to select between `Texture2DReader`, `Texture3DReader`
     * and `TextureCubeReader`. These three wrappers make the choice explicit at the call site
     * instead of hiding it in a runtime branch that could silently emit the wrong reader.
     */
    struct XnbTexture2DContent
    {
        /** @brief Canonical texture fields and level bytes; `kind` must be `Texture2D`. */
        XnbTextureData texture;

        /** @brief Compares the complete texture payload. */
        bool operator==(const XnbTexture2DContent& other) const = default;
    };

    /** @brief A `Texture3D` asset ready for serialization. */
    struct XnbTexture3DContent
    {
        /** @brief Canonical texture fields and level bytes; `kind` must be `Texture3D`. */
        XnbTextureData texture;

        /** @brief Compares the complete texture payload. */
        bool operator==(const XnbTexture3DContent& other) const = default;
    };

    /** @brief A `TextureCube` asset ready for serialization. */
    struct XnbTextureCubeContent
    {
        /** @brief Canonical texture fields and level bytes; `kind` must be `TextureCube`. */
        XnbTextureData texture;

        /** @brief Compares the complete texture payload. */
        bool operator==(const XnbTextureCubeContent& other) const = default;
    };

    /**
     * @brief An already-compiled `Effect` ready for serialization (plans/plan_xnapipeline.md
     *        `XNAP-29`).
     *
     * The writer serializes bytecode it is given; it never compiles shader source and never
     * embeds source text pretending to be bytecode. Producing bytecode a genuine XNA 4.0 runtime
     * accepts is a separate, unsolved problem tracked as `XNAP-84`.
     */
    struct XnbCompiledEffectContent
    {
        /** @brief Complete compiled effect bytecode exactly as the target runtime expects it. */
        std::vector<std::uint8_t> bytecode;

        /** @brief Compares the complete bytecode. */
        bool operator==(const XnbCompiledEffectContent& other) const = default;
    };

    /**
     * @brief A reference to another compiled asset, written through `ExternalReferenceReader`
     *        (plans/plan_xnapipeline.md `XNAP-2B`).
     *
     * `XnbWriter::WriteExternalReference()` covers the far more common case: a reference sitting
     * inline in a field whose static type is already known, consuming no dispatch index. This
     * type is the other case -- a reference stored where the static type is `object`, which is
     * how the content pipeline writes a texture-valued effect parameter, and which therefore
     * needs its own reader in the type table.
     */
    struct XnbExternalAssetReference
    {
        /**
         * @brief Reference to another asset in the same content tree, without an extension.
         *
         * Written verbatim after the same validation `WriteExternalReference()` applies: an
         * absolute path, or one that escapes the content root, is refused rather than written.
         */
        std::string reference;

        /** @brief Compares the reference string. */
        bool operator==(const XnbExternalAssetReference& other) const = default;
    };

    /**
     * @brief One value an `EffectMaterial`'s parameter table can hold.
     *
     * These are the types CNA's own `EffectMaterialReader` knows how to apply to an
     * `EffectParameter`, minus the array forms. Array-valued parameters are deliberately absent:
     * which reader instantiation XNA writes for them (`ArrayReader` or `ListReader`, and over
     * which element type) is not established from any fixture available here, and guessing would
     * produce a file that loads into the wrong shape rather than one that fails to load.
     */
    using XnbEffectParameterValue =
        std::variant<bool, std::int32_t, float, Microsoft::Xna::Framework::Vector2,
                     Microsoft::Xna::Framework::Vector3, Microsoft::Xna::Framework::Vector4,
                     Microsoft::Xna::Framework::Matrix, Microsoft::Xna::Framework::Quaternion,
                     XnbExternalAssetReference>;

    /**
     * @brief An `EffectMaterial`'s parameter table: `Dictionary<String, Object>`.
     *
     * A distinct type rather than a `std::map` alias, because the registry is keyed by C++ type
     * and this dictionary's values are polymorphic -- each one carries its own dispatch index --
     * which the homogeneous `XnbDictionaryTypeWriter` cannot express.
     */
    struct XnbEffectParameterTable
    {
        /** @brief Parameter values by effect parameter name, written in sorted key order. */
        std::map<std::string, XnbEffectParameterValue> values;

        /** @brief Compares every parameter name and value. */
        bool operator==(const XnbEffectParameterTable& other) const = default;
    };

    /**
     * @brief A material that clones a compiled custom effect and overrides its parameters
     *        (plans/plan_xnapipeline.md `XNAP-29`).
     *
     * This is the shape XNA's `ModelProcessor` produces when a model's material names an `.fx`
     * file instead of resolving to a stock effect.
     */
    struct XnbEffectMaterialData
    {
        /** @brief Reference to the compiled effect asset this material clones. */
        std::string effectReference;

        /** @brief Parameter values the build resolved, applied to the clone after loading. */
        XnbEffectParameterTable parameters;

        /** @brief Compares the effect reference and every parameter. */
        bool operator==(const XnbEffectMaterialData& other) const = default;
    };

    /**
     * @brief Returns whether a surface format can be written into a given container version.
     *
     * Container version 5 stores the `SurfaceFormat` ordinal directly, so every XNA 4.0
     * `SurfaceFormat` is expressible and every CNA-only extension format is not. Version 4 has an
     * earlier, sparser numbering that reaches only four formats. This is the single authority for
     * the rule; both the texture writers and the pipeline's format selection consult it.
     *
     * @param format The surface format to test.
     * @param version The container version being written.
     * @return True when the format has an encoding in that container version.
     */
    [[nodiscard]] bool IsXnbWritableSurfaceFormat(
        Microsoft::Xna::Framework::Graphics::SurfaceFormat format, XnbContainerVersion version)
        noexcept;

    /**
     * @brief Returns the reader identity CNA writes for `Texture2D`.
     * @return The identity, evidenced by a committed fixture's own type-reader table.
     */
    [[nodiscard]] XnbReaderIdentity XnbTexture2DReaderIdentity();

    /**
     * @brief Returns the reader identity CNA writes for `Texture3D`.
     * @return The identity, derived from the rule the committed fixtures establish.
     */
    [[nodiscard]] XnbReaderIdentity XnbTexture3DReaderIdentity();

    /**
     * @brief Returns the reader identity CNA writes for `TextureCube`.
     * @return The identity, evidenced by a committed fixture's own type-reader table.
     */
    [[nodiscard]] XnbReaderIdentity XnbTextureCubeReaderIdentity();

    /**
     * @brief Registers every built-in asset writer: textures, `SpriteFont`, `SoundEffect`, `Song`,
     *        `Video`, vertex/index resources, the stock effects, compiled effects and `Model`.
     *
     * @param registry Mutable registry to configure.
     */
    void RegisterBuiltInAssetXnbWriters(XnbTypeWriterRegistry& registry);
}
