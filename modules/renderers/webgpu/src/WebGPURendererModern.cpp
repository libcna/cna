// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu_modern_graphics.md: the renderer half of the modern CNA Graphics API on WebGPU
// -- ordered submission of modern work, buffer and texture transfers, compute dispatch, timestamps,
// device limits and the per-format usage table. The records themselves are WebGPUModern.cpp.

#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include "CNA/RendererCapabilityProfile.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

namespace CNA::Internal::Renderers::WebGPU
{
    namespace
    {
        [[nodiscard]] WGPUStringView Label(const char* text)
        {
            return WGPUStringView{text, text != nullptr ? std::strlen(text) : 0};
        }

        [[nodiscard]] std::string Text(WGPUStringView text)
        {
            if (text.data == nullptr) return {};
            if (text.length == WGPU_STRLEN) return std::string(text.data);
            return std::string(text.data, text.length);
        }

        [[nodiscard]] std::uint64_t AlignUp(std::uint64_t value, std::uint64_t alignment)
        {
            return (value + alignment - 1) / alignment * alignment;
        }

        constexpr std::uint64_t kWaitTimeoutNanoseconds = 10'000'000'000ULL;

        struct ScopeState
        {
            bool completed = false;
            bool ok = false;
            std::string message;
        };

        void OnScopePopped(WGPUPopErrorScopeStatus status, WGPUErrorType type,
                           WGPUStringView message, void* userdata1, void*)
        {
            auto& state = *static_cast<ScopeState*>(userdata1);
            state.ok = status == WGPUPopErrorScopeStatus_Success && type == WGPUErrorType_NoError;
            if (!state.ok) state.message = Text(message);
            state.completed = true;
        }

        struct ReadState
        {
            bool completed = false;
            WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
            std::string message;
        };

        void OnReadMapped(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void*)
        {
            auto& state = *static_cast<ReadState*>(userdata1);
            state.status = status;
            if (status != WGPUMapAsyncStatus_Success) state.message = Text(message);
            state.completed = true;
        }

