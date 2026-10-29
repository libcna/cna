// SPDX-License-Identifier: MS-PL
//
// The descriptor binding contract for a WGSL `ShaderEffect` -- the one CNA's generated WGSL
// (tools/shader_package) follows -- and the explicit layouts built from a program's own WGSL.
//
// THE CONTRACT (also in docs/webgpu-renderer.md)
//
//   @group(0) @binding(0)   the draw's own texture: GpuDrawParams::texture0, or the sprite's
//   @group(0) @binding(32)  its sampler -- SamplerStates[0]
//   @group(1) @binding(0..3)    sampled 2D units 0..3     (ShaderEffect::SetTexture(unit, Texture2D))
//   @group(1) @binding(4..7)    cube units 0..3
//   @group(1) @binding(8..11)   volume units 0..3
//   @group(1) @binding(12..15)  the float/vec2/vec3/mat4 uniform arrays
//   @group(1) @binding(16..18)  RESERVED -- formerly sampled 2D-array units; never reassigned
//   @group(1) @binding(19)      the six engine-owned named matrices
//   @group(1) @binding(32 + b)  the sampler for the texture at binding b
//   @group(2)                   RESERVED -- formerly draw storage buffers; a ShaderEffect binds
//                               nothing here (the stock lit families use it for shadow reception)
//   @group(3) @binding(0)       the 128-byte scalar block (viewport size, matrix, vector, scalar)
//
// These numbers are a public contract: shipped packages declare them (for example the engine
// matrices at @group(1) @binding(19)), so a retired slot stays a gap and nothing is renumbered.
//
// A sampler's state is the XNA SamplerState of the unit its texture belongs to, captured at the
// draw call. Every resource is resolved and retained at the draw call too: this renderer records a
// frame and issues it later, so a binding read at replay would be whatever the game bound last.
//
// WHY THE LAYOUTS ARE EXPLICIT
//
// WebGPU's `layout: auto` derives a bind-group layout that belongs to the ONE pipeline it came
// from, and a `ShaderEffect` owns many pipelines (one per render-target shape, blend state, vertex
// layout ...) that must all accept the same bind groups. So the layout is built here from the
// program's reflected declarations (WebGPUWgslReflection.hpp) and shared by every pipeline.

