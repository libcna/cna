// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu_modern_graphics.md: the modern CNA Graphics API on WebGPU. See
// WebGPUModern.hpp for the ordering and lifetime rules every record here follows.

#include "CNA/Internal/Renderers/WebGPU/WebGPUModern.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Internal::Renderers::WebGPU
{
    namespace
    {
        [[nodiscard]] WGPUStringView Label(const char* text)
        {
            return WGPUStringView{text, text != nullptr ? std::strlen(text) : 0};
        }

        [[nodiscard]] std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment)
        {
            return (value + alignment - 1) / alignment * alignment;
        }

        // The portable usage bits of CNA::Graphics::StorageBufferUsage, kept numeric here because
        // this renderer does not include the engine-layer headers.
        constexpr std::uint32_t kUsageIndirectArguments = UINT32_C(1) << 3;
        constexpr std::uint32_t kUsageVertex = UINT32_C(1) << 4;
        constexpr std::uint32_t kUsageIndex = UINT32_C(1) << 5;
        constexpr std::uint32_t kUsageConstant = UINT32_C(1) << 6;

        // CNA::Graphics::Texture2DArrayUsage / StorageTexture2DUsage bits.
        constexpr std::uint32_t kArrayUsageTransferSource = UINT32_C(1) << 2;
        constexpr std::uint32_t kStorageUsageRead = UINT32_C(1) << 0;
        constexpr std::uint32_t kStorageUsageWrite = UINT32_C(1) << 1;
        constexpr std::uint32_t kStorageUsageSampled = UINT32_C(1) << 2;

    }

    // ---- WebGPUProgramLayoutEXT ----------------------------------------------------------------

    std::shared_ptr<WebGPUProgramLayoutEXT> WebGPUProgramLayoutEXT::Create(
        WGPUDevice device,
        const std::vector<std::pair<const WgslModuleReflection*, WGPUShaderStage>>& stages,
        std::string& error)
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
                slot.storageFormat = r.storageFormat;
                slot.storageAccess = r.storageAccess;
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
                    break;
                case WgslResourceKind::StorageBuffer:
                    e.buffer.type = WGPUBufferBindingType_Storage;
                    e.buffer.minBindingSize = s.minBindingSize;
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
                case WgslResourceKind::StorageTexture:
                    e.storageTexture.access = s.storageAccess;
                    e.storageTexture.format = s.storageFormat;
                    e.storageTexture.viewDimension = s.viewDimension;
                    break;
                }
                entries.push_back(e);
            }
            WGPUBindGroupLayoutDescriptor descriptor{};
            descriptor.label = Label("CNA WebGPU modern program BindGroupLayout");
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
        pipelineDescriptor.label = Label("CNA WebGPU modern program PipelineLayout");
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

    // ---- WebGPUStorageBufferRenderer -----------------------------------------------------------

    WebGPUStorageBufferRenderer::WebGPUStorageBufferRenderer(
        WebGPURenderer* owner, std::size_t byteSize, std::uint32_t usage, std::uint32_t cpuAccess)
        : owner_(owner), byteSize_(byteSize), nativeSize_(AlignUp(std::max<std::uint64_t>(byteSize, 4), 4)),
          usage_(usage), cpuAccess_(cpuAccess)
    {
        if (owner_ == nullptr || owner_->DeviceEXT() == nullptr)
            throw std::runtime_error("CNA WebGPU: a StorageBuffer needs a live device");
        const WGPULimits& limits = owner_->DeviceLimitsEXT();
        if (nativeSize_ > limits.maxBufferSize)
            throw System::NotSupportedException(
                "CNA WebGPU: a StorageBuffer of " + std::to_string(byteSize) +
                " bytes exceeds this device's maxBufferSize of " + std::to_string(limits.maxBufferSize));

        WGPUBufferUsage native = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc |
                                 WGPUBufferUsage_CopyDst;
        if ((usage & kUsageConstant) != 0) native |= WGPUBufferUsage_Uniform;
        if ((usage & kUsageIndirectArguments) != 0) native |= WGPUBufferUsage_Indirect;
        if ((usage & kUsageVertex) != 0) native |= WGPUBufferUsage_Vertex;
        if ((usage & kUsageIndex) != 0) native |= WGPUBufferUsage_Index;

        WGPUBufferDescriptor descriptor{};
        descriptor.label = Label("CNA WebGPU StorageBuffer");
        descriptor.usage = native;
        descriptor.size = nativeSize_;
        buffer_ = wgpuDeviceCreateBuffer(owner_->DeviceEXT(), &descriptor);
        if (buffer_ == nullptr)
            throw std::runtime_error("CNA WebGPU: wgpuDeviceCreateBuffer failed for a StorageBuffer");
        // A new WebGPU buffer is zero-initialised by the implementation, which is what an unwritten
        // StorageBuffer reads on every other renderer too.
        owner_->RegisterModernResourceEXT(this);
    }

    WebGPUStorageBufferRenderer::~WebGPUStorageBufferRenderer()
    {
        if (owner_ != nullptr) owner_->UnregisterModernResourceEXT(this);
        if (buffer_ != nullptr) wgpuBufferRelease(buffer_);
    }

    void WebGPUStorageBufferRenderer::RequireOwner(const char* operation) const
    {
        if (owner_ == nullptr)
            throw std::runtime_error(std::string("CNA WebGPU StorageBuffer::") + operation +
                                     ": the GraphicsDevice that created this buffer is gone");
    }

    void WebGPUStorageBufferRenderer::SetData(const void* data, std::size_t byteSize)
    {
        if (!SetDataRangeEXT(0, data, byteSize))
            throw std::out_of_range("CNA WebGPU StorageBuffer::SetData: the range exceeds the buffer");
    }

    void WebGPUStorageBufferRenderer::GetData(void* out, std::size_t byteSize) const
    {
        if (!GetDataRangeEXT(0, out, byteSize))
            throw std::out_of_range("CNA WebGPU StorageBuffer::GetData: the range exceeds the buffer");
    }

    bool WebGPUStorageBufferRenderer::SetDataRangeEXT(
        std::size_t byteOffset, const void* data, std::size_t byteSize)
    {
        RequireOwner("SetData");
        if (byteOffset > byteSize_ || byteSize > byteSize_ - byteOffset) return false;
        if (byteSize == 0) return true;
        if (data == nullptr) return false;
        owner_->WriteBufferBytesEXT(buffer_, byteOffset, data, byteSize);
        return true;
    }

    bool WebGPUStorageBufferRenderer::GetDataRangeEXT(
        std::size_t byteOffset, void* out, std::size_t byteSize) const
    {
        RequireOwner("GetData");
        if (byteOffset > byteSize_ || byteSize > byteSize_ - byteOffset) return false;
        if (byteSize == 0) return true;
        if (out == nullptr) return false;
        owner_->ReadBufferBytesEXT(buffer_, byteOffset, out, byteSize);
        return true;
    }

    bool WebGPUStorageBufferRenderer::CopyToEXT(
        IStorageBufferRenderer& destination, std::size_t sourceByteOffset,
        std::size_t destinationByteOffset, std::size_t byteSize)
    {
        RequireOwner("CopyTo");
        auto* target = dynamic_cast<WebGPUStorageBufferRenderer*>(&destination);
        if (target == nullptr || !target->IsOwnedByEXT(owner_)) return false;
        if (sourceByteOffset > byteSize_ || byteSize > byteSize_ - sourceByteOffset) return false;
        if (destinationByteOffset > target->byteSize_ ||
            byteSize > target->byteSize_ - destinationByteOffset)
            return false;
        if (byteSize == 0) return true;
        owner_->CopyBufferBytesEXT(buffer_, sourceByteOffset, target->buffer_,
                                   destinationByteOffset, byteSize);
        return true;
    }

    // ---- WebGPUTexture2DArrayRenderer ----------------------------------------------------------

    WebGPUTexture2DArrayRenderer::WebGPUTexture2DArrayRenderer(
        WebGPURenderer* owner, int width, int height, int layers, int mipLevels,
        WGPUTextureFormat format, int bytesPerTexel, std::uint32_t usage)
        : owner_(owner), format_(format), width_(width), height_(height), layers_(layers),
          mipLevels_(mipLevels), bytesPerTexel_(bytesPerTexel)
    {
        WGPUTextureDescriptor descriptor{};
        descriptor.label = Label("CNA WebGPU Texture2DArray");
        descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst |
                           ((usage & kArrayUsageTransferSource) != 0 ? WGPUTextureUsage_CopySrc
                                                                     : WGPUTextureUsage_None);
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                           static_cast<std::uint32_t>(layers)};
        descriptor.format = format;
        descriptor.mipLevelCount = static_cast<std::uint32_t>(mipLevels);
        descriptor.sampleCount = 1;
        texture_ = wgpuDeviceCreateTexture(owner_->DeviceEXT(), &descriptor);
        if (texture_ == nullptr)
            throw std::runtime_error("CNA WebGPU: wgpuDeviceCreateTexture failed for a Texture2DArray");
        WGPUTextureViewDescriptor view{};
        view.label = Label("CNA WebGPU Texture2DArray View");
        view.format = format;
        view.dimension = WGPUTextureViewDimension_2DArray;
        view.baseMipLevel = 0;
        view.mipLevelCount = static_cast<std::uint32_t>(mipLevels);
        view.baseArrayLayer = 0;
        view.arrayLayerCount = static_cast<std::uint32_t>(layers);
        view.aspect = WGPUTextureAspect_All;
        view_ = wgpuTextureCreateView(texture_, &view);
        keepAlive_ = std::make_shared<const WebGPUSampledResourceEXT>(texture_, view_);
        owner_->RegisterModernResourceEXT(this);
    }

    WebGPUSampledTextureEXT WebGPUTexture2DArrayRenderer::SampledEXT() const
    {
        return WebGPUSampledTextureEXT{view_, keepAlive_};
    }

    WebGPUTexture2DArrayRenderer::~WebGPUTexture2DArrayRenderer()
    {
        if (owner_ != nullptr) owner_->UnregisterModernResourceEXT(this);
        if (view_ != nullptr) wgpuTextureViewRelease(view_);
        if (texture_ != nullptr) wgpuTextureRelease(texture_);
    }

    bool WebGPUTexture2DArrayRenderer::SetData(int layer, int mipLevel, int x, int y, int width,
                                               int height, const void* data, std::size_t byteCount)
    {
        if (owner_ == nullptr || data == nullptr) return false;
        const std::size_t expected = static_cast<std::size_t>(width) * height * bytesPerTexel_;
        if (byteCount != expected) return false;
        owner_->WriteTextureRegionEXT(texture_, mipLevel, layer, x, y, width, height,
                                      bytesPerTexel_, data);
        return true;
    }

    bool WebGPUTexture2DArrayRenderer::GetData(int layer, int mipLevel, int x, int y, int width,
                                               int height, void* data, std::size_t byteCount) const
    {
        if (owner_ == nullptr || data == nullptr) return false;
        const std::size_t expected = static_cast<std::size_t>(width) * height * bytesPerTexel_;
        if (byteCount != expected) return false;
        owner_->ReadTextureRegionEXT(texture_, mipLevel, layer, x, y, width, height,
                                     bytesPerTexel_, data);
        return true;
    }

    // ---- WebGPUStorageTexture2DRenderer --------------------------------------------------------

    WebGPUStorageTexture2DRenderer::WebGPUStorageTexture2DRenderer(
        WebGPURenderer* owner, int width, int height, int mipLevels, WGPUTextureFormat format,
        int bytesPerTexel, std::uint32_t usage)
        : owner_(owner), format_(format), width_(width), height_(height), mipLevels_(mipLevels),
          bytesPerTexel_(bytesPerTexel), usage_(usage)
    {
        WGPUTextureDescriptor descriptor{};
        descriptor.label = Label("CNA WebGPU StorageTexture2D");
        // Transfers are this renderer's own upload/readback mechanism, so both copy usages are
        // always present; the portable usage mask is enforced by the public layer.
        descriptor.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopyDst |
                           WGPUTextureUsage_CopySrc;
        if ((usage & kStorageUsageSampled) != 0)
            descriptor.usage |= WGPUTextureUsage_TextureBinding;
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
        descriptor.format = format;
        descriptor.mipLevelCount = static_cast<std::uint32_t>(mipLevels);
        descriptor.sampleCount = 1;
        texture_ = wgpuDeviceCreateTexture(owner_->DeviceEXT(), &descriptor);
        if (texture_ == nullptr)
            throw std::runtime_error("CNA WebGPU: wgpuDeviceCreateTexture failed for a StorageTexture2D");

        WGPUTextureViewDescriptor view{};
        view.label = Label("CNA WebGPU StorageTexture2D storage view");
        view.format = format;
        view.dimension = WGPUTextureViewDimension_2D;
        view.baseMipLevel = 0;
        view.mipLevelCount = 1;   // a storage binding names exactly one level
        view.baseArrayLayer = 0;
        view.arrayLayerCount = 1;
        view.aspect = WGPUTextureAspect_All;
        storageView_ = wgpuTextureCreateView(texture_, &view);
        if ((usage & kStorageUsageSampled) != 0)
        {
            view.label = Label("CNA WebGPU StorageTexture2D sampled view");
            view.mipLevelCount = static_cast<std::uint32_t>(mipLevels);
            sampledView_ = wgpuTextureCreateView(texture_, &view);
            keepAlive_ = std::make_shared<const WebGPUSampledResourceEXT>(texture_, sampledView_);
        }
        owner_->RegisterModernResourceEXT(this);
    }

    WebGPUSampledTextureEXT WebGPUStorageTexture2DRenderer::SampledEXT() const
    {
        if (sampledView_ == nullptr) return {};
        return WebGPUSampledTextureEXT{sampledView_, keepAlive_};
    }

    WebGPUStorageTexture2DRenderer::~WebGPUStorageTexture2DRenderer()
    {
        if (owner_ != nullptr) owner_->UnregisterModernResourceEXT(this);
        if (sampledView_ != nullptr) wgpuTextureViewRelease(sampledView_);
        if (storageView_ != nullptr) wgpuTextureViewRelease(storageView_);
        if (texture_ != nullptr) wgpuTextureRelease(texture_);
    }

    bool WebGPUStorageTexture2DRenderer::SetData(int mipLevel, int x, int y, int width, int height,
                                                 const void* data, std::size_t byteCount)
    {
        if (owner_ == nullptr || data == nullptr) return false;
        const std::size_t expected = static_cast<std::size_t>(width) * height * bytesPerTexel_;
        if (byteCount != expected) return false;
        owner_->WriteTextureRegionEXT(texture_, mipLevel, 0, x, y, width, height, bytesPerTexel_,
                                      data);
        return true;
    }

    bool WebGPUStorageTexture2DRenderer::GetData(int mipLevel, int x, int y, int width, int height,
                                                 void* data, std::size_t byteCount) const
    {
        if (owner_ == nullptr || data == nullptr) return false;
        const std::size_t expected = static_cast<std::size_t>(width) * height * bytesPerTexel_;
        if (byteCount != expected) return false;
        owner_->ReadTextureRegionEXT(texture_, mipLevel, 0, x, y, width, height, bytesPerTexel_,
                                     data);
        return true;
    }

    // ---- WebGPUComputeShaderRenderer -----------------------------------------------------------

    WebGPUComputeShaderRenderer::WebGPUComputeShaderRenderer(WebGPURenderer* owner,
                                                             const std::string& source)
        : owner_(owner)
    {
        if (owner_ != nullptr) owner_->RegisterModernResourceEXT(this);
        CompileProgram(source);
    }

    WebGPUComputeShaderRenderer::~WebGPUComputeShaderRenderer()
    {
        if (owner_ != nullptr) owner_->UnregisterModernResourceEXT(this);
        ReleaseProgramEXT();
    }

    void WebGPUComputeShaderRenderer::ReleaseProgramEXT() noexcept
    {
        if (pipeline_ != nullptr) wgpuComputePipelineRelease(pipeline_);
        if (module_ != nullptr) wgpuShaderModuleRelease(module_);
        pipeline_ = nullptr;
        module_ = nullptr;
        layout_.reset();
        scalars_.clear();
        scalarBytes_.clear();
        buffers_.clear();
        images_.clear();
        imageTextures_.clear();
        textures_.clear();
    }

    bool WebGPUComputeShaderRenderer::CompileProgram(const std::string& computeSrc)
    {
        ReleaseProgramEXT();
        compileError_.clear();
        if (owner_ == nullptr || owner_->DeviceEXT() == nullptr)
        {
            compileError_ = "WebGPU: no device is available to compile a ComputeShader";
            return false;
        }

        const WgslModuleReflection reflection = ReflectWgsl(computeSrc);
        if (!reflection.ok)
        {
            compileError_ = "WebGPU ComputeShader: " + reflection.error;
            return false;
        }
        const WgslEntryPoint* entry = reflection.FindEntryPoint(WGPUShaderStage_Compute);
        if (entry == nullptr)
        {
            compileError_ = "WebGPU ComputeShader: the WGSL declares no @compute entry point";
            return false;
        }
        for (const WgslResourceBinding& r : reflection.resources)
        {
            const bool scalarBlock = r.group == kScalarGroup && r.binding == 0 &&
                                     r.kind == WgslResourceKind::UniformBuffer;
            if (r.group != 0 && !scalarBlock)
            {
                compileError_ = "WebGPU ComputeShader: '" + r.name + "' is at @group(" +
                                std::to_string(r.group) + ") @binding(" + std::to_string(r.binding) +
                                "); a compute program binds its resources in @group(0) and its "
                                "named scalars at @group(3) @binding(0)";
                return false;
            }
            if (r.kind == WgslResourceKind::StorageTexture &&
                r.viewDimension != WGPUTextureViewDimension_2D)
            {
                compileError_ = "WebGPU ComputeShader: storage image '" + r.name +
                                "' is not two-dimensional; only StorageTexture2D binds here";
                return false;
            }
        }

        std::string error;
        layout_ = WebGPUProgramLayoutEXT::Create(
            owner_->DeviceEXT(), {{&reflection, WGPUShaderStage_Compute}}, error);
        if (layout_ == nullptr)
        {
            compileError_ = "WebGPU ComputeShader: " + error;
            return false;
        }

        if (const WgslResourceBinding* block = reflection.FindResource(kScalarGroup, 0))
        {
            const auto it = reflection.structs.find(block->type);
            if (it != reflection.structs.end())
            {
                for (const WgslStructMember& m : it->second.members)
                {
                    if (m.type == "i32" || m.type == "u32")
                        scalars_[m.name] = ScalarMember{m.offset, false};
                    else if (m.type == "f32")
                        scalars_[m.name] = ScalarMember{m.offset, true};
                }
            }
            scalarBytes_.assign(static_cast<std::size_t>(AlignUp(
                std::max<std::uint64_t>(block->minBindingSize, 16), 16)), std::uint8_t{0});
        }

        module_ = owner_->CreateShaderModuleCheckedEXT(computeSrc, "CNA WebGPU ComputeShader",
                                                       compileError_);
        if (module_ == nullptr)
        {
            ReleaseProgramEXT();
            return false;
        }
        entryPoint_ = entry->name;
        pipeline_ = owner_->CreateComputePipelineCheckedEXT(module_, entryPoint_.c_str(),
                                                            layout_->PipelineLayout(), compileError_);
        if (pipeline_ == nullptr)
        {
            ReleaseProgramEXT();
            return false;
        }

        for (const WebGPUBindingSlotEXT& s : layout_->Group(0))
        {
            switch (s.kind)
            {
            case WgslResourceKind::UniformBuffer:
            case WgslResourceKind::StorageBuffer:
            case WgslResourceKind::ReadOnlyStorageBuffer:
                buffers_[s.binding] = nullptr;
                break;
            case WgslResourceKind::StorageTexture:
                images_[s.binding] = nullptr;
                break;
            case WgslResourceKind::SampledTexture:
                textures_[s.binding] = WebGPUSampledTextureEXT{};
                break;
            default:
                break;
            }
        }
        return true;
    }

    void WebGPUComputeShaderRenderer::WriteScalar(const char* name, const void* value, bool isFloat)
    {
        if (!IsValid())
            throw std::runtime_error("WebGPU compute shader: cannot set a scalar on an invalid program");
        const std::string key = name != nullptr ? name : "";
        const auto it = scalars_.find(key);
        if (it == scalars_.end())
            throw std::out_of_range("WebGPU compute shader: no scalar named '" + key +
                                    "' in the @group(3) @binding(0) block");
        if (it->second.isFloat != isFloat)
            throw std::invalid_argument("WebGPU compute shader: scalar '" + key + "' is " +
                                        (it->second.isFloat ? "f32" : "an integer") + ", not " +
                                        (isFloat ? "f32" : "an integer"));
        std::memcpy(scalarBytes_.data() + it->second.offset, value, 4);
    }

    void WebGPUComputeShaderRenderer::SetUniformInt(const char* name, int value)
    {
        const std::int32_t v = value;
        WriteScalar(name, &v, false);
    }

    void WebGPUComputeShaderRenderer::SetUniformFloat(const char* name, float value)
    {
        WriteScalar(name, &value, true);
    }

    void WebGPUComputeShaderRenderer::BindStorageBuffer(int binding, IStorageBufferRenderer* buffer)
    {
        const auto it = buffers_.find(static_cast<std::uint32_t>(binding));
        const WebGPUBindingSlotEXT* slot =
            layout_ != nullptr ? layout_->Find(0, static_cast<std::uint32_t>(binding)) : nullptr;
        if (binding < 0 || it == buffers_.end() || slot == nullptr ||
            slot->kind == WgslResourceKind::UniformBuffer)
            throw std::out_of_range("WebGPU compute shader: storage binding " +
                                    std::to_string(binding) +
                                    " is not declared by this WGSL module in @group(0)");
        if (buffer == nullptr)
        {
            it->second.reset();
            return;
        }
        auto* native = dynamic_cast<WebGPUStorageBufferRenderer*>(buffer);
        if (native == nullptr || !native->IsOwnedByEXT(owner_))
            throw std::invalid_argument(
                "WebGPU compute shader: the storage buffer belongs to another renderer");
        it->second = std::static_pointer_cast<WebGPUStorageBufferRenderer>(
            std::dynamic_pointer_cast<WebGPUStorageBufferRenderer>(native->shared_from_this()));
    }

    bool WebGPUComputeShaderRenderer::BindConstantBufferEXT(int binding, IStorageBufferRenderer* buffer)
    {
        const auto it = buffers_.find(static_cast<std::uint32_t>(binding));
        const WebGPUBindingSlotEXT* slot =
            layout_ != nullptr ? layout_->Find(0, static_cast<std::uint32_t>(binding)) : nullptr;
        if (binding < 0 || it == buffers_.end() || slot == nullptr ||
            slot->kind != WgslResourceKind::UniformBuffer)
            throw std::out_of_range("WebGPU compute shader: constant binding " +
                                    std::to_string(binding) +
                                    " is not declared as var<uniform> in @group(0)");
        if (buffer == nullptr)
        {
            it->second.reset();
            return true;
        }
        auto* native = dynamic_cast<WebGPUStorageBufferRenderer*>(buffer);
        if (native == nullptr || !native->IsOwnedByEXT(owner_))
            throw std::invalid_argument(
                "WebGPU compute shader: the constant buffer belongs to another renderer");
        if ((native->GetUsageEXT() & kUsageConstant) == 0)
            return false;
        it->second = std::dynamic_pointer_cast<WebGPUStorageBufferRenderer>(native->shared_from_this());
        return true;
    }

    void WebGPUComputeShaderRenderer::BindImageTexture(int unit, ITextureRenderer* texture,
                                                       int accessMode)
    {
        const auto it = images_.find(static_cast<std::uint32_t>(unit));
        const WebGPUBindingSlotEXT* slot =
            layout_ != nullptr ? layout_->Find(0, static_cast<std::uint32_t>(unit)) : nullptr;
        if (unit < 0 || it == images_.end() || slot == nullptr)
            throw std::out_of_range("WebGPU compute shader: storage-image binding " +
                                    std::to_string(unit) +
                                    " is not declared by this WGSL module in @group(0)");
        if (texture == nullptr)
        {
            it->second.reset();
            imageTextures_.erase(static_cast<std::uint32_t>(unit));
            return;
        }
        auto* native = dynamic_cast<WebGPUTextureRenderer*>(texture);
        if (native == nullptr)
            throw System::NotSupportedException(
                "WebGPU compute shader: image unit " + std::to_string(unit) +
                " -- only a Texture2D (or a StorageTexture2D, through bindStorageTexture) binds as a "
                "compute image here; a RenderTarget2D carries the swap chain's format, which WebGPU "
                "does not allow as a storage image without an optional feature");
        if (!native->SupportsStorageBindingEXT() || slot->storageFormat != WGPUTextureFormat_RGBA8Unorm)
            throw System::NotSupportedException(
                "WebGPU compute shader: image unit " + std::to_string(unit) +
                " -- a Texture2D binds as a compute image only in SurfaceFormat::Color (rgba8unorm), "
                "and the shader must declare that same texel format");
        const bool reads = slot->storageAccess != WGPUStorageTextureAccess_WriteOnly;
        const bool writes = slot->storageAccess != WGPUStorageTextureAccess_ReadOnly;
        if ((reads && accessMode == 1) || (writes && accessMode == 0))
            throw std::invalid_argument(
                "WebGPU compute shader: image unit " + std::to_string(unit) +
                " is declared with an access the requested GraphicsImageAccess does not grant");
        it->second.reset();
        imageTextures_[static_cast<std::uint32_t>(unit)] = native->StorageBindingEXT();
    }

    bool WebGPUComputeShaderRenderer::BindStorageTexture2DEXT(
        int unit, std::shared_ptr<IStorageTexture2DRenderer> texture, int accessMode)
    {
        const auto it = images_.find(static_cast<std::uint32_t>(unit));
        const WebGPUBindingSlotEXT* slot =
            layout_ != nullptr ? layout_->Find(0, static_cast<std::uint32_t>(unit)) : nullptr;
        if (unit < 0 || it == images_.end() || slot == nullptr)
            return false;
        if (texture == nullptr)
        {
            it->second.reset();
            return true;
        }
        auto native = std::dynamic_pointer_cast<WebGPUStorageTexture2DRenderer>(texture);
        if (native == nullptr || !native->IsOwnedByEXT(owner_))
            return false;
        if (native->Format() != slot->storageFormat)
            throw std::invalid_argument(
                "WebGPU compute shader: storage image " + std::to_string(unit) +
                " is declared with a different texel format than the bound StorageTexture2D");
        // GraphicsImageAccess: 0 ReadOnly, 1 WriteOnly, 2 ReadWrite.
        const bool reads = slot->storageAccess != WGPUStorageTextureAccess_WriteOnly;
        const bool writes = slot->storageAccess != WGPUStorageTextureAccess_ReadOnly;
        if ((reads && accessMode == 1) || (writes && accessMode == 0))
            throw std::invalid_argument(
                "WebGPU compute shader: storage image " + std::to_string(unit) +
                " is declared with an access the requested GraphicsImageAccess does not grant");
        if ((reads && (native->Usage() & kStorageUsageRead) == 0) ||
            (writes && (native->Usage() & kStorageUsageWrite) == 0))
            throw std::invalid_argument(
                "WebGPU compute shader: storage image " + std::to_string(unit) +
                " needs a storage usage the texture did not declare");
        it->second = std::move(native);
        imageTextures_.erase(static_cast<std::uint32_t>(unit));
        return true;
    }

    void WebGPUComputeShaderRenderer::BindTexture(int unit, ITextureRenderer* texture)
    {
        const auto it = textures_.find(static_cast<std::uint32_t>(unit));
        if (unit < 0 || it == textures_.end())
            throw std::out_of_range("WebGPU compute shader: sampled-texture binding " +
                                    std::to_string(unit) +
                                    " is not declared by this WGSL module in @group(0)");
        if (texture == nullptr)
        {
            it->second = WebGPUSampledTextureEXT{};
            return;
        }
        const auto* samplable = dynamic_cast<const IWebGPUSamplable*>(texture);
        if (samplable == nullptr)
            throw std::invalid_argument(
                "WebGPU compute shader: the texture belongs to another renderer");
        it->second = samplable->Sampled();
    }

    void WebGPUComputeShaderRenderer::DispatchEXT(int groupsX, int groupsY, int groupsZ)
    {
        if (owner_ == nullptr)
            throw std::runtime_error("WebGPU compute shader: the GraphicsDevice is gone");
        if (!IsValid())
            throw std::runtime_error("WebGPU compute shader: cannot dispatch an invalid program");
        const int groups[3] = {groupsX, groupsY, groupsZ};
        const std::uint32_t limit = owner_->DeviceLimitsEXT().maxComputeWorkgroupsPerDimension;
        for (int axis = 0; axis < 3; ++axis)
            if (groups[axis] <= 0 || static_cast<std::uint32_t>(groups[axis]) > limit)
                throw std::invalid_argument(
                    "WebGPU compute shader: dispatch group count exceeds the device limit");

        // Everything a dispatch reads is resolved and retained now: later bind/setUniform calls
        // cannot change an accepted dispatch, and disposing a bound resource cannot free it.
        WebGPURenderer::ModernDispatchEXT dispatch;
        dispatch.pipeline = pipeline_;
        dispatch.layout = layout_;
        dispatch.groups = {static_cast<std::uint32_t>(groupsX), static_cast<std::uint32_t>(groupsY),
                           static_cast<std::uint32_t>(groupsZ)};
        dispatch.scalarBytes = scalarBytes_;
        for (const WebGPUBindingSlotEXT& s : layout_->Group(0))
        {
            WebGPURenderer::ModernBindingEXT b;
            b.binding = s.binding;
            switch (s.kind)
            {
            case WgslResourceKind::UniformBuffer:
            case WgslResourceKind::StorageBuffer:
            case WgslResourceKind::ReadOnlyStorageBuffer:
            {
                const auto& buffer = buffers_.at(s.binding);
                if (buffer == nullptr)
                    throw std::runtime_error("WebGPU compute shader: required @group(0) buffer binding " +
                                             std::to_string(s.binding) + " ('" + s.name +
                                             "') is not bound");
                b.buffer = buffer;
                break;
            }
            case WgslResourceKind::StorageTexture:
            {
                const auto& image = images_.at(s.binding);
                const auto texture = imageTextures_.find(s.binding);
                if (image == nullptr && texture == imageTextures_.end())
                    throw std::runtime_error("WebGPU compute shader: required storage image binding " +
                                             std::to_string(s.binding) + " ('" + s.name +
                                             "') is not bound");
                if (image != nullptr)
                {
                    b.storageTexture = image;
                    b.textureView = image->StorageView();
                }
                else
                {
                    b.sampled = texture->second;   // WMG-0008: a Texture2D's own storage view
                    b.textureView = texture->second.View();
                }
                break;
            }
            case WgslResourceKind::SampledTexture:
            {
                const auto& texture = textures_.at(s.binding);
                if (!texture)
                    throw std::runtime_error("WebGPU compute shader: required sampled texture binding " +
                                             std::to_string(s.binding) + " ('" + s.name +
                                             "') is not bound");
                b.sampled = texture;
                b.textureView = texture.View();
                break;
            }
            case WgslResourceKind::Sampler:
            case WgslResourceKind::ComparisonSampler:
            {
                const int unit = s.binding >= kSamplerBindingOffset
                                     ? static_cast<int>(s.binding - kSamplerBindingOffset)
                                     : static_cast<int>(s.binding);
                b.sampler = owner_->SamplerForUnitEXT(unit);
                break;
            }
            }
            dispatch.bindings.push_back(std::move(b));
        }
        owner_->SubmitComputeDispatchEXT(std::move(dispatch));
    }

    // ---- WebGPUGpuTimerRenderer ----------------------------------------------------------------

    struct WebGPUGpuTimerRenderer::MapState
    {
        bool completed = false;
        WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
    };

    WebGPUGpuTimerRenderer::WebGPUGpuTimerRenderer(WebGPURenderer* owner) : owner_(owner)
    {
        WGPUQuerySetDescriptor query{};
        query.label = Label("CNA WebGPU GpuTimer");
        query.type = WGPUQueryType_Timestamp;
        query.count = 2;
        querySet_ = wgpuDeviceCreateQuerySet(owner_->DeviceEXT(), &query);
        WGPUBufferDescriptor resolve{};
        resolve.label = Label("CNA WebGPU GpuTimer resolve");
        resolve.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
        resolve.size = 2 * sizeof(std::uint64_t);
        resolveBuffer_ = wgpuDeviceCreateBuffer(owner_->DeviceEXT(), &resolve);
        WGPUBufferDescriptor readback{};
        readback.label = Label("CNA WebGPU GpuTimer readback");
        readback.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        readback.size = 2 * sizeof(std::uint64_t);
        readbackBuffer_ = wgpuDeviceCreateBuffer(owner_->DeviceEXT(), &readback);
        if (querySet_ == nullptr || resolveBuffer_ == nullptr || readbackBuffer_ == nullptr)
            throw std::runtime_error("CNA WebGPU: GpuTimer resources could not be created");
        owner_->RegisterModernResourceEXT(this);
    }

    WebGPUGpuTimerRenderer::~WebGPUGpuTimerRenderer()
    {
        if (owner_ != nullptr)
        {
            // plans/plan_webgpu_modern_graphics.md WMG-0027: a timer destroyed while its range is
            // still open would leave the renderer naming a query set that is about to be released,
            // and WMG-0017 made every render pass carry that name until the range closes. The next
            // pass built after this destructor then references a freed query set, which
            // wgpu-native reports as a non-unwinding panic inside
            // wgpuCommandEncoderBeginRenderPass rather than as an error this process can handle.
            //
            // Nothing exotic reaches this: any exception thrown between begin() and end() -- a
            // read-back that fails, a dispatch the device refuses -- unwinds through here with the
            // range open, and turned a recoverable error into an abort at the next Present.
            owner_->ForgetOpenGpuTimerEXT(querySet_);
            owner_->UnregisterModernResourceEXT(this);
            // A map still in flight names mapState_ as its userdata; let it land before release.
            if (mapping_ && mapState_ != nullptr && !mapState_->completed)
            {
                try { owner_->WaitForCompletionEXT(mapState_->completed, "GpuTimer teardown"); }
                catch (...) {}
            }
        }
        if (mapping_ && mapState_ != nullptr && mapState_->completed &&
            mapState_->status == WGPUMapAsyncStatus_Success)
            wgpuBufferUnmap(readbackBuffer_);
        if (readbackBuffer_ != nullptr) wgpuBufferRelease(readbackBuffer_);
        if (resolveBuffer_ != nullptr) wgpuBufferRelease(resolveBuffer_);
        if (querySet_ != nullptr) wgpuQuerySetRelease(querySet_);
    }

    void WebGPUGpuTimerRenderer::Begin()
    {
        if (owner_ == nullptr) throw std::runtime_error("GpuTimer: the GraphicsDevice is gone");
        if (mapping_)
        {
            // A new range replaces an unread one; drain the old map so the buffer is reusable.
            Poll();
            if (mapping_ && mapState_ != nullptr && !mapState_->completed)
                owner_->WaitForCompletionEXT(mapState_->completed, "GpuTimer restart");
            Poll();
        }
        available_ = false;
        elapsed_ = 0;
        owner_->WriteTimestampEXT(querySet_, 0, nullptr, nullptr);
        began_ = true;
    }

    void WebGPUGpuTimerRenderer::End()
    {
        if (owner_ == nullptr) throw std::runtime_error("GpuTimer: the GraphicsDevice is gone");
        if (!began_) throw std::logic_error("GpuTimer::End without Begin");
        began_ = false;
        owner_->WriteTimestampEXT(querySet_, 1, resolveBuffer_, readbackBuffer_);
        mapState_ = std::make_shared<MapState>();
        WGPUBufferMapCallbackInfo info{};
        info.mode = WGPUCallbackMode_AllowProcessEvents;
        info.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*) {
            auto& state = *static_cast<MapState*>(userdata1);
            state.status = status;
            state.completed = true;
        };
        info.userdata1 = mapState_.get();
        wgpuBufferMapAsync(readbackBuffer_, WGPUMapMode_Read, 0, 2 * sizeof(std::uint64_t), info);
        mapping_ = true;
    }

    void WebGPUGpuTimerRenderer::Poll() const
    {
        if (!mapping_ || mapState_ == nullptr || owner_ == nullptr) return;
        if (!mapState_->completed) owner_->ProcessEventsEXT();
        if (!mapState_->completed) return;
        mapping_ = false;
        if (mapState_->status != WGPUMapAsyncStatus_Success) return;
        const auto* ticks = static_cast<const std::uint64_t*>(
            wgpuBufferGetConstMappedRange(readbackBuffer_, 0, 2 * sizeof(std::uint64_t)));
        if (ticks != nullptr)
        {
            // WebGPU timestamps are nanoseconds; a reordered or reset counter reads as zero rather
            // than as a wrapped enormous value.
            elapsed_ = ticks[1] >= ticks[0] ? ticks[1] - ticks[0] : 0;
            available_ = true;
        }
        wgpuBufferUnmap(readbackBuffer_);
    }

    bool WebGPUGpuTimerRenderer::IsResultAvailable() const
    {
        Poll();
        return available_;
    }

    std::uint64_t WebGPUGpuTimerRenderer::ElapsedNanoseconds() const
    {
        Poll();
        return available_ ? elapsed_ : 0;
    }
}
