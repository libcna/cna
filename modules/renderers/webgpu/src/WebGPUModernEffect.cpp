// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu_modern_graphics.md WMG-0006: the descriptor binding contract for a WGSL
// `ShaderEffect` -- the one CNA's generated WGSL (tools/shader_package) and its Vulkan SPIR-V share,
// so one portable package drives both renderers.
//
// THE CONTRACT (also in docs/webgpu-renderer.md)
//
//   @group(0) @binding(0)   the draw's own texture: GpuDrawParams::texture0, or the sprite's
//   @group(0) @binding(32)  its sampler -- SamplerStates[0]
//   @group(1) @binding(0..3)    sampled 2D units 0..3     (ShaderEffect::SetTexture(unit, Texture2D))
//   @group(1) @binding(4..7)    cube units 0..3
//   @group(1) @binding(8..11)   volume units 0..3
//   @group(1) @binding(12..15)  the float/vec2/vec3/mat4 uniform arrays
//   @group(1) @binding(16..18)  sampled 2D-array units 0..2
//   @group(1) @binding(19)      the six engine-owned named matrices
//   @group(1) @binding(32 + b)  the sampler for the texture at binding b
//   @group(2) @binding(n)       storage buffers bound for the draw
//   @group(3) @binding(0)       the 128-byte scalar block (viewport size, matrix, vector, scalar)
//
// A sampler's state is the XNA SamplerState of the unit its texture belongs to, captured at the
// draw call. Every resource is resolved and retained at the draw call too: this renderer records a
// frame and issues it later, so a binding read at replay would be whatever the game bound last.

