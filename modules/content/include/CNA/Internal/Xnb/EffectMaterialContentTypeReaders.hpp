// SPDX-License-Identifier: MS-PL
#pragma once

#include <any>
#include <map>
#include <memory>
#include <string>

#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

// The three .xnb readers an XNA Model needs when one of its materials references a compiled
// custom effect rather than a stock one -- the shape the official ModelProcessor produces
// from a .x/.fbx whose material names an .fx file.
//
// A Car.xnb built that way declares ten type readers; CNA already had seven. These are the
// other three, and they only ever appear together: ModelReader reaches EffectMaterialReader
// for the part's material, that reader reads the parameter table through
// DictionaryReader`2[[System.String],[System.Object]], and a texture-valued parameter inside
// that table is an ExternalReferenceReader entry.

namespace CNA::Internal::Xnb
{
    using Microsoft::Xna::Framework::Content::ContentReader;
    using Microsoft::Xna::Framework::Content::ContentTypeReader;
    using Microsoft::Xna::Framework::Graphics::Effect;

    /**
     * @brief Reads a material that is a clone of a compiled custom effect, plus its
     *        pipeline-assigned parameter values.
     *
     * Erases to `std::shared_ptr<Effect>` for the same reason every stock-effect reader does:
     * a `ModelMeshPart`'s effect is dispatched through `ReadSharedResource<std::shared_ptr<Effect>>()`,
     * whose `std::any_cast` needs one exact type for every effect a file might contain.
     */
    class EffectMaterialReader : public ContentTypeReader<std::shared_ptr<Effect>>
    {
    public:
        /** @brief Constructs the reader with its .NET target type name. */
        EffectMaterialReader()
            : ContentTypeReader<std::shared_ptr<Effect>>(
                  "Microsoft.Xna.Framework.Graphics.EffectMaterial")
        {
        }

    protected:
        /**
         * @brief Reads the referenced effect, clones it, and applies the stored parameters.
         *
         * @param input The reader positioned at the material.
         * @param existingInstance Never provided.
         * @return The cloned effect as an EffectMaterial.
         */
        std::shared_ptr<Effect> Read(
            ContentReader& input,
            std::optional<std::shared_ptr<Effect>> existingInstance) override;
    };

    /**
     * @brief Reads `Dictionary<string, object>`, the shape an EffectMaterial's parameter
     *        table is written in.
     *
     * The values are type-erased: each is dispatched through the reader table like any other
     * object, so a texture value arrives as an ExternalReferenceReader result and a numeric
     * one as its own primitive reader's result.
     */
    class StringObjectDictionaryReader
        : public ContentTypeReader<std::map<std::string, std::any>>
    {
    public:
        /** @brief Constructs the reader with its .NET target type name. */
        StringObjectDictionaryReader()
            : ContentTypeReader<std::map<std::string, std::any>>(
                  "System.Collections.Generic.Dictionary`2[[System.String],[System.Object]]")
        {
        }

    protected:
        /**
         * @brief Reads the count, then that many key/value pairs.
         *
         * @param input The reader positioned at the dictionary.
         * @param existingInstance Never provided.
         * @return The key/value pairs, values type-erased.
         */
        std::map<std::string, std::any> Read(
            ContentReader& input,
            std::optional<std::map<std::string, std::any>> existingInstance) override;
    };

    /**
     * @brief Reads an `ExternalReference<T>` written as a typed object.
     *
     * `ContentReader::ReadExternalReference<T>()` handles the far more common case where the
     * reference sits inline in a known field, consuming no reader index. This reader is the
     * other case: a reference stored where the static type is `object`, which is how the
     * content pipeline writes a texture-valued effect parameter.
     *
     * The referenced XNB's own root reader determines the concrete result type, matching FNA's
     * `ReadExternalReference<object>()`. This is essential for effect parameter dictionaries:
     * their external references may resolve to `Texture2D`, `Texture3D`, or `TextureCube`.
     */
    class ExternalReferenceReader : public ContentTypeReader<std::any>
    {
    public:
        /** @brief Constructs the reader with its .NET target type name. */
        ExternalReferenceReader()
            : ContentTypeReader<std::any>("System.Object")
        {
        }

    protected:
        /**
         * @brief Resolves the reference and loads the asset it names.
         *
         * @param input The reader positioned at the reference.
         * @param existingInstance Never provided.
         * @return The loaded asset, type-erased; empty when the reference string is empty.
         */
        std::any Read(ContentReader& input,
                      std::optional<std::any> existingInstance) override;
    };

    /** @brief Registers all three readers with the ContentTypeReaderManager. */
    void RegisterEffectMaterialXnbReaders();
}
