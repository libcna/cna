// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbModelV2Data.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Xnb/XnbFileOptions.hpp"
#include "CNA/Content/Xnb/XnbTypeWriter.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"

namespace CNA::Content::Pipeline
{
    /**
     * @brief Returns the stable lowercase spelling of an output format.
     *
     * @param format The output format.
     * @return `"cnb"` or `"xnb"`, as a process-lifetime string literal.
     */
    [[nodiscard]] const char* ContentOutputFormatName(ContentOutputFormat format) noexcept;

    /**
     * @brief Returns the file extension an output format publishes, including the dot.
     *
     * @param format The output format.
     * @return `".cnb"` or `".xnb"`, as a process-lifetime string literal.
     */
    [[nodiscard]] const char* ContentOutputFormatExtension(ContentOutputFormat format) noexcept;

    /**
     * @brief Parses a stable lowercase output-format spelling.
     *
     * @param name `"cnb"` or `"xnb"`.
     * @return The matching format.
     * @throws std::invalid_argument when @p name is neither.
     */
    [[nodiscard]] ContentOutputFormat ParseContentOutputFormat(const std::string& name);

    /** @brief One additional `.xnb` output produced beside a primary output. */
    struct XnbAdditionalWriteOutput
    {
        /** @brief Complete logical ContentManager name and stable output identity. */
        std::string logicalName;

        /** @brief Complete `.xnb` file image. */
        std::vector<std::uint8_t> bytes;

        /** @brief Root `ContentTypeReader` name the file dispatches to. */
        std::string rootReaderName;
    };

    /** @brief Primary `.xnb` output and any bounded, explicitly named additional outputs. */
    struct XnbWriteResult
    {
        /** @brief Complete primary `.xnb` file image. */
        std::vector<std::uint8_t> bytes;

        /** @brief Root `ContentTypeReader` name the primary file dispatches to. */
        std::string rootReaderName;

        /** @brief Container description the file declares. */
        Xnb::XnbFileOptions options;

        /** @brief Additional outputs whose logical names differ from the primary asset. */
        std::vector<XnbAdditionalWriteOutput> additionalOutputs;
    };

    /**
     * @brief Serializes one processed pipeline value as a complete `.xnb` file.
     *
     * The `.xnb` counterpart of `ContentTypeWriter`. The two are deliberately separate interfaces
     * rather than one with a format switch: `ContentTypeWriter`'s result carries CNB container
     * identities (asset type id, schema version, codec) that have no meaning in `.xnb`, and an
     * `.xnb` file's identity is its root reader name, which has no meaning in CNB.
     *
     * One registered instance may serve concurrent build nodes once the registry is frozen, so an
     * implementation must be reentrant or synchronize its own mutable state.
     */
    class XnbAssetWriter
    {
    public:
        /** @brief Enables correct destruction through the writer interface. */
        virtual ~XnbAssetWriter() = default;

        /** @brief Returns the writer's stable name and build version. */
        [[nodiscard]] virtual ContentComponentIdentity Identity() const = 0;

        /** @brief Returns the stable processed type this writer accepts. */
        [[nodiscard]] virtual std::string InputType() const = 0;

        /**
         * @brief Returns the root `ContentTypeReader` name files from this writer dispatch to.
         *
         * Recorded in the build result and the manifest, so a format or reader change invalidates
         * cached output.
         *
         * @return The canonical reader name.
         */
        [[nodiscard]] virtual std::string RootReaderName() const = 0;

        /**
         * @brief Serializes a processed value into a complete `.xnb` file.
         *
         * @param input Processed value whose stable type equals InputType().
         * @param registry Frozen type-writer registry to serialize the object graph through.
         * @param options Container description to produce.
         * @param logicalName Logical asset name, used for diagnostics and additional outputs.
         * @return Primary file bytes plus any explicitly named additional outputs.
         * @throws Xnb::XnbWriteException when the value cannot be represented in `.xnb`.
         */
        [[nodiscard]] virtual XnbWriteResult Write(const ContentValue& input,
                                                    const Xnb::XnbTypeWriterRegistry& registry,
                                                    const Xnb::XnbFileOptions& options,
                                                    const std::string& logicalName) const = 0;
    };

    /**
     * @brief Registers the built-in `.xnb` asset writers on a pipeline registry.
     *
     * Covers every processed type the pipeline currently produces that `.xnb` can represent:
     * `Texture2D`, `Texture3D`, `TextureCube`, `SpriteFont`, `SoundEffect`, `Curve`, `Song` and
     * `Video`. A processed type with no XNA 4.0 counterpart is deliberately absent, so selecting
     * `.xnb` output for it fails with a clear "no writer" diagnostic rather than silently
     * producing something an XNA reader could not load.
     *
     * @param registry Pipeline registry to configure before builds begin.
     */
    void RegisterBuiltInXnbAssetWriters(ContentPipelineRegistry& registry);

    /**
     * @brief Registers the `Model` `.xnb` asset writer.
     *
     * Separate from RegisterBuiltInXnbAssetWriters() only so the Model graph's conversion stays in
     * its own translation unit; RegisterBuiltInXnbAssetWriters() already calls it.
     *
     * @param registry Pipeline registry to configure before builds begin.
     */
    void RegisterXnbModelAssetWriter(ContentPipelineRegistry& registry);

    /**
     * @brief Builds the type-writer registry the built-in asset writers need, already frozen.
     *
     * @return A shared, frozen registry safe for concurrent use.
     */
    [[nodiscard]] std::shared_ptr<const Xnb::XnbTypeWriterRegistry> CreateXnbTypeWriterRegistry();

    /**
     * @brief Converts a lossless schema-2 canonical Model into the XNB Model graph
     *        (plans/plan_xnapipeline.md `XNAP-022`).
     *
     * CNB Model schema 2 exists precisely to hold XNA `Model` semantics exactly -- interned vertex
     * declarations, shared vertex/index buffers and stock effects -- so this is a structural
     * inverse of the `.xnb` → schema-2 import, not a reinterpretation. Schema 1 has no vertex
     * declaration at all and therefore cannot be converted; that route says so rather than
     * inventing one.
     *
     * The three separate schema-2 resource arrays become one flat XNB shared-resource list in a
     * fixed order -- vertex buffers, then index buffers, then effects -- so the mapping is a pure
     * function of the input.
     *
     * @param model Canonical schema-2 Model document.
     * @param logicalName Logical name of the Model asset, used to make texture references
     *        relative to it exactly as XNA's own external references are.
     * @return The equivalent canonical XNB Model graph.
     * @throws Xnb::XnbWriteException when the document is internally inconsistent.
     */
    [[nodiscard]] CNA::Internal::Xnb::XnbModelData ConvertCnbModelV2ToXnb(
        const Cnb::CnbModelV2Data& model, const std::string& logicalName);

    /**
     * @brief Returns @p target expressed relative to the asset at @p from.
     *
     * XNA external references are relative to the referring `.xnb`, while CNB logical names are
     * relative to the content root. A `Model` at `Models/robot` referencing `Textures/wall`
     * therefore writes `../Textures/wall`.
     *
     * @param from Logical name of the referring asset.
     * @param target Logical name of the referenced asset, or empty for no reference.
     * @return The relative reference to write, or empty when @p target is empty.
     */
    [[nodiscard]] std::string XnbRelativeAssetReference(const std::string& from,
                                                        const std::string& target);
}
