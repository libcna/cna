// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelV2Codec.hpp"
#include "CNA/Content/Cnb/CnbModelFromCnj.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Internal/CnjCanonicalRead.hpp"
#include "CNA/Internal/CnjEnvelope.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/Json.hpp"
#include "CNA/Internal/PathContainment.hpp"
#include "GltfToCnjEntry.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        constexpr const char* kGltfImporterName = "CNA.GltfImporter";
        constexpr const char* kModelProcessorName = "CNA.ModelProcessor";
        constexpr const char* kModelWriterName = "CNA.ModelContentWriter";

        using Microsoft::Xna::Framework::Content::ContentLoadException;

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

        [[nodiscard]] std::string ReadText(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                throw ContentLoadException(
                    "cannot open generated glTF document '" +
                    CNA::Internal::ContentPathToUtf8(path) + "'.");
            }
            std::ostringstream text;
            text << stream.rdbuf();
            return text.str();
        }

        [[nodiscard]] bool GenerateChildAssets(
            const ContentProcessorParameters& parameters)
        {
            const ContentProcessorParameterValue* value =
                parameters.Find(ModelGenerateChildAssetsParameter);
            if (value == nullptr) { return false; }
            const bool* enabled = std::get_if<bool>(value);
            if (enabled == nullptr)
            {
                throw std::invalid_argument(
                    "ModelProcessor parameter 'generateChildAssets' must be a bool value.");
            }
            return *enabled;
        }

        [[nodiscard]] std::string GeneratedLogicalName(
            const ImportedModelDocument& imported, const std::filesystem::path& generated,
            const std::string& logicalName, bool retainExtension)
        {
            const std::string generatedName = CNA::Internal::ContentPathToUtf8(
                retainExtension ? generated.filename() : generated.stem());
            if (imported.generatedBaseName.empty() ||
                generatedName.size() <= imported.generatedBaseName.size() ||
                generatedName.compare(0u, imported.generatedBaseName.size(),
                                      imported.generatedBaseName) != 0 ||
                generatedName[imported.generatedBaseName.size()] != '_')
            {
                throw ContentLoadException(
                    "generated glTF child '" + generatedName +
                    "' does not have the canonical source-stem prefix '" +
                    imported.generatedBaseName + "_'.");
            }
            const std::string child =
                logicalName + generatedName.substr(imported.generatedBaseName.size());
            const std::string problem = Cnb::CnbLogicalNameProblem(child);
            if (!problem.empty())
            {
                throw ContentLoadException(
                    "generated glTF child logical name '" + child +
                    "' is invalid: " + problem + ".");
            }
            return child;
        }

        template<typename Callback>
        void VisitTextureReferences(Cnb::CnbModelData& model, Callback&& callback)
        {
            for (Cnb::CnbModelPart& part : model.parts)
            {
                std::string* references[] = {
                    &part.material.baseColorTexture, &part.material.texture2,
                    &part.material.normalMap, &part.material.metallicRoughnessMap,
                    &part.material.emissiveMap, &part.material.occlusionMap,
                    &part.material.specularMap, &part.material.specularColorMap};
                for (std::string* reference : references)
                {
                    if (!reference->empty()) { callback(*reference); }
                }
            }
        }

        void AddModelRuntimeReferences(const Cnb::CnbModelData& model,
                                       ContentProcessorContext& context,
                                       const std::set<std::string>* generatedTextures = nullptr)
        {
            for (const Cnb::CnbModelPart& part : model.parts)
            {
                const std::string* references[] = {
                    &part.material.baseColorTexture, &part.material.texture2,
                    &part.material.normalMap, &part.material.metallicRoughnessMap,
                    &part.material.emissiveMap, &part.material.occlusionMap,
                    &part.material.specularMap, &part.material.specularColorMap};
                for (const std::string* reference : references)
                {
                    if (!reference->empty())
                    {
                        const std::uint32_t expected =
                            generatedTextures != nullptr &&
                                    generatedTextures->contains(*reference)
                                ? Cnb::CnbAssetTypeId::Texture2D
                                : 0u;
                        context.AddRuntimeReference(*reference, expected);
                    }
                }
                if (!part.externalEffect.empty())
                {
                    context.AddRuntimeReference(part.externalEffect);
                }
            }
        }

        void AddModelRuntimeReferences(const Cnb::CnbModelV2Data& model,
                                       ContentProcessorContext& context)
        {
            for (const Cnb::CnbModelV2Effect& effect : model.effects)
            {
                if (!effect.primaryTexture.empty())
                {
                    context.AddRuntimeReference(
                        effect.primaryTexture, Cnb::CnbAssetTypeId::Texture2D);
                }
                if (!effect.secondaryTexture.empty())
                {
                    context.AddRuntimeReference(
                        effect.secondaryTexture, Cnb::CnbAssetTypeId::Texture2D);
                }
                if (!effect.cubeTexture.empty())
                {
                    context.AddRuntimeReference(
                        effect.cubeTexture, Cnb::CnbAssetTypeId::TextureCube);
                }
            }
        }

        [[nodiscard]] Microsoft::Xna::Framework::Graphics::AnimationClipEXT
        ReadGeneratedAnimation(const ImportedModelDocument& imported,
                               const std::filesystem::path& document)
        {
            const std::string documentText = ReadText(document);
            const CNA::Internal::JsonValue root = CNA::Internal::ParseJson(documentText);
            return CNA::Internal::ReadCnjAnimationClip(
                root, CNA::Internal::ContentPathToUtf8(document),
                [&](const std::string& authored)
                {
                    const CNA::Internal::ContainedNativePathResult resolved =
                        CNA::Internal::ResolveContainedUtf8Path(
                            imported.intermediateRoot, authored);
                    if (!resolved.ok ||
                        !std::filesystem::is_regular_file(resolved.resolvedPath))
                    {
                        throw ContentLoadException(
                            "generated AnimationClip sidecar '" + authored +
                            "' escapes or is missing from the glTF intermediate root.");
                    }
                    return resolved.resolvedPath;
                });
        }
    }

    ContentComponentIdentity GltfImporter::Identity() const
    {
        return {kGltfImporterName, "2"};
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

        std::vector<std::filesystem::path> modelDocuments;
        std::vector<std::filesystem::path> animationDocuments;
        for (const std::filesystem::path& document : converted.documents)
        {
            if (!std::filesystem::is_regular_file(document))
            {
                throw std::runtime_error(
                    "glTF conversion reported a generated document that is not a regular file: " +
                    CNA::Internal::ContentPathToUtf8(document) + ".");
            }
            const CNA::Internal::CnjEnvelope envelope =
                CNA::Internal::ParseCnjEnvelope(ReadText(document));
            if (envelope.type == "Model") { modelDocuments.push_back(document); }
            else if (envelope.type == "AnimationClip")
            {
                animationDocuments.push_back(document);
            }
            else
            {
                throw std::runtime_error(
                    "glTF conversion produced unexpected CNJ type '" + envelope.type + "'.");
            }
        }
        if (modelDocuments.empty())
        {
            throw std::runtime_error("glTF conversion produced no Model document.");
        }
        std::sort(modelDocuments.begin(), modelDocuments.end());
        std::sort(animationDocuments.begin(), animationDocuments.end());

        context.LogInfo("imported glTF through CNA's shared canonical model front end.");
        ImportedModelDocument imported;
        imported.document = modelDocuments.front();
        imported.additionalModelDocuments.assign(modelDocuments.begin() + 1u,
                                                  modelDocuments.end());
        imported.animationDocuments = std::move(animationDocuments);
        imported.generatedTextureFiles = converted.generatedTextures;
        imported.generatedBaseName = baseName;
        imported.intermediateRoot = storage->Path();
        imported.intermediateLifetime = storage;
        imported.recordAuthoredSidecars = false;
        return ContentValue::Create(ImportedModelDocumentType, std::move(imported));
    }

    ContentComponentIdentity ModelProcessor::Identity() const
    {
        return {kModelProcessorName, "3"};
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
        for (const auto& [name, value] : parameters.Values())
        {
            static_cast<void>(value);
            if (name != ModelGenerateChildAssetsParameter)
            {
                throw ContentParameterError(
                    ContentParameterFault::UnknownName, name,
                    "ModelProcessor does not recognize parameter '" + name + "'.");
            }
        }
        static_cast<void>(GenerateChildAssets(parameters));
    }

    ContentValue ModelProcessor::Process(const ContentValue& input,
                                         ContentProcessorContext& context) const
    {
        const ImportedModelDocument& imported = input.Get<ImportedModelDocument>();
        const bool generateChildren = GenerateChildAssets(context.Parameters());
        ProcessedModelBundle bundle;
        if (imported.canonicalModel.has_value())
        {
            if (generateChildren)
            {
                throw std::invalid_argument(
                    "ModelProcessor parameter 'generateChildAssets' applies only to glTF "
                    "sources, not canonical XNB Model data.");
            }
            bundle.primary = *imported.canonicalModel;
            if (const auto* schema1 = std::get_if<Cnb::CnbModelData>(&bundle.primary))
            {
                AddModelRuntimeReferences(*schema1, context);
                context.LogInfo("prepared canonical XNB Model schema-1 data for CNB encoding.");
            }
            else
            {
                AddModelRuntimeReferences(
                    std::get<Cnb::CnbModelV2Data>(bundle.primary), context);
                context.LogInfo("prepared canonical XNB Model schema-2 data for CNB encoding.");
            }
            return ContentValue::Create(ProcessedModelType, std::move(bundle));
        }

        if (generateChildren && imported.generatedBaseName.empty())
        {
            throw std::invalid_argument(
                "ModelProcessor parameter 'generateChildAssets' applies only to glTF sources, "
                "not authored Model CNJ documents.");
        }
        if (!generateChildren && !imported.additionalModelDocuments.empty())
        {
            throw ContentLoadException(
                "glTF produced " +
                std::to_string(imported.additionalModelDocuments.size() + 1u) +
                " Model documents; set ModelProcessor bool parameter 'generateChildAssets' "
                "to true to publish the deterministic multi-Model output set.");
        }

        Cnb::CnbModelFromCnjResult primary =
            Cnb::BuildCnbModelFromCnj(imported.document, imported.intermediateRoot);
        bundle.primary = std::move(primary.model);
        if (imported.recordAuthoredSidecars)
        {
            for (const std::string& authoredPath : primary.absorbedFiles)
            {
                static_cast<void>(context.ResolveSourceDependency(authoredPath));
            }
        }

        std::vector<std::pair<std::string, Cnb::CnbModelData>> additionalModels;
        std::set<std::string> generatedTextureLogicalNames;
        if (generateChildren)
        {
            for (const std::filesystem::path& document : imported.additionalModelDocuments)
            {
                Cnb::CnbModelFromCnjResult processed =
                    Cnb::BuildCnbModelFromCnj(document, imported.intermediateRoot);
                additionalModels.emplace_back(
                    GeneratedLogicalName(imported, document, context.LogicalName(), false),
                    std::move(processed.model));
            }

            std::map<std::string, std::string> generatedTextureNames;
            for (const std::filesystem::path& texture : imported.generatedTextureFiles)
            {
                const CNA::Internal::ContainedNativePathResult contained =
                    CNA::Internal::ValidateContainedNativePath(imported.intermediateRoot,
                                                               texture);
                std::error_code error;
                const std::filesystem::file_status status =
                    std::filesystem::symlink_status(texture, error);
                if (!contained.ok || error || std::filesystem::is_symlink(status) ||
                    !std::filesystem::is_regular_file(status))
                {
                    throw ContentLoadException(
                        "generated glTF texture is missing, symlinked, or outside its "
                        "intermediate root: " +
                        CNA::Internal::ContentPathToUtf8(texture) + ".");
                }
                const std::string oldName =
                    CNA::Internal::ContentPathToUtf8(texture.filename());
                generatedTextureNames.emplace(
                    oldName, GeneratedLogicalName(imported, texture,
                                                  context.LogicalName(), true));
            }

            std::set<std::string> referencedGeneratedTextures;
            const auto remap = [&](std::string& reference)
            {
                const auto found = generatedTextureNames.find(reference);
                if (found == generatedTextureNames.end()) { return; }
                referencedGeneratedTextures.insert(found->first);
                reference = found->second;
            };
            VisitTextureReferences(std::get<Cnb::CnbModelData>(bundle.primary), remap);
            for (auto& [logicalName, model] : additionalModels)
            {
                static_cast<void>(logicalName);
                VisitTextureReferences(model, remap);
            }

            for (const std::filesystem::path& texture : imported.generatedTextureFiles)
            {
                const std::string oldName =
                    CNA::Internal::ContentPathToUtf8(texture.filename());
                if (!referencedGeneratedTextures.contains(oldName)) { continue; }
                generatedTextureLogicalNames.insert(generatedTextureNames.at(oldName));
                bundle.children.push_back(
                    {generatedTextureNames.at(oldName),
                     BuildCnbTexture2DData(DecodeImportedImage(texture))});
            }

            for (auto& [logicalName, model] : additionalModels)
            {
                AddModelRuntimeReferences(model, context,
                                          &generatedTextureLogicalNames);
                bundle.children.push_back({std::move(logicalName), std::move(model)});
            }
            for (const std::filesystem::path& animation : imported.animationDocuments)
            {
                bundle.children.push_back(
                    {GeneratedLogicalName(imported, animation, context.LogicalName(), false),
                     ReadGeneratedAnimation(imported, animation)});
            }
        }

        AddModelRuntimeReferences(std::get<Cnb::CnbModelData>(bundle.primary), context,
                                  &generatedTextureLogicalNames);
        std::sort(bundle.children.begin(), bundle.children.end(),
                  [](const ProcessedModelChild& left, const ProcessedModelChild& right)
        {
            return left.logicalName < right.logicalName;
        });
        for (std::size_t index = 1u; index < bundle.children.size(); ++index)
        {
            if (bundle.children[index - 1u].logicalName == bundle.children[index].logicalName)
            {
                throw ContentLoadException(
                    "generated glTF children collide on logical name '" +
                    bundle.children[index].logicalName + "'.");
            }
        }
        if (bundle.children.size() >= MaxContentBuildOutputs)
        {
            throw ContentLoadException(
                "glTF generated more than the maximum of " +
                std::to_string(MaxContentBuildOutputs - 1u) + " child outputs.");
        }
        context.LogInfo("prepared canonical Model data for CNB encoding.");
        return ContentValue::Create(ProcessedModelType, std::move(bundle));
    }

    ContentComponentIdentity ModelContentWriter::Identity() const
    {
        return {kModelWriterName, "3"};
    }

    std::vector<ContentWriterSchemaIdentity>
    ModelContentWriter::OutputSchemaIdentities() const
    {
        return {{Cnb::CnbAssetTypeId::Texture2D, Cnb::CnbTextureSchemaVersion,
                 "Microsoft.Xna.Framework.Graphics.Texture2D",
                 {"CNA.Cnb.EncodeTexture2DToCnb", "1"}},
                {Cnb::CnbAssetTypeId::Model, Cnb::CnbModelSchemaVersion,
                 "Microsoft.Xna.Framework.Graphics.Model",
                 {"CNA.Cnb.EncodeModelToCnb", "1"}},
                {Cnb::CnbAssetTypeId::Model, Cnb::CnbModelV2SchemaVersion,
                 "Microsoft.Xna.Framework.Graphics.Model",
                 {"CNA.Cnb.EncodeModelV2ToCnb", "1"}},
                {Cnb::CnbAssetTypeId::AnimationClip,
                 Cnb::CnbAnimationClipSchemaVersion,
                 "Microsoft.Xna.Framework.Graphics.AnimationClipEXT",
                 {"CNA.Cnb.EncodeAnimationClipToCnb", "1"}}};
    }

    std::string ModelContentWriter::InputType() const
    {
        return ProcessedModelType;
    }

    ContentWriteResult ModelContentWriter::Write(const ContentValue& input,
                                                 const std::string& logicalName) const
    {
        const ProcessedModelBundle& bundle = input.Get<ProcessedModelBundle>();
        ContentWriteResult result;
        result.assetTypeId = Cnb::CnbAssetTypeId::Model;
        result.assetTypeName = "Microsoft.Xna.Framework.Graphics.Model";
        if (const auto* schema1 = std::get_if<Cnb::CnbModelData>(&bundle.primary))
        {
            result.bytes = Cnb::EncodeModelToCnb(*schema1, logicalName);
            result.assetSchemaVersion = Cnb::CnbModelSchemaVersion;
        }
        else
        {
            result.bytes = Cnb::EncodeModelV2ToCnb(
                std::get<Cnb::CnbModelV2Data>(bundle.primary), logicalName);
            result.assetSchemaVersion = Cnb::CnbModelV2SchemaVersion;
        }
        result.additionalOutputs.reserve(bundle.children.size());
        for (const ProcessedModelChild& child : bundle.children)
        {
            if (const auto* texture = std::get_if<Cnb::CnbTextureData>(&child.value))
            {
                result.additionalOutputs.push_back(
                    {child.logicalName,
                     Cnb::EncodeTexture2DToCnb(*texture, child.logicalName),
                     Cnb::CnbAssetTypeId::Texture2D,
                     "Microsoft.Xna.Framework.Graphics.Texture2D",
                     Cnb::CnbTextureSchemaVersion});
            }
            else if (const auto* model = std::get_if<Cnb::CnbModelData>(&child.value))
            {
                result.additionalOutputs.push_back(
                    {child.logicalName, Cnb::EncodeModelToCnb(*model, child.logicalName),
                     Cnb::CnbAssetTypeId::Model,
                     "Microsoft.Xna.Framework.Graphics.Model",
                     Cnb::CnbModelSchemaVersion});
            }
            else
            {
                const auto& animation =
                    std::get<Microsoft::Xna::Framework::Graphics::AnimationClipEXT>(
                        child.value);
                result.additionalOutputs.push_back(
                    {child.logicalName,
                     Cnb::EncodeAnimationClipToCnb(animation, child.logicalName),
                     Cnb::CnbAssetTypeId::AnimationClip,
                     "Microsoft.Xna.Framework.Graphics.AnimationClipEXT",
                     Cnb::CnbAnimationClipSchemaVersion});
            }
        }
        return result;
    }

    void RegisterModelContentPipeline(ContentPipelineRegistry& registry)
    {
        registry.RegisterImporter(std::make_shared<GltfImporter>());
        registry.RegisterProcessor(std::make_shared<ModelProcessor>());
        registry.RegisterWriter(std::make_shared<ModelContentWriter>());
    }
}