#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::WebGPU
{
    namespace
    {
        [[nodiscard]] WGPUStringView Label(const char* text)
        {
            return WGPUStringView{text, text != nullptr ? std::strlen(text) : 0};
        }

        [[nodiscard]] WebGPUSampledTextureEXT ResolveSamplable(const ITextureRenderer* texture)
        {
            const auto* samplable =
                texture != nullptr ? dynamic_cast<const IWebGPUSamplable*>(texture) : nullptr;
            return samplable != nullptr ? samplable->Sampled() : WebGPUSampledTextureEXT{};
        }

        [[nodiscard]] WebGPUSampledTextureEXT ResolveCubeSamplable(const ITextureCubeRenderer* texture)
        {
            const auto* samplable =
                texture != nullptr ? dynamic_cast<const IWebGPUCubeSamplable*>(texture) : nullptr;
            return samplable != nullptr ? samplable->SampledCube() : WebGPUSampledTextureEXT{};
        }

        [[nodiscard]] WebGPUSampledTextureEXT ResolveVolumeSamplable(const ITexture3DRenderer* texture)
        {
            const auto* volume = dynamic_cast<const WebGPUTexture3DRenderer*>(texture);
            if (volume == nullptr) return {};
            return volume->SampledEXT();
        }

        /// The XNA sampler unit whose SamplerState governs the texture at set-1 binding @p binding.
        [[nodiscard]] int SamplerUnitForBinding(std::uint32_t binding)
        {
            if (binding < 4) return static_cast<int>(binding);              // 2D units 0..3
            if (binding < 8) return static_cast<int>(binding - 4);          // cube units 0..3
            if (binding < 12) return static_cast<int>(binding - 8);         // volume units 0..3
            if (binding >= 16 && binding <= 18) return static_cast<int>(binding - 16);
            return 0;
        }
    }

    bool WebGPUEffectRenderer::IsDescriptorContractEXT(const WgslModuleReflection& vertex,
                                                       const WgslModuleReflection& fragment)
    {
        for (const WgslModuleReflection* stage : {&vertex, &fragment})
        {
            // The legacy contract's one declared block. Nothing in the descriptor contract puts a
            // buffer there (group 0 carries the draw's texture and its sampler).
            if (const WgslResourceBinding* legacyBlock = stage->FindResource(0, 0);
                legacyBlock != nullptr && legacyBlock->kind == WgslResourceKind::UniformBuffer)
                return false;
        }
        for (const WgslModuleReflection* stage : {&vertex, &fragment})
        {
            for (const WgslResourceBinding& r : stage->resources)
            {
                if (r.group >= 1) return true;
                if (r.binding >= static_cast<std::uint32_t>(kDescriptorSamplerOffset)) return true;
            }
        }
        return false;
    }

    bool WebGPUEffectRenderer::CompileDescriptorProgramEXT(
        const std::string& vertSrc, const std::string& fragSrc,
        const WgslModuleReflection& vertexReflection, const WgslModuleReflection& fragmentReflection)
    {
        const WgslEntryPoint* vertexEntry = vertexReflection.FindEntryPoint(WGPUShaderStage_Vertex);
        const WgslEntryPoint* fragmentEntry =
            fragmentReflection.FindEntryPoint(WGPUShaderStage_Fragment);
        if (vertexEntry == nullptr || fragmentEntry == nullptr)
        {
            compileError_ = "WebGPU ShaderEffect: the WGSL declares no @vertex/@fragment entry point";
            return false;
        }

        std::string error;
        programLayout_ = WebGPUProgramLayoutEXT::Create(
            owner_->DeviceEXT(),
            {{&vertexReflection, WGPUShaderStage_Vertex},
             {&fragmentReflection, WGPUShaderStage_Fragment}},
            error);
        if (programLayout_ == nullptr)
        {
            compileError_ = "WebGPU ShaderEffect: " + error;
            return false;
        }

        vertexModule_ = owner_->CreateShaderModuleCheckedEXT(
            vertSrc, "CNA WebGPU ShaderEffect VS", compileError_);
        if (vertexModule_ == nullptr)
        {
            programLayout_.reset();
            return false;
        }
        fragmentModule_ = owner_->CreateShaderModuleCheckedEXT(
            fragSrc, "CNA WebGPU ShaderEffect FS", compileError_);
        if (fragmentModule_ == nullptr)
        {
            wgpuShaderModuleRelease(vertexModule_);
            vertexModule_ = nullptr;
            programLayout_.reset();
            return false;
        }

        contract_ = ContractEXT::Descriptor;
        vertexEntryPoint_ = vertexEntry->name;
        fragmentEntryPoint_ = fragmentEntry->name;
        colorOutputCount_ = std::max<std::uint32_t>(1, fragmentEntry->colorOutputCount);
        vertexInputLocations_.clear();
        for (const WgslVertexInput& input : vertexEntry->vertexInputs)
            vertexInputLocations_.push_back(input.location);
        std::sort(vertexInputLocations_.begin(), vertexInputLocations_.end());
        vertexInputLocations_.erase(
            std::unique(vertexInputLocations_.begin(), vertexInputLocations_.end()),
            vertexInputLocations_.end());
        samplesTexture_ = false;
        valid_ = true;
        return true;
    }

    WebGPUEffectArraysEXT& WebGPUEffectRenderer::MutableArraysEXT()
    {
        // Copy-on-write: a queued draw holds the snapshot it captured, so a later setter must not
        // reach into it. Nothing is copied while no draw is holding one.
        if (arrays_ == nullptr)
            arrays_ = std::make_shared<WebGPUEffectArraysEXT>();
        else if (arrays_.use_count() > 1)
            arrays_ = std::make_shared<WebGPUEffectArraysEXT>(*arrays_);
        return const_cast<WebGPUEffectArraysEXT&>(*arrays_);
    }

    void WebGPUEffectRenderer::WriteUniformArrayEXT(const char* setter, int elementFloats,
                                                    const float* values, int count)
    {
        if (count < 0 || count > kDescriptorArrayCapacity)
            throw System::NotSupportedException(
                std::string("CNA WebGPU ShaderEffect::") + setter + ": this renderer carries " +
                std::to_string(kDescriptorArrayCapacity) +
                " elements per uniform array; " + std::to_string(count) + " were supplied");
        if (count == 0) return;
        if (values == nullptr)
            throw System::NotSupportedException(
                std::string("CNA WebGPU ShaderEffect::") + setter + ": the values are null");
        WebGPUEffectArraysEXT& arrays = MutableArraysEXT();
        std::vector<float>* destination = nullptr;
        switch (elementFloats)
        {
        case 1: destination = &arrays.floats; break;
        case 2: destination = &arrays.vec2s; break;
        case 3: destination = &arrays.vec3s; break;
        default: destination = &arrays.mat4s; break;
        }
        const std::size_t floats = static_cast<std::size_t>(count) * elementFloats;
        if (destination->size() < floats) destination->resize(floats, 0.0f);
        std::memcpy(destination->data(), values, floats * sizeof(float));
    }

    void WebGPUEffectRenderer::BindTextureCube(int unit, ITextureCubeRenderer* texture)
    {
        if (contract_ != ContractEXT::Descriptor)
            throw System::NotSupportedException(
                "CNA WebGPU: a custom WGSL ShaderEffect following the WEBGPU-76 contract has no cube "
                "sampler binding; a package that declares one follows the descriptor contract");
        if (unit < 0 || unit >= kDescriptorMaxTextures)
            throw System::NotSupportedException(
                "CNA WebGPU: cube sampler units are 0.." +
                std::to_string(kDescriptorMaxTextures - 1) + "; unit " + std::to_string(unit) +
                " was asked for");
        descriptorCubes_[static_cast<std::size_t>(unit)] = ResolveCubeSamplable(texture);
    }

    void WebGPUEffectRenderer::BindTexture3D(int unit, ITexture3DRenderer* texture)
    {
        if (contract_ != ContractEXT::Descriptor)
            throw System::NotSupportedException(
                "CNA WebGPU: a custom WGSL ShaderEffect following the WEBGPU-76 contract has no "
                "volume sampler binding; a package that declares one follows the descriptor contract");
        if (unit < 0 || unit >= kDescriptorMaxTextures)
            throw System::NotSupportedException(
                "CNA WebGPU: volume sampler units are 0.." +
                std::to_string(kDescriptorMaxTextures - 1) + "; unit " + std::to_string(unit) +
                " was asked for");
        descriptorVolumes_[static_cast<std::size_t>(unit)] = ResolveVolumeSamplable(texture);
    }

    bool WebGPUEffectRenderer::BindTexture2DArrayEXT(
        int unit, std::shared_ptr<ITexture2DArrayRenderer> texture)
    {
        if (contract_ != ContractEXT::Descriptor) return false;
        if (unit < 0 || unit >= static_cast<int>(descriptorTextureArrays_.size())) return false;
        if (texture == nullptr)
        {
            descriptorTextureArrays_[static_cast<std::size_t>(unit)] = WebGPUSampledTextureEXT{};
            return true;
        }
        auto native = std::dynamic_pointer_cast<WebGPUTexture2DArrayRenderer>(texture);
        if (native == nullptr || !native->IsOwnedByEXT(owner_)) return false;
        descriptorTextureArrays_[static_cast<std::size_t>(unit)] = native->SampledEXT();
        return true;
    }

    bool WebGPUEffectRenderer::BindStorageTexture2DEXT(
        int unit, std::shared_ptr<IStorageTexture2DRenderer> texture)
    {
        if (contract_ != ContractEXT::Descriptor) return false;
        if (unit < 0 || unit >= kDescriptorMaxTextures) return false;
        if (texture == nullptr)
        {
            descriptorTextures_[static_cast<std::size_t>(unit)] = WebGPUSampledTextureEXT{};
            return true;
        }
        auto native = std::dynamic_pointer_cast<WebGPUStorageTexture2DRenderer>(texture);
        if (native == nullptr || !native->IsOwnedByEXT(owner_)) return false;
        const WebGPUSampledTextureEXT sampled = native->SampledEXT();
        if (!sampled) return false;   // the texture declared no Sampled usage
        descriptorTextures_[static_cast<std::size_t>(unit)] = sampled;
        return true;
    }

    std::shared_ptr<WebGPUDescriptorEffectSnapshotEXT>
        WebGPUEffectRenderer::CaptureDescriptorStateEXT(const WebGPURenderer& owner) const
    {
        auto snapshot = std::make_shared<WebGPUDescriptorEffectSnapshotEXT>();
        snapshot->scalars = scalarBlock_;
        snapshot->arrays = arrays_;
        snapshot->textures2D = descriptorTextures_;
        snapshot->cubes = descriptorCubes_;
        snapshot->volumes = descriptorVolumes_;
        snapshot->textureArrays = descriptorTextureArrays_;
        snapshot->samplers = owner.SamplerStatesEXT();
        snapshot->storageBuffers = owner.DrawStorageBuffersEXT();
        return snapshot;
    }

    void WebGPUEffectRenderer::ApplyDrawMatrixEXT(WebGPUDescriptorEffectSnapshotEXT& snapshot,
                                                  const float* wvp) const
    {
        // VULKAN-255's rule, kept identical here: the draw's own world*view*projection reaches the
        // one matrix slot only where the game did not set it itself, so the two routes never fight.
        if (!matrixSetByGame_ && wvp != nullptr)
            std::memcpy(snapshot.scalars.data() + 4, wvp, 64);
    }

    // ---- the renderer's half --------------------------------------------------------------------

    std::array<WebGPUSamplerStateEXT, 16> WebGPURenderer::SamplerStatesEXT() const
    {
        std::array<WebGPUSamplerStateEXT, 16> states{};
        for (std::size_t i = 0; i < states.size() && i < slotSamplers_.size(); ++i)
        {
            states[i].filter = slotSamplers_[i].filter;
            states[i].addressU = slotSamplers_[i].addressU;
            states[i].addressV = slotSamplers_[i].addressV;
            states[i].addressW = slotSamplers_[i].addressW;
            states[i].maxMipLevel = slotSamplers_[i].maxMipLevel;
            states[i].maxAnisotropy = slotSamplers_[i].maxAnisotropy;
        }
        return states;
    }

    std::vector<std::pair<std::uint32_t, std::shared_ptr<WebGPUStorageBufferRenderer>>>
        WebGPURenderer::DrawStorageBuffersEXT() const
    {
        return drawStorageBuffers_;
    }

    void WebGPURenderer::BindStorageBufferForDrawEXT(int binding, const IStorageBufferRenderer& buffer)
    {
        if (binding < 0)
            throw std::out_of_range("CNA WebGPU: a draw storage binding must not be negative");
        auto* native = dynamic_cast<WebGPUStorageBufferRenderer*>(
            const_cast<IStorageBufferRenderer*>(&buffer));
        if (native == nullptr || !native->IsOwnedByEXT(this))
            throw std::invalid_argument(
                "CNA WebGPU: the storage buffer bound for a draw belongs to another renderer");
        auto shared = std::dynamic_pointer_cast<WebGPUStorageBufferRenderer>(native->shared_from_this());
        const auto slot = static_cast<std::uint32_t>(binding);
        for (auto& entry : drawStorageBuffers_)
        {
            if (entry.first == slot)
            {
                entry.second = std::move(shared);
                return;
            }
        }
        drawStorageBuffers_.emplace_back(slot, std::move(shared));
    }

    WGPUSampler WebGPURenderer::SamplerForStateEXT(const WebGPUSamplerStateEXT& state)
    {
        return GetOrCreateSlotSampler(state.filter, state.addressU, state.addressV, state.addressW,
                                      state.maxMipLevel, state.maxAnisotropy, "ShaderEffect");
    }

    WebGPUSampledTextureEXT WebGPURenderer::NeutralTextureForDimensionEXT(
        WGPUTextureViewDimension dimension)
    {
        const auto makeWhite = [&](WGPUTextureDimension textureDimension,
                                   WGPUTextureViewDimension viewDimension, std::uint32_t layers,
                                   std::uint32_t depth, WGPUTexture& texture, WGPUTextureView& view,
                                   const char* label) {
            if (view != nullptr) return WebGPUSampledTextureEXT{
                view, std::make_shared<const WebGPUSampledResourceEXT>(texture, view)};
            WGPUTextureDescriptor descriptor{};
            descriptor.label = Label(label);
            descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
            descriptor.dimension = textureDimension;
            descriptor.size = {1, 1, std::max(layers, depth)};
            descriptor.format = WGPUTextureFormat_RGBA8Unorm;
            descriptor.mipLevelCount = 1;
            descriptor.sampleCount = 1;
            texture = wgpuDeviceCreateTexture(device_, &descriptor);
            const std::uint32_t faces = std::max(layers, 1u);
            const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
            for (std::uint32_t layer = 0; layer < std::max(faces, depth); ++layer)
                WriteTextureRegionEXT(texture, 0, static_cast<int>(layer), 0, 0, 1, 1, 4,
                                      white.data());
            WGPUTextureViewDescriptor viewDescriptor{};
            viewDescriptor.label = Label(label);
            viewDescriptor.format = WGPUTextureFormat_RGBA8Unorm;
            viewDescriptor.dimension = viewDimension;
            viewDescriptor.baseMipLevel = 0;
            viewDescriptor.mipLevelCount = 1;
            viewDescriptor.baseArrayLayer = 0;
            viewDescriptor.arrayLayerCount = textureDimension == WGPUTextureDimension_3D ? 1 : faces;
            viewDescriptor.aspect = WGPUTextureAspect_All;
            view = wgpuTextureCreateView(texture, &viewDescriptor);
            return WebGPUSampledTextureEXT{
                view, std::make_shared<const WebGPUSampledResourceEXT>(texture, view)};
        };

        switch (dimension)
        {
        case WGPUTextureViewDimension_Cube:
        case WGPUTextureViewDimension_CubeArray:
            return makeWhite(WGPUTextureDimension_2D, WGPUTextureViewDimension_Cube, 6, 1,
                             neutralCubeTexture_, neutralCubeView_, "CNA WebGPU neutral cube");
        case WGPUTextureViewDimension_3D:
            return makeWhite(WGPUTextureDimension_3D, WGPUTextureViewDimension_3D, 1, 1,
                             neutralVolumeTexture_, neutralVolumeView_, "CNA WebGPU neutral volume");
        case WGPUTextureViewDimension_2DArray:
            return makeWhite(WGPUTextureDimension_2D, WGPUTextureViewDimension_2DArray, 1, 1,
                             neutralArrayTexture_, neutralArrayView_, "CNA WebGPU neutral array");
        default:
            EnsurePbrDefaultTextures();
            return ResolveSamplable(pbrDefaultWhiteTexture_.get());
        }
    }

    std::array<WGPUBindGroup, 4> WebGPURenderer::BuildDescriptorEffectBindGroupsEXT(
        const WebGPUEffectRenderer& effect, const WebGPUDescriptorEffectSnapshotEXT& snapshot,
        std::vector<WGPUBuffer>& transient)
    {
        const WebGPUProgramLayoutEXT& layout = *effect.ProgramLayoutEXT();
        std::array<WGPUBindGroup, 4> groups{};

        // One transient uniform (or storage) buffer per declared block, written from the snapshot.
        const auto makeBuffer = [&](const void* data, std::uint64_t byteSize, bool storage) {
            const std::uint64_t size = std::max<std::uint64_t>((byteSize + 15u) & ~std::uint64_t{15u}, 16u);
            WGPUBuffer buffer = AcquireTransientBuffer(
                static_cast<WGPUBufferUsage>((storage ? WGPUBufferUsage_Storage
                                                      : WGPUBufferUsage_Uniform) |
                                             WGPUBufferUsage_CopyDst),
                size);
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size), 0);
            if (data != nullptr && byteSize > 0)
                std::memcpy(bytes.data(), data, static_cast<std::size_t>(byteSize));
            wgpuQueueWriteBuffer(queue_, buffer, 0, bytes.data(), bytes.size());
            transient.push_back(buffer);
            return std::pair<WGPUBuffer, std::uint64_t>{buffer, size};
        };

        // The uniform arrays. A block the WGSL declares as storage is tightly packed (std430); one
        // it declares as uniform carries each element in sixteen bytes (std140) -- the two layouts
        // the generator produces, so the bytes always mean what the shader reads.
        const auto arrayBytes = [&](int elementFloats, bool storage) {
            static thread_local std::vector<float> packed;
            packed.clear();
            const WebGPUEffectArraysEXT* arrays = snapshot.arrays.get();
            const std::vector<float>* source = nullptr;
            if (arrays != nullptr)
            {
                switch (elementFloats)
                {
                case 1: source = &arrays->floats; break;
                case 2: source = &arrays->vec2s; break;
                case 3: source = &arrays->vec3s; break;
                default: source = &arrays->mat4s; break;
                }
            }
            const int stride = storage ? elementFloats : (elementFloats == 16 ? 16 : 4);
            packed.assign(static_cast<std::size_t>(WebGPUEffectRenderer::kDescriptorArrayCapacity) *
                              stride, 0.0f);
            if (source != nullptr)
            {
                const std::size_t elements = std::min<std::size_t>(
                    source->size() / static_cast<std::size_t>(elementFloats),
                    static_cast<std::size_t>(WebGPUEffectRenderer::kDescriptorArrayCapacity));
                for (std::size_t e = 0; e < elements; ++e)
                    std::memcpy(packed.data() + e * stride,
                                source->data() + e * static_cast<std::size_t>(elementFloats),
                                static_cast<std::size_t>(elementFloats) * sizeof(float));
            }
            return &packed;
        };

        for (std::uint32_t g = 0; g < layout.GroupCount(); ++g)
        {
            std::vector<WGPUBindGroupEntry> entries;
            for (const WebGPUBindingSlotEXT& slot : layout.Group(g))
            {
                WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
                entry.binding = slot.binding;
                switch (slot.kind)
                {
                case WgslResourceKind::Sampler:
                case WgslResourceKind::ComparisonSampler:
                {
                    const int unit = g == 0
                        ? 0
                        : SamplerUnitForBinding(slot.binding -
                                                static_cast<std::uint32_t>(WebGPUEffectRenderer::kDescriptorSamplerOffset));
                    entry.sampler = SamplerForStateEXT(
                        snapshot.samplers[static_cast<std::size_t>(std::clamp(unit, 0, 15))]);
                    break;
                }
                case WgslResourceKind::SampledTexture:
                {
                    WebGPUSampledTextureEXT texture;
                    if (g == 0)
                        texture = snapshot.primaryTexture;
                    else
                    {
                        const std::uint32_t b = slot.binding;
                        if (b < 4) texture = snapshot.textures2D[b];
                        else if (b < 8) texture = snapshot.cubes[b - 4];
                        else if (b < 12) texture = snapshot.volumes[b - 8];
                        else if (b >= 16 && b <= 18) texture = snapshot.textureArrays[b - 16];
                    }
                    if (!texture)
                    {
                        // Never nothing: a pipeline statically uses every binding its layout
                        // declares, so an unbound unit gets the renderer's own neutral texture of
                        // the right shape -- the same rule Vulkan's typed fillers follow.
                        texture = NeutralTextureForDimensionEXT(slot.viewDimension);
                    }
                    entry.textureView = texture.View();
                    break;
                }
                case WgslResourceKind::UniformBuffer:
                case WgslResourceKind::StorageBuffer:
                case WgslResourceKind::ReadOnlyStorageBuffer:
                {
                    const bool storage = slot.kind != WgslResourceKind::UniformBuffer;
                    if (g == static_cast<std::uint32_t>(WebGPUEffectRenderer::kDescriptorScalarGroup) && slot.binding == 0)
                    {
                        const auto [buffer, size] =
                            makeBuffer(snapshot.scalars.data(), snapshot.scalars.size() * sizeof(float),
                                       storage);
                        entry.buffer = buffer;
                        entry.size = size;
                    }
                    else if (g == 2)
                    {
                        std::shared_ptr<WebGPUStorageBufferRenderer> bound;
                        for (const auto& [binding, buffer] : snapshot.storageBuffers)
                            if (binding == slot.binding) bound = buffer;
                        if (bound == nullptr)
                            throw System::NotSupportedException(
                                "CNA WebGPU: this ShaderEffect reads a storage buffer at @group(2) "
                                "@binding(" + std::to_string(slot.binding) +
                                ") and none was bound for the draw");
                        entry.buffer = bound->Buffer();
                        entry.size = bound->NativeSize();
                    }
                    else if (slot.binding == static_cast<std::uint32_t>(WebGPUEffectRenderer::kDescriptorEngineMatrixBinding))
                    {
                        const float* matrices = snapshot.arrays != nullptr
                            ? snapshot.arrays->engineMatrices.data() : nullptr;
                        static constexpr std::uint64_t kEngineMatrixBytes = 6 * 64;
                        const auto [buffer, size] = makeBuffer(matrices, kEngineMatrixBytes, storage);
                        entry.buffer = buffer;
                        entry.size = size;
                    }
                    else
                    {
                        const int elementFloats =
                            slot.binding == static_cast<std::uint32_t>(WebGPUEffectRenderer::kDescriptorFloatArrayBinding) ? 1
                            : slot.binding == static_cast<std::uint32_t>(WebGPUEffectRenderer::kDescriptorFloatArrayBinding) + 1 ? 2
                            : slot.binding == static_cast<std::uint32_t>(WebGPUEffectRenderer::kDescriptorFloatArrayBinding) + 2 ? 3
                            : 16;
                        const std::vector<float>* packed = arrayBytes(elementFloats, storage);
                        const auto [buffer, size] =
                            makeBuffer(packed->data(), packed->size() * sizeof(float), storage);
                        entry.buffer = buffer;
                        entry.size = size;
                    }
                    break;
                }
                case WgslResourceKind::StorageTexture:
                    throw System::NotSupportedException(
                        "CNA WebGPU: a ShaderEffect declares a storage image at @group(" +
                        std::to_string(g) + ") @binding(" + std::to_string(slot.binding) +
                        "); a graphics stage on this renderer samples storage textures instead");
                }
                entries.push_back(entry);
            }
            WGPUBindGroupDescriptor descriptor{};
            descriptor.label = Label("CNA WebGPU ShaderEffect BindGroup");
            descriptor.layout = layout.GroupLayout(g);
            descriptor.entryCount = entries.size();
            descriptor.entries = entries.empty() ? nullptr : entries.data();
            groups[g] = wgpuDeviceCreateBindGroup(device_, &descriptor);
        }
        return groups;
    }
}
