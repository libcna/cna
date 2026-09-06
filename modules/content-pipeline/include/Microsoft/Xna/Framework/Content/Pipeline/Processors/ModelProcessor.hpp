// SPDX-License-Identifier: MS-PL
#pragma once

#include <memory>
#include <string>
#include <vector>
#include <string_view>

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessor.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/ProcessorParameter.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Graphics/NodeContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ModelContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/ProcessorEnums.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    /**
     * @brief Turns a scene of nodes into a model: one bone per node, one mesh per mesh node, one
     *        part per geometry batch.
     *
     * Every step is measured on the XNA 4.0 runtime against a build context that records what it
     * is asked to build and convert (`tests/reference/xna40/graphics/graphics-content-oracle.json`,
     * cases `modelprocessor/*`).
     */
    class ModelProcessor : public ContentProcessor<Graphics::NodeContent, ModelContent>
    {
    public:
        /** @brief .NET full name of this type. */
        CNAEXT static constexpr std::string_view XnaTypeName =
            "Microsoft.Xna.Framework.Content.Pipeline.Processors.ModelProcessor";

        /** @brief Initializes a processor with the measured defaults. */
        ModelProcessor() = default;

        /** @brief Destroys the processor. */
        ~ModelProcessor() override = default;

        /**
         * @brief Gets the colour keyed out of every texture the model's materials name.
         * @return The key colour; magenta by default.
         */
        [[nodiscard]] Color getColorKeyColorProperty() const noexcept;
        /**
         * @brief Sets the colour keyed out of every texture the model's materials name.
         * @param value The key colour.
         */
        void setColorKeyColorProperty(Color value) noexcept;

        /**
         * @brief Gets whether the key colour is turned transparent.
         * @return true by default.
         */
        [[nodiscard]] bool getColorKeyEnabledProperty() const noexcept;
        /**
         * @brief Sets whether the key colour is turned transparent.
         * @param value true to key the colour out.
         */
        void setColorKeyEnabledProperty(bool value) noexcept;

        /**
         * @brief Gets the effect a geometry with no material of its own is given.
         * @return `BasicEffect` by default.
         */
        [[nodiscard]] MaterialProcessorDefaultEffect getDefaultEffectProperty() const noexcept;
        /**
         * @brief Sets the effect a geometry with no material of its own is given.
         * @param value The wanted effect.
         */
        void setDefaultEffectProperty(MaterialProcessorDefaultEffect value) noexcept;

        /**
         * @brief Gets whether each texture is given a mipmap chain.
         * @return true by default.
         */
        [[nodiscard]] bool getGenerateMipmapsProperty() const noexcept;
        /**
         * @brief Sets whether each texture is given a mipmap chain.
         * @param value true to build one.
         */
        void setGenerateMipmapsProperty(bool value) noexcept;

        /**
         * @brief Gets whether tangent frames are computed for meshes that lack them.
         * @return false by default.
         */
        [[nodiscard]] bool getGenerateTangentFramesProperty() const noexcept;
        /**
         * @brief Sets whether tangent frames are computed for meshes that lack them.
         * @param value true to compute them.
         */
        void setGenerateTangentFramesProperty(bool value) noexcept;

        /**
         * @brief Gets whether each texture's colour is multiplied by its alpha.
         * @return true by default.
         */
        [[nodiscard]] bool getPremultiplyTextureAlphaProperty() const noexcept;
        /**
         * @brief Sets whether each texture's colour is multiplied by its alpha.
         * @param value true to premultiply.
         */
        void setPremultiplyTextureAlphaProperty(bool value) noexcept;

        /**
         * @brief Gets whether vertex colours are multiplied by their alpha.
         * @return true by default.
         */
        [[nodiscard]] bool getPremultiplyVertexColorsProperty() const noexcept;
        /**
         * @brief Sets whether vertex colours are multiplied by their alpha.
         * @param value true to premultiply.
         */
        void setPremultiplyVertexColorsProperty(bool value) noexcept;

        /**
         * @brief Gets whether each texture is resized to the next power of two.
         * @return false by default.
         */
        [[nodiscard]] bool getResizeTexturesToPowerOfTwoProperty() const noexcept;
        /**
         * @brief Sets whether each texture is resized to the next power of two.
         * @param value true to resize.
         */
        void setResizeTexturesToPowerOfTwoProperty(bool value) noexcept;

        /**
         * @brief Gets the rotation applied about the X axis, in degrees.
         * @return Zero by default.
         */
        [[nodiscard]] SharpRuntime::Single getRotationXProperty() const noexcept;
        /**
         * @brief Sets the rotation applied about the X axis, in degrees.
         * @param value The rotation.
         */
        void setRotationXProperty(SharpRuntime::Single value) noexcept;

        /**
         * @brief Gets the rotation applied about the Y axis, in degrees.
         * @return Zero by default.
         */
        [[nodiscard]] SharpRuntime::Single getRotationYProperty() const noexcept;
        /**
         * @brief Sets the rotation applied about the Y axis, in degrees.
         * @param value The rotation.
         */
        void setRotationYProperty(SharpRuntime::Single value) noexcept;

        /**
         * @brief Gets the rotation applied about the Z axis, in degrees.
         * @return Zero by default.
         */
        [[nodiscard]] SharpRuntime::Single getRotationZProperty() const noexcept;
        /**
         * @brief Sets the rotation applied about the Z axis, in degrees.
         * @param value The rotation.
         */
        void setRotationZProperty(SharpRuntime::Single value) noexcept;

        /**
         * @brief Gets the uniform scale applied to the model.
         * @return One by default.
         */
        [[nodiscard]] SharpRuntime::Single getScaleProperty() const noexcept;
        /**
         * @brief Sets the uniform scale applied to the model.
         * @param value The scale.
         */
        void setScaleProperty(SharpRuntime::Single value) noexcept;

        /**
         * @brief Gets whether triangle winding is reversed.
         * @return false by default.
         */
        [[nodiscard]] bool getSwapWindingOrderProperty() const noexcept;
        /**
         * @brief Sets whether triangle winding is reversed.
         * @param value true to reverse it.
         */
        void setSwapWindingOrderProperty(bool value) noexcept;

        /**
         * @brief Gets the format each texture is converted to.
         * @return `DxtCompressed` by default.
         */
        [[nodiscard]] TextureProcessorOutputFormat getTextureFormatProperty() const noexcept;
        /**
         * @brief Sets the format each texture is converted to.
         * @param value The wanted format.
         */
        void setTextureFormatProperty(TextureProcessorOutputFormat value) noexcept;

        /**
         * @brief Processes the scene into a model.
         *
         * @param input The root node of the scene.
         * @param context The processor context, which converts the materials.
         * @return The model.
         * @throws System::ArgumentNullException when the input is null.
         * @throws InvalidContentException when a skinned mesh has no vertex weights.
         */
        [[nodiscard]] std::shared_ptr<ModelContent> Process(const std::shared_ptr<Graphics::NodeContent>& input,
                                                            ContentProcessorContext& context) override;

        /**
         * @brief Returns the type's stable name.
         *
         * @return The .NET full name of this processor.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const;

        /**
         * @brief Declares the properties a build may set by name.
         *
         * The names are XNA's own property spellings, which is what a `.contentproj` writes in a
         * `<ProcessorParameters_Name>` element and what `MaterialProcessor` forwards to the
         * texture processor. Without this a build could construct the processor and never
         * configure it (plans/plan_xnapipeline_parity.md `XNAPP-021`).
         *
         * @param bindings The bindings to add to.
         */
        CNAEXT static void DescribeParameters(ProcessorParameterBindings<ModelProcessor>& bindings);


    protected:
        /**
         * @brief Converts one material, which is where a derived processor changes what a model is
         *        drawn with.
         *
         * @param material The material to convert.
         * @param context The processor context.
         * @return The converted material.
         */
        [[nodiscard]] virtual std::shared_ptr<Graphics::MaterialContent> ConvertMaterial(
            const std::shared_ptr<Graphics::MaterialContent>& material, ContentProcessorContext& context);

        /**
         * @brief Processes the geometry batches that share one material, which is where a derived
         *        processor changes how geometry is built.
         *
         * @param material The material the batches share.
         * @param geometryCollection The batches using that material.
         * @param context The processor context.
         */
        virtual void ProcessGeometryUsingMaterial(
            const std::shared_ptr<Graphics::MaterialContent>& material,
            const std::vector<std::shared_ptr<Graphics::GeometryContent>>& geometryCollection,
            ContentProcessorContext& context);

        /**
         * @brief Processes one vertex channel, which is where a derived processor changes or drops
         *        the data a channel carries.
         *
         * @param geometry The batch the channel belongs to.
         * @param vertexChannelIndex The index of the channel.
         * @param context The processor context.
         */
        virtual void ProcessVertexChannel(const std::shared_ptr<Graphics::GeometryContent>& geometry,
                                          SharpRuntime::intcs vertexChannelIndex, ContentProcessorContext& context);

    private:
        /**
         * @brief Replaces a `Weights` channel with the `BlendIndices` and `BlendWeight` a vertex
         *        buffer can carry.
         *
         * A vertex names its bones; a vertex buffer indexes them. Without this the channel is left
         * as a `BoneWeightCollection`, which no vertex element format can hold, so the buffer
         * builder drops it and a skinned model reaches the runtime with no skinning data at all
         * (plans/plan_xnapipeline_parity.md `XNAPP-266`).
         *
         * @param geometry The batch whose channel is converted.
         * @param vertexChannelIndex The channel's index; the two replacements take its place.
         */
        void ConvertWeightsChannel(const std::shared_ptr<Graphics::GeometryContent>& geometry,
                                   SharpRuntime::intcs vertexChannelIndex);

        /** @brief How many bones may influence one vertex; XNA's own ceiling. */
        static constexpr SharpRuntime::intcs kMaxBoneInfluences = 4;

        /** @brief The flattened skeleton's bone names, in the order their indices follow. */
        std::vector<std::string> skeleton_;

        Color colorKeyColor_{255, 0, 255, 255};
        bool colorKeyEnabled_ = true;
        MaterialProcessorDefaultEffect defaultEffect_ = MaterialProcessorDefaultEffect::BasicEffect;
        bool generateMipmaps_ = true;
        bool generateTangentFrames_ = false;
        bool premultiplyTextureAlpha_ = true;
        bool premultiplyVertexColors_ = true;
        bool resizeTexturesToPowerOfTwo_ = false;
        SharpRuntime::Single rotationX_ = 0.0f;
        SharpRuntime::Single rotationY_ = 0.0f;
        SharpRuntime::Single rotationZ_ = 0.0f;
        SharpRuntime::Single scale_ = 1.0f;
        bool swapWindingOrder_ = false;
        TextureProcessorOutputFormat textureFormat_ = TextureProcessorOutputFormat::DxtCompressed;
    };
}
