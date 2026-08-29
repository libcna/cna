// SPDX-License-Identifier: MS-PL
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "CNA/Content/Cnb/CnbModelData.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"

namespace CNA::Content::Pipeline
{
    /** @brief Stable in-memory type identity for an imported glTF model document. */
    inline constexpr const char* ImportedModelDocumentType =
        "CNA.Content.Pipeline.ImportedModelDocument";

    /** @brief Stable in-memory type identity for a processed Model and optional child bundle. */
    inline constexpr const char* ProcessedModelType =
        "CNA.Content.Pipeline.ProcessedModelBundle";

    /** @brief Boolean ModelProcessor option enabling generated glTF child CNB outputs. */
    inline constexpr const char* ModelGenerateChildAssetsParameter = "generateChildAssets";

    /**
     * @brief Source-oriented Model import represented by canonical CPU data or CNJ staging.
     *
     * This is an explicit migration seam over CNA's equivalence-hardened glTF interpretation,
     * not a second glTF representation. The opaque owner keeps the temporary document and all
     * absorbed sidecars alive until processing completes; components must not retain either path
     * after the build invocation.
     */
    struct ImportedModelDocument
    {
        /** @brief Primary canonical Model CNJ document. */
        std::filesystem::path document;

        /** @brief Additional generated canonical Model CNJ documents in deterministic order. */
        std::vector<std::filesystem::path> additionalModelDocuments;

        /** @brief Generated standalone AnimationClip CNJ documents in deterministic order. */
        std::vector<std::filesystem::path> animationDocuments;

        /** @brief Extracted or derived texture images in deterministic order. */
        std::vector<std::filesystem::path> generatedTextureFiles;

        /** @brief Source stem prefix used to derive safe generated logical child names. */
        std::string generatedBaseName;

        /** @brief Contained root for the generated CNJ sidecars. */
        std::filesystem::path intermediateRoot;

        /** @brief Opaque shared lifetime for a temporary intermediate tree, if one is used. */
        std::shared_ptr<const void> intermediateLifetime;

        /** @brief Whether processor-read authored sidecars must be recorded as source inputs. */
        bool recordAuthoredSidecars = false;

        /** @brief Canonical CPU Model supplied directly by XNB import, bypassing CNJ staging. */
        std::optional<CNA::Content::Cnb::CnbModelData> canonicalModel;
    };

    /** @brief Canonical value carried by one generated glTF child output. */
    using ProcessedModelChildValue =
        std::variant<CNA::Content::Cnb::CnbTextureData,
                     CNA::Content::Cnb::CnbModelData,
                     Microsoft::Xna::Framework::Graphics::AnimationClipEXT>;

    /** @brief One explicitly named generated glTF child in runtime-oriented form. */
    struct ProcessedModelChild
    {
        /** @brief Complete logical ContentManager name for the child CNB. */
        std::string logicalName;

        /** @brief Canonical Texture2D, Model, or AnimationClip value for an existing encoder. */
        ProcessedModelChildValue value;
    };

    /** @brief Primary Model plus optional deterministic generated child assets. */
    struct ProcessedModelBundle
    {
        /** @brief Primary canonical Model value. */
        CNA::Content::Cnb::CnbModelData primary;

        /** @brief Generated child values sorted by logical name. */
        std::vector<ProcessedModelChild> children;
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

    /** @brief Converts canonical Model documents into a primary Model and optional child bundle. */
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
         * @brief Validates the optional boolean generated-child policy.
         *
         * @param parameters Parameters to validate.
         */
        void ValidateParameters(const ContentProcessorParameters& parameters) const override;

        /**
         * @brief Builds canonical Model data and reports its runtime references.
         *
         * @param input ImportedModelDocument value.
         * @param context Call-scoped processor context.
         * @return Canonical ProcessedModelBundle boxed as ProcessedModelType.
         */
        [[nodiscard]] ContentValue Process(const ContentValue& input,
                                           ContentProcessorContext& context) const override;
    };

    /** @brief Writer adapter over authoritative Model and optional child CNB codecs. */
    class ModelContentWriter final : public ContentTypeWriter
    {
    public:
        /** @brief Returns the stable built-in writer identity. */
        [[nodiscard]] ContentComponentIdentity Identity() const override;

        /**
         * @brief Returns every frozen schema/encoder identity this bundle writer can emit.
         * @return Sorted Texture2D, Model, and AnimationClip declarations.
         */
        [[nodiscard]] std::vector<ContentWriterSchemaIdentity>
        OutputSchemaIdentities() const override;

        /** @brief Returns ProcessedModelType. */
        [[nodiscard]] std::string InputType() const override;

        /**
         * @brief Calls the existing typed encoders for the primary Model and each child.
         *
         * @param input Canonical CnbModelData value.
         * @param logicalName Logical asset name written to CNB metadata.
         * @return Complete primary Model bytes and any explicitly named generated children.
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
