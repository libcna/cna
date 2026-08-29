// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"

#include <filesystem>
#include <stdexcept>
#include <system_error>

#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelFromCnj.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "GltfToCnjEntry.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        constexpr const char* kGltfImporterName = "CNA.GltfImporter";
        constexpr const char* kModelProcessorName = "CNA.ModelProcessor";
        constexpr const char* kModelWriterName = "CNA.ModelContentWriter";

        class ModelIntermediateStorage final
        {
        public:
            ModelIntermediateStorage()
            {
                std::error_code error;
                const std::filesystem::path temporary =
                    std::filesystem::temp_directory_path(error);
                if (error)
                {
                    throw std::runtime_error("no temporary directory is available: " +
                                             error.message() + ".");
                }

                for (std::size_t attempt = 0u; attempt < 1024u; ++attempt)
                {
                    error.clear();
                    const std::filesystem::path candidate =
                        temporary / ("cna_content_gltf_" + std::to_string(attempt));
                    if (std::filesystem::create_directory(candidate, error))
                    {
                        path_ = candidate;
                        return;
                    }
                    if (error && error != std::errc::file_exists)
                    {
                        throw std::runtime_error("cannot create glTF intermediate directory: " +
                                                 error.message() + ".");
                    }
                }
                throw std::runtime_error("cannot reserve a glTF intermediate directory.");
            }

            ~ModelIntermediateStorage()
            {
                std::error_code ignored;
                std::filesystem::remove_all(path_, ignored);
            }

            ModelIntermediateStorage(const ModelIntermediateStorage&) = delete;
            ModelIntermediateStorage& operator=(const ModelIntermediateStorage&) = delete;

            [[nodiscard]] const std::filesystem::path& Path() const noexcept { return path_; }

        private:
            std::filesystem::path path_;
        };
    }

    ContentComponentIdentity GltfImporter::Identity() const
    {
        return {kGltfImporterName, "1"};
    }

    std::vector<std::string> GltfImporter::SourceExtensions() const
    {
        return {".gltf", ".glb"};
    }

    std::vector<std::string> GltfImporter::OutputTypes() const
    {
        return {ImportedModelDocumentType};
    }

    ContentValue GltfImporter::Import(ContentImporterContext& context) const
    {
        const auto storage = std::make_shared<ModelIntermediateStorage>();
        const std::string baseName =
            CNA::Internal::ContentPathToUtf8(context.SourcePath().stem());
        if (baseName.empty())
        {
            throw std::runtime_error("glTF source has no usable file-name stem.");
        }

        const CNA::Tools::Gltf::GltfToCnjResult converted =
            CNA::Tools::Gltf::ConvertGltfToCnj(
                context.SourcePath(), storage->Path(), baseName, 1.0f, false);
        for (const std::filesystem::path& dependency : converted.sourceDependencies)
        {
            const std::filesystem::path authored =
                dependency.lexically_relative(context.SourcePath().parent_path());
            if (authored.empty())
            {
                throw std::runtime_error("glTF dependency cannot be expressed relative to its "
                                         "source directory: " +
                                         CNA::Internal::ContentPathToUtf8(dependency) + ".");
            }
            static_cast<void>(context.ResolveSourceDependency(authored));
        }
        for (const std::string& warning : converted.warnings)
        {
            context.LogWarning(warning);
        }

        if (converted.documents.size() != 1u ||
            !std::filesystem::is_regular_file(converted.documents.front()))
        {
            throw std::runtime_error(
                "glTF produced multiple Model documents. One-source-to-many-output graph "
                "scheduling is not enabled in the initial Model pipeline slice.");
        }

        context.LogInfo("imported glTF through CNA's shared canonical model front end.");
        ImportedModelDocument imported;
        imported.document = converted.documents.front();
        imported.intermediateRoot = storage->Path();
        imported.intermediateLifetime = storage;
        imported.recordAuthoredSidecars = false;
        return ContentValue::Create(ImportedModelDocumentType, std::move(imported));
    }

    ContentComponentIdentity ModelProcessor::Identity() const
    {
        return {kModelProcessorName, "1"};
    }

    std::string ModelProcessor::InputType() const
    {
        return ImportedModelDocumentType;
    }

    std::string ModelProcessor::OutputType() const
    {
        return ProcessedModelType;
    }

    void ModelProcessor::ValidateParameters(const ContentProcessorParameters& parameters) const
    {
        if (!parameters.Empty())
        {
            throw std::invalid_argument("ModelProcessor does not accept processor parameters.");
        }
    }

    ContentValue ModelProcessor::Process(const ContentValue& input,
                                         ContentProcessorContext& context) const
    {
        const ImportedModelDocument& imported = input.Get<ImportedModelDocument>();
        if (imported.canonicalModel.has_value())
        {
            const Cnb::CnbModelData& model = *imported.canonicalModel;
            for (const Cnb::CnbModelPart& part : model.parts)
            {
                const std::string* references[] = {
                    &part.material.baseColorTexture, &part.material.texture2,
                    &part.material.normalMap, &part.material.metallicRoughnessMap,
                    &part.material.emissiveMap, &part.material.occlusionMap,
                    &part.material.specularMap, &part.material.specularColorMap};
                for (const std::string* reference : references)
                {
                    if (!reference->empty()) { context.AddRuntimeReference(*reference); }
                }
                if (!part.externalEffect.empty())
                {
                    context.AddRuntimeReference(part.externalEffect);
                }
            }
            context.LogInfo("prepared canonical XNB Model data for CNB encoding.");
            return ContentValue::Create(ProcessedModelType, model);
        }
        Cnb::CnbModelFromCnjResult processed =
            Cnb::BuildCnbModelFromCnj(imported.document, imported.intermediateRoot);
        if (imported.recordAuthoredSidecars)
        {
            for (const std::string& authoredPath : processed.absorbedFiles)
            {
                static_cast<void>(context.ResolveSourceDependency(authoredPath));
            }
        }
        for (const std::string& logicalName : processed.externalReferences)
        {
            context.AddRuntimeReference(logicalName);
        }
        context.LogInfo("prepared canonical Model data for CNB encoding.");
        return ContentValue::Create(ProcessedModelType, std::move(processed.model));
    }

    ContentComponentIdentity ModelContentWriter::Identity() const
    {
        return {kModelWriterName, "1"};
    }

    std::vector<ContentWriterSchemaIdentity>
    ModelContentWriter::OutputSchemaIdentities() const
    {
        return {{Cnb::CnbAssetTypeId::Model, Cnb::CnbModelSchemaVersion,
                 "Microsoft.Xna.Framework.Graphics.Model",
                 {"CNA.Cnb.EncodeModelToCnb", "1"}}};
    }

    std::string ModelContentWriter::InputType() const
    {
        return ProcessedModelType;
    }

    ContentWriteResult ModelContentWriter::Write(const ContentValue& input,
                                                 const std::string& logicalName) const
    {
        const Cnb::CnbModelData& model = input.Get<Cnb::CnbModelData>();
        return {Cnb::EncodeModelToCnb(model, logicalName), Cnb::CnbAssetTypeId::Model,
                "Microsoft.Xna.Framework.Graphics.Model"};
    }

    void RegisterModelContentPipeline(ContentPipelineRegistry& registry)
    {
        registry.RegisterImporter(std::make_shared<GltfImporter>());
        registry.RegisterProcessor(std::make_shared<ModelProcessor>());
        registry.RegisterWriter(std::make_shared<ModelContentWriter>());
    }
}