#include "CNA/Internal/Renderers/WebGPU/WebGPUDescriptorEffect.hpp"
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
            return 0;
        }
    }

    // ---- WebGPUProgramLayoutEXT ----------------------------------------------------------------

    std::shared_ptr<WebGPUProgramLayoutEXT> WebGPUProgramLayoutEXT::Create(
        WGPUDevice device,
        const std::vector<std::pair<const WgslModuleReflection*, WGPUShaderStage>>& stages,
        std::string& error, const std::uint32_t maxDynamicUniformBlocks)
    {
        std::shared_ptr<WebGPUProgramLayoutEXT> layout(new WebGPUProgramLayoutEXT());
        for (const auto& [reflection, stage] : stages)
        {
            for (const WgslResourceBinding& r : reflection->resources)
            {
                if (r.group >= kMaxGroups)
                {
                    error = "'" + r.name + "' is in @group(" + std::to_string(r.group) +
                            "); WebGPU guarantees groups 0.." + std::to_string(kMaxGroups - 1);
                    return nullptr;
                }
                auto& group = layout->groups_[r.group];
                auto existing = std::find_if(group.begin(), group.end(),
                                             [&](const WebGPUBindingSlotEXT& s) {
                                                 return s.binding == r.binding;
                                             });
                if (existing != group.end())
                {
                    if (existing->kind != r.kind || existing->type != r.type)
                    {
                        error = "@group(" + std::to_string(r.group) + ") @binding(" +
                                std::to_string(r.binding) + ") is '" + existing->type +
                                "' in one stage and '" + r.type + "' in another";
                        return nullptr;
                    }
                    existing->visibility = static_cast<WGPUShaderStage>(existing->visibility | stage);
                    existing->minBindingSize = std::max(existing->minBindingSize, r.minBindingSize);
                    continue;
                }
                WebGPUBindingSlotEXT slot;
                slot.group = r.group;
                slot.binding = r.binding;
                slot.kind = r.kind;
                slot.visibility = stage;
                slot.viewDimension = r.viewDimension;
                slot.sampleType = r.sampleType;
                slot.multisampled = r.multisampled;
                slot.minBindingSize = r.minBindingSize;
                slot.name = r.name;
                slot.type = r.type;
                group.push_back(std::move(slot));
            }
        }

        std::uint32_t used = 0;
        for (std::uint32_t g = 0; g < kMaxGroups; ++g)
        {
            auto& group = layout->groups_[g];
            std::sort(group.begin(), group.end(),
                      [](const WebGPUBindingSlotEXT& a, const WebGPUBindingSlotEXT& b) {
                          return a.binding < b.binding;
                      });
            if (!group.empty()) used = g + 1;
        }
        layout->groupCount_ = used;

        // WEBGPUPERF-0005: all of a program's uniform blocks become
        // dynamic-offset bindings, or none do -- a pipeline layout may hold only so many, and a
        // program past that keeps the per-draw buffers it always had.
        std::uint32_t uniformBlocks = 0;
        for (std::uint32_t g = 0; g < layout->groupCount_; ++g)
            for (const WebGPUBindingSlotEXT& s : layout->groups_[g])
                if (s.kind == WgslResourceKind::UniformBuffer) ++uniformBlocks;
        if (uniformBlocks > 0 && uniformBlocks <= maxDynamicUniformBlocks)
            for (std::uint32_t g = 0; g < layout->groupCount_; ++g)
                for (WebGPUBindingSlotEXT& s : layout->groups_[g])
                    if (s.kind == WgslResourceKind::UniformBuffer) s.dynamicOffset = true;

        for (std::uint32_t g = 0; g < layout->groupCount_; ++g)
        {
            std::vector<WGPUBindGroupLayoutEntry> entries;
            entries.reserve(layout->groups_[g].size());
            for (const WebGPUBindingSlotEXT& s : layout->groups_[g])
            {
                WGPUBindGroupLayoutEntry e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
                e.binding = s.binding;
                e.visibility = s.visibility;
                switch (s.kind)
                {
                case WgslResourceKind::UniformBuffer:
                    e.buffer.type = WGPUBufferBindingType_Uniform;
                    e.buffer.minBindingSize = s.minBindingSize;
                    e.buffer.hasDynamicOffset = s.dynamicOffset;
                    break;
                case WgslResourceKind::ReadOnlyStorageBuffer:
                    e.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
                    e.buffer.minBindingSize = s.minBindingSize;
                    break;
                case WgslResourceKind::Sampler:
                    e.sampler.type = WGPUSamplerBindingType_Filtering;
                    break;
                case WgslResourceKind::ComparisonSampler:
                    e.sampler.type = WGPUSamplerBindingType_Comparison;
                    break;
                case WgslResourceKind::SampledTexture:
                    e.texture.sampleType = s.sampleType;
                    e.texture.viewDimension = s.viewDimension;
                    e.texture.multisampled = s.multisampled ? 1u : 0u;
                    break;
                }
                entries.push_back(e);
            }
            WGPUBindGroupLayoutDescriptor descriptor{};
            descriptor.label = Label("CNA WebGPU ShaderEffect BindGroupLayout");
            descriptor.entryCount = entries.size();
            descriptor.entries = entries.empty() ? nullptr : entries.data();
            layout->layouts_[g] = wgpuDeviceCreateBindGroupLayout(device, &descriptor);
            if (layout->layouts_[g] == nullptr)
            {
                error = "wgpuDeviceCreateBindGroupLayout failed for @group(" + std::to_string(g) + ")";
                return nullptr;
            }
        }

        WGPUPipelineLayoutDescriptor pipelineDescriptor{};
        pipelineDescriptor.label = Label("CNA WebGPU ShaderEffect PipelineLayout");
        pipelineDescriptor.bindGroupLayoutCount = layout->groupCount_;
        pipelineDescriptor.bindGroupLayouts =
            layout->groupCount_ > 0 ? layout->layouts_.data() : nullptr;
        layout->pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device, &pipelineDescriptor);
        if (layout->pipelineLayout_ == nullptr)
        {
            error = "wgpuDeviceCreatePipelineLayout failed";
            return nullptr;
        }
        return layout;
    }

    WebGPUProgramLayoutEXT::~WebGPUProgramLayoutEXT()
    {
        if (pipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(pipelineLayout_);
        for (WGPUBindGroupLayout l : layouts_)
            if (l != nullptr) wgpuBindGroupLayoutRelease(l);
    }

    const WebGPUBindingSlotEXT* WebGPUProgramLayoutEXT::Find(std::uint32_t group,
                                                             std::uint32_t binding) const noexcept
    {
        if (group >= kMaxGroups) return nullptr;
        for (const WebGPUBindingSlotEXT& s : groups_[group])
            if (s.binding == binding) return &s;
        return nullptr;
    }

    // ---- the effect's half ----------------------------------------------------------------------

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

        // A pipeline statically uses every binding its layout declares, so a declaration that no
        // CNA call can fill is refused here, by name, rather than drawn with whatever happens to
        // satisfy the validator. Group 2 carries no ShaderEffect binding on this renderer, and CNA
        // has no texture-array type to put behind a `texture_2d_array`.
        for (const WgslModuleReflection* stage : {&vertexReflection, &fragmentReflection})
        {
            for (const WgslResourceBinding& r : stage->resources)
            {
                if (r.group == 2)
                {
                    compileError_ = "WebGPU ShaderEffect: '" + r.name + "' is at @group(2) @binding(" +
                                    std::to_string(r.binding) +
                                    "); a ShaderEffect binds nothing in @group(2) on this renderer";
                    return false;
                }
                if (r.kind == WgslResourceKind::SampledTexture &&
                    (r.viewDimension == WGPUTextureViewDimension_2DArray ||
                     r.viewDimension == WGPUTextureViewDimension_CubeArray))
                {
                    compileError_ = "WebGPU ShaderEffect: '" + r.name + "' is a " + r.type +
                                    "; CNA has no texture-array type to bind there";
                    return false;
                }
            }
        }

        std::string error;
        // WEBGPUPERF-0005: the uniform blocks as dynamic-offset bindings,
        // so a draw binds them inside the flush's uniform arena and its groups can be cached.
        const std::uint32_t deviceDynamicLimit =
            owner_->DeviceLimitsEXT().maxDynamicUniformBuffersPerPipelineLayout;
        programLayout_ = WebGPUProgramLayoutEXT::Create(
            owner_->DeviceEXT(),
            {{&vertexReflection, WGPUShaderStage_Vertex},
             {&fragmentReflection, WGPUShaderStage_Fragment}},
            error,
            // Eight is core WebGPU's floor, and what DescriptorEffectBindingsEXT holds per group.
            deviceDynamicLimit != WGPU_LIMIT_U32_UNDEFINED && deviceDynamicLimit > 0
                ? std::min(deviceDynamicLimit, 8u) : 8u);
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

    std::shared_ptr<WebGPUDescriptorEffectSnapshotEXT>
        WebGPUEffectRenderer::CaptureDescriptorStateEXT(const WebGPURenderer& owner) const
    {
        auto snapshot = std::make_shared<WebGPUDescriptorEffectSnapshotEXT>();
        snapshot->scalars = scalarBlock_;
        snapshot->arrays = arrays_;
        snapshot->textures2D = descriptorTextures_;
        snapshot->cubes = descriptorCubes_;
        snapshot->volumes = descriptorVolumes_;
        snapshot->samplers = owner.SamplerStatesEXT();
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

    WGPUSampler WebGPURenderer::SamplerForStateEXT(const WebGPUSamplerStateEXT& state)
    {
        return GetOrCreateSlotSampler(state.filter, state.addressU, state.addressV, state.addressW,
                                      state.maxMipLevel, state.maxAnisotropy, "ShaderEffect");
    }

    WebGPUSampledTextureEXT WebGPURenderer::NeutralTextureForDimensionEXT(
        WGPUTextureViewDimension dimension)
    {
        // WEBGPUPERF-0003: one reference object per shape, made with the
        // texture and handed out on every call. A fresh one per call cost two heap allocations per
        // draw, and an object only the binding cache held would read to its sweep as a texture
        // nothing can bind any more.
        const auto makeWhite = [&](WGPUTextureDimension textureDimension,
                                   WGPUTextureViewDimension viewDimension, std::uint32_t layers,
                                   std::uint32_t depth, WGPUTexture& texture, WGPUTextureView& view,
                                   std::shared_ptr<const WebGPUSampledResourceEXT>& keepAlive,
                                   const char* label) {
            if (view != nullptr)
            {
                if (keepAlive == nullptr)
                    keepAlive = std::make_shared<const WebGPUSampledResourceEXT>(texture, view);
                return WebGPUSampledTextureEXT{view, keepAlive};
            }
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
            keepAlive = std::make_shared<const WebGPUSampledResourceEXT>(texture, view);
            return WebGPUSampledTextureEXT{view, keepAlive};
        };

        switch (dimension)
        {
        case WGPUTextureViewDimension_Cube:
            return makeWhite(WGPUTextureDimension_2D, WGPUTextureViewDimension_Cube, 6, 1,
                             neutralCubeTexture_, neutralCubeView_, neutralCubeKeepAliveEXT_,
                             "CNA WebGPU neutral cube");
        case WGPUTextureViewDimension_3D:
            return makeWhite(WGPUTextureDimension_3D, WGPUTextureViewDimension_3D, 1, 1,
                             neutralVolumeTexture_, neutralVolumeView_, neutralVolumeKeepAliveEXT_,
                             "CNA WebGPU neutral volume");
        default:
            EnsurePbrDefaultTextures();
            return ResolveSamplable(pbrDefaultWhiteTexture_.get());
        }
    }

    void WebGPURenderer::ReleaseNeutralTexturesEXT()
    {
        neutralCubeKeepAliveEXT_.reset();
        neutralVolumeKeepAliveEXT_.reset();
        const auto release = [](WGPUTexture& texture, WGPUTextureView& view) {
            if (view != nullptr) wgpuTextureViewRelease(view);
            if (texture != nullptr) wgpuTextureRelease(texture);
            view = nullptr;
            texture = nullptr;
        };
        release(neutralCubeTexture_, neutralCubeView_);
        release(neutralVolumeTexture_, neutralVolumeView_);
    }

    void WebGPURenderer::SetDescriptorEffectBindGroupsEXT(WGPURenderPassEncoder pass,
                                                          const DescriptorEffectBindingsEXT& bindings,
                                                          const std::uint32_t groupCount)
    {
        for (std::uint32_t g = 0; g < groupCount; ++g)
            wgpuRenderPassEncoderSetBindGroup(pass, g, bindings.groups[g], bindings.offsetCounts[g],
                                              bindings.offsetCounts[g] > 0
                                                  ? bindings.offsets[g].data() : nullptr);
    }

    WebGPURenderer::DescriptorEffectBindingsEXT WebGPURenderer::BuildDescriptorEffectBindGroupsEXT(
        const WebGPUEffectRenderer& effect, const WebGPUDescriptorEffectSnapshotEXT& snapshot,
        std::vector<WGPUBuffer>& transient)
    {
        const WebGPUProgramLayoutEXT& layout = *effect.ProgramLayoutEXT();
        DescriptorEffectBindingsEXT result;

        // A block the layout declares dynamic goes into the flush's uniform arena
        // (WEBGPUPERF-0005), keyed by its slot so an unchanged block is
        // appended once per flush rather than once per draw. Any other block keeps its own transient
        // buffer, and the group that binds it is then built for this draw alone.
        const auto bindBlock = [&](const WebGPUBindingSlotEXT& slot, const void* data,
                                   std::uint64_t byteSize, bool storage, WGPUBindGroupEntry& entry,
                                   std::uint32_t group, bool& cacheable) -> const void* {
            const std::uint64_t size = std::max<std::uint64_t>((byteSize + 15u) & ~std::uint64_t{15u}, 16u);
            if (slot.dynamicOffset && !storage)
            {
                const ArenaSliceEXT slice = AppendUniformBlockEXT(
                    &slot, data, static_cast<std::size_t>(data != nullptr ? byteSize : 0),
                    static_cast<std::size_t>(size));
                entry.buffer = slice.buffer;
                entry.size = size;
                result.offsets[group][result.offsetCounts[group]++] = slice.offset;
                return ArenaBlockHandleEXT(slice, size);
            }
            WGPUBuffer buffer = AcquireTransientBuffer(
                static_cast<WGPUBufferUsage>((storage ? WGPUBufferUsage_Storage
                                                      : WGPUBufferUsage_Uniform) |
                                             WGPUBufferUsage_CopyDst),
                size);
            std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size), 0);
            if (data != nullptr && byteSize > 0)
                std::memcpy(bytes.data(), data, static_cast<std::size_t>(byteSize));
            QueueWriteBufferEXT(buffer, 0, bytes.data(), bytes.size());
            transient.push_back(buffer);
            entry.buffer = buffer;
            entry.size = size;
            cacheable = false;
            return nullptr;
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
            const std::vector<WebGPUBindingSlotEXT>& slots = layout.Group(g);
            std::vector<WGPUBindGroupEntry> entries;
            entries.reserve(slots.size());
            BindingCacheKeyEXT key;
            key.layout = layout.GroupLayout(g);
            bool cacheable = slots.size() <= key.handles.size();
            // Reserved up front: the cache is handed pointers into this vector.
            std::vector<WebGPUSampledTextureEXT> textureStorage;
            textureStorage.reserve(slots.size());
            std::vector<const WebGPUSampledTextureEXT*> textures;
            for (const WebGPUBindingSlotEXT& slot : slots)
            {
                WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
                entry.binding = slot.binding;
                const void* handle = nullptr;
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
                    handle = entry.sampler;
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
                    }
                    if (!texture)
                    {
                        // Never nothing: a pipeline statically uses every binding its layout
                        // declares, so an unbound unit gets the renderer's own neutral texture of
                        // the right shape.
                        texture = NeutralTextureForDimensionEXT(slot.viewDimension);
                    }
                    entry.textureView = texture.View();
                    handle = entry.textureView;
                    textureStorage.push_back(std::move(texture));
                    textures.push_back(&textureStorage.back());
                    break;
                }
                case WgslResourceKind::UniformBuffer:
                case WgslResourceKind::ReadOnlyStorageBuffer:
                {
                    const bool storage = slot.kind != WgslResourceKind::UniformBuffer;
                    if (g == static_cast<std::uint32_t>(WebGPUEffectRenderer::kDescriptorScalarGroup) && slot.binding == 0)
                    {
                        handle = bindBlock(slot, snapshot.scalars.data(),
                                           snapshot.scalars.size() * sizeof(float), storage, entry,
                                           g, cacheable);
                    }
                    else if (slot.binding == static_cast<std::uint32_t>(WebGPUEffectRenderer::kDescriptorEngineMatrixBinding))
                    {
                        const float* matrices = snapshot.arrays != nullptr
                            ? snapshot.arrays->engineMatrices.data() : nullptr;
                        static constexpr std::uint64_t kEngineMatrixBytes = 6 * 64;
                        handle = bindBlock(slot, matrices, kEngineMatrixBytes, storage, entry, g,
                                           cacheable);
                    }
                    else
                    {
                        const int elementFloats =
                            slot.binding == static_cast<std::uint32_t>(WebGPUEffectRenderer::kDescriptorFloatArrayBinding) ? 1
                            : slot.binding == static_cast<std::uint32_t>(WebGPUEffectRenderer::kDescriptorFloatArrayBinding) + 1 ? 2
                            : slot.binding == static_cast<std::uint32_t>(WebGPUEffectRenderer::kDescriptorFloatArrayBinding) + 2 ? 3
                            : 16;
                        const std::vector<float>* packed = arrayBytes(elementFloats, storage);
                        handle = bindBlock(slot, packed->data(), packed->size() * sizeof(float),
                                           storage, entry, g, cacheable);
                    }
                    break;
                }
                }
                if (cacheable) key.handles[entries.size()] = handle;
                entries.push_back(entry);
            }
            WGPUBindGroupDescriptor descriptor{};
            descriptor.label = Label("CNA WebGPU ShaderEffect BindGroup");
            descriptor.layout = layout.GroupLayout(g);
            descriptor.entryCount = entries.size();
            descriptor.entries = entries.empty() ? nullptr : entries.data();
            if (cacheable)
            {
                result.groups[g] = AcquireCachedBindGroupEXT(key, textures, descriptor);
            }
            else
            {
                result.groups[g] = CreateBindGroupEXT(&descriptor);
                pendingBindGroupReleases_.push_back(result.groups[g]);
            }
        }
        return result;
    }
}
