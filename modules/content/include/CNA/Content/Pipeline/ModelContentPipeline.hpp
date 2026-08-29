// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbModelData.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"

namespace CNA::Content::Pipeline
{
    /** @brief Stable in-memory type identity for an imported glTF model document. */
    inline constexpr const char* ImportedModelDocumentType =
        "CNA.Content.Pipeline.ImportedModelDocument";

    /** @brief Stable in-memory type identity for processed Model CNB data. */
    inline constexpr const char* ProcessedModelType = "CNA.Content.Cnb.ModelData";

    /**
     * @brief Source-oriented glTF import represented by the existing canonical CNJ staging form.
     *
     * This is an explicit migration seam over CNA's equivalence-hardened glTF interpretation,
     * not a second glTF representation. The opaque owner keeps the temporary document and all
     * absorbed sidecars alive until processing completes; components must not retain either path
     * after the build invocation.
     */
    struct ImportedModelDocument
    {
        /** @brief Generated canonical Model CNJ document. */
        std::filesystem::path document;

        /** @brief Contained root for the generated CNJ sidecars. */
        std::filesystem::path intermediateRoot;

        /** @brief Opaque shared lifetime for a temporary intermediate tree, if one is used. */
        std::shared_ptr<const void> intermediateLifetime;

        /** @brief Whether processor-read authored sidecars must be recorded as source inputs. */
        bool recordAuthoredSidecars = false;

        /** @brief Canonical CPU Model supplied directly by XNB import, bypassing CNJ staging. */
        std::optional<CNA::Content::Cnb::CnbModelData> canonicalModel;
    };

    /** @brief Headless glTF importer backed by CNA's single shared glTF interpretation. */
    class GltfImporter final : public ContentImporter
    {
    public:
        /** @brief Returns the stable built-in importer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns the `.gltf` and `.glb` source routes. */
        [[nodiscard]] std::vector<std::string> SourceExtensions() const override;

        /**
         * @brief Returns the only imported type this component can produce.
         * @return A vector containing ImportedModelDocumentType.
         */
        [[nodiscard]] std::vector<std::string> OutputTypes() const override;

        /**
         * @brief Imports glTF through the shared glTF-to-CNJ implementation.
         *
         * @param context Call-scoped importer context.
         * @return Canonical source-oriented Model document with owned staging lifetime.
         */
        [[nodiscard]] ContentValue Import(ContentImporterContext& context) const override;
    };

    /** @brief Converts an imported canonical model document into CnbModelData. */
    class ModelProcessor final : public ContentProcessor
    {
    public:
        /** @brief Returns the stable built-in processor identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /** @brief Returns ImportedModelDocumentType. */
        [[nodiscard]] std::string InputType() const override;

        /** @brief Returns ProcessedModelType. */
        [[nodiscard]] std::string OutputType() const override;

        /**
         * @brief Rejects every parameter because the initial glTF policy preserves legacy defaults.
         *
         * @param parameters Parameters to validate.
         */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;

        /**
         * @brief Builds canonical Model data and reports its runtime references.
         *
         * @param input ImportedModelDocument value.
         * @param context Call-scoped processor context.
         * @return Canonical CnbModelData boxed as ProcessedModelType.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;
    };

    /** @brief Pipeline writer adapter over the authoritative Model CNB codec. */
    class ModelContentWriter final : public ContentTypeWriter
    {
    public:
        /** @brief Returns the stable built-in writer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /**
         * @brief Returns the frozen Model schema and encoder identity.
         * @return One stable Model asset/schema/codec declaration.
         */
        [[nodiscard]] std::vector<ContentWriterSchemaIdentity>
        OutputSchemaIdentities() const override;

        /** @brief Returns ProcessedModelType. */
        [[nodiscard]] std::string InputType() const override;

        /**
         * @brief Calls the existing EncodeModelToCnb() implementation.
         *
         * @param input Canonical CnbModelData value.
         * @param logicalName Logical asset name written to CNB metadata.
         * @return Complete CNB bytes and the frozen Model asset identity.
         */
        [[nodiscard]] ContentWriteResult Write(const ContentValue& input,
                                               const std::string& logicalName) const override;
    };

    /**
     * @brief Registers the built-in glTF importer, Model processor and Model writer.
     *
     * @param registry Explicit registry to configure before builds begin.
     */
    void RegisterModelContentPipeline(ContentPipelineRegistry& registry);
}
