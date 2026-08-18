// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Igl/IglRenderer.hpp"

#include "IglConversions.hpp"

#include "CNA/Logger.hpp"
#include "CNA/LogCategory.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <igl/CommandBuffer.h>
#include <igl/CommandQueue.h>
#include <igl/Device.h>
#include <igl/RenderCommandEncoder.h>
#include <igl/RenderPipelineState.h>
#include <igl/Shader.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Renderers::Igl
{
    namespace
    {
        using Matrix4 = Microsoft::Xna::Framework::Matrix;

        constexpr std::size_t kUniformAlignment = 256;

        /** @brief Attribute name a slot is declared with in the generated shaders. */
        [[nodiscard]] const char* SlotName(const std::uint32_t slot)
        {
            switch (slot)
            {
                case VertexAttributeSlot::Position:     return "aPosition";
                case VertexAttributeSlot::Normal:       return "aNormal";
                case VertexAttributeSlot::Color:        return "aColor";
                case VertexAttributeSlot::TexCoord0:    return "aTexCoord0";
                case VertexAttributeSlot::TexCoord1:    return "aTexCoord1";
                case VertexAttributeSlot::BlendIndices: return "aBlendIndices";
                case VertexAttributeSlot::BlendWeights: return "aBlendWeights";
                case VertexAttributeSlot::Tangent:      return "aTangent";
                default:                                return "";
            }
        }

        /**
         * @brief Builds the presentation transform applied after the game's own projection.
         *
         * IGL's two backends disagree about what a non-zero viewport origin means, so this renderer
         * always binds a full-surface viewport and expresses XNA's `Viewport` rectangle and the
         * letterbox/overscan presentation rectangle here instead, as a scale and offset in clip
         * space. Clipping is handled separately by the scissor rectangle.
         *
         * @param targetWidth   Bound attachment width in pixels.
         * @param targetHeight  Bound attachment height in pixels.
         * @param rectX         Left edge of the destination rectangle, in attachment pixels.
         * @param rectY         Top edge of the destination rectangle, in attachment pixels.
         * @param rectWidth     Destination width in attachment pixels.
         * @param rectHeight    Destination height in attachment pixels.
         * @return The transform, in XNA row-vector storage.
         */
        [[nodiscard]] Matrix4 BuildPresentationMatrix(const float targetWidth,
                                                       const float targetHeight, const float rectX,
                                                       const float rectY, const float rectWidth,
                                                       const float rectHeight)
        {
            Matrix4 matrix = Matrix4::getIdentityProperty();
            if (targetWidth <= 0.0f || targetHeight <= 0.0f || rectWidth <= 0.0f ||
                rectHeight <= 0.0f)
            {
                return matrix;
            }

            const float scaleX = rectWidth / targetWidth;
            const float scaleY = rectHeight / targetHeight;

            const float centreX = rectX + rectWidth * 0.5f;
            // Clip space has Y up on both backends, so a top-relative rectangle is measured from
            // the bottom before it becomes an offset.
            const float centreYFromBottom = targetHeight - (rectY + rectHeight * 0.5f);

            matrix.M11 = scaleX;
            matrix.M22 = scaleY;
            matrix.M41 = 2.0f * centreX / targetWidth - 1.0f;
            matrix.M42 = 2.0f * centreYFromBottom / targetHeight - 1.0f;
            return matrix;
        }
    }

    void IglRenderer::FillEffectUniforms(const Matrix& world, const Matrix& view,
                                          const Matrix& projection, const GpuDrawParams& params,
                                          IglEffectUniforms& uniforms)
    {
        IglBoundTarget& target = CurrentTarget();
        const bool isBackBuffer = boundTarget_ == backBufferTarget_.get();
        const float targetWidth = static_cast<float>(std::max(1, target.GetTargetWidth()));
        const float targetHeight = static_cast<float>(std::max(1, target.GetTargetHeight()));

        float rectX = 0.0f;
        float rectY = 0.0f;
        float rectWidth = targetWidth;
        float rectHeight = targetHeight;

        if (isBackBuffer)
        {
            rectX = static_cast<float>(presentX_);
            rectY = static_cast<float>(presentY_);
            rectWidth = static_cast<float>(presentWidth_);
            rectHeight = static_cast<float>(presentHeight_);
        }

        if (viewportSet_ && viewportRect_[2] > 0 && viewportRect_[3] > 0)
        {
            const float scaleX = isBackBuffer && logicalWidth_ > 0
                                     ? rectWidth / static_cast<float>(logicalWidth_)
                                     : 1.0f;
            const float scaleY = isBackBuffer && logicalHeight_ > 0
                                     ? rectHeight / static_cast<float>(logicalHeight_)
                                     : 1.0f;
            const float viewportX = rectX + static_cast<float>(viewportRect_[0]) * scaleX;
            const float viewportY = rectY + static_cast<float>(viewportRect_[1]) * scaleY;
            rectWidth = static_cast<float>(viewportRect_[2]) * scaleX;
            rectHeight = static_cast<float>(viewportRect_[3]) * scaleY;
            rectX = viewportX;
            rectY = viewportY;
        }

        Matrix combined = world * view * projection;

        if (!isBackBuffer && !IsVulkanBackend())
        {
            // An off-screen target is rendered with a flipped Y so its rows are stored top-first,
            // which is what makes it read back and sample in the same orientation as an uploaded
            // texture. The pipeline's front-face winding is reversed to match (see AcquirePipeline).
            //
            // OpenGL only, because the correction the two backends need is not the same one. IGL's
            // Vulkan encoder already flips the viewport height so that clip +Y runs up the screen
            // on both backends -- but "up the screen" is a DIFFERENT direction through image memory
            // in each: a GL texture's row 0 sits at t=0, the bottom, while a Vulkan image's row 0
            // is the top. So on OpenGL this flip is what puts a target's rows in the same order
            // SetData uploads them, and on Vulkan it is what took them OUT of that order.
            //
            // Applying it on both stored rendered content upside down relative to uploaded content,
            // which is a contradiction the renderer cannot satisfy: the sampler, the readback and
            // SetRenderTarget-then-draw all read one image. It stayed invisible because it cancelled
            // against two other Vulkan-only defects -- IGL's unconditional readback row flip
            // (IglRenderTargetRenderer::GetData) and ReadBackbuffer's inverted row origin -- so a
            // rendered target read back correctly and a 1x1 pixel check of a sampled one passed,
            // each for the wrong reason. Measured directly by Igl_ReadbackOrientation.
            Matrix flip = Matrix::getIdentityProperty();
            flip.M22 = -1.0f;
            combined = combined * flip;
        }

        combined = combined * BuildPresentationMatrix(targetWidth, targetHeight, rectX, rectY,
                                                       rectWidth, rectHeight);

        // igl::IDevice::getNormalizedZRange() defaults to NegOneToOne in the base class, and
        // neither IGL's OpenGL nor Vulkan backend overrides it -- correct for OpenGL (whose native
        // clip-space Z genuinely is [-1, 1]), but silently wrong for Vulkan, whose native Z range
        // is [0, 1] like Direct3D. Trusting the reported value applies this OpenGL-only remap to
        // Vulkan too, corrupting every Z value into the wrong range. IsVulkanBackend() is checked
        // directly rather than trusting the (for this IGL version, unreliable) API report.
        if (!IsVulkanBackend() &&
            GetDevice().getNormalizedZRange() == igl::NormalizedZRange::NegOneToOne)
        {
            ApplyNegativeOneToOneDepth(combined);
        }

        CopyMatrixToGlsl(combined, uniforms.worldViewProjection);
        CopyMatrixToGlsl(world, uniforms.world);
        CopyMatrixToGlsl(Matrix::Transpose(Matrix::Invert(world)), uniforms.worldInverseTranspose);

        std::memcpy(uniforms.diffuseColor, params.diffuseColor, sizeof(uniforms.diffuseColor));
        uniforms.emissiveColor[0] = params.emissiveColor[0];
        uniforms.emissiveColor[1] = params.emissiveColor[1];
        uniforms.emissiveColor[2] = params.emissiveColor[2];
        uniforms.emissiveColor[3] = params.specularPower;
        uniforms.specularColor[0] = params.specularColor[0];
        uniforms.specularColor[1] = params.specularColor[1];
        uniforms.specularColor[2] = params.specularColor[2];
        uniforms.specularColor[3] = params.envMapAmount;
        uniforms.ambientColor[0] = params.ambientColor[0];
        uniforms.ambientColor[1] = params.ambientColor[1];
        uniforms.ambientColor[2] = params.ambientColor[2];
        uniforms.ambientColor[3] = params.fresnelFactor;
        uniforms.eyePosition[0] = params.eyePositionWorld[0];
        uniforms.eyePosition[1] = params.eyePositionWorld[1];
        uniforms.eyePosition[2] = params.eyePositionWorld[2];
        uniforms.eyePosition[3] = 1.0f;
        uniforms.fogColor[0] = params.fogColor[0];
        uniforms.fogColor[1] = params.fogColor[1];
        uniforms.fogColor[2] = params.fogColor[2];
        uniforms.fogColor[3] = 0.0f;
        std::memcpy(uniforms.fogVector, params.fogVector, sizeof(uniforms.fogVector));
        std::memcpy(uniforms.alphaTest, params.alphaTest, sizeof(uniforms.alphaTest));
        uniforms.envMapSpecular[0] = params.envMapSpecular[0];
        uniforms.envMapSpecular[1] = params.envMapSpecular[1];
        uniforms.envMapSpecular[2] = params.envMapSpecular[2];
        uniforms.envMapSpecular[3] = 0.0f;
        uniforms.pbrFactors[0] = params.pbrMetallicFactor;
        uniforms.pbrFactors[1] = params.pbrRoughnessFactor;

        const float* lightDirections[3] = {params.light0Dir, params.light1Dir, params.light2Dir};
        const float* lightDiffuse[3] = {params.light0Diffuse, params.light1Diffuse,
                                        params.light2Diffuse};
        const float* lightSpecular[3] = {params.light0Specular, params.light1Specular,
                                         params.light2Specular};
        for (int i = 0; i < 3; ++i)
        {
            for (int c = 0; c < 3; ++c)
            {
                uniforms.lightDirection[i][c] = lightDirections[i][c];
                uniforms.lightDiffuse[i][c] = lightDiffuse[i][c];
                uniforms.lightSpecular[i][c] = lightSpecular[i][c];
            }
        }

        if (params.cpu2DColorMatrixEnabled)
        {
            std::memcpy(uniforms.colorMatrix, params.cpu2DColorMatrix, sizeof(uniforms.colorMatrix));
            std::memcpy(uniforms.colorOffset, params.cpu2DColorOffset, sizeof(uniforms.colorOffset));
        }

        std::int32_t flags = 0;
        if (params.textureEnabled && params.texture0 != nullptr)
            flags |= EffectFeature::TextureEnabled;
        if (params.vertexColorEnabled)
            flags |= EffectFeature::VertexColorEnabled;
        if (params.lightingEnabled)
            flags |= EffectFeature::LightingEnabled;
        if (params.preferPerPixelLighting)
            flags |= EffectFeature::PerPixelLighting;
        if (params.fogEnabled)
            flags |= EffectFeature::FogEnabled;
        if (params.dualTexture && params.texture1 != nullptr)
            flags |= EffectFeature::DualTexture;
        if (params.envMapping && params.envMap != nullptr)
            flags |= EffectFeature::EnvMapping;
        if (params.fresnelEnabled)
            flags |= EffectFeature::FresnelEnabled;
        if (params.skinned && params.boneCount > 0)
            flags |= EffectFeature::Skinned;
        if (params.pbr)
            flags |= EffectFeature::Pbr;
        if (params.cpu2DColorMatrixEnabled)
            flags |= EffectFeature::ColorMatrix;
        if (params.pbrNormalMap != nullptr)
            flags |= EffectFeature::NormalMap;
        if (params.pbrMetallicRoughnessMap != nullptr)
            flags |= EffectFeature::MetallicRoughnessMap;
        if (params.pbrEmissiveMap != nullptr)
            flags |= EffectFeature::EmissiveMap;
        if (params.pbrOcclusionMap != nullptr)
            flags |= EffectFeature::OcclusionMap;

        // The alpha test's default {0, 0, 1, 1} is XNA's "always pass"; only a real AlphaTestEffect
        // produces weights that can go negative, so the shader branch is enabled only for those.
        if (params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f)
            flags |= EffectFeature::AlphaTestEnabled;

        uniforms.flags[0] = flags;
        uniforms.flags[1] = std::clamp(params.weightsPerVertex, 1, 4);
        uniforms.flags[2] = std::clamp(params.boneCount, 0, kMaxBones);
    }

    void IglRenderer::BindEffectResources(igl::IRenderCommandEncoder& encoder,
                                           const GpuDrawParams& params,
                                           const IglEffectUniforms& uniforms,
                                           const IglBoneUniforms* bones)
    {
        const IglDynamicBufferPool::Allocation effectAllocation =
            dynamicUniformPool_->Upload(&uniforms, sizeof(IglEffectUniforms), kUniformAlignment);
        encoder.bindBuffer(UniformBufferBinding::Effect, effectAllocation.buffer,
                           effectAllocation.offset, sizeof(IglEffectUniforms));

        // The bone block is always bound, even for a draw that does not skin: the generated shader
        // declares it unconditionally, and Vulkan requires every declared descriptor to have a
        // resource behind it.
        static const IglBoneUniforms identityBones{};
        const IglBoneUniforms& boneData = bones != nullptr ? *bones : identityBones;
        const IglDynamicBufferPool::Allocation boneAllocation =
            dynamicUniformPool_->Upload(&boneData, sizeof(IglBoneUniforms), kUniformAlignment);
        encoder.bindBuffer(UniformBufferBinding::Bones, boneAllocation.buffer,
                           boneAllocation.offset, sizeof(IglBoneUniforms));

        const auto bindUnitNeutral = [&](const std::uint32_t unit, igl::ITexture* texture,
                                         const NeutralTextureKind neutral) {
            igl::ITexture* resolved =
                texture != nullptr ? texture : ResolveNeutralTexture(neutral);
            encoder.bindTexture(unit, igl::BindTarget::kFragment, resolved);
            encoder.bindSamplerState(unit, igl::BindTarget::kFragment,
                                     AcquireSamplerState(static_cast<int>(unit)).get());
        };

        const auto bindUnit = [&](const std::uint32_t unit, igl::ITexture* texture,
                                  const bool cube) {
            bindUnitNeutral(unit, texture,
                            cube ? NeutralTextureKind::WhiteCube : NeutralTextureKind::White2D);
        };

        const auto textureOf = [](const ITextureRenderer* texture) -> igl::ITexture* {
            if (texture == nullptr)
                return nullptr;
            if (const auto* plain = dynamic_cast<const IglTextureRenderer*>(texture))
                return plain->GetIglTexture().get();
            if (const auto* target = dynamic_cast<const IglRenderTargetRenderer*>(texture))
                return target->GetColorTexture().get();
            return nullptr;
        };

        const auto cubeOf = [](const ITextureCubeRenderer* texture) -> igl::ITexture* {
            if (texture == nullptr)
                return nullptr;
            if (const auto* plain = dynamic_cast<const IglTextureCubeRenderer*>(texture))
                return plain->GetIglTexture().get();
            if (const auto* target = dynamic_cast<const IglRenderTargetCubeRenderer*>(texture))
                return target->GetIglTexture().get();
            return nullptr;
        };

        bindUnit(TextureUnit::Texture0, textureOf(params.texture0), false);
        bindUnit(TextureUnit::Texture1, textureOf(params.texture1), false);
        bindUnit(TextureUnit::EnvironmentMap, cubeOf(params.envMap), true);
        // GLTF-374: the normal slot's neutral value is the flat-normal texel, not white -- an
        // unbound white normal map tilts every pixel of an otherwise unperturbed surface.
        bindUnitNeutral(TextureUnit::NormalMap, textureOf(params.pbrNormalMap),
                        NeutralTextureKind::FlatNormal2D);
        bindUnit(TextureUnit::MetallicRoughnessMap, textureOf(params.pbrMetallicRoughnessMap),
                 false);
        bindUnit(TextureUnit::EmissiveMap, textureOf(params.pbrEmissiveMap), false);
        bindUnit(TextureUnit::OcclusionMap, textureOf(params.pbrOcclusionMap), false);

        encoder.setBlendColor(igl::Color(blendFactor_[0], blendFactor_[1], blendFactor_[2],
                                         blendFactor_[3]));
        encoder.setStencilReferenceValue(static_cast<std::uint32_t>(depthStencil_.referenceStencil));
        encoder.setDepthBias(depthBias_, slopeScaleDepthBias_, 0.0f);
    }

    void IglRenderer::SubmitDraw(const IglVertexBufferRenderer& vb,
                                  const IglIndexBufferRenderer* ib, const Matrix& world,
                                  const Matrix& view, const Matrix& projection,
                                  const PrimitiveType primitive, const int primitiveCount,
                                  const int instanceCount, const GpuDrawParams& params)
    {
        if (primitiveCount <= 0)
            return;
        if (vb.GetIglBuffer() == nullptr)
        {
            throw std::runtime_error(
                "IGL renderer: a draw referenced a vertex buffer that has never been uploaded to");
        }

        EnsurePassOpen();

        // ---- Vertex layout -------------------------------------------------------------------
        std::vector<igl::VertexAttribute> attributes;
        std::vector<igl::VertexInputBinding> bindings;
        std::uint32_t attributeMask = 0;

        struct BoundStream
        {
            const IglVertexBufferRenderer* buffer = nullptr;
            std::size_t byteOffset = 0;
        };
        std::vector<BoundStream> streams;

        const int declaredStreams = params.vertexStreamCount;
        if (declaredStreams > 0)
        {
            for (int i = 0; i < declaredStreams; ++i)
            {
                const GpuVertexStreamBinding& stream = params.vertexStreams[i];
                const auto* streamBuffer =
                    dynamic_cast<const IglVertexBufferRenderer*>(stream.buffer);
                if (streamBuffer == nullptr || streamBuffer->GetIglBuffer() == nullptr)
                {
                    throw std::runtime_error(
                        "IGL renderer: a bound VertexBufferBinding names a buffer this renderer "
                        "does not own, or one that has never been uploaded to");
                }

                igl::VertexInputBinding binding;
                binding.stride = stream.strideInBytes > 0
                                     ? static_cast<std::size_t>(stream.strideInBytes)
                                     : streamBuffer->GetStride();
                binding.sampleFunction = stream.instanceFrequency > 0
                                             ? igl::VertexSampleFunction::Instance
                                             : igl::VertexSampleFunction::PerVertex;
                binding.sampleRate = stream.instanceFrequency > 0
                                         ? static_cast<std::size_t>(stream.instanceFrequency)
                                         : 1;
                bindings.push_back(binding);

                // IGL's draw calls carry no base-vertex term on the OpenGL backend, so the whole
                // start offset is folded into the buffer binding. That is exactly equivalent and
                // works identically on both backends.
                const std::size_t startElement =
                    stream.instanceFrequency > 0
                        ? static_cast<std::size_t>(stream.vertexOffset)
                        : static_cast<std::size_t>(stream.vertexOffset) +
                              static_cast<std::size_t>(ib != nullptr ? params.baseVertex
                                                                     : params.vertexStart);
                streams.push_back(BoundStream{streamBuffer, startElement * binding.stride});

                if (!streamBuffer->HasDeclaration())
                    continue;

                for (const VertexElement& element :
                     streamBuffer->GetDeclaration().GetVertexElements())
                {
                    const std::uint32_t slot = ToVertexAttributeSlot(
                        element.getVertexElementUsageProperty(), element.getUsageIndexProperty());
                    if (slot >= VertexAttributeSlot::Count)
                        continue;
                    if ((attributeMask & VertexAttributeBit(slot)) != 0)
                        continue;

                    igl::VertexAttribute attribute;
                    attribute.bufferIndex = static_cast<std::size_t>(i);
                    attribute.format =
                        ToIglVertexFormat(element.getVertexElementFormatProperty());
                    attribute.offset =
                        static_cast<std::uintptr_t>(element.getOffsetProperty());
                    attribute.name = SlotName(slot);
                    attribute.location = static_cast<int>(slot);
                    attributes.push_back(attribute);
                    attributeMask |= VertexAttributeBit(slot);
                }
            }
        }

        if (attributes.empty())
        {
            // REMED-GFX-233's legacy route: a single buffer with an intentionally empty
            // VertexDeclaration, where the upload stride is the only description that exists.
            igl::VertexInputBinding binding;
            binding.stride = vb.GetStride();
            binding.sampleFunction = igl::VertexSampleFunction::PerVertex;
            bindings.assign(1, binding);

            if (!DescribeStrideLayoutForDraw(vb.GetStride(), attributes, attributeMask))
            {
                throw std::runtime_error(
                    "IGL renderer: a draw supplied neither a VertexDeclaration nor a stride this "
                    "renderer recognises (" + std::to_string(vb.GetStride()) + " bytes).");
            }

            const std::size_t startElement =
                static_cast<std::size_t>(ib != nullptr ? params.baseVertex : params.vertexStart);
            streams.assign(1, BoundStream{&vb, startElement * binding.stride});
        }

        std::uint64_t vertexInputId = 0;
        const std::shared_ptr<igl::IVertexInputState> vertexInput =
            AcquireVertexInputState(attributes, bindings, vertexInputId);

        // ---- Shader --------------------------------------------------------------------------
        IglBoundTarget& target = CurrentTarget();
        const int colorAttachmentCount = std::max(1, target.GetColorAttachmentCount());

        std::shared_ptr<igl::IShaderStages> stages;
        std::uint64_t programId = 0;
        auto* custom = dynamic_cast<IglEffectRenderer*>(params.customEffectRenderer);
        if (custom != nullptr)
        {
            if (!custom->IsValid())
            {
                throw std::runtime_error("IGL renderer: a draw used a ShaderEffect that failed to "
                                         "compile: " + custom->GetCompileError());
            }
            stages = custom->GetStages();
            programId = custom->GetProgramId();
        }
        else
        {
            stages = AcquireBuiltInShader(attributeMask, colorAttachmentCount);
        }

        // ---- Pipeline ------------------------------------------------------------------------
        PipelineKey key;
        key.vertexInputId = vertexInputId;
        key.programId = programId;
        key.attributeMask = attributeMask;
        key.topology = static_cast<std::uint8_t>(ToIglPrimitiveType(primitive));
        key.colorAttachmentCount = static_cast<std::uint8_t>(colorAttachmentCount);
        key.cullMode = static_cast<std::uint8_t>(ToIglCullMode(cullMode_));
        key.fillMode = static_cast<std::uint8_t>(ToIglFillMode(fillMode_));
        key.sampleCount = static_cast<std::uint8_t>(std::max(1, target.GetTargetSampleCount()));
        key.blendEnabled = blendEnabled_ ? 1 : 0;
        key.srcRgb = static_cast<std::uint8_t>(ToIglBlendFactor(blendColorSrc_));
        key.dstRgb = static_cast<std::uint8_t>(ToIglBlendFactor(blendColorDst_));
        key.srcAlpha = static_cast<std::uint8_t>(ToIglBlendFactor(blendAlphaSrc_));
        key.dstAlpha = static_cast<std::uint8_t>(ToIglBlendFactor(blendAlphaDst_));
        key.opRgb = static_cast<std::uint8_t>(ToIglBlendOp(blendColorFunc_));
        key.opAlpha = static_cast<std::uint8_t>(ToIglBlendOp(blendAlphaFunc_));
        for (int i = 0; i < 4; ++i)
        {
            key.colorWriteMask[static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>(blendWriteState_.colorWriteChannels[i]);
        }

        const std::shared_ptr<igl::IFramebuffer>& framebuffer = target.GetFramebuffer();
        for (int i = 0; i < colorAttachmentCount; ++i)
        {
            const std::shared_ptr<igl::ITexture> attachment =
                framebuffer->getColorAttachment(static_cast<std::size_t>(i));
            key.colorFormat[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(
                attachment ? attachment->getFormat() : igl::TextureFormat::RGBA_UNorm8);
        }
        const std::shared_ptr<igl::ITexture> depthAttachment = framebuffer->getDepthAttachment();
        key.depthFormat = static_cast<std::uint8_t>(
            depthAttachment ? depthAttachment->getFormat() : igl::TextureFormat::Invalid);
        const std::shared_ptr<igl::ITexture> stencilAttachment = framebuffer->getStencilAttachment();
        key.stencilFormat = static_cast<std::uint8_t>(
            stencilAttachment ? stencilAttachment->getFormat() : igl::TextureFormat::Invalid);

        const std::shared_ptr<igl::IRenderPipelineState> pipeline =
            AcquirePipeline(key, vertexInput, stages, custom);

        // ---- Bind and draw --------------------------------------------------------------------
        encoder_->bindRenderPipelineState(pipeline);
        encoder_->bindDepthStencilState(AcquireDepthStencilState());

        for (std::size_t i = 0; i < streams.size(); ++i)
        {
            igl::IBuffer* buffer = streams[i].buffer->GetIglBuffer();
            encoder_->bindVertexBuffer(static_cast<std::uint32_t>(i), *buffer,
                                       streams[i].byteOffset);
        }

        IglEffectUniforms uniforms;
        FillEffectUniforms(world, view, projection, params, uniforms);

        std::unique_ptr<IglBoneUniforms> bones;
        if (params.skinned && params.boneCount > 0)
        {
            bones = std::make_unique<IglBoneUniforms>();
            const std::size_t floats =
                static_cast<std::size_t>(std::clamp(params.boneCount, 0, kMaxBones)) * 16;
            std::memcpy(bones->bones, params.boneTransforms, floats * sizeof(float));
        }

        BindEffectResources(*encoder_, params, uniforms, bones.get());

        if (custom != nullptr)
            ApplyCustomEffectUniforms(*encoder_, *pipeline, *custom);

        const int elementCount = VertexCountForPrimitives(primitive, primitiveCount);
        if (ib != nullptr)
        {
            if (ib->GetIglBuffer() == nullptr)
            {
                throw std::runtime_error(
                    "IGL renderer: a draw referenced an index buffer that has never been uploaded "
                    "to");
            }
            encoder_->bindIndexBuffer(*ib->GetIglBuffer(),
                                      ib->IsThirtyTwoBit() ? igl::IndexFormat::UInt32
                                                           : igl::IndexFormat::UInt16);
            encoder_->drawIndexed(static_cast<std::size_t>(elementCount),
                                  static_cast<std::uint32_t>(std::max(1, instanceCount)),
                                  static_cast<std::uint32_t>(params.startIndex), 0, 0);
        }
        else
        {
            encoder_->draw(static_cast<std::size_t>(elementCount),
                           static_cast<std::uint32_t>(std::max(1, instanceCount)), 0, 0);
        }
    }

    bool IglRenderer::DescribeStrideLayoutForDraw(const std::size_t stride,
                                                   std::vector<igl::VertexAttribute>& attributes,
                                                   std::uint32_t& attributeMask)
    {
        const auto add = [&](const std::uint32_t slot, const igl::VertexAttributeFormat format,
                             const std::uintptr_t offset) {
            igl::VertexAttribute attribute;
            attribute.bufferIndex = 0;
            attribute.format = format;
            attribute.offset = offset;
            attribute.name = SlotName(slot);
            attribute.location = static_cast<int>(slot);
            attributes.push_back(attribute);
            attributeMask |= VertexAttributeBit(slot);
        };

        switch (stride)
        {
        case 16: // VertexPositionColor
            add(VertexAttributeSlot::Position, igl::VertexAttributeFormat::Float3, 0);
            add(VertexAttributeSlot::Color, igl::VertexAttributeFormat::UByte4Norm, 12);
            return true;
        case 20: // VertexPositionTexture
            add(VertexAttributeSlot::Position, igl::VertexAttributeFormat::Float3, 0);
            add(VertexAttributeSlot::TexCoord0, igl::VertexAttributeFormat::Float2, 12);
            return true;
        case 24: // VertexPositionColorTexture
            add(VertexAttributeSlot::Position, igl::VertexAttributeFormat::Float3, 0);
            add(VertexAttributeSlot::Color, igl::VertexAttributeFormat::UByte4Norm, 12);
            add(VertexAttributeSlot::TexCoord0, igl::VertexAttributeFormat::Float2, 16);
            return true;
        case 32: // VertexPositionNormalTexture
            add(VertexAttributeSlot::Position, igl::VertexAttributeFormat::Float3, 0);
            add(VertexAttributeSlot::Normal, igl::VertexAttributeFormat::Float3, 12);
            add(VertexAttributeSlot::TexCoord0, igl::VertexAttributeFormat::Float2, 24);
            return true;
        default:
            return false;
        }
    }

    void IglRenderer::ApplyCustomEffectUniforms(igl::IRenderCommandEncoder& encoder,
                                                 const igl::IRenderPipelineState& pipeline,
                                                 const IglEffectRenderer& effect)
    {
        if (IsVulkanBackend())
        {
            // Loose (non-block) uniforms do not exist in Vulkan GLSL, and IGL's Vulkan encoder
            // deliberately leaves bindUniform unimplemented. A ShaderEffect that relies on them is
            // refused by name rather than drawn with stale or default values.
            if (!effect.GetUniforms().empty())
            {
                throw std::runtime_error(
                    "IGL renderer: ShaderEffect parameters are only supported on the OpenGL "
                    "backend. Write the shader with a std140 uniform block, or select "
                    "CNA_IGL_BACKEND=opengl.");
            }
            return;
        }

        for (const auto& [name, value] : effect.GetUniforms())
        {
            igl::UniformDesc desc;
            desc.name = name;
            desc.type = value.type;
            desc.numElements = static_cast<std::size_t>(std::max(1, value.numElements));
            desc.location = pipeline.getIndexByName(name, igl::ShaderStage::Fragment);
            if (desc.location < 0)
                desc.location = pipeline.getIndexByName(name, igl::ShaderStage::Vertex);
            if (desc.location < 0)
                continue;

            encoder.bindUniform(desc, value.data.data());
        }

        const std::array<igl::ITexture*, igl::IGL_TEXTURE_SAMPLERS_MAX>& textures =
            effect.GetBoundTextures();
        for (std::size_t unit = 0; unit < textures.size(); ++unit)
        {
            if (textures[unit] == nullptr)
                continue;
            encoder.bindTexture(unit, igl::BindTarget::kFragment, textures[unit]);
            encoder.bindSamplerState(unit, igl::BindTarget::kFragment,
                                     AcquireSamplerState(static_cast<int>(unit)).get());
        }
    }

    // ---------------------------------------------------------------------------------------------
    // Public draw entry points
    // ---------------------------------------------------------------------------------------------

    void IglRenderer::DrawColoredPrimitives(const IVertexBufferRenderer& vb, const Matrix& world,
                                             const Matrix& view, const Matrix& projection,
                                             const PrimitiveType primitive,
                                             const int primitiveCount)
    {
        GpuDrawParams params;
        params.vertexColorEnabled = true;
        params.textureEnabled = false;
        DrawPrimitivesEx(vb, world, view, projection, primitive, primitiveCount, params);
    }

    void IglRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                                    const IIndexBufferRenderer& ib,
                                                    const Matrix& world, const Matrix& view,
                                                    const Matrix& projection,
                                                    const PrimitiveType primitive,
                                                    const int primitiveCount)
    {
        GpuDrawParams params;
        params.vertexColorEnabled = true;
        params.textureEnabled = false;
        DrawIndexedPrimitivesEx(vb, ib, world, view, projection, primitive, primitiveCount, params);
    }

    void IglRenderer::DrawPrimitivesEx(const IVertexBufferRenderer& vb, const Matrix& world,
                                        const Matrix& view, const Matrix& projection,
                                        const PrimitiveType primitive, const int primitiveCount,
                                        const GpuDrawParams& params)
    {
        const auto* buffer = dynamic_cast<const IglVertexBufferRenderer*>(&vb);
        if (buffer == nullptr)
            throw std::runtime_error("IGL renderer: a draw named a vertex buffer it does not own");

        SubmitDraw(*buffer, nullptr, world, view, projection, primitive, primitiveCount, 1, params);
    }

    void IglRenderer::DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                               const IIndexBufferRenderer& ib, const Matrix& world,
                                               const Matrix& view, const Matrix& projection,
                                               const PrimitiveType primitive,
                                               const int primitiveCount,
                                               const GpuDrawParams& params)
    {
        const auto* vertexBuffer = dynamic_cast<const IglVertexBufferRenderer*>(&vb);
        const auto* indexBuffer = dynamic_cast<const IglIndexBufferRenderer*>(&ib);
        if (vertexBuffer == nullptr || indexBuffer == nullptr)
        {
            throw std::runtime_error(
                "IGL renderer: an indexed draw named a buffer this renderer does not own");
        }

        SubmitDraw(*vertexBuffer, indexBuffer, world, view, projection, primitive, primitiveCount, 1,
                   params);
    }

    void IglRenderer::DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb,
                                                 const IIndexBufferRenderer& ib, const Matrix& world,
                                                 const Matrix& view, const Matrix& projection,
                                                 const PrimitiveType primitive,
                                                 const int primitiveCount, const int instanceCount,
                                                 const GpuDrawParams& params)
    {
        const auto* vertexBuffer = dynamic_cast<const IglVertexBufferRenderer*>(&vb);
        const auto* indexBuffer = dynamic_cast<const IglIndexBufferRenderer*>(&ib);
        if (vertexBuffer == nullptr || indexBuffer == nullptr)
        {
            throw std::runtime_error(
                "IGL renderer: an instanced draw named a buffer this renderer does not own");
        }

        SubmitDraw(*vertexBuffer, indexBuffer, world, view, projection, primitive, primitiveCount,
                   std::max(1, instanceCount), params);
    }

    // ---------------------------------------------------------------------------------------------
    // 2D
    // ---------------------------------------------------------------------------------------------

    void IglRenderer::DrawSpriteBatchEXT(const std::vector<IglSpriteVertex>& vertices,
                                          const std::vector<std::uint16_t>& indices,
                                          const ITextureRenderer* texture, const int filter,
                                          const int addressU, const int addressV,
                                          const Matrix& transform, Effect* customEffect)
    {
        if (vertices.empty() || indices.empty())
            return;

        EnsurePassOpen();

        IglBoundTarget& target = CurrentTarget();
        const bool isBackBuffer = boundTarget_ == backBufferTarget_.get();
        const int logicalWidth = isBackBuffer ? logicalWidth_ : target.GetTargetWidth();
        const int logicalHeight = isBackBuffer ? logicalHeight_ : target.GetTargetHeight();
        if (logicalWidth <= 0 || logicalHeight <= 0)
            return;

        // Sprites are authored with a top-left origin and Y growing downwards; clip space has Y up,
        // so the ortho projection flips it. An off-screen target is additionally rendered flipped
        // (see FillEffectUniforms), and the two flips cancel, which is exactly why a render target
        // both reads back and samples in the same orientation as an uploaded texture.
        const Matrix projection = Matrix::CreateOrthographicOffCenter(
            0.0f, static_cast<float>(logicalWidth), static_cast<float>(logicalHeight), 0.0f, 0.0f,
            1.0f);

        // The sampler slot SpriteBatch draws through is unit 0; its state comes from the batch's own
        // SamplerState rather than from GraphicsDevice.SamplerStates.
        const SamplerTracking previousSampler = samplers_[TextureUnit::Texture0];
        SamplerTracking& spriteSampler = samplers_[TextureUnit::Texture0];
        spriteSampler.filter = filter;
        spriteSampler.addressU = addressU;
        spriteSampler.addressV = addressV;

        GpuDrawParams params;
        params.vertexColorEnabled = true;
        params.textureEnabled = texture != nullptr;
        params.texture0 = texture;
        params.lightingEnabled = false;
        if (customEffect != nullptr)
            params.customEffectRenderer = customEffect->GetEffectRendererPtr();

        const IglDynamicBufferPool::Allocation vertexAllocation = dynamicVertexPool_->Upload(
            vertices.data(), vertices.size() * sizeof(IglSpriteVertex), sizeof(IglSpriteVertex));
        const IglDynamicBufferPool::Allocation indexAllocation = dynamicIndexPool_->Upload(
            indices.data(), indices.size() * sizeof(std::uint16_t), sizeof(std::uint32_t));

        std::vector<igl::VertexAttribute> attributes;
        std::uint32_t attributeMask = 0;
        (void)DescribeStrideLayoutForDraw(sizeof(IglSpriteVertex), attributes, attributeMask);

        igl::VertexInputBinding binding;
        binding.stride = sizeof(IglSpriteVertex);
        binding.sampleFunction = igl::VertexSampleFunction::PerVertex;

        std::uint64_t vertexInputId = 0;
        const std::shared_ptr<igl::IVertexInputState> vertexInput =
            AcquireVertexInputState(attributes, {binding}, vertexInputId);

        const int colorAttachmentCount = std::max(1, target.GetColorAttachmentCount());

        std::shared_ptr<igl::IShaderStages> stages;
        std::uint64_t programId = 0;
        auto* customRenderer = dynamic_cast<IglEffectRenderer*>(params.customEffectRenderer);
        if (customRenderer != nullptr && customRenderer->IsValid())
        {
            stages = customRenderer->GetStages();
            programId = customRenderer->GetProgramId();
        }
        else
        {
            customRenderer = nullptr;
            params.customEffectRenderer = nullptr;
            stages = AcquireBuiltInShader(attributeMask, colorAttachmentCount);
        }

        PipelineKey key;
        key.vertexInputId = vertexInputId;
        key.programId = programId;
        key.attributeMask = attributeMask;
        key.topology = static_cast<std::uint8_t>(igl::PrimitiveType::Triangle);
        key.colorAttachmentCount = static_cast<std::uint8_t>(colorAttachmentCount);
        key.cullMode = static_cast<std::uint8_t>(igl::CullMode::Disabled);
        key.fillMode = static_cast<std::uint8_t>(ToIglFillMode(fillMode_));
        key.sampleCount = static_cast<std::uint8_t>(std::max(1, target.GetTargetSampleCount()));
        key.blendEnabled = blendEnabled_ ? 1 : 0;
        key.srcRgb = static_cast<std::uint8_t>(ToIglBlendFactor(blendColorSrc_));
        key.dstRgb = static_cast<std::uint8_t>(ToIglBlendFactor(blendColorDst_));
        key.srcAlpha = static_cast<std::uint8_t>(ToIglBlendFactor(blendAlphaSrc_));
        key.dstAlpha = static_cast<std::uint8_t>(ToIglBlendFactor(blendAlphaDst_));
        key.opRgb = static_cast<std::uint8_t>(ToIglBlendOp(blendColorFunc_));
        key.opAlpha = static_cast<std::uint8_t>(ToIglBlendOp(blendAlphaFunc_));
        for (int i = 0; i < 4; ++i)
        {
            key.colorWriteMask[static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>(blendWriteState_.colorWriteChannels[i]);
        }

        const std::shared_ptr<igl::IFramebuffer>& framebuffer = target.GetFramebuffer();
        for (int i = 0; i < colorAttachmentCount; ++i)
        {
            const std::shared_ptr<igl::ITexture> attachment =
                framebuffer->getColorAttachment(static_cast<std::size_t>(i));
            key.colorFormat[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(
                attachment ? attachment->getFormat() : igl::TextureFormat::RGBA_UNorm8);
        }
        const std::shared_ptr<igl::ITexture> depthAttachment = framebuffer->getDepthAttachment();
        key.depthFormat = static_cast<std::uint8_t>(
            depthAttachment ? depthAttachment->getFormat() : igl::TextureFormat::Invalid);
        const std::shared_ptr<igl::ITexture> stencilAttachment = framebuffer->getStencilAttachment();
        key.stencilFormat = static_cast<std::uint8_t>(
            stencilAttachment ? stencilAttachment->getFormat() : igl::TextureFormat::Invalid);

        const std::shared_ptr<igl::IRenderPipelineState> pipeline =
            AcquirePipeline(key, vertexInput, stages, customRenderer);

        encoder_->bindRenderPipelineState(pipeline);
        encoder_->bindDepthStencilState(AcquireDepthStencilState());
        encoder_->bindVertexBuffer(0, *vertexAllocation.buffer, vertexAllocation.offset);

        IglEffectUniforms uniforms;
        FillEffectUniforms(Matrix::getIdentityProperty(), transform, projection, params, uniforms);
        BindEffectResources(*encoder_, params, uniforms, nullptr);

        if (customRenderer != nullptr)
            ApplyCustomEffectUniforms(*encoder_, *pipeline, *customRenderer);

        encoder_->bindIndexBuffer(*indexAllocation.buffer, igl::IndexFormat::UInt16,
                                  indexAllocation.offset);
        encoder_->drawIndexed(indices.size(), 1, 0, 0, 0);

        samplers_[TextureUnit::Texture0] = previousSampler;
    }
}
