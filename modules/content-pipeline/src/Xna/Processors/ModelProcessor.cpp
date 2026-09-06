// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ModelProcessor.hpp"

#include <algorithm>
#include <limits>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorContext.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/MeshHelper.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/StockMaterials.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/VertexChannelNames.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    namespace
    {
        /** @brief The children of a node, read through the collection's index. */
        [[nodiscard]] std::vector<std::shared_ptr<Graphics::NodeContent>> ChildrenOf(
            const Graphics::NodeContent& node)
        {
            const auto& children =
                static_cast<const System::Collections::ObjectModel::Collection<
                    std::shared_ptr<Graphics::NodeContent>>&>(node.getChildrenProperty());
            std::vector<std::shared_ptr<Graphics::NodeContent>> result;
            for (SharpRuntime::intcs i = 0; i < children.getCountProperty(); ++i)
            {
                result.push_back(children[i]);
            }
            return result;
        }

        /** @brief The geometry batches of a mesh, read through the collection's index. */
        [[nodiscard]] std::vector<std::shared_ptr<Graphics::GeometryContent>> GeometryOf(
            const Graphics::MeshContent& mesh)
        {
            const auto& batches = static_cast<const System::Collections::ObjectModel::Collection<
                std::shared_ptr<Graphics::GeometryContent>>&>(mesh.getGeometryProperty());
            std::vector<std::shared_ptr<Graphics::GeometryContent>> result;
            for (SharpRuntime::intcs i = 0; i < batches.getCountProperty(); ++i)
            {
                result.push_back(batches[i]);
            }
            return result;
        }

        /** @brief The sphere that bounds a mesh's own positions. */
        [[nodiscard]] BoundingSphere BoundsOf(const Graphics::MeshContent& mesh)
        {
            const auto& positions = static_cast<const System::Collections::ObjectModel::Collection<Vector3>&>(
                mesh.getPositionsProperty());
            std::vector<Vector3> points;
            for (SharpRuntime::intcs i = 0; i < positions.getCountProperty(); ++i)
            {
                points.push_back(positions[i]);
            }
            if (points.empty())
            {
                return BoundingSphere(Vector3(0, 0, 0), 0.0f);
            }
            return BoundingSphere::CreateFromPoints(points);
        }
    }

    void ModelProcessor::DescribeParameters(ProcessorParameterBindings<ModelProcessor>& bindings)
    {
        bindings.Add<Color>("ColorKeyColor", &ModelProcessor::getColorKeyColorProperty,
                            &ModelProcessor::setColorKeyColorProperty);
        bindings.Add<bool>("ColorKeyEnabled", &ModelProcessor::getColorKeyEnabledProperty,
                           &ModelProcessor::setColorKeyEnabledProperty);
        bindings.AddEnum<MaterialProcessorDefaultEffect>("DefaultEffect",
                                                         &ModelProcessor::getDefaultEffectProperty,
                                                         &ModelProcessor::setDefaultEffectProperty,
                                                         DeclaredEnumSpellings<MaterialProcessorDefaultEffect>());
        bindings.Add<bool>("GenerateMipmaps", &ModelProcessor::getGenerateMipmapsProperty,
                           &ModelProcessor::setGenerateMipmapsProperty);
        bindings.Add<bool>("GenerateTangentFrames", &ModelProcessor::getGenerateTangentFramesProperty,
                           &ModelProcessor::setGenerateTangentFramesProperty);
        bindings.Add<bool>("PremultiplyTextureAlpha", &ModelProcessor::getPremultiplyTextureAlphaProperty,
                           &ModelProcessor::setPremultiplyTextureAlphaProperty);
        bindings.Add<bool>("PremultiplyVertexColors", &ModelProcessor::getPremultiplyVertexColorsProperty,
                           &ModelProcessor::setPremultiplyVertexColorsProperty);
        bindings.Add<bool>("ResizeTexturesToPowerOfTwo", &ModelProcessor::getResizeTexturesToPowerOfTwoProperty,
                           &ModelProcessor::setResizeTexturesToPowerOfTwoProperty);
        bindings.Add<SharpRuntime::Single>("RotationX", &ModelProcessor::getRotationXProperty,
                                           &ModelProcessor::setRotationXProperty);
        bindings.Add<SharpRuntime::Single>("RotationY", &ModelProcessor::getRotationYProperty,
                                           &ModelProcessor::setRotationYProperty);
        bindings.Add<SharpRuntime::Single>("RotationZ", &ModelProcessor::getRotationZProperty,
                                           &ModelProcessor::setRotationZProperty);
        bindings.Add<SharpRuntime::Single>("Scale", &ModelProcessor::getScaleProperty,
                                           &ModelProcessor::setScaleProperty);
        bindings.Add<bool>("SwapWindingOrder", &ModelProcessor::getSwapWindingOrderProperty,
                           &ModelProcessor::setSwapWindingOrderProperty);
        bindings.AddEnum<TextureProcessorOutputFormat>("TextureFormat",
                                                       &ModelProcessor::getTextureFormatProperty,
                                                       &ModelProcessor::setTextureFormatProperty,
                                                       DeclaredEnumSpellings<TextureProcessorOutputFormat>());
    }

    Color ModelProcessor::getColorKeyColorProperty() const noexcept { return colorKeyColor_; }
    void ModelProcessor::setColorKeyColorProperty(Color value) noexcept { colorKeyColor_ = value; }
    bool ModelProcessor::getColorKeyEnabledProperty() const noexcept { return colorKeyEnabled_; }
    void ModelProcessor::setColorKeyEnabledProperty(bool value) noexcept { colorKeyEnabled_ = value; }
    MaterialProcessorDefaultEffect ModelProcessor::getDefaultEffectProperty() const noexcept
    {
        return defaultEffect_;
    }
    void ModelProcessor::setDefaultEffectProperty(MaterialProcessorDefaultEffect value) noexcept
    {
        defaultEffect_ = value;
    }
    bool ModelProcessor::getGenerateMipmapsProperty() const noexcept { return generateMipmaps_; }
    void ModelProcessor::setGenerateMipmapsProperty(bool value) noexcept { generateMipmaps_ = value; }
    bool ModelProcessor::getGenerateTangentFramesProperty() const noexcept { return generateTangentFrames_; }
    void ModelProcessor::setGenerateTangentFramesProperty(bool value) noexcept { generateTangentFrames_ = value; }
    bool ModelProcessor::getPremultiplyTextureAlphaProperty() const noexcept { return premultiplyTextureAlpha_; }
    void ModelProcessor::setPremultiplyTextureAlphaProperty(bool value) noexcept
    {
        premultiplyTextureAlpha_ = value;
    }
    bool ModelProcessor::getPremultiplyVertexColorsProperty() const noexcept { return premultiplyVertexColors_; }
    void ModelProcessor::setPremultiplyVertexColorsProperty(bool value) noexcept
    {
        premultiplyVertexColors_ = value;
    }
    bool ModelProcessor::getResizeTexturesToPowerOfTwoProperty() const noexcept
    {
        return resizeTexturesToPowerOfTwo_;
    }
    void ModelProcessor::setResizeTexturesToPowerOfTwoProperty(bool value) noexcept
    {
        resizeTexturesToPowerOfTwo_ = value;
    }
    SharpRuntime::Single ModelProcessor::getRotationXProperty() const noexcept { return rotationX_; }
    void ModelProcessor::setRotationXProperty(SharpRuntime::Single value) noexcept { rotationX_ = value; }
    SharpRuntime::Single ModelProcessor::getRotationYProperty() const noexcept { return rotationY_; }
    void ModelProcessor::setRotationYProperty(SharpRuntime::Single value) noexcept { rotationY_ = value; }
    SharpRuntime::Single ModelProcessor::getRotationZProperty() const noexcept { return rotationZ_; }
    void ModelProcessor::setRotationZProperty(SharpRuntime::Single value) noexcept { rotationZ_ = value; }
    SharpRuntime::Single ModelProcessor::getScaleProperty() const noexcept { return scale_; }
    void ModelProcessor::setScaleProperty(SharpRuntime::Single value) noexcept { scale_ = value; }
    bool ModelProcessor::getSwapWindingOrderProperty() const noexcept { return swapWindingOrder_; }
    void ModelProcessor::setSwapWindingOrderProperty(bool value) noexcept { swapWindingOrder_ = value; }
    TextureProcessorOutputFormat ModelProcessor::getTextureFormatProperty() const noexcept { return textureFormat_; }
    void ModelProcessor::setTextureFormatProperty(TextureProcessorOutputFormat value) noexcept
    {
        textureFormat_ = value;
    }

    std::shared_ptr<ModelContent> ModelProcessor::Process(const std::shared_ptr<Graphics::NodeContent>& input,
                                                          ContentProcessorContext& context)
    {
        if (input == nullptr)
        {
            throw System::ArgumentNullException("input");
        }
        // The processor's own scale and rotation are baked into the scene rather than left on the
        // root bone: the geometry moves and every node's transform is re-expressed in the new
        // frame (measured, modelprocessor/scale_rotation_detail and rotation_order, which also
        // name the order the three rotations compose in).
        const Matrix adjustment = Matrix::CreateRotationZ(MathHelper::ToRadians(rotationZ_)) *
                                  Matrix::CreateRotationX(MathHelper::ToRadians(rotationX_)) *
                                  Matrix::CreateRotationY(MathHelper::ToRadians(rotationY_)) *
                                  Matrix::CreateScale(scale_);
        Graphics::MeshHelper::TransformScene(input, adjustment);
        ModelBoneContentCollection bones;
        ModelMeshContentCollection meshes;
        // Every node becomes a bone, in the order a depth-first walk reaches them (measured,
        // modelprocessor/bone_hierarchy).
        const std::function<std::shared_ptr<ModelBoneContent>(const std::shared_ptr<Graphics::NodeContent>&,
                                                              const std::shared_ptr<ModelBoneContent>&)>
            walk = [&](const std::shared_ptr<Graphics::NodeContent>& node,
                       const std::shared_ptr<ModelBoneContent>& parent) -> std::shared_ptr<ModelBoneContent>
        {
            const Matrix transform = node->getTransformProperty();
            auto bone = std::make_shared<ModelBoneContent>(node->getNameProperty(),
                                                           static_cast<SharpRuntime::intcs>(bones.size()),
                                                           transform, parent);
            bones.push_back(bone);
            if (parent != nullptr)
            {
                parent->AddChild(bone);
            }
            if (const auto mesh = std::dynamic_pointer_cast<Graphics::MeshContent>(node))
            {
                ModelMeshPartContentCollection parts;
                if (generateTangentFrames_)
                {
                    Graphics::MeshHelper::CalculateTangentFrames(
                        mesh, Graphics::VertexChannelNames::TextureCoordinate(0),
                        Graphics::VertexChannelNames::Tangent(0), Graphics::VertexChannelNames::Binormal(0));
                }
                const std::vector<std::shared_ptr<Graphics::GeometryContent>> batches = GeometryOf(*mesh);
                // A geometry with no material of its own is given one (measured,
                // modelprocessor/triangle answers a BasicMaterialContent), and the batches that
                // share a material are handed to the material step together, as the signature of
                // that step says they are.
                std::vector<std::shared_ptr<Graphics::MaterialContent>> materials;
                std::vector<std::vector<std::shared_ptr<Graphics::GeometryContent>>> grouped;
                for (const std::shared_ptr<Graphics::GeometryContent>& geometry : batches)
                {
                    if (geometry->getMaterialProperty() == nullptr)
                    {
                        geometry->setMaterialProperty(std::make_shared<Graphics::BasicMaterialContent>());
                    }
                    const auto found = std::find(materials.begin(), materials.end(),
                                                 geometry->getMaterialProperty());
                    if (found == materials.end())
                    {
                        materials.push_back(geometry->getMaterialProperty());
                        grouped.push_back({geometry});
                    }
                    else
                    {
                        grouped[static_cast<std::size_t>(found - materials.begin())].push_back(geometry);
                    }
                }
                for (std::size_t group = 0; group < materials.size(); ++group)
                {
                    ProcessGeometryUsingMaterial(materials[group], grouped[group], context);
                }
                if (swapWindingOrder_)
                {
                    Graphics::MeshHelper::SwapWindingOrder(mesh);
                }
                // Every batch is put through the cache optimization, which reverses its triangles
                // and renumbers its vertices in the order the reversed list reaches them
                // (measured, modelprocessor/quad_ordering and swap_winding_detail).
                Graphics::MeshHelper::OptimizeForCache(mesh);
                for (const std::shared_ptr<Graphics::GeometryContent>& geometry : batches)
                {
                    const auto& indices = static_cast<const System::Collections::ObjectModel::Collection<
                        SharpRuntime::intcs>&>(geometry->getIndicesProperty());
                    auto indexBuffer = std::make_shared<Graphics::IndexCollection>();
                    for (SharpRuntime::intcs i = 0; i < indices.getCountProperty(); ++i)
                    {
                        indexBuffer->Add(indices[i]);
                    }
                    auto part = std::make_shared<ModelMeshPartContent>(
                        geometry->getVerticesProperty().CreateVertexBuffer(), indexBuffer, 0,
                        geometry->getVerticesProperty().getVertexCountProperty(), 0,
                        indexBuffer->getCountProperty() / 3);
                    part->setMaterialProperty(geometry->getMaterialProperty());
                    parts.push_back(part);
                }
                meshes.push_back(std::make_shared<ModelMeshContent>(mesh->getNameProperty(), mesh, bone,
                                                                    BoundsOf(*mesh), std::move(parts)));
            }
            for (const std::shared_ptr<Graphics::NodeContent>& child : ChildrenOf(*node))
            {
                (void)walk(child, bone);
            }
            return bone;
        };
        const std::shared_ptr<ModelBoneContent> root = walk(input, nullptr);
        return std::make_shared<ModelContent>(root, std::move(bones), std::move(meshes));
    }

    std::shared_ptr<Graphics::MaterialContent> ModelProcessor::ConvertMaterial(
        const std::shared_ptr<Graphics::MaterialContent>& material, ContentProcessorContext& context)
    {
        // The material goes through the material processor, with this processor's own texture
        // properties (measured, modelprocessor/triangle records the conversion and its parameters).
        OpaqueDataDictionary parameters;
        parameters.SetValue<Color>("ColorKeyColor", colorKeyColor_);
        parameters.SetValue<bool>("ColorKeyEnabled", colorKeyEnabled_);
        parameters.SetValue<MaterialProcessorDefaultEffect>("DefaultEffect", defaultEffect_);
        parameters.SetValue<bool>("GenerateMipmaps", generateMipmaps_);
        parameters.SetValue<bool>("PremultiplyTextureAlpha", premultiplyTextureAlpha_);
        parameters.SetValue<bool>("ResizeTexturesToPowerOfTwo", resizeTexturesToPowerOfTwo_);
        parameters.SetValue<TextureProcessorOutputFormat>("TextureFormat", textureFormat_);
        return context.Convert<Graphics::MaterialContent, Graphics::MaterialContent>(material, "MaterialProcessor",
                                                                                     parameters);
    }

    void ModelProcessor::ProcessGeometryUsingMaterial(
        const std::shared_ptr<Graphics::MaterialContent>& material,
        const std::vector<std::shared_ptr<Graphics::GeometryContent>>& geometryCollection,
        ContentProcessorContext& context)
    {
        const std::shared_ptr<Graphics::MaterialContent> converted =
            material == nullptr ? nullptr : ConvertMaterial(material, context);
        for (const std::shared_ptr<Graphics::GeometryContent>& geometry : geometryCollection)
        {
            if (geometry == nullptr)
            {
                continue;
            }
            // A skinned model needs its weights; without them the runtime refuses by name
            // (measured, modelprocessor/default_effect_skinned).
            if (defaultEffect_ == MaterialProcessorDefaultEffect::SkinnedEffect &&
                !geometry->getVerticesProperty().getChannelsProperty().Contains(
                    std::string(Graphics::VertexChannelNames::Weights())))
            {
                const Graphics::MeshContent* mesh = geometry->getParentProperty();
                throw InvalidContentException("The skinned mesh \"" +
                                              (mesh == nullptr ? std::string() : mesh->getNameProperty()) +
                                              "\" contains geometry without any vertex weights.");
            }
            for (SharpRuntime::intcs i = 0;
                 i < geometry->getVerticesProperty().getChannelsProperty().getCountProperty(); ++i)
            {
                ProcessVertexChannel(geometry, i, context);
            }
            geometry->setMaterialProperty(converted);
        }
    }

    void ModelProcessor::ProcessVertexChannel(const std::shared_ptr<Graphics::GeometryContent>& geometry,
                                              SharpRuntime::intcs vertexChannelIndex,
                                              ContentProcessorContext& context)
    {
        (void)context;
        if (geometry == nullptr || !premultiplyVertexColors_)
        {
            return;
        }
        auto& channels = geometry->getVerticesProperty().getChannelsProperty();
        if (vertexChannelIndex < 0 || vertexChannelIndex >= channels.getCountProperty())
        {
            return;
        }
        const std::shared_ptr<Graphics::VertexChannelBase>& channel = channels[vertexChannelIndex];
        if (Graphics::VertexChannelNames::DecodeBaseName(channel->getNameProperty()) != "Color")
        {
            return;
        }
        auto typed = std::dynamic_pointer_cast<Graphics::VertexChannel<Color>>(channel);
        if (typed == nullptr)
        {
            return;
        }
        // Each colour channel is scaled by its own alpha in whole bytes, and the remainder is
        // dropped rather than rounded (measured, modelprocessor/vertex_colors_rounding: 129 at
        // alpha 3 answers 1, where rounding would answer 2).
        for (SharpRuntime::intcs i = 0; i < typed->getCountProperty(); ++i)
        {
            const Color colour = typed->At(i);
            const SharpRuntime::intcs alpha = static_cast<SharpRuntime::intcs>(colour.getAProperty());
            const auto scale = [alpha](SharpRuntime::bytecs value)
            {
                return static_cast<SharpRuntime::bytecs>(static_cast<SharpRuntime::intcs>(value) * alpha / 255);
            };
            typed->SetAt(i, Color(scale(colour.getRProperty()), scale(colour.getGProperty()),
                                  scale(colour.getBProperty()), colour.getAProperty()));
        }
    }

    const std::string& ModelProcessor::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