        // The byte-granular copy. WebGPU's copies and queue writes need 4-byte-aligned offsets and
        // sizes, and CNA::Graphics::StorageBuffer promises arbitrary byte ranges (setBytes at an odd
        // offset, copyTo between odd offsets). Each invocation owns one destination word, merges
        // exactly the bytes of the range into it and leaves the others, so neighbouring bytes are
        // never touched. The source is always a separate staging buffer, so no binding aliases.
        constexpr const char* kByteCopyWgsl = R"WGSL(
struct Params {
    destinationOffset: u32,
    sourceOffset: u32,
    size: u32,
    firstWord: u32,
    wordCount: u32,
    pad0: u32,
    pad1: u32,
    pad2: u32,
}
@group(0) @binding(0) var<storage, read_write> destination: array<u32>;
@group(0) @binding(1) var<storage, read> source: array<u32>;
@group(0) @binding(2) var<uniform> params: Params;

@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x >= params.wordCount) {
        return;
    }
    let word = params.firstWord + id.x;
    var value = destination[word];
    for (var b = 0u; b < 4u; b = b + 1u) {
        let d = word * 4u + b;
        if (d >= params.destinationOffset && d < params.destinationOffset + params.size) {
            let s = d - params.destinationOffset + params.sourceOffset;
            let byteValue = (source[s / 4u] >> (8u * (s % 4u))) & 0xFFu;
            value = (value & ~(0xFFu << (8u * b))) | (byteValue << (8u * b));
        }
    }
    destination[word] = value;
}
)WGSL";

        /// One SurfaceFormat's native storage for the modern texture records.
        struct ModernFormat
        {
            WGPUTextureFormat format = WGPUTextureFormat_Undefined;
            int bytesPerTexel = 0;
            bool storageWrite = false;   ///< core WebGPU write-only storage format
            bool storageRead = false;    ///< core WebGPU read-only/read-write storage format
            bool filterable = true;      ///< false for 32-bit float without the feature
        };

        [[nodiscard]] ModernFormat ModernFormatFor(int surfaceFormat, bool float32Filterable)
        {
            using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
            switch (static_cast<SurfaceFormat>(surfaceFormat))
            {
            case SurfaceFormat::Color:
                return {WGPUTextureFormat_RGBA8Unorm, 4, true, false, true};
            case SurfaceFormat::NormalizedByte2:
                return {WGPUTextureFormat_RG8Snorm, 2, false, false, true};
            case SurfaceFormat::NormalizedByte4:
                return {WGPUTextureFormat_RGBA8Snorm, 4, true, false, true};
            case SurfaceFormat::Single:
                return {WGPUTextureFormat_R32Float, 4, true, true, float32Filterable};
            case SurfaceFormat::Vector2:
                return {WGPUTextureFormat_RG32Float, 8, true, false, float32Filterable};
            case SurfaceFormat::Vector4:
                return {WGPUTextureFormat_RGBA32Float, 16, true, false, float32Filterable};
            case SurfaceFormat::HalfSingle:
                return {WGPUTextureFormat_R16Float, 2, false, false, true};
            case SurfaceFormat::HalfVector2:
                return {WGPUTextureFormat_RG16Float, 4, false, false, true};
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
                return {WGPUTextureFormat_RGBA16Float, 8, true, false, true};
            case SurfaceFormat::Rgba1010102:
                return {WGPUTextureFormat_RGB10A2Unorm, 4, false, false, true};
            case SurfaceFormat::ByteEXT:
                return {WGPUTextureFormat_R8Unorm, 1, false, false, true};
            default:
                return {};
            }
        }

        [[nodiscard]] const char* BackendName(WGPUBackendType type)
        {
            switch (type)
            {
            case WGPUBackendType_Null: return "Null";
            case WGPUBackendType_WebGPU: return "WebGPU";
            case WGPUBackendType_D3D11: return "D3D11";
            case WGPUBackendType_D3D12: return "D3D12";
            case WGPUBackendType_Metal: return "Metal";
            case WGPUBackendType_Vulkan: return "Vulkan";
            case WGPUBackendType_OpenGL: return "OpenGL";
            case WGPUBackendType_OpenGLES: return "OpenGLES";
            default: return "Unknown";
            }
        }

        [[nodiscard]] const char* AdapterTypeName(WGPUAdapterType type)
        {
            switch (type)
            {
            case WGPUAdapterType_DiscreteGPU: return "discrete GPU";
            case WGPUAdapterType_IntegratedGPU: return "integrated GPU";
            case WGPUAdapterType_CPU: return "CPU";
            default: return "unknown adapter type";
            }
        }
    }

    // ---- adapter description -------------------------------------------------------------------

    std::string WebGPURenderer::DescribeAdapterEXT(WGPUAdapter adapter)
    {
        WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
        if (adapter == nullptr || wgpuAdapterGetInfo(adapter, &info) != WGPUStatus_Success)
            return "unknown adapter";
        std::string text = Text(info.device);
        const std::string description = Text(info.description);
        if (!description.empty()) text += " (" + description + ")";
        text += ", ";
        text += BackendName(info.backendType);
        text += " backend, ";
        text += AdapterTypeName(info.adapterType);
        char ids[48];
        std::snprintf(ids, sizeof(ids), ", vendor 0x%04x device 0x%04x", info.vendorID, info.deviceID);
        text += ids;
        wgpuAdapterInfoFreeMembers(info);
        return text;
    }

    // ---- shader languages, factories -----------------------------------------------------------

    bool WebGPURenderer::SupportsShaderLanguageEXT(int language, int stage) const
    {
        if (language != static_cast<int>(CNA::ShaderLanguageEXT::Wgsl) || device_ == nullptr)
            return false;
        return stage == static_cast<int>(CNA::ShaderStageEXT::Vertex) ||
               stage == static_cast<int>(CNA::ShaderStageEXT::Fragment) ||
               stage == static_cast<int>(CNA::ShaderStageEXT::Compute);
    }

    std::unique_ptr<IComputeShaderRenderer> WebGPURenderer::CreateComputeShader(
        const std::string& computeSrc)
    {
        return std::make_unique<WebGPUComputeShaderRenderer>(this, computeSrc);
    }

    std::unique_ptr<IStorageBufferRenderer> WebGPURenderer::CreateStorageBuffer(std::size_t byteSize)
    {
        // The pre-descriptor behaviour IStorageBufferRenderer documents: storage, two-way transfer
        // and indirect arguments, with CPU read and write.
        return CreateStorageBufferEXT(byteSize, UINT32_C(0x0F), UINT32_C(0x03));
    }

    std::unique_ptr<IStorageBufferRenderer> WebGPURenderer::CreateStorageBufferEXT(
        std::size_t byteSize, std::uint32_t usage, std::uint32_t cpuAccess)
    {
        if (byteSize == 0 || device_ == nullptr) return nullptr;
        return std::make_unique<WebGPUStorageBufferRenderer>(this, byteSize, usage, cpuAccess);
    }

    void WebGPURenderer::DispatchCompute(IComputeShaderRenderer* shader, int groupsX, int groupsY,
                                         int groupsZ)
    {
        auto* program = dynamic_cast<WebGPUComputeShaderRenderer*>(shader);
        if (program == nullptr)
            throw std::invalid_argument("CNA WebGPU: DispatchCompute was handed another renderer's program");
        program->DispatchEXT(groupsX, groupsY, groupsZ);
    }

    std::unique_ptr<ITexture2DArrayRenderer> WebGPURenderer::CreateTexture2DArrayEXT(
        int width, int height, int layerCount, int mipLevelCount, int surfaceFormat,
        std::uint32_t usage)
    {
        if (device_ == nullptr) return nullptr;
        const CNA::RendererFormatSupport support = GetSurfaceFormatUsageSupportEXT(surfaceFormat);
        constexpr auto sampled = static_cast<std::uint32_t>(CNA::RendererFormatUsage::Sampled);
        const ModernFormat format = ModernFormatFor(surfaceFormat, float32Filterable_);
        if ((support.supportedUsages & sampled) == 0 || format.format == WGPUTextureFormat_Undefined)
            return nullptr;
        if (width <= 0 || height <= 0 || layerCount <= 0 || mipLevelCount <= 0 ||
            static_cast<std::uint32_t>(layerCount) > deviceLimits_.maxTextureArrayLayers ||
            static_cast<std::uint32_t>(std::max(width, height)) > deviceLimits_.maxTextureDimension2D)
            return nullptr;
        return std::make_unique<WebGPUTexture2DArrayRenderer>(
            this, width, height, layerCount, mipLevelCount, format.format, format.bytesPerTexel, usage);
    }

    std::unique_ptr<IStorageTexture2DRenderer> WebGPURenderer::CreateStorageTexture2DEXT(
        int width, int height, int mipLevelCount, int surfaceFormat, std::uint32_t usage)
    {
        if (device_ == nullptr) return nullptr;
        const ModernFormat format = ModernFormatFor(surfaceFormat, float32Filterable_);
        const CNA::RendererFormatSupport support = GetSurfaceFormatUsageSupportEXT(surfaceFormat);
        constexpr auto write = static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageWrite);
        if (format.format == WGPUTextureFormat_Undefined || (support.supportedUsages & write) == 0)
            return nullptr;
        if (width <= 0 || height <= 0 || mipLevelCount <= 0 ||
            static_cast<std::uint32_t>(std::max(width, height)) > deviceLimits_.maxTextureDimension2D)
            return nullptr;
        return std::make_unique<WebGPUStorageTexture2DRenderer>(
            this, width, height, mipLevelCount, format.format, format.bytesPerTexel, usage);
    }

    std::unique_ptr<IGpuTimerRenderer> WebGPURenderer::CreateGpuTimerEXT()
    {
        if (!timestampQuerySupported_ || device_ == nullptr) return nullptr;
        return std::make_unique<WebGPUGpuTimerRenderer>(this);
    }

    // ---- limits and formats --------------------------------------------------------------------

    int WebGPURenderer::GetMaxComputeWorkGroupCountEXT(int axis) const
    {
        if (axis < 0 || axis > 2 || device_ == nullptr) return 0;
        return static_cast<int>(std::min<std::uint32_t>(
            deviceLimits_.maxComputeWorkgroupsPerDimension,
            static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
    }

    int WebGPURenderer::GetMaxComputeWorkGroupSizeEXT(int axis) const
    {
        if (device_ == nullptr) return 0;
        switch (axis)
        {
        case 0: return static_cast<int>(deviceLimits_.maxComputeWorkgroupSizeX);
        case 1: return static_cast<int>(deviceLimits_.maxComputeWorkgroupSizeY);
        case 2: return static_cast<int>(deviceLimits_.maxComputeWorkgroupSizeZ);
        default: return 0;
        }
    }

    int WebGPURenderer::GetMaxComputeWorkGroupInvocationsEXT() const
    {
        return device_ != nullptr ? static_cast<int>(deviceLimits_.maxComputeInvocationsPerWorkgroup) : 0;
    }

    std::uint64_t WebGPURenderer::GetMaxStorageBufferBytesEXT() const
    {
        return device_ != nullptr ? deviceLimits_.maxStorageBufferBindingSize : 0;
    }

    std::uint64_t WebGPURenderer::GetMaxUniformBufferBytesEXT() const
    {
        return device_ != nullptr ? deviceLimits_.maxUniformBufferBindingSize : 0;
    }

    int WebGPURenderer::GetMaxComputeStorageBufferBindingsEXT() const
    {
        return device_ != nullptr ? static_cast<int>(deviceLimits_.maxStorageBuffersPerShaderStage) : 0;
    }

    int WebGPURenderer::GetMaxVertexShaderStorageBlocksEXT() const
    {
        // Core WebGPU lets a vertex stage READ storage buffers, up to the per-stage limit; writable
        // storage in a vertex stage is what needs a feature, and the draw route binds read-only.
        return device_ != nullptr ? static_cast<int>(deviceLimits_.maxStorageBuffersPerShaderStage) : 0;
    }

    int WebGPURenderer::GetMaxTextureArrayLayersEXT() const
    {
        return device_ != nullptr ? static_cast<int>(deviceLimits_.maxTextureArrayLayers) : 0;
    }

    int WebGPURenderer::GetMaxSampledTexturesPerShaderStageEXT() const
    {
        return device_ != nullptr ? static_cast<int>(deviceLimits_.maxSampledTexturesPerShaderStage) : 0;
    }

    int WebGPURenderer::GetMaxStorageImagesPerShaderStageEXT() const
    {
        return device_ != nullptr ? static_cast<int>(deviceLimits_.maxStorageTexturesPerShaderStage) : 0;
    }

    int WebGPURenderer::GetMaxVertexInputBindingsEXT() const
    {
        return device_ != nullptr ? static_cast<int>(deviceLimits_.maxVertexBuffers) : 0;
    }

    int WebGPURenderer::GetMaxVertexInputAttributesEXT() const
    {
        return device_ != nullptr ? static_cast<int>(deviceLimits_.maxVertexAttributes) : 0;
    }

    int WebGPURenderer::GetMaxColorAttachmentsEXT() const
    {
        // The renderer's MRT route carries at most four targets (PassDestination), whatever the
        // device allows beyond that.
        return device_ != nullptr ? static_cast<int>(std::min<std::uint32_t>(
                                        deviceLimits_.maxColorAttachments, 4u))
                                  : 0;
    }

    std::uint64_t WebGPURenderer::GetMinStorageBufferOffsetAlignmentEXT() const
    {
        return device_ != nullptr ? deviceLimits_.minStorageBufferOffsetAlignment : 0;
    }

    std::uint64_t WebGPURenderer::GetMinUniformBufferOffsetAlignmentEXT() const
    {
        return device_ != nullptr ? deviceLimits_.minUniformBufferOffsetAlignment : 0;
    }

    CNA::RendererFormatSupport WebGPURenderer::GetSurfaceFormatUsageSupportEXT(int surfaceFormat) const
    {
        using CNA::RendererFormatUsage;
        if (device_ == nullptr) return {};
        const ModernFormat modern = ModernFormatFor(surfaceFormat, float32Filterable_);
        if (modern.format == WGPUTextureFormat_Undefined) return {};

        // TextureStorage and RenderTarget are left to the classic classification this renderer
        // already reports through ClassifySurfaceFormatEXT/ClassifyRenderTargetFormatEXT; this table
        // answers only the modern usages, and only where the renderer has an implemented path. A
        // format the Texture2D path does not store faithfully cannot be sampled as a Texture2D here,
        // so it reports no texture usage at all rather than claiming a native capability no CNA
        // route reaches.
        constexpr std::uint32_t known =
            static_cast<std::uint32_t>(RendererFormatUsage::Sampled) |
            static_cast<std::uint32_t>(RendererFormatUsage::Filterable) |
            static_cast<std::uint32_t>(RendererFormatUsage::StorageRead) |
            static_cast<std::uint32_t>(RendererFormatUsage::StorageWrite) |
            static_cast<std::uint32_t>(RendererFormatUsage::StorageAtomic) |
            static_cast<std::uint32_t>(RendererFormatUsage::TransferSource) |
            static_cast<std::uint32_t>(RendererFormatUsage::TransferDestination) |
            static_cast<std::uint32_t>(RendererFormatUsage::Mipmapped);
        CNA::RendererFormatSupport support{known, 0};
        const bool textureStored =
            ClassifySurfaceFormatEXT(surfaceFormat) == RendererFormatVerdict::Supported ||
            surfaceFormat == static_cast<int>(Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color);
        if (!textureStored) return support;
        const auto set = [&](RendererFormatUsage usage, bool yes) {
            if (yes) support.supportedUsages |= static_cast<std::uint32_t>(usage);
        };
        set(RendererFormatUsage::Sampled, true);
        set(RendererFormatUsage::Filterable, modern.filterable);
        set(RendererFormatUsage::StorageWrite, modern.storageWrite);
        set(RendererFormatUsage::StorageRead, modern.storageRead);
        set(RendererFormatUsage::TransferSource, true);
        set(RendererFormatUsage::TransferDestination, true);
        set(RendererFormatUsage::Mipmapped, true);
        return support;
    }

    // ---- registry, waiting, ordering -----------------------------------------------------------

    void WebGPURenderer::RegisterModernResourceEXT(IWebGPUModernResourceEXT* resource)
    {
        liveModernResources_.push_back(resource);
        CountUnrecoverableResourceEXT(1);
    }

    void WebGPURenderer::UnregisterModernResourceEXT(IWebGPUModernResourceEXT* resource)
    {
        const auto it = std::find(liveModernResources_.begin(), liveModernResources_.end(), resource);
        if (it == liveModernResources_.end()) return;
        liveModernResources_.erase(it);
        CountUnrecoverableResourceEXT(-1);
    }

    void WebGPURenderer::WaitForCompletionEXT(const bool& completed, const char* operation) const
    {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::nanoseconds(kWaitTimeoutNanoseconds);
        while (!completed && std::chrono::steady_clock::now() < deadline)
        {
#if defined(__EMSCRIPTEN__)
            emscripten_sleep(1);
            wgpuInstanceProcessEvents(instance_);
#else
            wgpuInstanceProcessEvents(instance_);
            if (!completed) std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
        }
        if (!completed)
            throw std::runtime_error(std::string("CNA WebGPU: timed out waiting for ") + operation);
    }

    void WebGPURenderer::ProcessEventsEXT() const
    {
        if (instance_ != nullptr) wgpuInstanceProcessEvents(instance_);
    }

    void WebGPURenderer::FlushPendingDrawsForModernEXT()
    {
        if (drawOrder_.empty()) return;
        FlushCurrentRenderTarget();
        // The bound target's cycle goes on: what follows must load what was just stored.
        cycleContinuationEXT_ = true;
        pendingDrawsUseModernResourcesEXT_ = false;
    }

    void WebGPURenderer::BeforeModernResourceWriteEXT()
    {
        if (pendingDrawsUseModernResourcesEXT_) FlushPendingDrawsForModernEXT();
    }

    WGPUShaderModule WebGPURenderer::CreateShaderModuleCheckedEXT(
        const std::string& wgsl, const char* label, std::string& error)
    {
        WGPUShaderSourceWGSL source{};
        source.chain.sType = WGPUSType_ShaderSourceWGSL;
        source.code = WGPUStringView{wgsl.data(), wgsl.size()};
        WGPUShaderModuleDescriptor descriptor{};
        descriptor.label = Label(label);
        descriptor.nextInChain = &source.chain;
        ScopeState scope;
        wgpuDevicePushErrorScope(device_, WGPUErrorFilter_Validation);
        WGPUShaderModule module = wgpuDeviceCreateShaderModule(device_, &descriptor);
        WGPUPopErrorScopeCallbackInfo callback{};
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.callback = OnScopePopped;
        callback.userdata1 = &scope;
        wgpuDevicePopErrorScope(device_, callback);
        WaitForCompletionEXT(scope.completed, "WGSL module compile");
        if (!scope.ok || module == nullptr)
        {
            if (module != nullptr) wgpuShaderModuleRelease(module);
            error = std::string(label) + ": " +
                    (scope.message.empty() ? std::string("WGSL validation failed") : scope.message);
            return nullptr;
        }
        return module;
    }

    WGPUComputePipeline WebGPURenderer::CreateComputePipelineCheckedEXT(
        WGPUShaderModule module, const char* entryPoint, WGPUPipelineLayout layout,
        std::string& error)
    {
        WGPUComputePipelineDescriptor descriptor{};
        descriptor.label = Label("CNA WebGPU compute pipeline");
        descriptor.layout = layout;
        descriptor.compute.module = module;
        descriptor.compute.entryPoint = Label(entryPoint);
        ScopeState scope;
        wgpuDevicePushErrorScope(device_, WGPUErrorFilter_Validation);
        WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device_, &descriptor);
        WGPUPopErrorScopeCallbackInfo callback{};
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.callback = OnScopePopped;
        callback.userdata1 = &scope;
        wgpuDevicePopErrorScope(device_, callback);
        WaitForCompletionEXT(scope.completed, "compute pipeline creation");
        if (!scope.ok || pipeline == nullptr)
        {
            if (pipeline != nullptr) wgpuComputePipelineRelease(pipeline);
            error = "compute pipeline: " +
                    (scope.message.empty() ? std::string("validation failed") : scope.message);
            return nullptr;
        }
        return pipeline;
    }

    WGPUSampler WebGPURenderer::SamplerForUnitEXT(int unit)
    {
        const SlotSamplerState state =
            unit >= 0 && static_cast<std::size_t>(unit) < slotSamplers_.size()
                ? slotSamplers_[static_cast<std::size_t>(unit)]
                : SlotSamplerState{};
        return GetOrCreateSlotSampler(state.filter, state.addressU, state.addressV, state.addressW,
                                      state.maxMipLevel, state.maxAnisotropy, "modern");
    }

    WGPUCommandEncoder WebGPURenderer::BeginModernEncoderEXT(const char* label)
    {
        WGPUCommandEncoderDescriptor descriptor{};
        descriptor.label = Label(label);
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &descriptor);
        if (encoder == nullptr)
            throw std::runtime_error("CNA WebGPU: wgpuDeviceCreateCommandEncoder failed");
        return encoder;
    }

    void WebGPURenderer::SubmitModernEncoderEXT(WGPUCommandEncoder encoder,
                                                std::vector<WGPUBuffer> transient,
                                                std::vector<WGPUBindGroup> bindGroups)
    {
        WGPUCommandBufferDescriptor descriptor{};
        descriptor.label = Label("CNA WebGPU modern commands");
        WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &descriptor);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(queue_, 1, &commands);
        ++queueSubmitCount_;
        wgpuCommandBufferRelease(commands);
        // The submission holds its own references; a pooled buffer reused by a later queue write
        // is written after this submission executes, because queue operations execute in order.
        for (WGPUBindGroup group : bindGroups) wgpuBindGroupRelease(group);
        for (WGPUBuffer buffer : transient) RecycleTransientBuffer(buffer);
    }

    // ---- compute -------------------------------------------------------------------------------

    void WebGPURenderer::SubmitComputeDispatchEXT(ModernDispatchEXT dispatch)
    {
        if (deviceLost_)
            throw std::runtime_error("CNA WebGPU: the device is lost; a dispatch is refused");
        FlushPendingDrawsForModernEXT();

        std::vector<WGPUBuffer> transient;
        std::vector<WGPUBindGroup> groups;
        const WebGPUProgramLayoutEXT& layout = *dispatch.layout;
        for (std::uint32_t g = 0; g < layout.GroupCount(); ++g)
        {
            std::vector<WGPUBindGroupEntry> entries;
            if (g == 0)
            {
                for (const ModernBindingEXT& b : dispatch.bindings)
                {
                    const WebGPUBindingSlotEXT* slot = layout.Find(0, b.binding);
                    WGPUBindGroupEntry e = WGPU_BIND_GROUP_ENTRY_INIT;
                    e.binding = b.binding;
                    switch (slot->kind)
                    {
                    case WgslResourceKind::UniformBuffer:
                    case WgslResourceKind::StorageBuffer:
                    case WgslResourceKind::ReadOnlyStorageBuffer:
                        e.buffer = b.buffer->Buffer();
                        e.offset = 0;
                        e.size = b.buffer->NativeSize();
                        break;
                    case WgslResourceKind::Sampler:
                    case WgslResourceKind::ComparisonSampler:
                        e.sampler = b.sampler;
                        break;
                    default:
                        e.textureView = b.textureView;
                        break;
                    }
                    entries.push_back(e);
                }
            }
            else if (g == WebGPUComputeShaderRenderer::kScalarGroup &&
                     layout.Find(g, 0) != nullptr)
            {
                const std::uint64_t size = std::max<std::uint64_t>(
                    AlignUp(dispatch.scalarBytes.size(), 16), 16);
                WGPUBuffer block = AcquireTransientBuffer(
                    WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, size);
                std::vector<std::uint8_t> bytes = dispatch.scalarBytes;
                bytes.resize(static_cast<std::size_t>(size), 0);
                wgpuQueueWriteBuffer(queue_, block, 0, bytes.data(), bytes.size());
                transient.push_back(block);
                WGPUBindGroupEntry e = WGPU_BIND_GROUP_ENTRY_INIT;
                e.binding = 0;
                e.buffer = block;
                e.size = size;
                entries.push_back(e);
            }
            WGPUBindGroupDescriptor descriptor{};
            descriptor.label = Label("CNA WebGPU compute BindGroup");
            descriptor.layout = layout.GroupLayout(g);
            descriptor.entryCount = entries.size();
            descriptor.entries = entries.empty() ? nullptr : entries.data();
            groups.push_back(wgpuDeviceCreateBindGroup(device_, &descriptor));
        }

        WGPUCommandEncoder encoder = BeginModernEncoderEXT("CNA WebGPU compute dispatch");
        WGPUComputePassDescriptor passDescriptor{};
        passDescriptor.label = Label("CNA WebGPU compute pass");
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDescriptor);
        wgpuComputePassEncoderSetPipeline(pass, dispatch.pipeline);
        for (std::uint32_t g = 0; g < groups.size(); ++g)
            wgpuComputePassEncoderSetBindGroup(pass, g, groups[g], 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, dispatch.groups[0], dispatch.groups[1],
                                                 dispatch.groups[2]);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        SubmitModernEncoderEXT(encoder, std::move(transient), std::move(groups));
    }

    // ---- buffers -------------------------------------------------------------------------------

    void WebGPURenderer::EnsureByteCopyPipelineEXT()
    {
        if (byteCopyPipeline_ != nullptr) return;
        std::string error;
        const WgslModuleReflection reflection = ReflectWgsl(kByteCopyWgsl);
        byteCopyLayout_ = WebGPUProgramLayoutEXT::Create(
            device_, {{&reflection, WGPUShaderStage_Compute}}, error);
        if (byteCopyLayout_ == nullptr)
            throw std::runtime_error("CNA WebGPU: byte-copy layout: " + error);
        byteCopyModule_ = CreateShaderModuleCheckedEXT(kByteCopyWgsl, "CNA WebGPU byte copy", error);
        if (byteCopyModule_ == nullptr)
            throw std::runtime_error("CNA WebGPU: " + error);
        byteCopyPipeline_ = CreateComputePipelineCheckedEXT(
            byteCopyModule_, "main", byteCopyLayout_->PipelineLayout(), error);
        if (byteCopyPipeline_ == nullptr)
            throw std::runtime_error("CNA WebGPU: byte copy " + error);
    }

    void WebGPURenderer::ReleaseModernDeviceObjectsEXT()
    {
        drawStorageBuffers_.clear();
        const auto releaseNeutral = [](WGPUTexture& texture, WGPUTextureView& view) {
            if (view != nullptr) wgpuTextureViewRelease(view);
            if (texture != nullptr) wgpuTextureRelease(texture);
            view = nullptr;
            texture = nullptr;
        };
        releaseNeutral(neutralCubeTexture_, neutralCubeView_);
        releaseNeutral(neutralVolumeTexture_, neutralVolumeView_);
        releaseNeutral(neutralArrayTexture_, neutralArrayView_);
        if (byteCopyPipeline_ != nullptr) wgpuComputePipelineRelease(byteCopyPipeline_);
        if (byteCopyModule_ != nullptr) wgpuShaderModuleRelease(byteCopyModule_);
        byteCopyPipeline_ = nullptr;
        byteCopyModule_ = nullptr;
        byteCopyLayout_.reset();
    }

    void WebGPURenderer::EncodeByteCopyEXT(WGPUCommandEncoder encoder, WGPUBuffer destination,
                                           std::uint64_t destinationOffset, WGPUBuffer source,
                                           std::uint64_t sourceOffset, std::uint64_t size,
                                           std::vector<WGPUBuffer>& transient,
                                           std::vector<WGPUBindGroup>& bindGroups)
    {
        EnsureByteCopyPipelineEXT();
        const std::uint64_t firstWord = destinationOffset / 4;
        const std::uint64_t lastWord = (destinationOffset + size + 3) / 4;   // exclusive
        const std::uint64_t perDispatch =
            static_cast<std::uint64_t>(deviceLimits_.maxComputeWorkgroupsPerDimension) * 64u;
        WGPUComputePassDescriptor passDescriptor{};
        passDescriptor.label = Label("CNA WebGPU byte copy");
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDescriptor);
        wgpuComputePassEncoderSetPipeline(pass, byteCopyPipeline_);
        for (std::uint64_t word = firstWord; word < lastWord; word += perDispatch)
        {
            const std::uint64_t count = std::min(perDispatch, lastWord - word);
            const std::uint32_t params[8] = {
                static_cast<std::uint32_t>(destinationOffset), static_cast<std::uint32_t>(sourceOffset),
                static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(word),
                static_cast<std::uint32_t>(count), 0, 0, 0};
            WGPUBuffer block = AcquireTransientBuffer(
                WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst, sizeof(params));
            wgpuQueueWriteBuffer(queue_, block, 0, params, sizeof(params));
            transient.push_back(block);
            std::array<WGPUBindGroupEntry, 3> entries{};
            for (auto& e : entries) e = WGPU_BIND_GROUP_ENTRY_INIT;
            entries[0].binding = 0;
            entries[0].buffer = destination;
            entries[0].size = wgpuBufferGetSize(destination);
            entries[1].binding = 1;
            entries[1].buffer = source;
            entries[1].size = wgpuBufferGetSize(source);
            entries[2].binding = 2;
            entries[2].buffer = block;
            entries[2].size = sizeof(params);
            WGPUBindGroupDescriptor descriptor{};
            descriptor.label = Label("CNA WebGPU byte copy BindGroup");
            descriptor.layout = byteCopyLayout_->GroupLayout(0);
            descriptor.entryCount = entries.size();
            descriptor.entries = entries.data();
            WGPUBindGroup group = wgpuDeviceCreateBindGroup(device_, &descriptor);
            bindGroups.push_back(group);
            wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(
                pass, static_cast<std::uint32_t>((count + 63) / 64), 1, 1);
        }
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
    }

    void WebGPURenderer::WriteBufferBytesEXT(WGPUBuffer buffer, std::uint64_t offset,
                                             const void* data, std::uint64_t size)
    {
        if (deviceLost_) throw std::runtime_error("CNA WebGPU: the device is lost; a write is refused");
        BeforeModernResourceWriteEXT();
        if (offset % 4 == 0 && size % 4 == 0)
        {
            wgpuQueueWriteBuffer(queue_, buffer, offset, data, static_cast<std::size_t>(size));
            return;
        }
        // An unaligned range: stage the bytes (padded) and merge them in with the byte-copy kernel.
        const std::uint64_t stagedSize = std::max<std::uint64_t>(AlignUp(size, 4), 4);
        WGPUBuffer staging = AcquireTransientBuffer(
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc, stagedSize);
        std::vector<std::uint8_t> padded(static_cast<std::size_t>(stagedSize), 0);
        std::memcpy(padded.data(), data, static_cast<std::size_t>(size));
        wgpuQueueWriteBuffer(queue_, staging, 0, padded.data(), padded.size());
        std::vector<WGPUBuffer> transient{staging};
        std::vector<WGPUBindGroup> groups;
        WGPUCommandEncoder encoder = BeginModernEncoderEXT("CNA WebGPU unaligned buffer write");
        EncodeByteCopyEXT(encoder, buffer, offset, staging, 0, size, transient, groups);
        SubmitModernEncoderEXT(encoder, std::move(transient), std::move(groups));
    }

    void WebGPURenderer::CopyBufferBytesEXT(WGPUBuffer source, std::uint64_t sourceOffset,
                                            WGPUBuffer destination,
                                            std::uint64_t destinationOffset, std::uint64_t size)
    {
        if (deviceLost_) throw std::runtime_error("CNA WebGPU: the device is lost; a copy is refused");
        BeforeModernResourceWriteEXT();
        std::vector<WGPUBuffer> transient;
        std::vector<WGPUBindGroup> groups;
        WGPUCommandEncoder encoder = BeginModernEncoderEXT("CNA WebGPU buffer copy");
        const bool aligned = sourceOffset % 4 == 0 && destinationOffset % 4 == 0 && size % 4 == 0;
        if (aligned && source != destination)
        {
            wgpuCommandEncoderCopyBufferToBuffer(encoder, source, sourceOffset, destination,
                                                 destinationOffset, size);
        }
        else
        {
            // WebGPU forbids a copy whose source and destination are one buffer, and an unaligned
            // range needs the byte kernel, which must not alias its input and output bindings. Both
            // go through a staging copy of the source's covering words.
            const std::uint64_t start = sourceOffset / 4 * 4;
            const std::uint64_t end = AlignUp(sourceOffset + size, 4);
            WGPUBuffer staging = AcquireTransientBuffer(
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc,
                end - start);
            transient.push_back(staging);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, source, start, staging, 0, end - start);
            if (aligned)
                wgpuCommandEncoderCopyBufferToBuffer(encoder, staging, sourceOffset - start,
                                                     destination, destinationOffset, size);
            else
                EncodeByteCopyEXT(encoder, destination, destinationOffset, staging,
                                  sourceOffset - start, size, transient, groups);
        }
        SubmitModernEncoderEXT(encoder, std::move(transient), std::move(groups));
    }

    void WebGPURenderer::ReadBufferBytesEXT(WGPUBuffer source, std::uint64_t offset, void* out,
                                            std::uint64_t size)
    {
        if (deviceLost_) throw std::runtime_error("CNA WebGPU: the device is lost; a readback is refused");
        // A draw may write a storage buffer (fragment-stage read_write storage is core WebGPU), so a
        // readback puts every queued draw into the order first.
        FlushPendingDrawsForModernEXT();
        const std::uint64_t start = offset / 4 * 4;
        const std::uint64_t end = AlignUp(offset + size, 4);
        WGPUBufferDescriptor descriptor{};
        descriptor.label = Label("CNA WebGPU buffer readback");
        descriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        descriptor.size = end - start;
        WGPUBuffer readback = wgpuDeviceCreateBuffer(device_, &descriptor);
        if (readback == nullptr)
            throw std::runtime_error("CNA WebGPU: the readback buffer could not be created");
        WGPUCommandEncoder encoder = BeginModernEncoderEXT("CNA WebGPU buffer readback");
        wgpuCommandEncoderCopyBufferToBuffer(encoder, source, start, readback, 0, end - start);
        SubmitModernEncoderEXT(encoder, {}, {});

        ReadState state;
        WGPUBufferMapCallbackInfo info{};
        info.mode = WGPUCallbackMode_AllowProcessEvents;
        info.callback = OnReadMapped;
        info.userdata1 = &state;
        wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, static_cast<std::size_t>(end - start), info);
        try
        {
            WaitForCompletionEXT(state.completed, "StorageBuffer readback");
        }
        catch (...)
        {
            // The callback still names `state`; it must land before `state` goes out of scope.
            wgpuBufferRelease(readback);
            throw;
        }
        if (state.status != WGPUMapAsyncStatus_Success)
        {
            wgpuBufferRelease(readback);
            throw std::runtime_error("CNA WebGPU: StorageBuffer readback map failed: " + state.message);
        }
        const auto* mapped = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(readback, 0, static_cast<std::size_t>(end - start)));
        if (mapped != nullptr)
            std::memcpy(out, mapped + (offset - start), static_cast<std::size_t>(size));
        wgpuBufferUnmap(readback);
        wgpuBufferRelease(readback);
        if (mapped == nullptr)
            throw std::runtime_error("CNA WebGPU: StorageBuffer readback mapped a null range");
    }

    // ---- textures ------------------------------------------------------------------------------

    void WebGPURenderer::WriteTextureRegionEXT(WGPUTexture texture, int mipLevel, int layer, int x,
                                               int y, int width, int height, int bytesPerTexel,
                                               const void* data)
    {
        if (deviceLost_) throw std::runtime_error("CNA WebGPU: the device is lost; an upload is refused");
        BeforeModernResourceWriteEXT();
        WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destination.texture = texture;
        destination.mipLevel = static_cast<std::uint32_t>(mipLevel);
        destination.origin = {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                              static_cast<std::uint32_t>(layer)};
        destination.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.offset = 0;
        layout.bytesPerRow = static_cast<std::uint32_t>(width * bytesPerTexel);
        layout.rowsPerImage = static_cast<std::uint32_t>(height);
        const WGPUExtent3D extent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
        wgpuQueueWriteTexture(queue_, &destination, data,
                              static_cast<std::size_t>(width) * height * bytesPerTexel, &layout,
                              &extent);
    }

    void WebGPURenderer::ReadTextureRegionEXT(WGPUTexture texture, int mipLevel, int layer, int x,
                                              int y, int width, int height, int bytesPerTexel,
                                              void* data)
    {
        if (deviceLost_) throw std::runtime_error("CNA WebGPU: the device is lost; a readback is refused");
        FlushPendingDrawsForModernEXT();
        const std::uint32_t tightRow = static_cast<std::uint32_t>(width * bytesPerTexel);
        const std::uint32_t paddedRow = static_cast<std::uint32_t>(AlignUp(tightRow, 256));
        const std::uint64_t size = static_cast<std::uint64_t>(paddedRow) * height;
        WGPUBufferDescriptor descriptor{};
        descriptor.label = Label("CNA WebGPU texture readback");
        descriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        descriptor.size = size;
        WGPUBuffer readback = wgpuDeviceCreateBuffer(device_, &descriptor);
        if (readback == nullptr)
            throw std::runtime_error("CNA WebGPU: the texture readback buffer could not be created");

        WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        source.texture = texture;
        source.mipLevel = static_cast<std::uint32_t>(mipLevel);
        source.origin = {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                         static_cast<std::uint32_t>(layer)};
        source.aspect = WGPUTextureAspect_All;
        WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        destination.buffer = readback;
        destination.layout.offset = 0;
        destination.layout.bytesPerRow = paddedRow;
        destination.layout.rowsPerImage = static_cast<std::uint32_t>(height);
        const WGPUExtent3D extent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), 1};
        WGPUCommandEncoder encoder = BeginModernEncoderEXT("CNA WebGPU texture readback");
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &extent);
        SubmitModernEncoderEXT(encoder, {}, {});

        ReadState state;
        WGPUBufferMapCallbackInfo info{};
        info.mode = WGPUCallbackMode_AllowProcessEvents;
        info.callback = OnReadMapped;
        info.userdata1 = &state;
        wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, static_cast<std::size_t>(size), info);
        try
        {
            WaitForCompletionEXT(state.completed, "texture readback");
        }
        catch (...)
        {
            wgpuBufferRelease(readback);
            throw;
        }
        if (state.status != WGPUMapAsyncStatus_Success)
        {
            wgpuBufferRelease(readback);
            throw std::runtime_error("CNA WebGPU: texture readback map failed: " + state.message);
        }
        const auto* mapped = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(readback, 0, static_cast<std::size_t>(size)));
        if (mapped != nullptr)
            for (int row = 0; row < height; ++row)
                std::memcpy(static_cast<std::uint8_t*>(data) + static_cast<std::size_t>(row) * tightRow,
                            mapped + static_cast<std::size_t>(row) * paddedRow, tightRow);
        wgpuBufferUnmap(readback);
        wgpuBufferRelease(readback);
        if (mapped == nullptr)
            throw std::runtime_error("CNA WebGPU: texture readback mapped a null range");
    }

    // ---- timestamps ----------------------------------------------------------------------------

    void WebGPURenderer::WriteTimestampEXT(WGPUQuerySet querySet, std::uint32_t index,
                                           WGPUBuffer resolveBuffer, WGPUBuffer readbackBuffer)
    {
        if (deviceLost_) throw std::runtime_error("CNA WebGPU: the device is lost; a timer is refused");
        FlushPendingDrawsForModernEXT();
        // Core WebGPU writes timestamps only at pass boundaries (encoder-level writes are a
        // wgpu-native extension), so a timestamp is an empty compute pass that records one.
        WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        writes.querySet = querySet;
        writes.beginningOfPassWriteIndex = index;
        writes.endOfPassWriteIndex = WGPU_QUERY_SET_INDEX_UNDEFINED;
        WGPUComputePassDescriptor passDescriptor{};
        passDescriptor.label = Label("CNA WebGPU timestamp");
        passDescriptor.timestampWrites = &writes;
        WGPUCommandEncoder encoder = BeginModernEncoderEXT("CNA WebGPU timestamp");
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDescriptor);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        if (resolveBuffer != nullptr)
        {
            wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, 2, resolveBuffer, 0);
            if (readbackBuffer != nullptr)
                wgpuCommandEncoderCopyBufferToBuffer(encoder, resolveBuffer, 0, readbackBuffer, 0,
                                                     2 * sizeof(std::uint64_t));
        }
        SubmitModernEncoderEXT(encoder, {}, {});
    }

    // ---- WMG-0013: indirect draws ---------------------------------------------------------------

    namespace
    {
        /// `CNA::Graphics::StorageBufferUsage::IndirectArguments`, as the portable bit the public
        /// descriptor carries. Named here rather than included because the enum lives in the
        /// engine layer, which this renderer does not depend on.
        constexpr std::uint32_t kStorageUsageIndirectArgumentsEXT = UINT32_C(1) << 3;
    }

    bool WebGPURenderer::IssueIndirectDrawIfRequestedEXT(
        WGPURenderPassEncoder pass, const WebGPUIndirectArgsEXT& indirect, const bool indexed)
    {
        if (!indirect.enabled || indirect.buffer == nullptr) return false;
        if (indexed)
            wgpuRenderPassEncoderDrawIndexedIndirect(pass, indirect.buffer, indirect.offset);
        else
            wgpuRenderPassEncoderDrawIndirect(pass, indirect.buffer, indirect.offset);
        return true;
    }

    void WebGPURenderer::DrawPrimitivesIndirectEXT(
        const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
        const Matrix& projection, const PrimitiveType primitive,
        const IStorageBufferRenderer& argumentBuffer, const int argumentByteOffset,
        const GpuDrawParams& params)
    {
        QueueIndirectDrawEXT(vb, nullptr, world, view, projection, primitive, argumentBuffer,
                             argumentByteOffset, params);
    }

    void WebGPURenderer::DrawIndexedPrimitivesIndirectEXT(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib, const Matrix& world,
        const Matrix& view, const Matrix& projection, const PrimitiveType primitive,
        const IStorageBufferRenderer& argumentBuffer, const int argumentByteOffset,
        const GpuDrawParams& params)
    {
        QueueIndirectDrawEXT(vb, &ib, world, view, projection, primitive, argumentBuffer,
                             argumentByteOffset, params);
    }

    void WebGPURenderer::QueueIndirectDrawEXT(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib, const Matrix& world,
        const Matrix& view, const Matrix& projection, const PrimitiveType primitive,
        const IStorageBufferRenderer& argumentBuffer, const int argumentByteOffset,
        const GpuDrawParams& params)
    {
        if (!SupportsIndirectDrawEXT())
            throw System::NotSupportedException(
                "CNA WebGPU: this device cannot execute an indirect draw whose arguments carry a "
                "first instance (the optional IndirectFirstInstance feature is absent).");
#if defined(CNA_WEBGPU_COMPILED_EFFECTS)
        if (params.compiledEffectRuntime != nullptr)
            throw System::NotSupportedException(
                "CNA WebGPU: an indirect draw does not accept a compiled (FX) effect; that route "
                "needs the primitive count this command deliberately leaves on the GPU.");
#endif
        if (fillModeWireframe_)
            throw System::NotSupportedException(
                "CNA WebGPU: an indirect draw cannot be rendered as wireframe here. The wireframe "
                "route rewrites a draw's own triangles into line segments at queue time, and an "
                "indirect draw's triangles are not known until the GPU reads them.");

        const auto* nativeArguments =
            dynamic_cast<const WebGPUStorageBufferRenderer*>(&argumentBuffer);
        if (nativeArguments == nullptr || !nativeArguments->IsOwnedByEXT(this) ||
            nativeArguments->Buffer() == nullptr)
            throw std::invalid_argument(
                "CNA WebGPU: indirect arguments must be a StorageBuffer belonging to this device.");
        // WGPUBufferUsage_Indirect is only set when the caller declared IndirectArguments, so the
        // check is on the portable usage the caller actually asked for rather than on the native
        // bits this renderer happens to add.
        if ((nativeArguments->GetUsageEXT() & kStorageUsageIndirectArgumentsEXT) == 0)
            throw System::NotSupportedException(
                "CNA WebGPU: this StorageBuffer was not declared with IndirectArguments usage, so "
                "the GPU may not read a draw command out of it. Refused rather than bound as "
                "something it is not.");

        // The public StorageBuffer owns its renderer record through shared_ptr, and retaining that
        // record is what keeps a Dispose() between this call and the flush from leaving a queued
        // command holding a freed WGPUBuffer. A renderer object created directly has no such
        // portable lifetime and is refused instead of borrowed.
        std::shared_ptr<const IStorageBufferRenderer> argumentLifetime =
            argumentBuffer.weak_from_this().lock();
        if (argumentLifetime == nullptr)
            throw System::NotSupportedException(
                "CNA WebGPU: a deferred indirect draw needs a tracked StorageBuffer, so its native "
                "argument allocation can be retained until the command is recorded.");

        // The counts are never copied to the CPU -- that is the whole point of the route -- so the
        // draw is queued through the ORDINARY path with a legal zero primitive count, purely to
        // capture this call's effect, declaration, pipeline state, viewport and scissor. Every
        // Queue*Draw already snapshots the COMPLETE vertex and index window rather than the
        // primitiveCount's worth, so the geometry the GPU will index into is already there.
        GpuDrawParams seed = params;
        seed.firstInstance = 0;
        const std::size_t orderBefore = drawOrder_.size();
        if (ib != nullptr && FirstInstanceStream(seed) != nullptr)
            DrawInstancedPrimitivesEx(vb, *ib, world, view, projection, primitive, 0, 1, seed);
        else if (ib != nullptr)
            DrawIndexedPrimitivesEx(vb, *ib, world, view, projection, primitive, 0, seed);
        else
            DrawPrimitivesEx(vb, world, view, projection, primitive, 0, seed);
        if (drawOrder_.size() != orderBefore + 1)
            throw std::runtime_error(
                "CNA WebGPU: indirect state capture did not produce exactly one queued draw.");

        WebGPUIndirectArgsEXT indirect;
        indirect.lifetime = std::move(argumentLifetime);
        indirect.buffer = nativeArguments->Buffer();
        indirect.offset = static_cast<std::uint64_t>(argumentByteOffset);
        indirect.enabled = true;

        const DrawOrderEntry& entry = drawOrder_.back();
        const std::size_t index = static_cast<std::size_t>(entry.index);
        switch (entry.family)
        {
        case DrawFamily::Colored:      coloredDrawCommands_[index].indirect = indirect; break;
        case DrawFamily::Textured:     texturedDrawCommands_[index].indirect = indirect; break;
        case DrawFamily::LitTextured:  litTexturedDrawCommands_[index].indirect = indirect; break;
        case DrawFamily::AlphaTest:    alphaTestDrawCommands_[index].indirect = indirect; break;
        case DrawFamily::DualTexture:  dualTextureDrawCommands_[index].indirect = indirect; break;
        case DrawFamily::EnvMap:       envMapDrawCommands_[index].indirect = indirect; break;
        case DrawFamily::Instanced:    instancedDrawCommands_[index].indirect = indirect; break;
        case DrawFamily::Pbr:          pbrDrawCommands_[index].indirect = indirect; break;
        case DrawFamily::Skinned:      skinnedDrawCommands_[index].indirect = indirect; break;
        case DrawFamily::SkinnedPbr:   skinnedPbrDrawCommands_[index].indirect = indirect; break;
        case DrawFamily::CustomEffect: customEffectDrawCommands_[index].indirect = indirect; break;
        default:
            throw System::NotSupportedException(
                std::string("CNA WebGPU: the ") + DrawFamilyName(entry.family) +
                " draw family has no indirect route.");
        }
    }

}
