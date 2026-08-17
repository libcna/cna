// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Igl/IglRenderer.hpp"

#include "IglConversions.hpp"

#include <igl/Device.h>
#include <igl/NameHandle.h>
#include <igl/Shader.h>
#include <igl/ShaderCreator.h>

#include <algorithm>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::Igl
{
    namespace
    {
        /// FNV-1a over an arbitrary byte range. Used to turn descriptor structs into cache keys.
        [[nodiscard]] std::uint64_t HashBytes(const void* data, const std::size_t size,
                                              std::uint64_t seed = 1469598103934665603ull)
        {
            const auto* bytes = static_cast<const std::uint8_t*>(data);
            for (std::size_t i = 0; i < size; ++i)
            {
                seed ^= bytes[i];
                seed *= 1099511628211ull;
            }
            return seed;
        }

        [[nodiscard]] igl::ColorWriteMask ToIglColorWriteMask(const int colorWriteChannels)
        {
            igl::ColorWriteMask mask = igl::ColorWriteBitsDisabled;
            if (ColorWriteHasRed(colorWriteChannels))   mask |= igl::ColorWriteBitsRed;
            if (ColorWriteHasGreen(colorWriteChannels)) mask |= igl::ColorWriteBitsGreen;
            if (ColorWriteHasBlue(colorWriteChannels))  mask |= igl::ColorWriteBitsBlue;
            if (ColorWriteHasAlpha(colorWriteChannels)) mask |= igl::ColorWriteBitsAlpha;
            return mask;
        }
    }

    bool IglRenderer::PipelineKey::operator==(const PipelineKey& other) const noexcept
    {
        return vertexInputId == other.vertexInputId && programId == other.programId &&
               attributeMask == other.attributeMask && topology == other.topology &&
               colorAttachmentCount == other.colorAttachmentCount && cullMode == other.cullMode &&
               fillMode == other.fillMode && sampleCount == other.sampleCount &&
               blendEnabled == other.blendEnabled && srcRgb == other.srcRgb &&
               dstRgb == other.dstRgb && srcAlpha == other.srcAlpha &&
               dstAlpha == other.dstAlpha && opRgb == other.opRgb && opAlpha == other.opAlpha &&
               colorWriteMask == other.colorWriteMask && colorFormat == other.colorFormat &&
               depthFormat == other.depthFormat && stencilFormat == other.stencilFormat;
    }

    std::size_t IglRenderer::PipelineKeyHash::operator()(const PipelineKey& key) const noexcept
    {
        return static_cast<std::size_t>(HashBytes(&key, sizeof(PipelineKey)));
    }

    bool IglRenderer::DepthStencilTracking::operator==(
        const DepthStencilTracking& other) const noexcept
    {
        return depthEnable == other.depthEnable && depthWriteEnable == other.depthWriteEnable &&
               depthFunc == other.depthFunc && stencilEnable == other.stencilEnable &&
               stencilFunc == other.stencilFunc && stencilPass == other.stencilPass &&
               stencilFail == other.stencilFail && stencilDepthFail == other.stencilDepthFail &&
               stencilMask == other.stencilMask && stencilWriteMask == other.stencilWriteMask &&
               referenceStencil == other.referenceStencil &&
               twoSidedStencilMode == other.twoSidedStencilMode &&
               ccwStencilFunc == other.ccwStencilFunc && ccwStencilPass == other.ccwStencilPass &&
               ccwStencilFail == other.ccwStencilFail &&
               ccwStencilDepthFail == other.ccwStencilDepthFail;
    }

    bool IglRenderer::SamplerTracking::operator==(const SamplerTracking& other) const noexcept
    {
        return filter == other.filter && addressU == other.addressU && addressV == other.addressV &&
               maxAnisotropy == other.maxAnisotropy && maxMipLevel == other.maxMipLevel &&
               lodBias == other.lodBias;
    }

    std::shared_ptr<igl::IVertexInputState> IglRenderer::AcquireVertexInputState(
        const std::vector<igl::VertexAttribute>& attributes,
        const std::vector<igl::VertexInputBinding>& bindings,
        std::uint64_t& outId)
    {
        igl::VertexInputStateDesc desc;
        desc.numAttributes = attributes.size();
        for (std::size_t i = 0; i < attributes.size() && i < igl::IGL_VERTEX_ATTRIBUTES_MAX; ++i)
            desc.attributes[i] = attributes[i];
        desc.numInputBindings = bindings.size();
        for (std::size_t i = 0; i < bindings.size() && i < igl::IGL_BUFFER_BINDINGS_MAX; ++i)
            desc.inputBindings[i] = bindings[i];

        // The descriptor's own name strings are part of its identity on the OpenGL backend (that is
        // how the attribute is located), so they are hashed alongside the numeric fields rather than
        // hashing the POD struct wholesale -- std::string members make that unsafe anyway.
        std::uint64_t hash = 1469598103934665603ull;
        for (const igl::VertexAttribute& attribute : attributes)
        {
            const std::uint32_t format = static_cast<std::uint32_t>(attribute.format);
            const std::uint64_t offset = static_cast<std::uint64_t>(attribute.offset);
            const std::uint64_t bufferIndex = static_cast<std::uint64_t>(attribute.bufferIndex);
            const std::int32_t location = attribute.location;
            hash = HashBytes(&format, sizeof(format), hash);
            hash = HashBytes(&offset, sizeof(offset), hash);
            hash = HashBytes(&bufferIndex, sizeof(bufferIndex), hash);
            hash = HashBytes(&location, sizeof(location), hash);
            hash = HashBytes(attribute.name.data(), attribute.name.size(), hash);
        }
        for (const igl::VertexInputBinding& binding : bindings)
            hash = HashBytes(&binding, sizeof(binding), hash);

        outId = hash;

        const auto cached = vertexInputStates_.find(hash);
        if (cached != vertexInputStates_.end())
            return cached->second;

        igl::Result result;
        std::shared_ptr<igl::IVertexInputState> state =
            GetDevice().createVertexInputState(desc, &result);
        if (!state || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not create a vertex input state (" +
                                     result.message + ")");
        }

        vertexInputStates_.emplace(hash, state);
        return state;
    }

    std::shared_ptr<igl::IShaderStages> IglRenderer::AcquireBuiltInShader(
        const std::uint32_t attributeMask, const int colorAttachmentCount)
    {
        const std::uint64_t key = (static_cast<std::uint64_t>(attributeMask) << 8) |
                                  static_cast<std::uint64_t>(colorAttachmentCount & 0xFF);

        const auto cached = builtInShaders_.find(key);
        if (cached != builtInShaders_.end())
            return cached->second;

        const IglShaderSources sources =
            BuildEffectShaderSources(attributeMask, IsVulkanBackend(), colorAttachmentCount);

        igl::Result result;
        std::shared_ptr<igl::IShaderStages> stages = igl::ShaderStagesCreator::fromModuleStringInput(
            GetDevice(), sources.vertex.c_str(), "main", "CNA effect VS",
            sources.fragment.c_str(), "main", "CNA effect FS", &result);
        if (!stages || !result.isOk())
        {
            throw std::runtime_error(
                "IGL renderer: the built-in effect shader failed to compile (" + result.message +
                ")\n--- vertex ---\n" + sources.vertex + "\n--- fragment ---\n" + sources.fragment);
        }

        builtInShaders_.emplace(key, stages);
        return stages;
    }

    std::shared_ptr<igl::IRenderPipelineState> IglRenderer::AcquirePipeline(
        const PipelineKey& key,
        const std::shared_ptr<igl::IVertexInputState>& vertexInput,
        const std::shared_ptr<igl::IShaderStages>& stages)
    {
        const auto cached = pipelines_.find(key);
        if (cached != pipelines_.end())
            return cached->second;

        igl::RenderPipelineDesc desc;
        desc.topology = static_cast<igl::PrimitiveType>(key.topology);
        desc.vertexInputState = vertexInput;
        desc.shaderStages = stages;
        desc.cullMode = static_cast<igl::CullMode>(key.cullMode);
        desc.polygonFillMode = static_cast<igl::PolygonFillMode>(key.fillMode);
        desc.sampleCount = key.sampleCount;
        desc.debugName = igl::genNameHandle("CNA pipeline");

        // XNA's front face is clockwise in a left-handed, Y-down projection. Both IGL backends
        // present a bottom-left origin (the Vulkan encoder flips the viewport height to match
        // OpenGL), so the winding that XNA calls "clockwise" arrives here counter-clockwise. An
        // off-screen target additionally renders with a flipped Y projection -- see
        // IglRenderer::SubmitDraw -- which reverses the winding once more, and the pipeline follows
        // that so CullClockwiseFace keeps culling the same triangles either way.
        desc.frontFaceWinding = boundTarget_ != nullptr && boundTarget_ != backBufferTarget_.get()
                                    ? igl::WindingMode::Clockwise
                                    : igl::WindingMode::CounterClockwise;

        desc.targetDesc.colorAttachments.resize(key.colorAttachmentCount);
        for (int i = 0; i < key.colorAttachmentCount; ++i)
        {
            igl::RenderPipelineDesc::TargetDesc::ColorAttachment& attachment =
                desc.targetDesc.colorAttachments[static_cast<std::size_t>(i)];
            attachment.textureFormat = static_cast<igl::TextureFormat>(key.colorFormat[i]);
            attachment.colorWriteMask = ToIglColorWriteMask(key.colorWriteMask[i]);
            attachment.blendEnabled = key.blendEnabled != 0;
            attachment.srcRGBBlendFactor = static_cast<igl::BlendFactor>(key.srcRgb);
            attachment.dstRGBBlendFactor = static_cast<igl::BlendFactor>(key.dstRgb);
            attachment.srcAlphaBlendFactor = static_cast<igl::BlendFactor>(key.srcAlpha);
            attachment.dstAlphaBlendFactor = static_cast<igl::BlendFactor>(key.dstAlpha);
            attachment.rgbBlendOp = static_cast<igl::BlendOp>(key.opRgb);
            attachment.alphaBlendOp = static_cast<igl::BlendOp>(key.opAlpha);
        }
        desc.targetDesc.depthAttachmentFormat = static_cast<igl::TextureFormat>(key.depthFormat);
        desc.targetDesc.stencilAttachmentFormat =
            static_cast<igl::TextureFormat>(key.stencilFormat);

        // OpenGL resolves samplers and uniform blocks by name rather than by an explicit binding
        // qualifier (the generated GLSL targets 4.1 core, where `layout(binding=)` on a uniform
        // block is not yet available). These two maps are what turns the generated names back into
        // the binding numbers the encoder uses, so both backends address the same slots.
        for (std::uint32_t unit = 0; unit < TextureUnit::Count; ++unit)
            desc.fragmentUnitSamplerMap[unit] = igl::genNameHandle(GetSamplerUniformName(unit));
        desc.uniformBlockBindingMap[UniformBufferBinding::Effect] = {
            {igl::genNameHandle(GetUniformBlockName(UniformBufferBinding::Effect)),
             igl::NameHandle{}}};
        desc.uniformBlockBindingMap[UniformBufferBinding::Bones] = {
            {igl::genNameHandle(GetUniformBlockName(UniformBufferBinding::Bones)),
             igl::NameHandle{}}};

        igl::Result result;
        std::shared_ptr<igl::IRenderPipelineState> pipeline =
            GetDevice().createRenderPipeline(desc, &result);
        if (!pipeline || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not create a render pipeline (" +
                                     result.message + ")");
        }

        pipelines_.emplace(key, pipeline);
        return pipeline;
    }

    std::shared_ptr<igl::IDepthStencilState> IglRenderer::AcquireDepthStencilState()
    {
        const std::uint64_t key = HashBytes(&depthStencil_, sizeof(DepthStencilTracking));

        const auto cached = depthStencilStates_.find(key);
        if (cached != depthStencilStates_.end())
            return cached->second;

        igl::DepthStencilStateDesc desc;
        desc.debugName = "CNA depth stencil";
        // XNA's DepthBufferEnable == false means "always pass, never write", which is exactly a
        // comparison of AlwaysPass with depth writes off -- not a separate disable switch.
        desc.compareFunction = depthStencil_.depthEnable
                                   ? ToIglCompareFunction(depthStencil_.depthFunc)
                                   : igl::CompareFunction::AlwaysPass;
        desc.isDepthWriteEnabled = depthStencil_.depthEnable && depthStencil_.depthWriteEnable;

        igl::StencilStateDesc front;
        igl::StencilStateDesc back;
        if (depthStencil_.stencilEnable)
        {
            front.stencilCompareFunction = ToIglCompareFunction(depthStencil_.stencilFunc);
            front.stencilFailureOperation = ToIglStencilOperation(depthStencil_.stencilFail);
            front.depthFailureOperation = ToIglStencilOperation(depthStencil_.stencilDepthFail);
            front.depthStencilPassOperation = ToIglStencilOperation(depthStencil_.stencilPass);
            front.readMask = static_cast<std::uint32_t>(depthStencil_.stencilMask);
            front.writeMask = static_cast<std::uint32_t>(depthStencil_.stencilWriteMask);

            if (depthStencil_.twoSidedStencilMode)
            {
                back.stencilCompareFunction = ToIglCompareFunction(depthStencil_.ccwStencilFunc);
                back.stencilFailureOperation = ToIglStencilOperation(depthStencil_.ccwStencilFail);
                back.depthFailureOperation =
                    ToIglStencilOperation(depthStencil_.ccwStencilDepthFail);
                back.depthStencilPassOperation = ToIglStencilOperation(depthStencil_.ccwStencilPass);
                back.readMask = front.readMask;
                back.writeMask = front.writeMask;
            }
            else
            {
                back = front;
            }
        }

        desc.frontFaceStencil = front;
        desc.backFaceStencil = back;

        igl::Result result;
        std::shared_ptr<igl::IDepthStencilState> state =
            GetDevice().createDepthStencilState(desc, &result);
        if (!state || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not create a depth/stencil state (" +
                                     result.message + ")");
        }

        depthStencilStates_.emplace(key, state);
        return state;
    }

    std::shared_ptr<igl::ISamplerState> IglRenderer::AcquireSamplerState(const int slot)
    {
        const int index = slot >= 0 && slot < kIglTrackedSamplerSlots ? slot : 0;
        const SamplerTracking& tracking = samplers_[static_cast<std::size_t>(index)];
        const std::uint64_t key = HashBytes(&tracking, sizeof(SamplerTracking));

        const auto cached = samplerStates_.find(key);
        if (cached != samplerStates_.end())
            return cached->second;

        igl::SamplerStateDesc desc;
        desc.debugName = "CNA sampler";
        desc.minFilter = ToIglMinFilter(tracking.filter);
        desc.magFilter = ToIglMagFilter(tracking.filter);
        desc.mipFilter = ToIglMipFilter(tracking.filter);
        desc.addressModeU = ToIglAddressMode(tracking.addressU);
        desc.addressModeV = ToIglAddressMode(tracking.addressV);
        desc.addressModeW = ToIglAddressMode(tracking.addressU);
        desc.maxAnisotropic =
            static_cast<std::uint8_t>(tracking.filter == 2 ? std::max(1, tracking.maxAnisotropy) : 1);
        // XNA's SamplerState.MaxMipLevel is "the highest-resolution level that may be sampled", so
        // it is a lower bound on the LOD IGL selects, not an upper one.
        desc.mipLodMin = static_cast<std::uint8_t>(std::clamp(tracking.maxMipLevel, 0, 15));
        desc.mipLodMax = 15;

        igl::Result result;
        std::shared_ptr<igl::ISamplerState> state = GetDevice().createSamplerState(desc, &result);
        if (!state || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not create a sampler state (" +
                                     result.message + ")");
        }

        samplerStates_.emplace(key, state);
        return state;
    }

    igl::ITexture* IglRenderer::ResolveDummyTexture(const bool cube)
    {
        // Vulkan requires every descriptor a shader declares to be bound, and the generated
        // fragment shader declares all seven sampler slots whether or not a given draw uses them.
        // A 1x1 opaque-white image is the neutral stand-in: a slot the shader never reads costs one
        // texel of memory, and a slot it reads by mistake shows white rather than undefined memory.
        std::shared_ptr<igl::ITexture>& slot = cube ? dummyTextureCube_ : dummyTexture2D_;
        if (slot)
            return slot.get();

        const std::uint8_t white[4] = {255, 255, 255, 255};

        igl::TextureDesc desc =
            cube ? igl::TextureDesc::newCube(igl::TextureFormat::RGBA_UNorm8, 1, 1,
                                             igl::TextureDesc::TextureUsageBits::Sampled,
                                             "CNA dummy cube")
                 : igl::TextureDesc::new2D(igl::TextureFormat::RGBA_UNorm8, 1, 1,
                                           igl::TextureDesc::TextureUsageBits::Sampled,
                                           "CNA dummy 2D");

        igl::Result result;
        slot = GetDevice().createTexture(desc, &result);
        if (!slot || !result.isOk())
        {
            throw std::runtime_error("IGL renderer: could not create the dummy texture (" +
                                     result.message + ")");
        }

        if (cube)
        {
            for (std::uint32_t face = 0; face < 6; ++face)
            {
                const igl::TextureRangeDesc range =
                    igl::TextureRangeDesc::newCubeFace(0, 0, 1, 1, face);
                (void)slot->upload(range, white, 0);
            }
        }
        else
        {
            (void)slot->upload(igl::TextureRangeDesc::new2D(0, 0, 1, 1), white, 0);
        }

        return slot.get();
    }
}
