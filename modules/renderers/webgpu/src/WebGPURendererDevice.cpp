// SPDX-License-Identifier: MS-PL
//
// What the renderer answers about the device it created: the adapter's description, the shader
// language it takes, the limits and per-format usage CNA's capability profile reports, checked
// WGSL module compilation, and the queue uploads that happen outside a render-target flush.

#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include "CNA/RendererCapabilityProfile.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

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

        /// One SurfaceFormat's native texel format, for the per-format usage table.
        struct SampledFormat
        {
            WGPUTextureFormat format = WGPUTextureFormat_Undefined;
            bool filterable = true;   ///< false for 32-bit float without the feature
        };

        [[nodiscard]] SampledFormat SampledFormatFor(int surfaceFormat, bool float32Filterable)
        {
            using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
            switch (static_cast<SurfaceFormat>(surfaceFormat))
            {
            case SurfaceFormat::Color:           return {WGPUTextureFormat_RGBA8Unorm, true};
            case SurfaceFormat::NormalizedByte2: return {WGPUTextureFormat_RG8Snorm, true};
            case SurfaceFormat::NormalizedByte4: return {WGPUTextureFormat_RGBA8Snorm, true};
            case SurfaceFormat::Single:          return {WGPUTextureFormat_R32Float, float32Filterable};
            case SurfaceFormat::Vector2:         return {WGPUTextureFormat_RG32Float, float32Filterable};
            case SurfaceFormat::Vector4:         return {WGPUTextureFormat_RGBA32Float, float32Filterable};
            case SurfaceFormat::HalfSingle:      return {WGPUTextureFormat_R16Float, true};
            case SurfaceFormat::HalfVector2:     return {WGPUTextureFormat_RG16Float, true};
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:    return {WGPUTextureFormat_RGBA16Float, true};
            case SurfaceFormat::Rgba1010102:     return {WGPUTextureFormat_RGB10A2Unorm, true};
            case SurfaceFormat::ByteEXT:         return {WGPUTextureFormat_R8Unorm, true};
            default:                             return {};
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

    // ---- shader language, limits and formats ---------------------------------------------------

    bool WebGPURenderer::SupportsShaderLanguageEXT(int language, int stage) const
    {
        if (language != static_cast<int>(CNA::ShaderLanguageEXT::Wgsl) || device_ == nullptr)
            return false;
        return stage == static_cast<int>(CNA::ShaderStageEXT::Vertex) ||
               stage == static_cast<int>(CNA::ShaderStageEXT::Fragment);
    }

    std::uint64_t WebGPURenderer::GetMaxUniformBufferBytesEXT() const
    {
        return device_ != nullptr ? deviceLimits_.maxUniformBufferBindingSize : 0;
    }

    int WebGPURenderer::GetMaxSampledTexturesPerShaderStageEXT() const
    {
        return device_ != nullptr ? static_cast<int>(deviceLimits_.maxSampledTexturesPerShaderStage) : 0;
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

    std::uint64_t WebGPURenderer::GetMinUniformBufferOffsetAlignmentEXT() const
    {
        return device_ != nullptr ? deviceLimits_.minUniformBufferOffsetAlignment : 0;
    }

    CNA::RendererFormatSupport WebGPURenderer::GetSurfaceFormatUsageSupportEXT(int surfaceFormat) const
    {
        using CNA::RendererFormatUsage;
        if (device_ == nullptr) return {};
        const SampledFormat sampled = SampledFormatFor(surfaceFormat, float32Filterable_);
        if (sampled.format == WGPUTextureFormat_Undefined) return {};

        // TextureStorage and RenderTarget are left to the classic classification this renderer
        // already reports through ClassifySurfaceFormatEXT/ClassifyRenderTargetFormatEXT; this table
        // answers only the sampling and transfer usages, and only where the renderer has an
        // implemented path. A format the Texture2D path does not store faithfully cannot be sampled
        // as a Texture2D here, so it reports no texture usage at all rather than claiming a native
        // capability no CNA route reaches.
        constexpr std::uint32_t known =
            static_cast<std::uint32_t>(RendererFormatUsage::Sampled) |
            static_cast<std::uint32_t>(RendererFormatUsage::Filterable) |
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
        set(RendererFormatUsage::Filterable, sampled.filterable);
        set(RendererFormatUsage::TransferSource, true);
        set(RendererFormatUsage::TransferDestination, true);
        set(RendererFormatUsage::Mipmapped, true);
        return support;
    }

    // ---- waiting, module compilation, uploads --------------------------------------------------

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

    void WebGPURenderer::WriteTextureRegionEXT(WGPUTexture texture, int mipLevel, int layer, int x,
                                               int y, int width, int height, int bytesPerTexel,
                                               const void* data)
    {
        if (deviceLost_) throw std::runtime_error("CNA WebGPU: the device is lost; an upload is refused");
        // WEBGPUPERF-0006: a flush only records; the write must still land after those passes.
        OrderExternalQueueWorkEXT();
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
}
