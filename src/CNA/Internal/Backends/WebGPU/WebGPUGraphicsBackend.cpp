#include "CNA/Internal/Backends/WebGPU/WebGPUGraphicsBackend.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_video.h>
#if defined(__APPLE__) && __has_include(<SDL3/SDL_metal.h>)
#include <SDL3/SDL_metal.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <iostream>
#include <iterator>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace CNA::Internal::Backends::WebGPU
{
    namespace
    {
        constexpr std::uint64_t kMinimumBufferSize = 4;
        constexpr std::uint64_t kRequestTimeoutNanoseconds = 10'000'000'000ULL;

        [[nodiscard]] WGPUStringView StringView(const char* text)
        {
            return WGPUStringView{text, text != nullptr ? WGPU_STRLEN : 0};
        }

        [[nodiscard]] std::string ToString(WGPUStringView text)
        {
            if (text.data == nullptr)
                return {};
            if (text.length == WGPU_STRLEN)
                return std::string(text.data);
            return std::string(text.data, text.length);
        }

        [[nodiscard]] std::uint64_t Align4(std::uint64_t value)
        {
            return std::max(kMinimumBufferSize, (value + 3u) & ~std::uint64_t{3u});
        }

        [[nodiscard]] bool HasPresentMode(const WGPUSurfaceCapabilities& capabilities, WGPUPresentMode mode)
        {
            for (std::size_t i = 0; i < capabilities.presentModeCount; ++i)
            {
                if (capabilities.presentModes[i] == mode)
                    return true;
            }
            return false;
        }

        [[nodiscard]] bool HasSurfaceFormat(const WGPUSurfaceCapabilities& capabilities, WGPUTextureFormat format)
        {
            for (std::size_t i = 0; i < capabilities.formatCount; ++i)
            {
                if (capabilities.formats[i] == format)
                    return true;
            }
            return false;
        }

        struct AdapterRequestState
        {
            WGPUAdapter adapter = nullptr;
            std::string error;
            bool completed = false;
        };

        void OnAdapterRequest(WGPURequestAdapterStatus status,
                              WGPUAdapter adapter,
                              WGPUStringView message,
                              void* userdata1,
                              void*)
        {
            auto& state = *static_cast<AdapterRequestState*>(userdata1);
            if (status == WGPURequestAdapterStatus_Success)
                state.adapter = adapter;
            else
                state.error = ToString(message);
            state.completed = true;
        }

        struct DeviceRequestState
        {
            WGPUDevice device = nullptr;
            std::string error;
            bool completed = false;
        };

        void OnDeviceRequest(WGPURequestDeviceStatus status,
                             WGPUDevice device,
                             WGPUStringView message,
                             void* userdata1,
                             void*)
        {
            auto& state = *static_cast<DeviceRequestState*>(userdata1);
            if (status == WGPURequestDeviceStatus_Success)
                state.device = device;
            else
                state.error = ToString(message);
            state.completed = true;
        }

        void OnUncapturedError(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*)
        {
            std::cerr << "CNA WebGPU uncaptured error (" << static_cast<int>(type) << "): "
                      << ToString(message) << '\n';
        }

        void OnDeviceLost(WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message, void*, void*)
        {
            std::cerr << "CNA WebGPU device lost (" << static_cast<int>(reason) << "): "
                      << ToString(message) << '\n';
        }

        struct BufferMapState
        {
            WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
            std::string error;
            bool completed = false;
        };

        void OnBufferMap(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata1, void*)
        {
            auto& state = *static_cast<BufferMapState*>(userdata1);
            state.status = status;
            if (status != WGPUMapAsyncStatus_Success)
                state.error = ToString(message);
            state.completed = true;
        }

        [[nodiscard]] std::uint32_t AlignBytesPerRow(std::uint32_t bytesPerRow)
        {
            constexpr std::uint32_t kAlignment = 256;
            return (bytesPerRow + kAlignment - 1) / kAlignment * kAlignment;
        }

        // XNA CompareFunction ordinals -> WGPUCompareFunction (mirrors Vulkan's own ToVkCompareOp):
        // Always=0, Never=1, Less=2, LessEqual=3, Equal=4, GreaterEqual=5, Greater=6, NotEqual=7.
        [[nodiscard]] WGPUCompareFunction ToWGPUCompareFunction(int xnaCompare)
        {
            switch (xnaCompare)
            {
                case 1: return WGPUCompareFunction_Never;
                case 2: return WGPUCompareFunction_Less;
                case 3: return WGPUCompareFunction_LessEqual;
                case 4: return WGPUCompareFunction_Equal;
                case 5: return WGPUCompareFunction_GreaterEqual;
                case 6: return WGPUCompareFunction_Greater;
                case 7: return WGPUCompareFunction_NotEqual;
                default: return WGPUCompareFunction_Always;
            }
        }

        [[nodiscard]] WGPUAddressMode ToAddressMode(int mode)
        {
            switch (mode)
            {
                case 0: return WGPUAddressMode_Repeat;
                case 2: return WGPUAddressMode_MirrorRepeat;
                default: return WGPUAddressMode_ClampToEdge;
            }
        }

        [[nodiscard]] int SamplerCacheIndex(int filter, int addressU, int addressV)
        {
            const int filterIndex = filter == 0 ? 0 : 1;
            const int u = std::clamp(addressU, 0, 2);
            const int v = std::clamp(addressV, 0, 2);
            return filterIndex * 9 + u * 3 + v;
        }

        // Mirrors VulkanGraphicsBackend::DrawColoredPrimitives()'s own use of
        // FillExtPushConst()'s byte layout: this path carries no BasicEffect diffuse/
        // VertexColorEnabled (no GpuDrawParams at all), so it preserves the historical XNA
        // behaviour of outputting the raw vertex colours unmodified (diffuseColor=white,
        // vertexColorEnabled=true), everything else left zeroed.
        void FillColoredUniforms(std::array<float, 32>& out, const Matrix& world, const Matrix& view,
                                 const Matrix& projection)
        {
            const Matrix wvp = world * view * projection;
            wvp.ToColumnMajor(out.data());
            out[16] = 1.0f; out[17] = 1.0f; out[18] = 1.0f; out[19] = 1.0f;
            for (int i = 20; i < 31; ++i) out[i] = 0.0f;
            out[31] = 1.0f;
        }

        // Mirrors VulkanGraphicsBackend::FillExtPushConst() field-for-field (real GpuDrawParams
        // this time, not DrawColoredPrimitives()'s hardcoded white/vertex-color-always-true
        // values) -- used by DrawPrimitivesEx()'s stride-16 dispatch so a BasicEffect draw's real
        // DiffuseColor/VertexColorEnabled actually reach the shader.
        void FillExtUniforms(std::array<float, 32>& out, const Matrix& wvp, const GpuDrawParams& p)
        {
            wvp.ToColumnMajor(out.data());
            out[16] = p.diffuseColor[0]; out[17] = p.diffuseColor[1];
            out[18] = p.diffuseColor[2]; out[19] = p.diffuseColor[3];
            out[20] = p.ambientColor[0]; out[21] = p.ambientColor[1]; out[22] = p.ambientColor[2];
            out[23] = p.lightingEnabled ? 1.0f : 0.0f;
            out[24] = p.light0Dir[0]; out[25] = p.light0Dir[1]; out[26] = p.light0Dir[2];
            out[27] = p.textureEnabled ? 1.0f : 0.0f;
            out[28] = p.light0Diffuse[0]; out[29] = p.light0Diffuse[1]; out[30] = p.light0Diffuse[2];
            out[31] = p.vertexColorEnabled ? 1.0f : 0.0f;
        }

        // Normal matrix = inverse(world3x3), via the cofactor/det shortcut, applied directly to
        // the already-GPU-space (dumped) world matrix -- mirrors
        // BgfxGraphicsBackend::ComputeNormalMatrix3x3() byte-for-byte (verified independently
        // against FNA's own Lighting.fxh: HLSL computes WorldInverseTranspose =
        // Transpose(Invert(World)) on the CPU and applies it as mul(normal, WorldInverseTranspose)
        // -- a row-vector multiply. Working through this codebase's established
        // dump(M) = M^T GPU-column-major convention for both World and the result reduces to
        // exactly this cofactor inverse of the dumped array, with no shader-side transpose.
        void ComputeNormalMatrix3x3(const float* w, float out[9])
        {
            const float a = w[0], d = w[1], g = w[2];
            const float b = w[4], e = w[5], h = w[6];
            const float c = w[8], f = w[9], i = w[10];
            const float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
            const float invDet = (det != 0.0f) ? (1.0f / det) : 0.0f;
            out[0] = (e * i - f * h) * invDet; out[1] = -(b * i - c * h) * invDet; out[2] = (b * f - c * e) * invDet;
            out[3] = -(d * i - f * g) * invDet; out[4] = (a * i - c * g) * invDet; out[5] = -(a * f - c * d) * invDet;
            out[6] = (d * h - e * g) * invDet; out[7] = -(a * h - b * g) * invDet; out[8] = (a * e - b * d) * invDet;
        }

        // Secondary UBO for lit_textured3d.wgsl: DirectionalLight1/DirectionalLight2, EmissiveColor,
        // World (for world-space position), EyePosition, per-light SpecularColor, material
        // SpecularColor/Power, and the 3x3 normal matrix -- forwarded here since the primary
        // 128-byte uniform block (FillExtUniforms) is already fully packed. Mirrors
        // VulkanGraphicsBackend's LitLightParams UBO field-for-field (minus fog, deliberately
        // deferred like the other WebGPU 3D shaders).
        void FillLitLightUniforms(std::array<float, 68>& out, const GpuDrawParams& p)
        {
            out[0] = p.light1Dir[0]; out[1] = p.light1Dir[1]; out[2] = p.light1Dir[2]; out[3] = 0.0f;
            out[4] = p.light1Diffuse[0]; out[5] = p.light1Diffuse[1]; out[6] = p.light1Diffuse[2]; out[7] = 0.0f;
            out[8] = p.light2Dir[0]; out[9] = p.light2Dir[1]; out[10] = p.light2Dir[2]; out[11] = 0.0f;
            out[12] = p.light2Diffuse[0]; out[13] = p.light2Diffuse[1]; out[14] = p.light2Diffuse[2]; out[15] = 0.0f;
            out[16] = p.emissiveColor[0]; out[17] = p.emissiveColor[1]; out[18] = p.emissiveColor[2]; out[19] = 0.0f;
            for (int wi = 0; wi < 16; ++wi) out[20 + wi] = p.worldColMajor[wi];
            out[36] = p.eyePositionWorld[0]; out[37] = p.eyePositionWorld[1]; out[38] = p.eyePositionWorld[2]; out[39] = 0.0f;
            out[40] = p.light0Specular[0]; out[41] = p.light0Specular[1]; out[42] = p.light0Specular[2]; out[43] = 0.0f;
            out[44] = p.light1Specular[0]; out[45] = p.light1Specular[1]; out[46] = p.light1Specular[2]; out[47] = 0.0f;
            out[48] = p.light2Specular[0]; out[49] = p.light2Specular[1]; out[50] = p.light2Specular[2]; out[51] = 0.0f;
            out[52] = p.specularColor[0]; out[53] = p.specularColor[1]; out[54] = p.specularColor[2]; out[55] = p.specularPower;
            float normalMatrix[9];
            ComputeNormalMatrix3x3(p.worldColMajor, normalMatrix);
            out[56] = normalMatrix[0]; out[57] = normalMatrix[1]; out[58] = normalMatrix[2]; out[59] = 0.0f;
            out[60] = normalMatrix[3]; out[61] = normalMatrix[4]; out[62] = normalMatrix[5]; out[63] = 0.0f;
            out[64] = normalMatrix[6]; out[65] = normalMatrix[7]; out[66] = normalMatrix[8]; out[67] = 0.0f;
        }

        // Mirrors VulkanGraphicsBackend::FillAlphaTestPushConst() field-for-field. AlphaTestEffect
        // has no lighting, so this repurposes the [20..23]/[24] slots (ambient/light0/
        // vertexColorEnabled in FillExtUniforms) for {alphaRef, alphaTolerance, passWeight,
        // failWeight, vertexColorEnabled} instead -- same 128-byte total size, so it still fits
        // the existing coloredBindGroupLayout_ unchanged.
        void FillAlphaTestUniforms(std::array<float, 32>& out, const Matrix& wvp, const GpuDrawParams& p)
        {
            wvp.ToColumnMajor(out.data());
            out[16] = p.diffuseColor[0]; out[17] = p.diffuseColor[1];
            out[18] = p.diffuseColor[2]; out[19] = p.diffuseColor[3];
            out[20] = p.alphaTest[0]; out[21] = p.alphaTest[1];
            out[22] = p.alphaTest[2]; out[23] = p.alphaTest[3];
            out[24] = p.vertexColorEnabled ? 1.0f : 0.0f;
            for (int i = 25; i < 32; ++i) out[i] = 0.0f;
        }

        // pbr3d.wgsl's third (small) uniform buffer: PbrEffect's MetallicFactor/RoughnessFactor,
        // the only per-draw PBR-specific scalars not already covered by FillExtUniforms()'s
        // diffuseColor/ambientColor or FillLitLightUniforms()'s emissiveColor/world/eyePos.
        void FillPbrFactors(std::array<float, 4>& out, const GpuDrawParams& p)
        {
            out[0] = p.pbrMetallicFactor;
            out[1] = p.pbrRoughnessFactor;
            out[2] = 0.0f;
            out[3] = 0.0f;
        }

        [[nodiscard]] bool IsSurfaceRecoverable(WGPUSurfaceGetCurrentTextureStatus status)
        {
            return status == WGPUSurfaceGetCurrentTextureStatus_Timeout ||
                   status == WGPUSurfaceGetCurrentTextureStatus_Outdated ||
                   status == WGPUSurfaceGetCurrentTextureStatus_Lost;
        }

        void WaitForCompletion(WGPUInstance instance, const bool& completed, const char* operation)
        {
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::nanoseconds(kRequestTimeoutNanoseconds);
            while (!completed && std::chrono::steady_clock::now() < deadline)
            {
                wgpuInstanceProcessEvents(instance);
                if (!completed)
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if (!completed)
            {
                throw std::runtime_error(
                    std::string("CNA WebGPU: timed out waiting for ") + operation);
            }
        }
    }

    WebGPUTextureBackend::WebGPUTextureBackend(WebGPUGraphicsBackend& owner, const ImageData& data)
        : owner_(&owner), width_(data.width), height_(data.height), mipLevels_(std::max(1, data.mipLevels))
    {
        if (width_ <= 0 || height_ <= 0)
            throw std::invalid_argument("CNA WebGPU: texture dimensions must be positive");

        if (!data.pixels.empty())
        {
            const std::size_t required = static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u;
            if (data.pixels.size() < required)
                throw std::invalid_argument("CNA WebGPU: Texture2D RGBA buffer is smaller than width*height*4");
        }

        WGPUTextureDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU Texture2D");
        descriptor.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size = WGPUExtent3D{static_cast<std::uint32_t>(width_), static_cast<std::uint32_t>(height_), 1};
        descriptor.format = WGPUTextureFormat_RGBA8Unorm;
        descriptor.mipLevelCount = static_cast<std::uint32_t>(mipLevels_);
        descriptor.sampleCount = 1;
        texture_ = wgpuDeviceCreateTexture(owner.Device(), &descriptor);
        if (texture_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Texture2D");

        view_ = wgpuTextureCreateView(texture_, nullptr);
        if (view_ == nullptr)
        {
            wgpuTextureRelease(texture_);
            texture_ = nullptr;
            throw std::runtime_error("CNA WebGPU: failed to create Texture2D view");
        }

        if (!data.pixels.empty())
            UpdatePixels(data.pixels.data(), width_ * 4);
    }

    WebGPUTextureBackend::~WebGPUTextureBackend()
    {
        if (view_ != nullptr)
            wgpuTextureViewRelease(view_);
        if (texture_ != nullptr)
            wgpuTextureRelease(texture_);
    }

    void WebGPUTextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (rgba == nullptr)
            throw std::invalid_argument("CNA WebGPU: texture update source cannot be null");
        if (stride < width_ * 4)
            throw std::invalid_argument("CNA WebGPU: texture update stride is too small");

        std::vector<std::uint8_t> tightlyPacked;
        const std::uint8_t* upload = rgba;
        if (stride != width_ * 4)
        {
            tightlyPacked.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) * 4u);
            for (int y = 0; y < height_; ++y)
            {
                std::memcpy(tightlyPacked.data() + static_cast<std::size_t>(y) * width_ * 4u,
                            rgba + static_cast<std::size_t>(y) * stride,
                            static_cast<std::size_t>(width_) * 4u);
            }
            upload = tightlyPacked.data();
        }
        UpdatePixelsLevel(0, upload, width_, height_);
    }

    void WebGPUTextureBackend::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (level < 0 || level >= mipLevels_ || rgba == nullptr || levelW <= 0 || levelH <= 0)
            throw std::invalid_argument("CNA WebGPU: invalid mip upload");

        WGPUTexelCopyTextureInfo destination{};
        destination.texture = texture_;
        destination.mipLevel = static_cast<std::uint32_t>(level);
        destination.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferLayout layout{};
        layout.bytesPerRow = static_cast<std::uint32_t>(levelW * 4);
        layout.rowsPerImage = static_cast<std::uint32_t>(levelH);
        const WGPUExtent3D extent{static_cast<std::uint32_t>(levelW), static_cast<std::uint32_t>(levelH), 1};
        const std::size_t byteCount = static_cast<std::size_t>(levelW) * static_cast<std::size_t>(levelH) * 4u;
        wgpuQueueWriteTexture(owner_->Queue(), &destination, rgba, byteCount, &layout, &extent);
    }

    WebGPUVertexBufferBackend::WebGPUVertexBufferBackend(WebGPUGraphicsBackend& owner, int vertexCapacity)
        : owner_(&owner), vertexCapacity_(std::max(0, vertexCapacity))
    {
    }

    WebGPUVertexBufferBackend::~WebGPUVertexBufferBackend()
    {
        if (buffer_ != nullptr)
            wgpuBufferRelease(buffer_);
    }

    void WebGPUVertexBufferBackend::SetData(const void* data, int vertexCount, std::size_t strideInBytes)
    {
        SetDataWithOptions(data, vertexCount, strideInBytes, SetDataOptions{});
    }

    void WebGPUVertexBufferBackend::SetDataWithOptions(const void* data,
                                                        int vertexCount,
                                                        std::size_t strideInBytes,
                                                        SetDataOptions)
    {
        if (data == nullptr || vertexCount < 0 || strideInBytes == 0)
            throw std::invalid_argument("CNA WebGPU: invalid vertex buffer upload");
        if (vertexCapacity_ > 0 && vertexCount > vertexCapacity_)
            throw std::out_of_range("CNA WebGPU: vertex buffer upload exceeds declared capacity");

        const std::uint64_t required = Align4(static_cast<std::uint64_t>(vertexCount) * strideInBytes);
        if (buffer_ == nullptr || required > capacityBytes_)
        {
            if (buffer_ != nullptr)
                wgpuBufferRelease(buffer_);
            WGPUBufferDescriptor descriptor{};
            descriptor.label = StringView("CNA WebGPU VertexBuffer");
            descriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            descriptor.size = required;
            buffer_ = wgpuDeviceCreateBuffer(owner_->Device(), &descriptor);
            capacityBytes_ = required;
        }
        wgpuQueueWriteBuffer(owner_->Queue(), buffer_, 0, data,
                             static_cast<std::size_t>(vertexCount) * strideInBytes);
        vertexCount_ = vertexCount;
        stride_ = strideInBytes;

        const auto* bytes = static_cast<const std::uint8_t*>(data);
        shadowData_.assign(bytes, bytes + static_cast<std::size_t>(vertexCount) * strideInBytes);
    }

    WebGPUIndexBufferBackend::WebGPUIndexBufferBackend(WebGPUGraphicsBackend& owner,
                                                        int indexCapacity,
                                                        bool thirtyTwoBit)
        : owner_(&owner), indexCapacity_(std::max(0, indexCapacity)), thirtyTwoBit_(thirtyTwoBit)
    {
    }

    WebGPUIndexBufferBackend::~WebGPUIndexBufferBackend()
    {
        if (buffer_ != nullptr)
            wgpuBufferRelease(buffer_);
    }

    void WebGPUIndexBufferBackend::SetData16(const void* data, int indexCount) { Upload(data, indexCount, false); }
    void WebGPUIndexBufferBackend::SetData32(const void* data, int indexCount) { Upload(data, indexCount, true); }
    void WebGPUIndexBufferBackend::SetData16WithOptions(const void* data, int indexCount, SetDataOptions) { Upload(data, indexCount, false); }
    void WebGPUIndexBufferBackend::SetData32WithOptions(const void* data, int indexCount, SetDataOptions) { Upload(data, indexCount, true); }

    void WebGPUIndexBufferBackend::Upload(const void* data, int indexCount, bool dataIsThirtyTwoBit)
    {
        if (data == nullptr || indexCount < 0 || dataIsThirtyTwoBit != thirtyTwoBit_)
            throw std::invalid_argument("CNA WebGPU: invalid index buffer upload");
        if (indexCapacity_ > 0 && indexCount > indexCapacity_)
            throw std::out_of_range("CNA WebGPU: index buffer upload exceeds declared capacity");

        const std::size_t stride = thirtyTwoBit_ ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        const std::uint64_t required = Align4(static_cast<std::uint64_t>(indexCount) * stride);
        if (buffer_ == nullptr || required > capacityBytes_)
        {
            if (buffer_ != nullptr)
                wgpuBufferRelease(buffer_);
            WGPUBufferDescriptor descriptor{};
            descriptor.label = StringView("CNA WebGPU IndexBuffer");
            descriptor.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
            descriptor.size = required;
            buffer_ = wgpuDeviceCreateBuffer(owner_->Device(), &descriptor);
            capacityBytes_ = required;
        }
        wgpuQueueWriteBuffer(owner_->Queue(), buffer_, 0, data, static_cast<std::size_t>(indexCount) * stride);
        indexCount_ = indexCount;

        const auto* bytes = static_cast<const std::uint8_t*>(data);
        shadowData_.assign(bytes, bytes + static_cast<std::size_t>(indexCount) * stride);
    }

    WebGPUSpriteBatchBackend::WebGPUSpriteBatchBackend(WebGPUGraphicsBackend& owner) : owner_(&owner) {}

    void WebGPUSpriteBatchBackend::Begin()
    {
        if (begun_)
            throw std::logic_error("CNA WebGPU SpriteBatch.Begin called twice without End");
        begun_ = true;
    }

    void WebGPUSpriteBatchBackend::End()
    {
        if (!begun_)
            throw std::logic_error("CNA WebGPU SpriteBatch.End called without Begin");
        begun_ = false;
    }

    void WebGPUSpriteBatchBackend::SetCustomEffect(Effect* effect)
    {
        if (effect != nullptr)
            throw std::runtime_error("CNA WebGPU: custom SpriteBatch effects are not implemented yet");
    }

    void WebGPUSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        const Rectangle source{0, 0, texture.GetWidth(), texture.GetHeight()};
        const Rectangle destination{static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()};
        Draw(texture, destination, source, Color::White);
    }

    void WebGPUSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2::Zero,
             SpriteEffects::None, 0.0f);
    }

    void WebGPUSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color,
                                         float rotation,
                                         const Vector2& origin,
                                         SpriteEffects effects,
                                         float layerDepth)
    {
        if (!begun_)
            throw std::logic_error("CNA WebGPU SpriteBatch.Draw called outside Begin/End");
        const auto* webGpuTexture = dynamic_cast<const WebGPUTextureBackend*>(&texture);
        if (webGpuTexture == nullptr)
            throw std::invalid_argument("CNA WebGPU: SpriteBatch received a texture from another graphics backend");
        owner_->QueueSprite(*webGpuTexture, destinationRectangle, sourceRectangle, color, rotation,
                            origin, effects, layerDepth, transform_, textureFilter_, addressU_, addressV_);
    }

    WebGPUGraphicsBackend::WebGPUGraphicsBackend(SDL_Window* window,
                                                  int virtualWidth,
                                                  int virtualHeight,
                                                  CnaPresentationMode presentationMode,
                                                  int swapInterval)
        : window_(window),
          virtualWidth_(virtualWidth),
          virtualHeight_(virtualHeight),
          presentationMode_(presentationMode),
          swapInterval_(swapInterval)
    {
        if (window_ == nullptr)
            throw std::invalid_argument("CNA WebGPU: SDL window cannot be null");

        instance_ = wgpuCreateInstance(nullptr);
        if (instance_ == nullptr)
            throw std::runtime_error("CNA WebGPU: wgpuCreateInstance failed");

        try
        {
            CreateSurface();
            RequestAdapterAndDevice();
            ConfigureSurface(true);
            IGraphicsBackend::RegisterForWindow(window_, this);
        }
        catch (...)
        {
            DestroySpriteResources();
            for (WGPUSampler& sampler : samplerCache_)
            {
                if (sampler != nullptr) wgpuSamplerRelease(sampler);
                sampler = nullptr;
            }
            if (depthView_ != nullptr) wgpuTextureViewRelease(depthView_);
            if (depthTexture_ != nullptr) wgpuTextureRelease(depthTexture_);
            if (surfaceConfigured_ && surface_ != nullptr) wgpuSurfaceUnconfigure(surface_);
            if (queue_ != nullptr) wgpuQueueRelease(queue_);
            if (device_ != nullptr) wgpuDeviceRelease(device_);
            if (adapter_ != nullptr) wgpuAdapterRelease(adapter_);
            if (surface_ != nullptr) wgpuSurfaceRelease(surface_);
            if (instance_ != nullptr) wgpuInstanceRelease(instance_);
#if defined(__APPLE__) && __has_include(<SDL3/SDL_metal.h>)
            if (metalView_ != nullptr) SDL_Metal_DestroyView(metalView_);
#endif
            throw;
        }
    }

    WebGPUGraphicsBackend::~WebGPUGraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);
        DestroySpriteResources();
        DestroyColoredResources();
        DestroyTexturedResources();
        DestroyLitTexturedResources();
        DestroyAlphaTestResources();
        DestroyDualTextureResources();
        DestroyPbrResources();
        pbrDefaultWhiteTexture_.reset();
        pbrDefaultFlatNormalTexture_.reset();
        for (WGPUBindGroup bg : pendingBindGroupReleases_) wgpuBindGroupRelease(bg);
        for (WGPUBuffer buf : pendingBufferReleases_) wgpuBufferRelease(buf);
        for (WGPUSampler& sampler : samplerCache_)
        {
            if (sampler != nullptr)
                wgpuSamplerRelease(sampler);
            sampler = nullptr;
        }
        if (depthView_ != nullptr) wgpuTextureViewRelease(depthView_);
        if (depthTexture_ != nullptr) wgpuTextureRelease(depthTexture_);
        if (readbackBuffer_ != nullptr) wgpuBufferRelease(readbackBuffer_);
        if (hasAcquiredTexture_ && acquiredTexture_ != nullptr) wgpuTextureRelease(acquiredTexture_);
        if (surfaceConfigured_ && surface_ != nullptr) wgpuSurfaceUnconfigure(surface_);
        if (queue_ != nullptr) wgpuQueueRelease(queue_);
        if (device_ != nullptr) wgpuDeviceRelease(device_);
        if (adapter_ != nullptr) wgpuAdapterRelease(adapter_);
        if (surface_ != nullptr) wgpuSurfaceRelease(surface_);
        if (instance_ != nullptr) wgpuInstanceRelease(instance_);
#if defined(__APPLE__) && __has_include(<SDL3/SDL_metal.h>)
        if (metalView_ != nullptr) SDL_Metal_DestroyView(metalView_);
#endif
    }

    void WebGPUGraphicsBackend::CreateSurface()
    {
        SDL_PropertiesID properties = SDL_GetWindowProperties(window_);
        const char* driver = SDL_GetCurrentVideoDriver();
        WGPUSurfaceDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU Surface");

#if defined(_WIN32)
        WGPUSurfaceSourceWindowsHWND source{};
        source.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
        source.hinstance = GetModuleHandleW(nullptr);
        source.hwnd = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        if (source.hwnd == nullptr)
            throw std::runtime_error("CNA WebGPU: SDL did not expose a Win32 HWND");
        descriptor.nextInChain = &source.chain;
        surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
#elif defined(__APPLE__) && __has_include(<SDL3/SDL_metal.h>)
        metalView_ = SDL_Metal_CreateView(window_);
        if (metalView_ == nullptr)
            throw std::runtime_error(std::string("CNA WebGPU: SDL_Metal_CreateView failed: ") + SDL_GetError());
        WGPUSurfaceSourceMetalLayer source{};
        source.chain.sType = WGPUSType_SurfaceSourceMetalLayer;
        source.layer = SDL_Metal_GetLayer(metalView_);
        descriptor.nextInChain = &source.chain;
        surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
#elif defined(__ANDROID__) && defined(SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER)
        WGPUSurfaceSourceAndroidNativeWindow source{};
        source.chain.sType = WGPUSType_SurfaceSourceAndroidNativeWindow;
        source.window = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr);
        if (source.window == nullptr)
            throw std::runtime_error("CNA WebGPU: SDL did not expose Android native window");
        descriptor.nextInChain = &source.chain;
        surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
#elif defined(__linux__)
        if (driver != nullptr && std::strcmp(driver, "wayland") == 0)
        {
            WGPUSurfaceSourceWaylandSurface source{};
            source.chain.sType = WGPUSType_SurfaceSourceWaylandSurface;
            source.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
            source.surface = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
            if (source.display == nullptr || source.surface == nullptr)
                throw std::runtime_error("CNA WebGPU: SDL did not expose Wayland display/surface properties");
            descriptor.nextInChain = &source.chain;
            surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
        }
        else if (driver != nullptr && std::strcmp(driver, "x11") == 0)
        {
            WGPUSurfaceSourceXlibWindow source{};
            source.chain.sType = WGPUSType_SurfaceSourceXlibWindow;
            source.display = SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
            source.window = static_cast<std::uint64_t>(SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
            if (source.display == nullptr || source.window == 0)
                throw std::runtime_error("CNA WebGPU: SDL did not expose X11 display/window properties");
            descriptor.nextInChain = &source.chain;
            surface_ = wgpuInstanceCreateSurface(instance_, &descriptor);
        }
        else
        {
            throw std::runtime_error(std::string("CNA WebGPU: unsupported SDL Linux video driver: ") +
                                     (driver != nullptr ? driver : "unknown"));
        }
#else
        (void) properties;
        (void) driver;
        throw std::runtime_error("CNA WebGPU: native surface creation is unsupported on this platform");
#endif

        if (surface_ == nullptr)
            throw std::runtime_error("CNA WebGPU: wgpuInstanceCreateSurface failed");
    }

    void WebGPUGraphicsBackend::RequestAdapterAndDevice()
    {
        AdapterRequestState adapterState;
        WGPURequestAdapterOptions adapterOptions{};
        adapterOptions.compatibleSurface = surface_;
        WGPURequestAdapterCallbackInfo adapterCallback{};
        adapterCallback.mode = WGPUCallbackMode_AllowProcessEvents;
        adapterCallback.callback = OnAdapterRequest;
        adapterCallback.userdata1 = &adapterState;
        wgpuInstanceRequestAdapter(instance_, &adapterOptions, adapterCallback);
        WaitForCompletion(instance_, adapterState.completed, "adapter request");
        if (adapterState.adapter == nullptr)
            throw std::runtime_error("CNA WebGPU: adapter request failed: " + adapterState.error);
        adapter_ = adapterState.adapter;

        DeviceRequestState deviceState;
        WGPUDeviceDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU Device");
        descriptor.defaultQueue.label = StringView("CNA WebGPU Queue");
        descriptor.uncapturedErrorCallbackInfo.callback = OnUncapturedError;
        descriptor.deviceLostCallbackInfo.callback = OnDeviceLost;
        WGPURequestDeviceCallbackInfo callback{};
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.callback = OnDeviceRequest;
        callback.userdata1 = &deviceState;
        wgpuAdapterRequestDevice(adapter_, &descriptor, callback);
        WaitForCompletion(instance_, deviceState.completed, "device request");
        if (deviceState.device == nullptr)
            throw std::runtime_error("CNA WebGPU: device request failed: " + deviceState.error);
        device_ = deviceState.device;
        queue_ = wgpuDeviceGetQueue(device_);
        if (queue_ == nullptr)
            throw std::runtime_error("CNA WebGPU: device returned no queue");
    }

    void WebGPUGraphicsBackend::ConfigureSurface(bool force)
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        const bool minimized = (SDL_GetWindowFlags(window_) & SDL_WINDOW_MINIMIZED) != 0;
        if (minimized || width <= 0 || height <= 0)
        {
            if (surfaceConfigured_ && surface_ != nullptr)
                wgpuSurfaceUnconfigure(surface_);
            surfaceConfigured_ = false;
            physicalWidth_ = width;
            physicalHeight_ = height;
            RecreateDepthTexture();
            return;
        }
        if (!force && surfaceConfigured_ && width == physicalWidth_ && height == physicalHeight_)
            return;

        WGPUSurfaceCapabilities capabilities{};
        const WGPUStatus capabilitiesStatus = wgpuSurfaceGetCapabilities(surface_, adapter_, &capabilities);
        if (capabilitiesStatus != WGPUStatus_Success || capabilities.formatCount == 0)
        {
            wgpuSurfaceCapabilitiesFreeMembers(capabilities);
            throw std::runtime_error("CNA WebGPU: failed to query surface capabilities");
        }

        WGPUTextureFormat chosenFormat = capabilities.formats[0];
        constexpr WGPUTextureFormat preferredFormats[] = {
            WGPUTextureFormat_BGRA8UnormSrgb,
            WGPUTextureFormat_RGBA8UnormSrgb,
            WGPUTextureFormat_BGRA8Unorm,
            WGPUTextureFormat_RGBA8Unorm
        };
        for (const auto format : preferredFormats)
        {
            if (HasSurfaceFormat(capabilities, format))
            {
                chosenFormat = format;
                break;
            }
        }

        WGPUPresentMode presentMode = WGPUPresentMode_Fifo;
        if (swapInterval_ == 0)
        {
            if (HasPresentMode(capabilities, WGPUPresentMode_Immediate))
                presentMode = WGPUPresentMode_Immediate;
            else if (HasPresentMode(capabilities, WGPUPresentMode_Mailbox))
                presentMode = WGPUPresentMode_Mailbox;
        }
        else if (!HasPresentMode(capabilities, presentMode) && capabilities.presentModeCount > 0)
        {
            presentMode = capabilities.presentModes[0];
        }

        const WGPUCompositeAlphaMode alphaMode = capabilities.alphaModeCount > 0
            ? capabilities.alphaModes[0]
            : WGPUCompositeAlphaMode_Auto;

        const bool formatChanged = surfaceFormat_ != chosenFormat;
        surfaceFormat_ = chosenFormat;
        surfaceConfig_ = {};
        surfaceConfig_.device = device_;
        surfaceConfig_.format = surfaceFormat_;
        surfaceConfig_.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        surfaceConfig_.width = static_cast<std::uint32_t>(width);
        surfaceConfig_.height = static_cast<std::uint32_t>(height);
        surfaceConfig_.presentMode = presentMode;
        surfaceConfig_.alphaMode = alphaMode;
        wgpuSurfaceConfigure(surface_, &surfaceConfig_);
        wgpuSurfaceCapabilitiesFreeMembers(capabilities);

        physicalWidth_ = width;
        physicalHeight_ = height;
        surfaceConfigured_ = true;
        RecreateDepthTexture();
        if (formatChanged || spritePipelineBlend_ == nullptr)
            CreateSpriteResources();
        if (formatChanged || coloredShader_ == nullptr)
            CreateColoredResources();
        if (formatChanged || texturedShader_ == nullptr)
            CreateTexturedResources();
        if (formatChanged || litTexturedShader_ == nullptr)
            CreateLitTexturedResources();
        if (formatChanged || alphaTestShader_ == nullptr)
            CreateAlphaTestResources();
        if (formatChanged || dualTextureShader_ == nullptr)
            CreateDualTextureResources();
        if (formatChanged || pbrShader_ == nullptr)
            CreatePbrResources();
    }

    void WebGPUGraphicsBackend::RecreateDepthTexture()
    {
        if (depthView_ != nullptr) wgpuTextureViewRelease(depthView_);
        if (depthTexture_ != nullptr) wgpuTextureRelease(depthTexture_);
        depthView_ = nullptr;
        depthTexture_ = nullptr;
        if (physicalWidth_ <= 0 || physicalHeight_ <= 0)
            return;

        WGPUTextureDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU DepthStencil");
        descriptor.usage = WGPUTextureUsage_RenderAttachment;
        descriptor.dimension = WGPUTextureDimension_2D;
        descriptor.size = WGPUExtent3D{static_cast<std::uint32_t>(physicalWidth_),
                                       static_cast<std::uint32_t>(physicalHeight_), 1};
        descriptor.format = WGPUTextureFormat_Depth24PlusStencil8;
        descriptor.mipLevelCount = 1;
        descriptor.sampleCount = 1;
        depthTexture_ = wgpuDeviceCreateTexture(device_, &descriptor);
        if (depthTexture_ != nullptr)
            depthView_ = wgpuTextureCreateView(depthTexture_, nullptr);
    }

    void WebGPUGraphicsBackend::DestroySpriteResources()
    {
        if (spriteVertexBuffer_ != nullptr) wgpuBufferRelease(spriteVertexBuffer_);
        if (spritePipelineOpaque_ != nullptr) wgpuRenderPipelineRelease(spritePipelineOpaque_);
        if (spritePipelineBlend_ != nullptr) wgpuRenderPipelineRelease(spritePipelineBlend_);
        if (spritePipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(spritePipelineLayout_);
        if (spriteBindGroupLayout_ != nullptr) wgpuBindGroupLayoutRelease(spriteBindGroupLayout_);
        if (spriteShader_ != nullptr) wgpuShaderModuleRelease(spriteShader_);
        spriteVertexBuffer_ = nullptr;
        spritePipelineOpaque_ = nullptr;
        spritePipelineBlend_ = nullptr;
        spritePipelineLayout_ = nullptr;
        spriteBindGroupLayout_ = nullptr;
        spriteShader_ = nullptr;
        spriteVertexCapacityBytes_ = 0;
    }

    void WebGPUGraphicsBackend::CreateSpriteResources()
    {
        DestroySpriteResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined)
            return;

        static constexpr char shaderSource[] = R"WGSL(
struct VertexInput {
    @location(0) position: vec3f,
    @location(1) uv: vec2f,
    @location(2) color: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = vec4f(input.position, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}
@group(0) @binding(0) var spriteSampler: sampler;
@group(0) @binding(1) var spriteTexture: texture_2d<f32>;
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return textureSample(spriteTexture, spriteSampler, input.uv) * input.color;
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU SpriteBatch WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        spriteShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);

        std::array<WGPUBindGroupLayoutEntry, 2> layoutEntries{};
        layoutEntries[0].binding = 0;
        layoutEntries[0].visibility = WGPUShaderStage_Fragment;
        layoutEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;
        layoutEntries[1].binding = 1;
        layoutEntries[1].visibility = WGPUShaderStage_Fragment;
        layoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
        layoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        layoutEntries[1].texture.multisampled = false;
        WGPUBindGroupLayoutDescriptor bindLayoutDescriptor{};
        bindLayoutDescriptor.label = StringView("CNA WebGPU SpriteBatch BindGroupLayout");
        bindLayoutDescriptor.entryCount = layoutEntries.size();
        bindLayoutDescriptor.entries = layoutEntries.data();
        spriteBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bindLayoutDescriptor);

        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU SpriteBatch PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
        pipelineLayoutDescriptor.bindGroupLayouts = &spriteBindGroupLayout_;
        spritePipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        std::array<WGPUVertexAttribute, 3> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = offsetof(SpriteVertex, position);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Float32x2;
        attributes[1].offset = offsetof(SpriteVertex, uv);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WGPUVertexFormat_Float32x4;
        attributes[2].offset = offsetof(SpriteVertex, color);
        attributes[2].shaderLocation = 2;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(SpriteVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fragment{};
        fragment.module = spriteShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU SpriteBatch Pipeline");
        pipeline.layout = spritePipelineLayout_;
        pipeline.vertex.module = spriteShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = WGPUCullMode_None;
        pipeline.multisample.count = 1;
        pipeline.multisample.mask = std::numeric_limits<std::uint32_t>::max();
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        // Present() always uses the shared Depth24PlusStencil8 attachment. The SpriteBatch
        // pipeline must declare the same attachment format even though 2D sprites do not write
        // depth, otherwise wgpu-native rejects the render pass as incompatible.
        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
        depthStencil.depthCompare = WGPUCompareFunction_Always;
        pipeline.depthStencil = &depthStencil;

        // Task WEBGPU-91 finding: the sprite fragment shader outputs straight (non-premultiplied)
        // color -- textureSample(...) * input.color, matching Vulkan's own sprite2d.frag.glsl
        // exactly. Vulkan pairs that with SRC_ALPHA/ONE_MINUS_SRC_ALPHA (a straight-alpha "over"
        // blend); this backend previously used ONE/ONE_MINUS_SRC_ALPHA (a premultiplied-alpha
        // blend equation), which silently ignored partial source alpha entirely for colour (only
        // alpha=0 or alpha=255 ever looked correct -- any translucent tint rendered fully opaque).
        // Matching Vulkan's factors here, not premultiplying in the shader, keeps both backends
        // consistent with the same non-premultiplied shader source.
        WGPUBlendState blend{};
        blend.color.operation = WGPUBlendOperation_Add;
        blend.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blend.alpha.operation = WGPUBlendOperation_Add;
        blend.alpha.srcFactor = WGPUBlendFactor_One;
        blend.alpha.dstFactor = WGPUBlendFactor_Zero;
        target.blend = &blend;
        spritePipelineBlend_ = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        target.blend = nullptr;
        spritePipelineOpaque_ = wgpuDeviceCreateRenderPipeline(device_, &pipeline);

        if (spriteShader_ == nullptr || spriteBindGroupLayout_ == nullptr || spritePipelineLayout_ == nullptr ||
            spritePipelineBlend_ == nullptr || spritePipelineOpaque_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create SpriteBatch GPU resources");
    }

    void WebGPUGraphicsBackend::DestroyColoredResources()
    {
        for (auto& [key, pipe] : coloredPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        coloredPipelines_.clear();
        if (coloredPipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(coloredPipelineLayout_);
        if (coloredBindGroupLayout_ != nullptr) wgpuBindGroupLayoutRelease(coloredBindGroupLayout_);
        if (coloredShader_ != nullptr) wgpuShaderModuleRelease(coloredShader_);
        coloredPipelineLayout_ = nullptr;
        coloredBindGroupLayout_ = nullptr;
        coloredShader_ = nullptr;
    }

    void WebGPUGraphicsBackend::CreateColoredResources()
    {
        DestroyColoredResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined)
            return;

        // Uniform layout matches VulkanGraphicsBackend::FillExtPushConst()'s 128-byte/32-float
        // push-constant layout byte-for-byte (see plan_webgpu.md's Phase 57 entry-point note):
        // [0..15] MVP, [16..19] diffuseColor, [20..23] ambient+lightingEnabled,
        // [24..27] light0Dir+textureEnabled, [28..31] light0Diffuse+vertexColorEnabled. Only the
        // fields this minimal DrawColoredPrimitives slice actually reads are named; the rest keep
        // the same byte offsets so a future DrawPrimitivesEx (BasicEffect) shader can reuse this
        // exact uniform block unchanged.
        static constexpr char shaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec4f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    let vertexColorEnabled = u.light0DiffuseVertexColor.w;
    output.color = select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return input.color;
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU Colored3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        coloredShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);

        WGPUBindGroupLayoutEntry uniformEntry{};
        uniformEntry.binding = 0;
        uniformEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        uniformEntry.buffer.type = WGPUBufferBindingType_Uniform;
        uniformEntry.buffer.minBindingSize = 128;
        WGPUBindGroupLayoutDescriptor bindLayoutDescriptor{};
        bindLayoutDescriptor.label = StringView("CNA WebGPU Colored3D BindGroupLayout");
        bindLayoutDescriptor.entryCount = 1;
        bindLayoutDescriptor.entries = &uniformEntry;
        coloredBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bindLayoutDescriptor);

        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU Colored3D PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = 1;
        pipelineLayoutDescriptor.bindGroupLayouts = &coloredBindGroupLayout_;
        coloredPipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        if (coloredShader_ == nullptr || coloredBindGroupLayout_ == nullptr || coloredPipelineLayout_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Colored3D GPU resources");
    }

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineColored3D(WGPUPrimitiveTopology topology,
                                                                            bool depthTest, bool depthWrite,
                                                                            int depthFunc)
    {
        const int key = (static_cast<int>(topology) * 8 + depthFunc) * 4 + (depthTest ? 2 : 0) + (depthWrite ? 1 : 0);
        if (auto it = coloredPipelines_.find(key); it != coloredPipelines_.end())
            return it->second;

        struct ColoredVertex { float x, y, z; std::uint8_t r, g, b, a; };
        std::array<WGPUVertexAttribute, 2> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = offsetof(ColoredVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Unorm8x4;
        attributes[1].offset = offsetof(ColoredVertex, r);
        attributes[1].shaderLocation = 1;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(ColoredVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fragment{};
        fragment.module = coloredShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU Colored3D Pipeline");
        pipeline.layout = coloredPipelineLayout_;
        pipeline.vertex.module = coloredShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = WGPUCullMode_None;
        pipeline.multisample.count = 1;
        pipeline.multisample.mask = std::numeric_limits<std::uint32_t>::max();
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Colored3D pipeline");
        coloredPipelines_[key] = created;
        return created;
    }

    void WebGPUGraphicsBackend::DestroyTexturedResources()
    {
        for (auto& [key, pipe] : texturedPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        texturedPipelines_.clear();
        for (auto& [key, pipe] : coloredTexturedPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        coloredTexturedPipelines_.clear();
        if (texturedPipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(texturedPipelineLayout_);
        if (texturedBindGroupLayout_ != nullptr) wgpuBindGroupLayoutRelease(texturedBindGroupLayout_);
        if (texturedShader_ != nullptr) wgpuShaderModuleRelease(texturedShader_);
        if (coloredTexturedShader_ != nullptr) wgpuShaderModuleRelease(coloredTexturedShader_);
        texturedPipelineLayout_ = nullptr;
        texturedBindGroupLayout_ = nullptr;
        texturedShader_ = nullptr;
        coloredTexturedShader_ = nullptr;
    }

    void WebGPUGraphicsBackend::CreateTexturedResources()
    {
        DestroyTexturedResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || coloredBindGroupLayout_ == nullptr)
            return;

        // Uniform layout is the exact same group-0 UBO as colored3d.wgsl (coloredBindGroupLayout_,
        // reused verbatim -- see plan_webgpu.md's WEBGPU-13 note). Group 1 (sampler + texture)
        // mirrors the SpriteBatch bind group layout shape exactly.
        static constexpr char shaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let textureEnabled = u.light0DirTexture.w;
    let sampled = select(vec4f(1.0), textureSample(tex, texSampler, input.uv), textureEnabled > 0.5);
    return sampled * u.diffuseColor;
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU Textured3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        texturedShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);

        std::array<WGPUBindGroupLayoutEntry, 2> layoutEntries{};
        layoutEntries[0].binding = 0;
        layoutEntries[0].visibility = WGPUShaderStage_Fragment;
        layoutEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;
        layoutEntries[1].binding = 1;
        layoutEntries[1].visibility = WGPUShaderStage_Fragment;
        layoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
        layoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        layoutEntries[1].texture.multisampled = false;
        WGPUBindGroupLayoutDescriptor bindLayoutDescriptor{};
        bindLayoutDescriptor.label = StringView("CNA WebGPU Textured3D BindGroupLayout");
        bindLayoutDescriptor.entryCount = layoutEntries.size();
        bindLayoutDescriptor.entries = layoutEntries.data();
        texturedBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bindLayoutDescriptor);

        std::array<WGPUBindGroupLayout, 2> groupLayouts{coloredBindGroupLayout_, texturedBindGroupLayout_};
        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU Textured3D PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = groupLayouts.size();
        pipelineLayoutDescriptor.bindGroupLayouts = groupLayouts.data();
        texturedPipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        if (texturedShader_ == nullptr || texturedBindGroupLayout_ == nullptr || texturedPipelineLayout_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Textured3D GPU resources");

        // WEBGPU-21: colored_textured3d (stride 24, VertexPositionColorTexture) -- same UBO
        // (group 0) and texture (group 1) bind groups as textured3d.wgsl above, just a different
        // vertex layout (adds a per-vertex colour) and shader that mixes it with DiffuseColor
        // before sampling, matching colored3d.wgsl's own vertexColorEnabled mixing formula.
        static constexpr char coloredTexturedShaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec4f,
    @location(2) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) tint: vec4f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    let vertexColorEnabled = u.light0DiffuseVertexColor.w;
    output.tint = select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let textureEnabled = u.light0DirTexture.w;
    let sampled = select(vec4f(1.0), textureSample(tex, texSampler, input.uv), textureEnabled > 0.5);
    return sampled * input.tint;
}
)WGSL";

        WGPUShaderSourceWGSL coloredTexturedWgsl{};
        coloredTexturedWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        coloredTexturedWgsl.code = StringView(coloredTexturedShaderSource);
        WGPUShaderModuleDescriptor coloredTexturedShaderDescriptor{};
        coloredTexturedShaderDescriptor.label = StringView("CNA WebGPU ColoredTextured3D WGSL");
        coloredTexturedShaderDescriptor.nextInChain = &coloredTexturedWgsl.chain;
        coloredTexturedShader_ = wgpuDeviceCreateShaderModule(device_, &coloredTexturedShaderDescriptor);
        if (coloredTexturedShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create ColoredTextured3D shader");
    }

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineTextured3D(WGPUPrimitiveTopology topology,
                                                                            bool depthTest, bool depthWrite,
                                                                            int depthFunc)
    {
        const int key = (static_cast<int>(topology) * 8 + depthFunc) * 4 + (depthTest ? 2 : 0) + (depthWrite ? 1 : 0);
        if (auto it = texturedPipelines_.find(key); it != texturedPipelines_.end())
            return it->second;

        struct TexturedVertex { float x, y, z; float u, v; };
        std::array<WGPUVertexAttribute, 2> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = offsetof(TexturedVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Float32x2;
        attributes[1].offset = offsetof(TexturedVertex, u);
        attributes[1].shaderLocation = 1;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(TexturedVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fragment{};
        fragment.module = texturedShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU Textured3D Pipeline");
        pipeline.layout = texturedPipelineLayout_;
        pipeline.vertex.module = texturedShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = WGPUCullMode_None;
        pipeline.multisample.count = 1;
        pipeline.multisample.mask = std::numeric_limits<std::uint32_t>::max();
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Textured3D pipeline");
        texturedPipelines_[key] = created;
        return created;
    }

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineColoredTextured3D(WGPUPrimitiveTopology topology,
                                                                                    bool depthTest, bool depthWrite,
                                                                                    int depthFunc)
    {
        const int key = (static_cast<int>(topology) * 8 + depthFunc) * 4 + (depthTest ? 2 : 0) + (depthWrite ? 1 : 0);
        if (auto it = coloredTexturedPipelines_.find(key); it != coloredTexturedPipelines_.end())
            return it->second;

        struct ColoredTexturedVertex { float x, y, z; std::uint8_t r, g, b, a; float u, v; };
        std::array<WGPUVertexAttribute, 3> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = offsetof(ColoredTexturedVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Unorm8x4;
        attributes[1].offset = offsetof(ColoredTexturedVertex, r);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WGPUVertexFormat_Float32x2;
        attributes[2].offset = offsetof(ColoredTexturedVertex, u);
        attributes[2].shaderLocation = 2;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(ColoredTexturedVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fragment{};
        fragment.module = coloredTexturedShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU ColoredTextured3D Pipeline");
        pipeline.layout = texturedPipelineLayout_;
        pipeline.vertex.module = coloredTexturedShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = WGPUCullMode_None;
        pipeline.multisample.count = 1;
        pipeline.multisample.mask = std::numeric_limits<std::uint32_t>::max();
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create ColoredTextured3D pipeline");
        coloredTexturedPipelines_[key] = created;
        return created;
    }

    void WebGPUGraphicsBackend::DestroyLitTexturedResources()
    {
        for (auto& [key, pipe] : litTexturedPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        litTexturedPipelines_.clear();
        // Task 1105: the vertex-lit sibling's own pipeline cache + shader module, torn down
        // alongside the per-pixel-lit one -- litBindGroupLayout_/litPipelineLayout_ are shared by
        // both and only released once, below.
        for (auto& [key, pipe] : litTexturedVertexLitPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        litTexturedVertexLitPipelines_.clear();
        if (litTexturedVertexLitShader_ != nullptr) wgpuShaderModuleRelease(litTexturedVertexLitShader_);
        litTexturedVertexLitShader_ = nullptr;
        if (litPipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(litPipelineLayout_);
        if (litBindGroupLayout_ != nullptr) wgpuBindGroupLayoutRelease(litBindGroupLayout_);
        if (litTexturedShader_ != nullptr) wgpuShaderModuleRelease(litTexturedShader_);
        litPipelineLayout_ = nullptr;
        litBindGroupLayout_ = nullptr;
        litTexturedShader_ = nullptr;
    }

    void WebGPUGraphicsBackend::CreateLitTexturedResources()
    {
        DestroyLitTexturedResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || texturedBindGroupLayout_ == nullptr)
            return;

        // Ported from VulkanGraphicsBackend's lit_textured3d.{vert,frag}.glsl (itself ported from
        // FNA's Lighting.fxh ComputeLights()). Group 0 binding 0 is the same primary Uniforms
        // block as colored3d/textured3d (MVP, diffuseColor, ambientColor+lightingEnabled,
        // light0Dir+textureEnabled, light0Diffuse); binding 1 is the secondary LitLightParams
        // block (light1/light2, emissive, world, eye position, per-light specular, material
        // specular, normal matrix) filled by FillLitLightUniforms(). Group 1 (sampler + texture)
        // is texturedBindGroupLayout_, reused unchanged.
        static constexpr char shaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) worldNormal: vec3f,
    @location(2) worldPos: vec3f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    output.worldNormal = normalMatrix * input.normal;
    output.worldPos = (lp.world * vec4f(input.position, 1.0)).xyz;
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let textureEnabled = u.light0DirTexture.w;
    let sampled = select(vec4f(1.0), textureSample(tex, texSampler, input.uv), textureEnabled > 0.5);
    let lightingEnabled = u.ambientLighting.w;
    if (lightingEnabled <= 0.5) {
        return u.diffuseColor * sampled;
    }
    let n = normalize(input.worldNormal);
    let e = normalize(lp.eyePos.xyz - input.worldPos);
    // A disabled/unconfigured DirectionalLight forwards Direction=(0,0,0) (its DiffuseColor/
    // SpecularColor are what get zeroed, matching FNA's own DirectionalLight.cs -- Direction
    // itself is untouched by Enabled=false). normalize() on a true zero vector is undefined and
    // can poison the whole lightSum/specular computation with NaN on real GPU hardware, even
    // though that light's own diffuse/specular contribution is already zero -- guard it here.
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let nl0 = select(vec3f(0.0), normalize(u.light0DirTexture.xyz), dir0sq > 0.0);
    let nl1 = select(vec3f(0.0), normalize(lp.light1Dir.xyz), dir1sq > 0.0);
    let nl2 = select(vec3f(0.0), normalize(lp.light2Dir.xyz), dir2sq > 0.0);
    let dotl0 = dot(n, -nl0); let zerol0 = step(0.0, dotl0); let ndotl0 = max(dotl0, 0.0);
    let dotl1 = dot(n, -nl1); let zerol1 = step(0.0, dotl1); let ndotl1 = max(dotl1, 0.0);
    let dotl2 = dot(n, -nl2); let zerol2 = step(0.0, dotl2); let ndotl2 = max(dotl2, 0.0);
    let lightSum = u.ambientLighting.xyz + ndotl0 * u.light0DiffuseVertexColor.xyz
                   + ndotl1 * lp.light1Diffuse.xyz + ndotl2 * lp.light2Diffuse.xyz;
    let h0 = normalize(e - nl0); let spec0 = pow(max(dot(h0, n), 0.0) * zerol0, lp.specularColorPower.w);
    let h1 = normalize(e - nl1); let spec1 = pow(max(dot(h1, n), 0.0) * zerol1, lp.specularColorPower.w);
    let h2 = normalize(e - nl2); let spec2 = pow(max(dot(h2, n), 0.0) * zerol2, lp.specularColorPower.w);
    let specularRgb = (spec0 * lp.light0Specular.xyz + spec1 * lp.light1Specular.xyz
                       + spec2 * lp.light2Specular.xyz) * lp.specularColorPower.xyz;
    let lit = lightSum * u.diffuseColor.rgb + lp.emissiveColor.xyz;
    var color = vec4f(lit, u.diffuseColor.a) * sampled;
    color = vec4f(color.rgb + specularRgb * color.a, color.a);
    return color;
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU LitTextured3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        litTexturedShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);

        // Task 1105 (plan_graphics.md Phase 80): real per-vertex-lit sibling -- identical
        // Blinn-Phong math to shaderSource above (FNA's Lighting.fxh ComputeLights()), moved from
        // fs_main into vs_main and passed onward as litRGB/specularRGB varyings (WGSL naturally
        // interpolates any @location(n) VertexOutput field across the triangle -- this alone is
        // what gives Gouraud shading, no separate interpolation logic needed). fs_main keeps the
        // exact same lightingEnabled<=0.5 unlit branch and non-lighting math (texture sample)
        // unchanged, just consuming the interpolated value instead of recomputing it per fragment.
        // Same UBO/binding layout as the per-pixel-lit shader (reuses litBindGroupLayout_/
        // litPipelineLayout_ unchanged below), so only a new shader module is needed here.
        static constexpr char vertexLitShaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) litRGB: vec3f,
    @location(2) specularRGB: vec3f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    let worldNormal = normalMatrix * input.normal;
    let worldPos = (lp.world * vec4f(input.position, 1.0)).xyz;
    let n = normalize(worldNormal);
    let e = normalize(lp.eyePos.xyz - worldPos);
    // Same disabled-light NaN guard as the per-pixel-lit shader: a disabled DirectionalLight
    // forwards Direction=(0,0,0) (only DiffuseColor/SpecularColor are zeroed), and normalize() on
    // a true zero vector is undefined and can poison the whole sum with NaN.
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let nl0 = select(vec3f(0.0), normalize(u.light0DirTexture.xyz), dir0sq > 0.0);
    let nl1 = select(vec3f(0.0), normalize(lp.light1Dir.xyz), dir1sq > 0.0);
    let nl2 = select(vec3f(0.0), normalize(lp.light2Dir.xyz), dir2sq > 0.0);
    let dotl0 = dot(n, -nl0); let zerol0 = step(0.0, dotl0); let ndotl0 = max(dotl0, 0.0);
    let dotl1 = dot(n, -nl1); let zerol1 = step(0.0, dotl1); let ndotl1 = max(dotl1, 0.0);
    let dotl2 = dot(n, -nl2); let zerol2 = step(0.0, dotl2); let ndotl2 = max(dotl2, 0.0);
    let lightSum = u.ambientLighting.xyz + ndotl0 * u.light0DiffuseVertexColor.xyz
                   + ndotl1 * lp.light1Diffuse.xyz + ndotl2 * lp.light2Diffuse.xyz;
    let h0 = normalize(e - nl0); let spec0 = pow(max(dot(h0, n), 0.0) * zerol0, lp.specularColorPower.w);
    let h1 = normalize(e - nl1); let spec1 = pow(max(dot(h1, n), 0.0) * zerol1, lp.specularColorPower.w);
    let h2 = normalize(e - nl2); let spec2 = pow(max(dot(h2, n), 0.0) * zerol2, lp.specularColorPower.w);
    output.specularRGB = (spec0 * lp.light0Specular.xyz + spec1 * lp.light1Specular.xyz
                          + spec2 * lp.light2Specular.xyz) * lp.specularColorPower.xyz;
    output.litRGB = lightSum * u.diffuseColor.rgb + lp.emissiveColor.xyz;
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let textureEnabled = u.light0DirTexture.w;
    let sampled = select(vec4f(1.0), textureSample(tex, texSampler, input.uv), textureEnabled > 0.5);
    let lightingEnabled = u.ambientLighting.w;
    if (lightingEnabled <= 0.5) {
        return u.diffuseColor * sampled;
    }
    var color = vec4f(input.litRGB, u.diffuseColor.a) * sampled;
    color = vec4f(color.rgb + input.specularRGB * color.a, color.a);
    return color;
}
)WGSL";

        WGPUShaderSourceWGSL vertexLitWgsl{};
        vertexLitWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        vertexLitWgsl.code = StringView(vertexLitShaderSource);
        WGPUShaderModuleDescriptor vertexLitShaderDescriptor{};
        vertexLitShaderDescriptor.label = StringView("CNA WebGPU LitTextured3D VertexLit WGSL");
        vertexLitShaderDescriptor.nextInChain = &vertexLitWgsl.chain;
        litTexturedVertexLitShader_ = wgpuDeviceCreateShaderModule(device_, &vertexLitShaderDescriptor);

        std::array<WGPUBindGroupLayoutEntry, 2> layoutEntries{};
        layoutEntries[0].binding = 0;
        layoutEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
        layoutEntries[0].buffer.minBindingSize = 128;
        layoutEntries[1].binding = 1;
        layoutEntries[1].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        layoutEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
        layoutEntries[1].buffer.minBindingSize = 272;
        WGPUBindGroupLayoutDescriptor bindLayoutDescriptor{};
        bindLayoutDescriptor.label = StringView("CNA WebGPU LitTextured3D BindGroupLayout");
        bindLayoutDescriptor.entryCount = layoutEntries.size();
        bindLayoutDescriptor.entries = layoutEntries.data();
        litBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bindLayoutDescriptor);

        std::array<WGPUBindGroupLayout, 2> groupLayouts{litBindGroupLayout_, texturedBindGroupLayout_};
        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU LitTextured3D PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = groupLayouts.size();
        pipelineLayoutDescriptor.bindGroupLayouts = groupLayouts.data();
        litPipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        if (litTexturedShader_ == nullptr || litBindGroupLayout_ == nullptr || litPipelineLayout_ == nullptr ||
            litTexturedVertexLitShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create LitTextured3D GPU resources");
    }

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineLitTextured3D(WGPUPrimitiveTopology topology,
                                                                                bool depthTest, bool depthWrite,
                                                                                int depthFunc)
    {
        const int key = (static_cast<int>(topology) * 8 + depthFunc) * 4 + (depthTest ? 2 : 0) + (depthWrite ? 1 : 0);
        if (auto it = litTexturedPipelines_.find(key); it != litTexturedPipelines_.end())
            return it->second;

        struct LitTexturedVertex { float x, y, z, nx, ny, nz, u, v; };
        std::array<WGPUVertexAttribute, 3> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = offsetof(LitTexturedVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Float32x3;
        attributes[1].offset = offsetof(LitTexturedVertex, nx);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WGPUVertexFormat_Float32x2;
        attributes[2].offset = offsetof(LitTexturedVertex, u);
        attributes[2].shaderLocation = 2;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(LitTexturedVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fragment{};
        fragment.module = litTexturedShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU LitTextured3D Pipeline");
        pipeline.layout = litPipelineLayout_;
        pipeline.vertex.module = litTexturedShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = WGPUCullMode_None;
        pipeline.multisample.count = 1;
        pipeline.multisample.mask = std::numeric_limits<std::uint32_t>::max();
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create LitTextured3D pipeline");
        litTexturedPipelines_[key] = created;
        return created;
    }

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineLitTextured3DVertexLit(
        WGPUPrimitiveTopology topology, bool depthTest, bool depthWrite, int depthFunc)
    {
        const int key = (static_cast<int>(topology) * 8 + depthFunc) * 4 + (depthTest ? 2 : 0) + (depthWrite ? 1 : 0);
        if (auto it = litTexturedVertexLitPipelines_.find(key); it != litTexturedVertexLitPipelines_.end())
            return it->second;

        struct LitTexturedVertex { float x, y, z, nx, ny, nz, u, v; };
        std::array<WGPUVertexAttribute, 3> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = offsetof(LitTexturedVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Float32x3;
        attributes[1].offset = offsetof(LitTexturedVertex, nx);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WGPUVertexFormat_Float32x2;
        attributes[2].offset = offsetof(LitTexturedVertex, u);
        attributes[2].shaderLocation = 2;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(LitTexturedVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fragment{};
        fragment.module = litTexturedVertexLitShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU LitTextured3D VertexLit Pipeline");
        pipeline.layout = litPipelineLayout_;
        pipeline.vertex.module = litTexturedVertexLitShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = WGPUCullMode_None;
        pipeline.multisample.count = 1;
        pipeline.multisample.mask = std::numeric_limits<std::uint32_t>::max();
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create LitTextured3D VertexLit pipeline");
        litTexturedVertexLitPipelines_[key] = created;
        return created;
    }

    void WebGPUGraphicsBackend::DestroyAlphaTestResources()
    {
        for (auto& [key, pipe] : alphaTestPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        alphaTestPipelines_.clear();
        for (auto& [key, pipe] : alphaTestColoredPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        alphaTestColoredPipelines_.clear();
        if (alphaTestShader_ != nullptr) wgpuShaderModuleRelease(alphaTestShader_);
        if (alphaTestColoredShader_ != nullptr) wgpuShaderModuleRelease(alphaTestColoredShader_);
        alphaTestShader_ = nullptr;
        alphaTestColoredShader_ = nullptr;
    }

    void WebGPUGraphicsBackend::CreateAlphaTestResources()
    {
        DestroyAlphaTestResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || texturedBindGroupLayout_ == nullptr)
            return;

        // Ported from VulkanGraphicsBackend's alpha_test3d.{vert,frag}.glsl. AlphaTestEffect has
        // no lighting, so the primary Uniforms block repurposes [20..23]/[24] for
        // {alphaRef, alphaTolerance, passWeight, failWeight, vertexColorEnabled} instead (see
        // FillAlphaTestUniforms()) -- same 128-byte shape, so this reuses coloredBindGroupLayout_
        // (group 0) and texturedBindGroupLayout_ (group 1) unchanged; no new bind group layout or
        // pipeline layout needed.
        static constexpr char shaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    alphaTest: vec4f,
    extra: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(tex, texSampler, input.uv) * u.diffuseColor;
    let alpha = color.a;
    let useTolerance = u.alphaTest.y > 0.0;
    let lessTest = (alpha < u.alphaTest.x);
    let toleranceTest = (abs(alpha - u.alphaTest.x) < u.alphaTest.y);
    let passTest = select(lessTest, toleranceTest, useTolerance);
    let w = select(u.alphaTest.w, u.alphaTest.z, passTest);
    if (w < 0.0) {
        discard;
    }
    return color;
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU AlphaTest3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        alphaTestShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);
        if (alphaTestShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create AlphaTest3D shader");

        // WEBGPU-23: stride 24 (VertexPositionColorTexture) variant -- same shape as
        // colored_textured3d.wgsl's own vertex-colour mixing, combined with the alpha-test
        // discard above.
        static constexpr char coloredShaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    alphaTest: vec4f,
    extra: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec4f,
    @location(2) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) tint: vec4f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    let vertexColorEnabled = u.extra.x;
    output.tint = select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(tex, texSampler, input.uv) * input.tint;
    let alpha = color.a;
    let useTolerance = u.alphaTest.y > 0.0;
    let lessTest = (alpha < u.alphaTest.x);
    let toleranceTest = (abs(alpha - u.alphaTest.x) < u.alphaTest.y);
    let passTest = select(lessTest, toleranceTest, useTolerance);
    let w = select(u.alphaTest.w, u.alphaTest.z, passTest);
    if (w < 0.0) {
        discard;
    }
    return color;
}
)WGSL";

        WGPUShaderSourceWGSL coloredWgsl{};
        coloredWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        coloredWgsl.code = StringView(coloredShaderSource);
        WGPUShaderModuleDescriptor coloredShaderDescriptor{};
        coloredShaderDescriptor.label = StringView("CNA WebGPU AlphaTestColored3D WGSL");
        coloredShaderDescriptor.nextInChain = &coloredWgsl.chain;
        alphaTestColoredShader_ = wgpuDeviceCreateShaderModule(device_, &coloredShaderDescriptor);
        if (alphaTestColoredShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create AlphaTestColored3D shader");
    }

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineAlphaTest3D(std::size_t stride,
                                                                              WGPUPrimitiveTopology topology,
                                                                              bool depthTest, bool depthWrite,
                                                                              int depthFunc)
    {
        const int key = ((static_cast<int>(stride) * 8 + static_cast<int>(topology)) * 8 + depthFunc) * 4 +
                        (depthTest ? 2 : 0) + (depthWrite ? 1 : 0);
        auto& cache = (stride == 24) ? alphaTestColoredPipelines_ : alphaTestPipelines_;
        if (auto it = cache.find(key); it != cache.end())
            return it->second;

        WGPUVertexAttribute attributes[3]{};
        std::uint32_t attributeCount = 0;
        std::uint64_t arrayStride = stride;
        WGPUShaderModule shaderModule = alphaTestShader_;

        if (stride == 24)
        {
            struct ColoredTexturedVertex { float x, y, z; std::uint8_t r, g, b, a; float u, v; };
            attributes[0].format = WGPUVertexFormat_Float32x3;
            attributes[0].offset = offsetof(ColoredTexturedVertex, x);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Unorm8x4;
            attributes[1].offset = offsetof(ColoredTexturedVertex, r);
            attributes[1].shaderLocation = 1;
            attributes[2].format = WGPUVertexFormat_Float32x2;
            attributes[2].offset = offsetof(ColoredTexturedVertex, u);
            attributes[2].shaderLocation = 2;
            attributeCount = 3;
            arrayStride = sizeof(ColoredTexturedVertex);
            shaderModule = alphaTestColoredShader_;
        }
        else if (stride == 32)
        {
            // VertexPositionNormalTexture: position (offset 0) + UV (offset 24, past the unread
            // 12-byte normal) -- one shared shader for strides 20 and 32, only the vertex buffer
            // layout differs.
            struct LitTexturedVertex { float x, y, z, nx, ny, nz, u, v; };
            attributes[0].format = WGPUVertexFormat_Float32x3;
            attributes[0].offset = offsetof(LitTexturedVertex, x);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x2;
            attributes[1].offset = offsetof(LitTexturedVertex, u);
            attributes[1].shaderLocation = 1;
            attributeCount = 2;
            arrayStride = sizeof(LitTexturedVertex);
        }
        else
        {
            struct TexturedVertex { float x, y, z; float u, v; };
            attributes[0].format = WGPUVertexFormat_Float32x3;
            attributes[0].offset = offsetof(TexturedVertex, x);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x2;
            attributes[1].offset = offsetof(TexturedVertex, u);
            attributes[1].shaderLocation = 1;
            attributeCount = 2;
            arrayStride = sizeof(TexturedVertex);
        }

        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = arrayStride;
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributeCount;
        vertexBufferLayout.attributes = attributes;

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fragment{};
        fragment.module = shaderModule;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU AlphaTest3D Pipeline");
        pipeline.layout = texturedPipelineLayout_;
        pipeline.vertex.module = shaderModule;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = WGPUCullMode_None;
        pipeline.multisample.count = 1;
        pipeline.multisample.mask = std::numeric_limits<std::uint32_t>::max();
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create AlphaTest3D pipeline");
        cache[key] = created;
        return created;
    }

    void WebGPUGraphicsBackend::DestroyDualTextureResources()
    {
        for (auto& [key, pipe] : dualTexturePipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        dualTexturePipelines_.clear();
        for (auto& [key, pipe] : dualTextureColoredPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        dualTextureColoredPipelines_.clear();
        if (dualTexturePipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(dualTexturePipelineLayout_);
        if (dualTextureBindGroupLayout_ != nullptr) wgpuBindGroupLayoutRelease(dualTextureBindGroupLayout_);
        if (dualTextureShader_ != nullptr) wgpuShaderModuleRelease(dualTextureShader_);
        if (dualTextureColoredShader_ != nullptr) wgpuShaderModuleRelease(dualTextureColoredShader_);
        dualTexturePipelineLayout_ = nullptr;
        dualTextureBindGroupLayout_ = nullptr;
        dualTextureShader_ = nullptr;
        dualTextureColoredShader_ = nullptr;
    }

    void WebGPUGraphicsBackend::CreateDualTextureResources()
    {
        DestroyDualTextureResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined || coloredBindGroupLayout_ == nullptr)
            return;

        // Ported from VulkanGraphicsBackend's dual_texture3d.{vert,frag}.glsl. DualTextureEffect
        // has no lighting and no alpha test, so group 0 reuses coloredBindGroupLayout_/the primary
        // Uniforms layout unchanged. Group 1 is a NEW shape (one shared sampler + two textures,
        // since DualTextureEffect samples both layers at the same UV with one shared
        // TextureFilter/AddressMode, matching every other WebGPU 3D shader's own
        // single-sampler-per-draw simplification).
        static constexpr char shaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex0: texture_2d<f32>;
@group(1) @binding(2) var tex1: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    var sample0 = textureSample(tex0, texSampler, input.uv);
    let sample1 = textureSample(tex1, texSampler, input.uv);
    sample0 = vec4f(sample0.rgb * 2.0, sample0.a);
    return sample0 * sample1 * u.diffuseColor;
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU DualTexture3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        dualTextureShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);
        if (dualTextureShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create DualTexture3D shader");

        // WEBGPU-24: stride 24 (VertexPositionColorTexture) variant -- adds vertex-colour tint,
        // mirroring colored_textured3d.wgsl's own mixing formula.
        static constexpr char coloredShaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var tex0: texture_2d<f32>;
@group(1) @binding(2) var tex1: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) color: vec4f,
    @location(2) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) tint: vec4f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    let vertexColorEnabled = u.light0DiffuseVertexColor.w;
    output.tint = select(u.diffuseColor, input.color * u.diffuseColor, vertexColorEnabled > 0.5);
    return output;
}
@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    var sample0 = textureSample(tex0, texSampler, input.uv);
    let sample1 = textureSample(tex1, texSampler, input.uv);
    sample0 = vec4f(sample0.rgb * 2.0, sample0.a);
    return sample0 * sample1 * input.tint;
}
)WGSL";

        WGPUShaderSourceWGSL coloredWgsl{};
        coloredWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        coloredWgsl.code = StringView(coloredShaderSource);
        WGPUShaderModuleDescriptor coloredShaderDescriptor{};
        coloredShaderDescriptor.label = StringView("CNA WebGPU DualTextureColored3D WGSL");
        coloredShaderDescriptor.nextInChain = &coloredWgsl.chain;
        dualTextureColoredShader_ = wgpuDeviceCreateShaderModule(device_, &coloredShaderDescriptor);
        if (dualTextureColoredShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create DualTextureColored3D shader");

        std::array<WGPUBindGroupLayoutEntry, 3> layoutEntries{};
        layoutEntries[0].binding = 0;
        layoutEntries[0].visibility = WGPUShaderStage_Fragment;
        layoutEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;
        layoutEntries[1].binding = 1;
        layoutEntries[1].visibility = WGPUShaderStage_Fragment;
        layoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
        layoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        layoutEntries[1].texture.multisampled = false;
        layoutEntries[2].binding = 2;
        layoutEntries[2].visibility = WGPUShaderStage_Fragment;
        layoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
        layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;
        layoutEntries[2].texture.multisampled = false;
        WGPUBindGroupLayoutDescriptor bindLayoutDescriptor{};
        bindLayoutDescriptor.label = StringView("CNA WebGPU DualTexture3D BindGroupLayout");
        bindLayoutDescriptor.entryCount = layoutEntries.size();
        bindLayoutDescriptor.entries = layoutEntries.data();
        dualTextureBindGroupLayout_ = wgpuDeviceCreateBindGroupLayout(device_, &bindLayoutDescriptor);

        std::array<WGPUBindGroupLayout, 2> groupLayouts{coloredBindGroupLayout_, dualTextureBindGroupLayout_};
        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU DualTexture3D PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = groupLayouts.size();
        pipelineLayoutDescriptor.bindGroupLayouts = groupLayouts.data();
        dualTexturePipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        if (dualTextureBindGroupLayout_ == nullptr || dualTexturePipelineLayout_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create DualTexture3D GPU resources");
    }

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelineDualTexture3D(std::size_t stride,
                                                                                WGPUPrimitiveTopology topology,
                                                                                bool depthTest, bool depthWrite,
                                                                                int depthFunc)
    {
        const int key = ((static_cast<int>(stride) * 8 + static_cast<int>(topology)) * 8 + depthFunc) * 4 +
                        (depthTest ? 2 : 0) + (depthWrite ? 1 : 0);
        auto& cache = (stride == 24) ? dualTextureColoredPipelines_ : dualTexturePipelines_;
        if (auto it = cache.find(key); it != cache.end())
            return it->second;

        WGPUVertexAttribute attributes[3]{};
        std::uint32_t attributeCount = 0;
        std::uint64_t arrayStride = stride;
        WGPUShaderModule shaderModule = dualTextureShader_;

        if (stride == 24)
        {
            struct ColoredTexturedVertex { float x, y, z; std::uint8_t r, g, b, a; float u, v; };
            attributes[0].format = WGPUVertexFormat_Float32x3;
            attributes[0].offset = offsetof(ColoredTexturedVertex, x);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Unorm8x4;
            attributes[1].offset = offsetof(ColoredTexturedVertex, r);
            attributes[1].shaderLocation = 1;
            attributes[2].format = WGPUVertexFormat_Float32x2;
            attributes[2].offset = offsetof(ColoredTexturedVertex, u);
            attributes[2].shaderLocation = 2;
            attributeCount = 3;
            arrayStride = sizeof(ColoredTexturedVertex);
            shaderModule = dualTextureColoredShader_;
        }
        else
        {
            struct TexturedVertex { float x, y, z; float u, v; };
            attributes[0].format = WGPUVertexFormat_Float32x3;
            attributes[0].offset = offsetof(TexturedVertex, x);
            attributes[0].shaderLocation = 0;
            attributes[1].format = WGPUVertexFormat_Float32x2;
            attributes[1].offset = offsetof(TexturedVertex, u);
            attributes[1].shaderLocation = 1;
            attributeCount = 2;
            arrayStride = sizeof(TexturedVertex);
        }

        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = arrayStride;
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributeCount;
        vertexBufferLayout.attributes = attributes;

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fragment{};
        fragment.module = shaderModule;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU DualTexture3D Pipeline");
        pipeline.layout = dualTexturePipelineLayout_;
        pipeline.vertex.module = shaderModule;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = WGPUCullMode_None;
        pipeline.multisample.count = 1;
        pipeline.multisample.mask = std::numeric_limits<std::uint32_t>::max();
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create DualTexture3D pipeline");
        cache[key] = created;
        return created;
    }

    WGPUSampler WebGPUGraphicsBackend::GetOrCreateSampler(int textureFilter, int addressU, int addressV)
    {
        const int index = SamplerCacheIndex(textureFilter, addressU, addressV);
        if (samplerCache_[index] != nullptr)
            return samplerCache_[index];
        WGPUSamplerDescriptor descriptor{};
        descriptor.label = StringView("CNA WebGPU SpriteBatch Sampler");
        descriptor.addressModeU = ToAddressMode(addressU);
        descriptor.addressModeV = ToAddressMode(addressV);
        descriptor.addressModeW = WGPUAddressMode_ClampToEdge;
        const WGPUFilterMode filter = textureFilter == 0 ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest;
        descriptor.magFilter = filter;
        descriptor.minFilter = filter;
        descriptor.mipmapFilter = textureFilter == 0 ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
        descriptor.lodMaxClamp = 32.0f;
        // A zero-initialized descriptor has maxAnisotropy=0, which wgpu-native rejects.
        // WebGPU's required default is 1 when anisotropic filtering is not requested.
        descriptor.maxAnisotropy = 1;
        samplerCache_[index] = wgpuDeviceCreateSampler(device_, &descriptor);
        if (samplerCache_[index] == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create sampler");
        return samplerCache_[index];
    }

    WebGPUGraphicsBackend::LogicalViewport WebGPUGraphicsBackend::ComputeLogicalViewport() const
    {
        LogicalViewport viewport{};
        viewport.width = static_cast<float>(std::max(0, physicalWidth_));
        viewport.height = static_cast<float>(std::max(0, physicalHeight_));
        viewport.logicalWidth = viewport.width;
        viewport.logicalHeight = viewport.height;
        if (physicalWidth_ <= 0 || physicalHeight_ <= 0)
            return viewport;
        if (presentationMode_ == CnaPresentationMode::NativeBackBuffer || virtualWidth_ <= 0 || virtualHeight_ <= 0)
            return viewport;

        float logicalWidth = static_cast<float>(virtualWidth_);
        float logicalHeight = static_cast<float>(virtualHeight_);
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth)
        {
            logicalHeight = static_cast<float>(virtualHeight_);
            logicalWidth = logicalHeight * static_cast<float>(physicalWidth_) / static_cast<float>(physicalHeight_);
            viewport.logicalWidth = logicalWidth;
            viewport.logicalHeight = logicalHeight;
            return viewport;
        }

        viewport.logicalWidth = logicalWidth;
        viewport.logicalHeight = logicalHeight;
        if (presentationMode_ == CnaPresentationMode::Stretch)
            return viewport;
        const float sx = static_cast<float>(physicalWidth_) / logicalWidth;
        const float sy = static_cast<float>(physicalHeight_) / logicalHeight;
        const float scale = presentationMode_ == CnaPresentationMode::Overscan ? std::max(sx, sy) : std::min(sx, sy);
        viewport.width = logicalWidth * scale;
        viewport.height = logicalHeight * scale;
        viewport.x = (static_cast<float>(physicalWidth_) - viewport.width) * 0.5f;
        viewport.y = (static_cast<float>(physicalHeight_) - viewport.height) * 0.5f;
        return viewport;
    }

    void WebGPUGraphicsBackend::QueueSprite(const WebGPUTextureBackend& texture,
                                             const Rectangle& destination,
                                             const Rectangle& source,
                                             const Color& color,
                                             float rotation,
                                             const Vector2& origin,
                                             SpriteEffects effects,
                                             float layerDepth,
                                             const Matrix& transform,
                                             int textureFilter,
                                             int addressU,
                                             int addressV)
    {
        if (destination.Width == 0 || destination.Height == 0 || source.Width == 0 || source.Height == 0)
            return;
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.logicalWidth <= 0.0f || viewport.logicalHeight <= 0.0f || physicalWidth_ <= 0 || physicalHeight_ <= 0)
            return;

        const float scaleX = static_cast<float>(destination.Width) / static_cast<float>(source.Width);
        const float scaleY = static_cast<float>(destination.Height) / static_cast<float>(source.Height);
        const float left = -origin.X * scaleX;
        const float top = -origin.Y * scaleY;
        const float right = left + static_cast<float>(destination.Width);
        const float bottom = top + static_cast<float>(destination.Height);
        std::array<Vector2, 4> points{Vector2{left, top}, Vector2{right, top}, Vector2{left, bottom}, Vector2{right, bottom}};
        const float s = std::sin(rotation);
        const float c = std::cos(rotation);
        for (Vector2& point : points)
        {
            const float rotatedX = point.X * c - point.Y * s + static_cast<float>(destination.X);
            const float rotatedY = point.X * s + point.Y * c + static_cast<float>(destination.Y);
            point.X = rotatedX * transform.M11 + rotatedY * transform.M21 + transform.M41;
            point.Y = rotatedX * transform.M12 + rotatedY * transform.M22 + transform.M42;
        }

        float u0 = static_cast<float>(source.X) / texture.GetWidth();
        float v0 = static_cast<float>(source.Y) / texture.GetHeight();
        float u1 = static_cast<float>(source.X + source.Width) / texture.GetWidth();
        float v1 = static_cast<float>(source.Y + source.Height) / texture.GetHeight();
        const int effectBits = static_cast<int>(effects);
        if ((effectBits & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0) std::swap(u0, u1);
        if ((effectBits & static_cast<int>(SpriteEffects::FlipVertically)) != 0) std::swap(v0, v1);
        const std::array<Vector2, 4> uv{Vector2{u0, v0}, Vector2{u1, v0}, Vector2{u0, v1}, Vector2{u1, v1}};
        constexpr int indices[6] = {0, 1, 2, 2, 1, 3};

        SpriteCommand command{};
        command.texture = &texture;
        command.textureFilter = textureFilter;
        command.addressU = addressU;
        command.addressV = addressV;
        const float rgba[4] = {
            static_cast<float>(color.getRProperty()) / 255.0f,
            static_cast<float>(color.getGProperty()) / 255.0f,
            static_cast<float>(color.getBProperty()) / 255.0f,
            static_cast<float>(color.getAProperty()) / 255.0f
        };
        for (int i = 0; i < 6; ++i)
        {
            const int corner = indices[i];
            const float px = viewport.x + points[corner].X * viewport.width / viewport.logicalWidth;
            const float py = viewport.y + points[corner].Y * viewport.height / viewport.logicalHeight;
            auto& vertex = command.vertices[static_cast<std::size_t>(i)];
            vertex.position[0] = 2.0f * px / static_cast<float>(physicalWidth_) - 1.0f;
            vertex.position[1] = 1.0f - 2.0f * py / static_cast<float>(physicalHeight_);
            vertex.position[2] = std::clamp(layerDepth, 0.0f, 1.0f);
            vertex.uv[0] = uv[corner].X;
            vertex.uv[1] = uv[corner].Y;
            std::copy(std::begin(rgba), std::end(rgba), vertex.color);
        }
        spriteCommands_.push_back(command);
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::RenderSprites(WGPURenderPassEncoder pass)
    {
        if (spriteCommands_.empty())
            return;
        const std::uint64_t required = Align4(spriteCommands_.size() * 6u * sizeof(SpriteVertex));
        if (spriteVertexBuffer_ == nullptr || required > spriteVertexCapacityBytes_)
        {
            if (spriteVertexBuffer_ != nullptr)
                wgpuBufferRelease(spriteVertexBuffer_);
            WGPUBufferDescriptor descriptor{};
            descriptor.label = StringView("CNA WebGPU SpriteBatch Vertex Buffer");
            descriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            descriptor.size = std::max<std::uint64_t>(required, 64u * 1024u);
            spriteVertexBuffer_ = wgpuDeviceCreateBuffer(device_, &descriptor);
            spriteVertexCapacityBytes_ = descriptor.size;
        }

        std::vector<SpriteVertex> vertices;
        vertices.reserve(spriteCommands_.size() * 6u);
        for (const SpriteCommand& command : spriteCommands_)
            vertices.insert(vertices.end(), command.vertices.begin(), command.vertices.end());
        wgpuQueueWriteBuffer(queue_, spriteVertexBuffer_, 0, vertices.data(), vertices.size() * sizeof(SpriteVertex));
        wgpuRenderPassEncoderSetPipeline(pass, blendEnabled_ ? spritePipelineBlend_ : spritePipelineOpaque_);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, spriteVertexBuffer_, 0, vertices.size() * sizeof(SpriteVertex));

        for (std::size_t i = 0; i < spriteCommands_.size(); ++i)
        {
            const SpriteCommand& command = spriteCommands_[i];
            std::array<WGPUBindGroupEntry, 2> entries{};
            entries[0].binding = 0;
            entries[0].sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
            entries[1].binding = 1;
            entries[1].textureView = command.texture->View();
            WGPUBindGroupDescriptor descriptor{};
            descriptor.label = StringView("CNA WebGPU SpriteBatch BindGroup");
            descriptor.layout = spriteBindGroupLayout_;
            descriptor.entryCount = entries.size();
            descriptor.entries = entries.data();
            WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device_, &descriptor);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 6, 1, static_cast<std::uint32_t>(i * 6u), 0);
            wgpuBindGroupRelease(bindGroup);
        }
    }

    void WebGPUGraphicsBackend::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                                        bool, int, int, int, int, int, int, int, bool, int, int, int, int)
    {
        depthTestEnabled_ = depthEnable;
        depthWriteEnabled_ = depthWriteEnable;
        depthCompareFunction_ = depthFunc;
    }

    void WebGPUGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        clearColor_ = WGPUColor{r, g, b, a};
        clearColorPending_ = true;
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        Clear(r, g, b, a);
        ClearDepth(depth);
    }
    void WebGPUGraphicsBackend::ClearDepth(float depth) { clearDepth_ = depth; clearDepthPending_ = true; framePending_ = true; }
    void WebGPUGraphicsBackend::ClearStencil(int stencil) { clearStencil_ = static_cast<std::uint32_t>(stencil); clearStencilPending_ = true; framePending_ = true; }
    void WebGPUGraphicsBackend::ClearDepthAndStencil(float depth, int stencil) { ClearDepth(depth); ClearStencil(stencil); }
    void WebGPUGraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil) { Clear(r, g, b, a); ClearStencil(stencil); }
    void WebGPUGraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    { Clear(r, g, b, a); ClearDepth(depth); ClearStencil(stencil); }

    bool WebGPUGraphicsBackend::EnsureFrameRendered()
    {
        if (!hasAcquiredTexture_)
        {
            ConfigureSurface(false);
            if (!surfaceConfigured_)
            {
                spriteCommands_.clear();
                return false;
            }

            WGPUSurfaceTexture surfaceTexture{};
            wgpuSurfaceGetCurrentTexture(surface_, &surfaceTexture);
            if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
                surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal)
            {
                if (surfaceTexture.texture != nullptr)
                    wgpuTextureRelease(surfaceTexture.texture);
                if (IsSurfaceRecoverable(surfaceTexture.status))
                    ConfigureSurface(true);
                else
                    throw std::runtime_error(
                        "CNA WebGPU: unrecoverable surface acquisition failure (status " +
                        std::to_string(static_cast<int>(surfaceTexture.status)) + ")");
                spriteCommands_.clear();
                return false;
            }

            acquiredTexture_ = surfaceTexture.texture;
            hasAcquiredTexture_ = true;
            framePending_ = true;
        }

        if (!framePending_)
            return true;

        WGPUTextureView backBuffer = wgpuTextureCreateView(acquiredTexture_, nullptr);
        WGPUCommandEncoderDescriptor encoderDescriptor{};
        encoderDescriptor.label = StringView("CNA WebGPU Frame Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device_, &encoderDescriptor);
        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = backBuffer;
        colorAttachment.loadOp = clearColorPending_ ? WGPULoadOp_Clear : WGPULoadOp_Load;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = clearColor_;

        WGPURenderPassDepthStencilAttachment depthAttachment{};
        depthAttachment.view = depthView_;
        depthAttachment.depthLoadOp = clearDepthPending_ ? WGPULoadOp_Clear : WGPULoadOp_Load;
        depthAttachment.depthStoreOp = WGPUStoreOp_Store;
        depthAttachment.depthClearValue = clearDepth_;
        depthAttachment.depthReadOnly = false;
        depthAttachment.stencilLoadOp = clearStencilPending_ ? WGPULoadOp_Clear : WGPULoadOp_Load;
        depthAttachment.stencilStoreOp = WGPUStoreOp_Store;
        depthAttachment.stencilClearValue = clearStencil_;
        depthAttachment.stencilReadOnly = false;

        WGPURenderPassDescriptor passDescriptor{};
        passDescriptor.label = StringView("CNA WebGPU Main RenderPass");
        passDescriptor.colorAttachmentCount = 1;
        passDescriptor.colorAttachments = &colorAttachment;
        passDescriptor.depthStencilAttachment = depthView_ != nullptr ? &depthAttachment : nullptr;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDescriptor);
        // 3D draws first, 2D SpriteBatch/UI on top -- matches typical XNA game draw order
        // (World.Draw() then a HUD SpriteBatch pass), both collapsed into this one deferred pass.
        RenderColoredDraws(pass);
        RenderTexturedDraws(pass);
        RenderLitTexturedDraws(pass);
        RenderAlphaTestDraws(pass);
        RenderDualTextureDraws(pass);
        RenderPbrDraws(pass);
        RenderSprites(pass);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        CaptureReadback(encoder, acquiredTexture_);

        WGPUCommandBufferDescriptor commandBufferDescriptor{};
        commandBufferDescriptor.label = StringView("CNA WebGPU Frame Commands");
        WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &commandBufferDescriptor);
        wgpuCommandEncoderRelease(encoder);
        wgpuQueueSubmit(queue_, 1, &commandBuffer);
        wgpuCommandBufferRelease(commandBuffer);
        wgpuTextureViewRelease(backBuffer);

        // Transient per-draw colored3D resources are only safe to release once the command
        // buffer referencing them has actually been submitted -- see pendingBufferReleases_'s
        // own doc comment in the header.
        for (WGPUBindGroup bg : pendingBindGroupReleases_) wgpuBindGroupRelease(bg);
        for (WGPUBuffer buf : pendingBufferReleases_) wgpuBufferRelease(buf);
        pendingBindGroupReleases_.clear();
        pendingBufferReleases_.clear();

        spriteCommands_.clear();
        clearColorPending_ = false;
        clearDepthPending_ = false;
        clearStencilPending_ = false;
        framePending_ = false;
        return true;
    }

    void WebGPUGraphicsBackend::Present()
    {
        if (!EnsureFrameRendered())
            return;
        wgpuSurfacePresent(surface_);
        wgpuTextureRelease(acquiredTexture_);
        acquiredTexture_ = nullptr;
        hasAcquiredTexture_ = false;
    }

    void WebGPUGraphicsBackend::CaptureReadback(WGPUCommandEncoder encoder, WGPUTexture surfaceTexture)
    {
        if (physicalWidth_ <= 0 || physicalHeight_ <= 0)
        {
            readbackValid_ = false;
            return;
        }

        const auto bytesPerRow = AlignBytesPerRow(static_cast<std::uint32_t>(physicalWidth_) * 4);
        const std::uint64_t requiredCapacity =
            static_cast<std::uint64_t>(bytesPerRow) * static_cast<std::uint64_t>(physicalHeight_);
        if (readbackBuffer_ == nullptr || readbackBufferCapacity_ < requiredCapacity)
        {
            if (readbackBuffer_ != nullptr)
                wgpuBufferRelease(readbackBuffer_);
            WGPUBufferDescriptor descriptor{};
            descriptor.label = StringView("CNA WebGPU Readback Buffer");
            descriptor.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
            descriptor.size = requiredCapacity;
            readbackBuffer_ = wgpuDeviceCreateBuffer(device_, &descriptor);
            readbackBufferCapacity_ = requiredCapacity;
        }
        if (readbackBuffer_ == nullptr)
        {
            readbackValid_ = false;
            return;
        }

        WGPUTexelCopyTextureInfo source{};
        source.texture = surfaceTexture;
        source.mipLevel = 0;
        source.origin = WGPUOrigin3D{0, 0, 0};
        source.aspect = WGPUTextureAspect_All;

        WGPUTexelCopyBufferInfo destination{};
        destination.buffer = readbackBuffer_;
        destination.layout.offset = 0;
        destination.layout.bytesPerRow = bytesPerRow;
        destination.layout.rowsPerImage = static_cast<std::uint32_t>(physicalHeight_);

        const WGPUExtent3D copySize{static_cast<std::uint32_t>(physicalWidth_),
                                     static_cast<std::uint32_t>(physicalHeight_), 1};
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);

        readbackBytesPerRow_ = bytesPerRow;
        readbackWidth_ = physicalWidth_;
        readbackHeight_ = physicalHeight_;
        readbackValid_ = true;
    }

    void WebGPUGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (w <= 0 || h <= 0)
            return;

        // Render whatever Clear()/sprite work has been queued so far this logical frame before
        // reading it back, so a Clear()+GetBackBufferData() pair observes its own frame's result
        // without needing a real Present() in between (matches Vulkan/Bgfx's on-demand submit).
        EnsureFrameRendered();

        if (!readbackValid_ || readbackBuffer_ == nullptr)
        {
            std::memset(pixels, 0, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
            return;
        }

        const std::uint64_t mapSize = readbackBufferCapacity_;
        BufferMapState mapState;
        WGPUBufferMapCallbackInfo callbackInfo{};
        callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        callbackInfo.callback = OnBufferMap;
        callbackInfo.userdata1 = &mapState;
        wgpuBufferMapAsync(readbackBuffer_, WGPUMapMode_Read, 0, mapSize, callbackInfo);
        WaitForCompletion(instance_, mapState.completed, "readback buffer map");
        if (mapState.status != WGPUMapAsyncStatus_Success)
            throw std::runtime_error("CNA WebGPU: readback buffer map failed: " + mapState.error);

        const auto* mapped = static_cast<const std::uint8_t*>(
            wgpuBufferGetConstMappedRange(readbackBuffer_, 0, mapSize));
        const bool isBgra = (surfaceFormat_ == WGPUTextureFormat_BGRA8Unorm ||
                             surfaceFormat_ == WGPUTextureFormat_BGRA8UnormSrgb);
        if (mapped == nullptr)
        {
            std::memset(pixels, 0, static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
        }
        else
        {
            for (int row = 0; row < h; ++row)
            {
                const int sy = y + row;
                for (int col = 0; col < w; ++col)
                {
                    const int sx = x + col;
                    std::uint8_t* d = pixels + (static_cast<std::size_t>(row) * w + col) * 4;
                    if (sx < 0 || sx >= readbackWidth_ || sy < 0 || sy >= readbackHeight_)
                    {
                        d[0] = d[1] = d[2] = d[3] = 0;
                        continue;
                    }
                    const std::uint8_t* s =
                        mapped + static_cast<std::size_t>(sy) * readbackBytesPerRow_ + static_cast<std::size_t>(sx) * 4;
                    if (isBgra) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; }
                    else        { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d[3] = s[3]; }
                }
            }
        }
        wgpuBufferUnmap(readbackBuffer_);
    }

    void WebGPUGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        width = static_cast<int>(std::lround(viewport.logicalWidth));
        height = static_cast<int>(std::lround(viewport.logicalHeight));
    }

    void WebGPUGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void WebGPUGraphicsBackend::SetPresentationMode(int mode)
    {
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
            mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
            throw std::out_of_range("CNA WebGPU: invalid presentation mode");
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void WebGPUGraphicsBackend::SetSwapInterval(int interval)
    {
        interval = std::max(0, interval);
        if (swapInterval_ != interval)
        {
            swapInterval_ = interval;
            ConfigureSurface(true);
        }
    }

    bool WebGPUGraphicsBackend::TransformWindowToLogical(float windowX, float windowY, float& logicalX, float& logicalY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.width == 0.0f || viewport.height == 0.0f)
            return false;
        logicalX = (windowX - viewport.x) * viewport.logicalWidth / viewport.width;
        logicalY = (windowY - viewport.y) * viewport.logicalHeight / viewport.height;
        return windowX >= viewport.x && windowX < viewport.x + viewport.width &&
               windowY >= viewport.y && windowY < viewport.y + viewport.height;
    }

    bool WebGPUGraphicsBackend::TransformLogicalToWindow(float logicalX, float logicalY, float& windowX, float& windowY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.logicalWidth == 0.0f || viewport.logicalHeight == 0.0f)
            return false;
        windowX = viewport.x + logicalX * viewport.width / viewport.logicalWidth;
        windowY = viewport.y + logicalY * viewport.height / viewport.logicalHeight;
        return true;
    }

    std::unique_ptr<ITextureBackend> WebGPUGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<WebGPUTextureBackend>(*this, data);
    }

    std::unique_ptr<ISpriteBatchBackend> WebGPUGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<WebGPUSpriteBatchBackend>(*this);
    }

    std::unique_ptr<IVertexBufferBackend> WebGPUGraphicsBackend::CreateVertexBuffer(int vertexCapacity)
    {
        return std::make_unique<WebGPUVertexBufferBackend>(*this, vertexCapacity);
    }

    std::unique_ptr<IIndexBufferBackend> WebGPUGraphicsBackend::CreateIndexBuffer16(int indexCapacity)
    {
        return std::make_unique<WebGPUIndexBufferBackend>(*this, indexCapacity, false);
    }

    std::unique_ptr<IIndexBufferBackend> WebGPUGraphicsBackend::CreateIndexBuffer32(int indexCapacity)
    {
        return std::make_unique<WebGPUIndexBufferBackend>(*this, indexCapacity, true);
    }

    WGPUPrimitiveTopology WebGPUGraphicsBackend::ToTopology(PrimitiveType primitive) const
    {
        switch (primitive)
        {
            case PrimitiveType::TriangleList: return WGPUPrimitiveTopology_TriangleList;
            case PrimitiveType::TriangleStrip: return WGPUPrimitiveTopology_TriangleStrip;
            case PrimitiveType::LineList: return WGPUPrimitiveTopology_LineList;
            case PrimitiveType::LineStrip: return WGPUPrimitiveTopology_LineStrip;
            case PrimitiveType::PointListEXT: return WGPUPrimitiveTopology_PointList;
        }
        throw std::invalid_argument("CNA WebGPU: unsupported primitive topology");
    }

    int WebGPUGraphicsBackend::PrimitiveVertexCount(PrimitiveType primitive, int primitiveCount) const
    {
        switch (primitive)
        {
            case PrimitiveType::TriangleList: return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList: return primitiveCount * 2;
            case PrimitiveType::LineStrip: return primitiveCount + 1;
            case PrimitiveType::PointListEXT: return primitiveCount;
        }
        return 0;
    }

    int WebGPUGraphicsBackend::PrimitiveIndexCount(PrimitiveType primitive, int primitiveCount) const
    {
        return PrimitiveVertexCount(primitive, primitiveCount);
    }

    [[noreturn]] void WebGPUGraphicsBackend::ThrowUnsupported3DDraw(const char* method)
    {
        throw std::runtime_error(std::string("CNA WebGPU: ") + method +
                                 " is not implemented in the initial backend. Clear/present, Texture2D, "
                                 "SpriteBatch and buffer upload are implemented; see plan_webgpu.md Phase 58+ for 3D parity.");
    }

    void WebGPUGraphicsBackend::QueueColoredDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams* params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        if (webgpuVb.Stride() != 16)
            throw std::invalid_argument("CNA WebGPU: DrawColoredPrimitives requires a stride-16 "
                                        "(VertexPositionColor) vertex buffer");

        ColoredDrawCommand command;
        const int vertexStart = params != nullptr ? params->vertexStart : 0;
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(vertexStart) * 16u;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        if (params != nullptr)
        {
            const Matrix wvp = world * view * projection;
            FillExtUniforms(command.uniforms, wvp, *params);
        }
        else
        {
            FillColoredUniforms(command.uniforms, world, view, projection);
        }

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        coloredDrawCommands_.push_back(std::move(command));
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                                        const Matrix& world, const Matrix& view, const Matrix& projection,
                                                        PrimitiveType primitive, int primitiveCount)
    {
        QueueColoredDraw(vb, nullptr, world, view, projection, primitive, primitiveCount);
    }

    void WebGPUGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                                               const IIndexBufferBackend& ib,
                                                               const Matrix& world, const Matrix& view, const Matrix& projection,
                                                               PrimitiveType primitive, int primitiveCount)
    {
        QueueColoredDraw(vb, &ib, world, view, projection, primitive, primitiveCount);
    }

    void WebGPUGraphicsBackend::DrawPrimitivesEx(const IVertexBufferBackend& vb,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        // Matches VulkanGraphicsBackend's own dispatch precedence: alpha test wins over
        // dual-texture/env-map/skinned/lit-textured; dual-texture wins over env-map/skinned/
        // lit-textured (an AlphaTestEffect or DualTextureEffect draw on a
        // VertexPositionNormalTexture buffer never reaches lit_textured3d -- the normal is simply
        // unread in both cases).
        const bool needsAlphaTest = params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f;
        const bool needsDualTexture = !needsAlphaTest && params.dualTexture;
        const bool needsUnsupportedEffect = !needsAlphaTest && !needsDualTexture &&
                                            (params.envMapping || params.skinned);
        const std::size_t stride = webgpuVb.Stride();
        if (needsAlphaTest && (stride == 20 || stride == 24 || stride == 32) && params.texture0 != nullptr)
        {
            QueueAlphaTestDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsDualTexture && (stride == 20 || stride == 24) &&
            params.texture0 != nullptr && params.texture1 != nullptr)
        {
            QueueDualTextureDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture && stride == 16)
        {
            QueueColoredDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, &params);
            return;
        }
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture && (stride == 20 || stride == 24) &&
            params.texture0 != nullptr)
        {
            QueueTexturedDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture && stride == 32 &&
            params.texture0 != nullptr)
        {
            QueueLitTexturedDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        // Unskinned PbrEffect (stride 48, VertexPositionNormalTangentTexture). Gated on
        // !params.skinned directly (rather than needsUnsupportedEffect, which already excludes
        // skinned draws via its own OR-condition) so a SkinnedPbrEffect draw -- stride 68, a
        // separate pre-existing gap since this backend has no skinning shader at all -- keeps
        // falling through to the fallback below unchanged.
        if (!needsAlphaTest && !needsDualTexture && params.pbr && !params.skinned && stride == 48 &&
            params.texture0 != nullptr)
        {
            QueuePbrDraw(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        // No env-map/skinned shader exists yet (env-map: Phase 58 remaining WGSL variant;
        // skinned: this backend has no skinning support at all, a separate pre-existing gap) --
        // fall back exactly like IGraphicsBackend's own default implementation did before this
        // override existed. This will itself throw for anything other than a stride-16 buffer
        // (DrawColoredPrimitives' own requirement), matching the pre-existing "unsupported, fail
        // loudly" behaviour.
        DrawColoredPrimitives(vb, world, view, projection, primitive, primitiveCount);
    }

    void WebGPUGraphicsBackend::DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                                         const Matrix& world, const Matrix& view, const Matrix& projection,
                                                         PrimitiveType primitive, int primitiveCount,
                                                         const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        const bool needsAlphaTest = params.alphaTest[2] < 0.0f || params.alphaTest[3] < 0.0f;
        const bool needsDualTexture = !needsAlphaTest && params.dualTexture;
        const bool needsUnsupportedEffect = !needsAlphaTest && !needsDualTexture &&
                                            (params.envMapping || params.skinned);
        const std::size_t stride = webgpuVb.Stride();
        if (needsAlphaTest && (stride == 20 || stride == 24 || stride == 32) && params.texture0 != nullptr)
        {
            QueueAlphaTestDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (needsDualTexture && (stride == 20 || stride == 24) &&
            params.texture0 != nullptr && params.texture1 != nullptr)
        {
            QueueDualTextureDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture && stride == 16)
        {
            QueueColoredDraw(vb, &ib, world, view, projection, primitive, primitiveCount, &params);
            return;
        }
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture && (stride == 20 || stride == 24) &&
            params.texture0 != nullptr)
        {
            QueueTexturedDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (!needsUnsupportedEffect && !needsAlphaTest && !needsDualTexture && stride == 32 &&
            params.texture0 != nullptr)
        {
            QueueLitTexturedDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        // See DrawPrimitivesEx()'s identical branch for why this is gated on !params.skinned
        // directly rather than needsUnsupportedEffect.
        if (!needsAlphaTest && !needsDualTexture && params.pbr && !params.skinned && stride == 48 &&
            params.texture0 != nullptr)
        {
            QueuePbrDraw(vb, &ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        DrawIndexedColoredPrimitives(vb, ib, world, view, projection, primitive, primitiveCount);
    }

    void WebGPUGraphicsBackend::RenderColoredDraws(WGPURenderPassEncoder pass)
    {
        for (const ColoredDrawCommand& command : coloredDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty())
                continue;

            WGPUBufferDescriptor vbDescriptor{};
            vbDescriptor.label = StringView("CNA WebGPU Colored3D VertexBuffer");
            vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            vbDescriptor.size = Align4(command.vertexData.size());
            WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
            wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

            WGPUBufferDescriptor uboDescriptor{};
            uboDescriptor.label = StringView("CNA WebGPU Colored3D UBO");
            uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            uboDescriptor.size = sizeof(command.uniforms);
            WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
            wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), sizeof(command.uniforms));

            WGPUBindGroupEntry bindEntry{};
            bindEntry.binding = 0;
            bindEntry.buffer = uniformBuffer;
            bindEntry.size = sizeof(command.uniforms);
            WGPUBindGroupDescriptor bindDescriptor{};
            bindDescriptor.label = StringView("CNA WebGPU Colored3D BindGroup");
            bindDescriptor.layout = coloredBindGroupLayout_;
            bindDescriptor.entryCount = 1;
            bindDescriptor.entries = &bindEntry;
            WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device_, &bindDescriptor);

            WGPURenderPipeline pipe = GetOrCreatePipelineColored3D(command.topology, command.depthTest,
                                                                   command.depthWrite, command.depthFunc);
            wgpuRenderPassEncoderSetPipeline(pass, pipe);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

            if (command.indexed && !command.indexData.empty())
            {
                WGPUBufferDescriptor ibDescriptor{};
                ibDescriptor.label = StringView("CNA WebGPU Colored3D IndexBuffer");
                ibDescriptor.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
                ibDescriptor.size = Align4(command.indexData.size());
                WGPUBuffer indexBuffer = wgpuDeviceCreateBuffer(device_, &ibDescriptor);
                wgpuQueueWriteBuffer(queue_, indexBuffer, 0, command.indexData.data(), command.indexData.size());
                wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer,
                    command.index32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
                    0, command.indexData.size());
                wgpuRenderPassEncoderDrawIndexed(pass, command.indexCount, 1, 0, 0, 0);
                pendingBufferReleases_.push_back(indexBuffer);
            }
            else
            {
                wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
            }

            pendingBindGroupReleases_.push_back(bindGroup);
            pendingBufferReleases_.push_back(uniformBuffer);
            pendingBufferReleases_.push_back(vertexBuffer);
        }
        coloredDrawCommands_.clear();
    }

    void WebGPUGraphicsBackend::RenderTexturedDraws(WGPURenderPassEncoder pass)
    {
        for (const TexturedDrawCommand& command : texturedDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty() || command.texture == nullptr)
                continue;

            WGPUBufferDescriptor vbDescriptor{};
            vbDescriptor.label = StringView("CNA WebGPU Textured3D VertexBuffer");
            vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            vbDescriptor.size = Align4(command.vertexData.size());
            WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
            wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

            WGPUBufferDescriptor uboDescriptor{};
            uboDescriptor.label = StringView("CNA WebGPU Textured3D UBO");
            uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            uboDescriptor.size = sizeof(command.uniforms);
            WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
            wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), sizeof(command.uniforms));

            WGPUBindGroupEntry uboEntry{};
            uboEntry.binding = 0;
            uboEntry.buffer = uniformBuffer;
            uboEntry.size = sizeof(command.uniforms);
            WGPUBindGroupDescriptor uboBindDescriptor{};
            uboBindDescriptor.label = StringView("CNA WebGPU Textured3D UBO BindGroup");
            uboBindDescriptor.layout = coloredBindGroupLayout_;
            uboBindDescriptor.entryCount = 1;
            uboBindDescriptor.entries = &uboEntry;
            WGPUBindGroup uboBindGroup = wgpuDeviceCreateBindGroup(device_, &uboBindDescriptor);

            WGPUSampler sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
            std::array<WGPUBindGroupEntry, 2> texEntries{};
            texEntries[0].binding = 0;
            texEntries[0].sampler = sampler;
            texEntries[1].binding = 1;
            texEntries[1].textureView = command.texture->View();
            WGPUBindGroupDescriptor texBindDescriptor{};
            texBindDescriptor.label = StringView("CNA WebGPU Textured3D Texture BindGroup");
            texBindDescriptor.layout = texturedBindGroupLayout_;
            texBindDescriptor.entryCount = texEntries.size();
            texBindDescriptor.entries = texEntries.data();
            WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

            WGPURenderPipeline pipe = command.hasVertexColor
                ? GetOrCreatePipelineColoredTextured3D(command.topology, command.depthTest,
                                                       command.depthWrite, command.depthFunc)
                : GetOrCreatePipelineTextured3D(command.topology, command.depthTest,
                                                command.depthWrite, command.depthFunc);
            wgpuRenderPassEncoderSetPipeline(pass, pipe);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, uboBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, texBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

            if (command.indexed && !command.indexData.empty())
            {
                WGPUBufferDescriptor ibDescriptor{};
                ibDescriptor.label = StringView("CNA WebGPU Textured3D IndexBuffer");
                ibDescriptor.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
                ibDescriptor.size = Align4(command.indexData.size());
                WGPUBuffer indexBuffer = wgpuDeviceCreateBuffer(device_, &ibDescriptor);
                wgpuQueueWriteBuffer(queue_, indexBuffer, 0, command.indexData.data(), command.indexData.size());
                wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer,
                    command.index32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
                    0, command.indexData.size());
                wgpuRenderPassEncoderDrawIndexed(pass, command.indexCount, 1, 0, 0, 0);
                pendingBufferReleases_.push_back(indexBuffer);
            }
            else
            {
                wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
            }

            pendingBindGroupReleases_.push_back(uboBindGroup);
            pendingBindGroupReleases_.push_back(texBindGroup);
            pendingBufferReleases_.push_back(uniformBuffer);
            pendingBufferReleases_.push_back(vertexBuffer);
        }
        texturedDrawCommands_.clear();
    }

    void WebGPUGraphicsBackend::RenderLitTexturedDraws(WGPURenderPassEncoder pass)
    {
        for (const LitTexturedDrawCommand& command : litTexturedDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty() || command.texture == nullptr)
                continue;

            WGPUBufferDescriptor vbDescriptor{};
            vbDescriptor.label = StringView("CNA WebGPU LitTextured3D VertexBuffer");
            vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            vbDescriptor.size = Align4(command.vertexData.size());
            WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
            wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

            WGPUBufferDescriptor uboDescriptor{};
            uboDescriptor.label = StringView("CNA WebGPU LitTextured3D UBO");
            uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            uboDescriptor.size = sizeof(command.uniforms);
            WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
            wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), sizeof(command.uniforms));

            WGPUBufferDescriptor lightUboDescriptor{};
            lightUboDescriptor.label = StringView("CNA WebGPU LitTextured3D LightUBO");
            lightUboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            lightUboDescriptor.size = sizeof(command.lightUniforms);
            WGPUBuffer lightUniformBuffer = wgpuDeviceCreateBuffer(device_, &lightUboDescriptor);
            wgpuQueueWriteBuffer(queue_, lightUniformBuffer, 0, command.lightUniforms.data(), sizeof(command.lightUniforms));

            std::array<WGPUBindGroupEntry, 2> uboEntries{};
            uboEntries[0].binding = 0;
            uboEntries[0].buffer = uniformBuffer;
            uboEntries[0].size = sizeof(command.uniforms);
            uboEntries[1].binding = 1;
            uboEntries[1].buffer = lightUniformBuffer;
            uboEntries[1].size = sizeof(command.lightUniforms);
            WGPUBindGroupDescriptor uboBindDescriptor{};
            uboBindDescriptor.label = StringView("CNA WebGPU LitTextured3D UBO BindGroup");
            uboBindDescriptor.layout = litBindGroupLayout_;
            uboBindDescriptor.entryCount = uboEntries.size();
            uboBindDescriptor.entries = uboEntries.data();
            WGPUBindGroup uboBindGroup = wgpuDeviceCreateBindGroup(device_, &uboBindDescriptor);

            WGPUSampler sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
            std::array<WGPUBindGroupEntry, 2> texEntries{};
            texEntries[0].binding = 0;
            texEntries[0].sampler = sampler;
            texEntries[1].binding = 1;
            texEntries[1].textureView = command.texture->View();
            WGPUBindGroupDescriptor texBindDescriptor{};
            texBindDescriptor.label = StringView("CNA WebGPU LitTextured3D Texture BindGroup");
            texBindDescriptor.layout = texturedBindGroupLayout_;
            texBindDescriptor.entryCount = texEntries.size();
            texBindDescriptor.entries = texEntries.data();
            WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

            WGPURenderPipeline pipe = command.preferVertexLit
                ? GetOrCreatePipelineLitTextured3DVertexLit(command.topology, command.depthTest,
                                                             command.depthWrite, command.depthFunc)
                : GetOrCreatePipelineLitTextured3D(command.topology, command.depthTest,
                                                    command.depthWrite, command.depthFunc);
            wgpuRenderPassEncoderSetPipeline(pass, pipe);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, uboBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, texBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

            if (command.indexed && !command.indexData.empty())
            {
                WGPUBufferDescriptor ibDescriptor{};
                ibDescriptor.label = StringView("CNA WebGPU LitTextured3D IndexBuffer");
                ibDescriptor.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
                ibDescriptor.size = Align4(command.indexData.size());
                WGPUBuffer indexBuffer = wgpuDeviceCreateBuffer(device_, &ibDescriptor);
                wgpuQueueWriteBuffer(queue_, indexBuffer, 0, command.indexData.data(), command.indexData.size());
                wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer,
                    command.index32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
                    0, command.indexData.size());
                wgpuRenderPassEncoderDrawIndexed(pass, command.indexCount, 1, 0, 0, 0);
                pendingBufferReleases_.push_back(indexBuffer);
            }
            else
            {
                wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
            }

            pendingBindGroupReleases_.push_back(uboBindGroup);
            pendingBindGroupReleases_.push_back(texBindGroup);
            pendingBufferReleases_.push_back(uniformBuffer);
            pendingBufferReleases_.push_back(lightUniformBuffer);
            pendingBufferReleases_.push_back(vertexBuffer);
        }
        litTexturedDrawCommands_.clear();
    }

    void WebGPUGraphicsBackend::QueueLitTexturedDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                      const Matrix& world, const Matrix& view, const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount,
                                                      const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        if (webgpuVb.Stride() != 32)
            throw std::invalid_argument("CNA WebGPU: QueueLitTexturedDraw requires a stride-32 "
                                        "(VertexPositionNormalTexture) vertex buffer");
        if (params.texture0 == nullptr)
            throw std::invalid_argument("CNA WebGPU: QueueLitTexturedDraw requires a bound texture0");

        LitTexturedDrawCommand command;
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(params.vertexStart) * 32u;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.texture = static_cast<const WebGPUTextureBackend*>(params.texture0);
        // Task 1105: XNA's real BasicEffect.PreferPerPixelLighting default is false (per-vertex),
        // matching every other backend's own dispatch condition for this flag.
        command.preferVertexLit = params.lightingEnabled && !params.preferPerPixelLighting;
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillLitLightUniforms(command.lightUniforms, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(params.vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        litTexturedDrawCommands_.push_back(std::move(command));
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::RenderAlphaTestDraws(WGPURenderPassEncoder pass)
    {
        for (const AlphaTestDrawCommand& command : alphaTestDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty() || command.texture == nullptr)
                continue;

            WGPUBufferDescriptor vbDescriptor{};
            vbDescriptor.label = StringView("CNA WebGPU AlphaTest3D VertexBuffer");
            vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            vbDescriptor.size = Align4(command.vertexData.size());
            WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
            wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

            WGPUBufferDescriptor uboDescriptor{};
            uboDescriptor.label = StringView("CNA WebGPU AlphaTest3D UBO");
            uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            uboDescriptor.size = sizeof(command.uniforms);
            WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
            wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), sizeof(command.uniforms));

            WGPUBindGroupEntry uboEntry{};
            uboEntry.binding = 0;
            uboEntry.buffer = uniformBuffer;
            uboEntry.size = sizeof(command.uniforms);
            WGPUBindGroupDescriptor uboBindDescriptor{};
            uboBindDescriptor.label = StringView("CNA WebGPU AlphaTest3D UBO BindGroup");
            uboBindDescriptor.layout = coloredBindGroupLayout_;
            uboBindDescriptor.entryCount = 1;
            uboBindDescriptor.entries = &uboEntry;
            WGPUBindGroup uboBindGroup = wgpuDeviceCreateBindGroup(device_, &uboBindDescriptor);

            WGPUSampler sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
            std::array<WGPUBindGroupEntry, 2> texEntries{};
            texEntries[0].binding = 0;
            texEntries[0].sampler = sampler;
            texEntries[1].binding = 1;
            texEntries[1].textureView = command.texture->View();
            WGPUBindGroupDescriptor texBindDescriptor{};
            texBindDescriptor.label = StringView("CNA WebGPU AlphaTest3D Texture BindGroup");
            texBindDescriptor.layout = texturedBindGroupLayout_;
            texBindDescriptor.entryCount = texEntries.size();
            texBindDescriptor.entries = texEntries.data();
            WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

            WGPURenderPipeline pipe = GetOrCreatePipelineAlphaTest3D(command.stride, command.topology,
                                                                     command.depthTest, command.depthWrite,
                                                                     command.depthFunc);
            wgpuRenderPassEncoderSetPipeline(pass, pipe);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, uboBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, texBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

            if (command.indexed && !command.indexData.empty())
            {
                WGPUBufferDescriptor ibDescriptor{};
                ibDescriptor.label = StringView("CNA WebGPU AlphaTest3D IndexBuffer");
                ibDescriptor.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
                ibDescriptor.size = Align4(command.indexData.size());
                WGPUBuffer indexBuffer = wgpuDeviceCreateBuffer(device_, &ibDescriptor);
                wgpuQueueWriteBuffer(queue_, indexBuffer, 0, command.indexData.data(), command.indexData.size());
                wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer,
                    command.index32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
                    0, command.indexData.size());
                wgpuRenderPassEncoderDrawIndexed(pass, command.indexCount, 1, 0, 0, 0);
                pendingBufferReleases_.push_back(indexBuffer);
            }
            else
            {
                wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
            }

            pendingBindGroupReleases_.push_back(uboBindGroup);
            pendingBindGroupReleases_.push_back(texBindGroup);
            pendingBufferReleases_.push_back(uniformBuffer);
            pendingBufferReleases_.push_back(vertexBuffer);
        }
        alphaTestDrawCommands_.clear();
    }

    void WebGPUGraphicsBackend::QueueAlphaTestDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                    const Matrix& world, const Matrix& view, const Matrix& projection,
                                                    PrimitiveType primitive, int primitiveCount,
                                                    const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        const std::size_t stride = webgpuVb.Stride();
        if (stride != 20 && stride != 24 && stride != 32)
            throw std::invalid_argument("CNA WebGPU: QueueAlphaTestDraw requires a stride-20, "
                                        "-24, or -32 vertex buffer");
        if (params.texture0 == nullptr)
            throw std::invalid_argument("CNA WebGPU: QueueAlphaTestDraw requires a bound texture0");

        AlphaTestDrawCommand command;
        command.stride = stride;
        command.hasVertexColor = (stride == 24);
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(params.vertexStart) * stride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.texture = static_cast<const WebGPUTextureBackend*>(params.texture0);
        const Matrix wvp = world * view * projection;
        FillAlphaTestUniforms(command.uniforms, wvp, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(params.vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        alphaTestDrawCommands_.push_back(std::move(command));
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::RenderDualTextureDraws(WGPURenderPassEncoder pass)
    {
        for (const DualTextureDrawCommand& command : dualTextureDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty() ||
                command.texture0 == nullptr || command.texture1 == nullptr)
                continue;

            WGPUBufferDescriptor vbDescriptor{};
            vbDescriptor.label = StringView("CNA WebGPU DualTexture3D VertexBuffer");
            vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            vbDescriptor.size = Align4(command.vertexData.size());
            WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
            wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

            WGPUBufferDescriptor uboDescriptor{};
            uboDescriptor.label = StringView("CNA WebGPU DualTexture3D UBO");
            uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            uboDescriptor.size = sizeof(command.uniforms);
            WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
            wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), sizeof(command.uniforms));

            WGPUBindGroupEntry uboEntry{};
            uboEntry.binding = 0;
            uboEntry.buffer = uniformBuffer;
            uboEntry.size = sizeof(command.uniforms);
            WGPUBindGroupDescriptor uboBindDescriptor{};
            uboBindDescriptor.label = StringView("CNA WebGPU DualTexture3D UBO BindGroup");
            uboBindDescriptor.layout = coloredBindGroupLayout_;
            uboBindDescriptor.entryCount = 1;
            uboBindDescriptor.entries = &uboEntry;
            WGPUBindGroup uboBindGroup = wgpuDeviceCreateBindGroup(device_, &uboBindDescriptor);

            WGPUSampler sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
            std::array<WGPUBindGroupEntry, 3> texEntries{};
            texEntries[0].binding = 0;
            texEntries[0].sampler = sampler;
            texEntries[1].binding = 1;
            texEntries[1].textureView = command.texture0->View();
            texEntries[2].binding = 2;
            texEntries[2].textureView = command.texture1->View();
            WGPUBindGroupDescriptor texBindDescriptor{};
            texBindDescriptor.label = StringView("CNA WebGPU DualTexture3D Texture BindGroup");
            texBindDescriptor.layout = dualTextureBindGroupLayout_;
            texBindDescriptor.entryCount = texEntries.size();
            texBindDescriptor.entries = texEntries.data();
            WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

            WGPURenderPipeline pipe = GetOrCreatePipelineDualTexture3D(command.hasVertexColor ? 24 : 20,
                                                                       command.topology, command.depthTest,
                                                                       command.depthWrite, command.depthFunc);
            wgpuRenderPassEncoderSetPipeline(pass, pipe);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, uboBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, texBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

            if (command.indexed && !command.indexData.empty())
            {
                WGPUBufferDescriptor ibDescriptor{};
                ibDescriptor.label = StringView("CNA WebGPU DualTexture3D IndexBuffer");
                ibDescriptor.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
                ibDescriptor.size = Align4(command.indexData.size());
                WGPUBuffer indexBuffer = wgpuDeviceCreateBuffer(device_, &ibDescriptor);
                wgpuQueueWriteBuffer(queue_, indexBuffer, 0, command.indexData.data(), command.indexData.size());
                wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer,
                    command.index32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
                    0, command.indexData.size());
                wgpuRenderPassEncoderDrawIndexed(pass, command.indexCount, 1, 0, 0, 0);
                pendingBufferReleases_.push_back(indexBuffer);
            }
            else
            {
                wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
            }

            pendingBindGroupReleases_.push_back(uboBindGroup);
            pendingBindGroupReleases_.push_back(texBindGroup);
            pendingBufferReleases_.push_back(uniformBuffer);
            pendingBufferReleases_.push_back(vertexBuffer);
        }
        dualTextureDrawCommands_.clear();
    }

    void WebGPUGraphicsBackend::QueueDualTextureDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                      const Matrix& world, const Matrix& view, const Matrix& projection,
                                                      PrimitiveType primitive, int primitiveCount,
                                                      const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        const std::size_t stride = webgpuVb.Stride();
        if (stride != 20 && stride != 24)
            throw std::invalid_argument("CNA WebGPU: QueueDualTextureDraw requires a stride-20 "
                                        "or stride-24 vertex buffer");
        if (params.texture0 == nullptr || params.texture1 == nullptr)
            throw std::invalid_argument("CNA WebGPU: QueueDualTextureDraw requires both texture0 "
                                        "and texture1 to be bound");

        DualTextureDrawCommand command;
        command.hasVertexColor = (stride == 24);
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(params.vertexStart) * stride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.texture0 = static_cast<const WebGPUTextureBackend*>(params.texture0);
        command.texture1 = static_cast<const WebGPUTextureBackend*>(params.texture1);
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(params.vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        dualTextureDrawCommands_.push_back(std::move(command));
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::QueueTexturedDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                                  PrimitiveType primitive, int primitiveCount,
                                                  const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        const std::size_t stride = webgpuVb.Stride();
        if (stride != 20 && stride != 24)
            throw std::invalid_argument("CNA WebGPU: QueueTexturedDraw requires a stride-20 "
                                        "(VertexPositionTexture) or stride-24 "
                                        "(VertexPositionColorTexture) vertex buffer");
        if (params.texture0 == nullptr)
            throw std::invalid_argument("CNA WebGPU: QueueTexturedDraw requires a bound texture0");

        TexturedDrawCommand command;
        command.hasVertexColor = (stride == 24);
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(params.vertexStart) * stride;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.texture = static_cast<const WebGPUTextureBackend*>(params.texture0);
        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(params.vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        texturedDrawCommands_.push_back(std::move(command));
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::DestroyPbrResources()
    {
        for (auto& [key, pipe] : pbrPipelines_)
        {
            if (pipe != nullptr) wgpuRenderPipelineRelease(pipe);
        }
        pbrPipelines_.clear();
        if (pbrPipelineLayout_ != nullptr) wgpuPipelineLayoutRelease(pbrPipelineLayout_);
        if (pbrBindGroupLayout1_ != nullptr) wgpuBindGroupLayoutRelease(pbrBindGroupLayout1_);
        if (pbrBindGroupLayout0_ != nullptr) wgpuBindGroupLayoutRelease(pbrBindGroupLayout0_);
        if (pbrShader_ != nullptr) wgpuShaderModuleRelease(pbrShader_);
        pbrPipelineLayout_ = nullptr;
        pbrBindGroupLayout1_ = nullptr;
        pbrBindGroupLayout0_ = nullptr;
        pbrShader_ = nullptr;
    }

    void WebGPUGraphicsBackend::CreatePbrResources()
    {
        DestroyPbrResources();
        if (surfaceFormat_ == WGPUTextureFormat_Undefined)
            return;

        // Ported from EasyGLGraphicsBackend::EnsurePbrProgram()'s GLSL PbrLight() helper
        // unchanged: GGX/Trowbridge-Reitz D, Smith-Schlick-GGX visibility (direct-lighting
        // k=(roughness+1)^2/8), and Schlick Fresnel -- the glTF 2.0 spec's own reference BRDF
        // (Appendix B.3.3/B.3.4/B.3.2). The TBN basis is built per-pixel from the vertex tangent
        // (Gram-Schmidt re-orthogonalized against the interpolated world normal), with the
        // bitangent sign from tangent.w (glTF convention) -- identical to the EasyGL fragment
        // shader's own construction. Group 0's Uniforms/LitLightParams struct shapes match
        // lit_textured3d.wgsl's own field-for-field (populated by the same
        // FillExtUniforms()/FillLitLightUniforms() helpers); PbrFactors is the one genuinely new
        // (small) buffer this shader needs.
        static constexpr char shaderSource[] = R"WGSL(
struct Uniforms {
    mvp: mat4x4f,
    diffuseColor: vec4f,
    ambientLighting: vec4f,
    light0DirTexture: vec4f,
    light0DiffuseVertexColor: vec4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

struct LitLightParams {
    light1Dir: vec4f,
    light1Diffuse: vec4f,
    light2Dir: vec4f,
    light2Diffuse: vec4f,
    emissiveColor: vec4f,
    world: mat4x4f,
    eyePos: vec4f,
    light0Specular: vec4f,
    light1Specular: vec4f,
    light2Specular: vec4f,
    specularColorPower: vec4f,
    normalMatrixCol0: vec4f,
    normalMatrixCol1: vec4f,
    normalMatrixCol2: vec4f,
};
@group(0) @binding(1) var<uniform> lp: LitLightParams;

struct PbrFactors {
    metallicRoughness: vec4f,
};
@group(0) @binding(2) var<uniform> pf: PbrFactors;

@group(1) @binding(0) var texSampler: sampler;
@group(1) @binding(1) var baseColorTex: texture_2d<f32>;
@group(1) @binding(2) var normalTex: texture_2d<f32>;
@group(1) @binding(3) var metallicRoughnessTex: texture_2d<f32>;
@group(1) @binding(4) var emissiveTex: texture_2d<f32>;
@group(1) @binding(5) var occlusionTex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
};
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) worldNormal: vec3f,
    @location(2) worldTangent: vec3f,
    @location(3) bitangentSign: f32,
    @location(4) worldPos: vec3f,
};
@vertex fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;
    output.position = u.mvp * vec4f(input.position, 1.0);
    output.uv = input.uv;
    let normalMatrix = mat3x3f(lp.normalMatrixCol0.xyz, lp.normalMatrixCol1.xyz, lp.normalMatrixCol2.xyz);
    output.worldNormal = normalMatrix * input.normal;
    // Tangent transforms as a plain direction under mat3(world) (uniform-scale assumption),
    // matching EnsurePbrProgram()'s own documented simplification.
    let worldMat3 = mat3x3f(lp.world[0].xyz, lp.world[1].xyz, lp.world[2].xyz);
    output.worldTangent = worldMat3 * input.tangent.xyz;
    output.bitangentSign = input.tangent.w;
    output.worldPos = (lp.world * vec4f(input.position, 1.0)).xyz;
    return output;
}

fn pbrLight(n: vec3f, v: vec3f, l: vec3f, lightColor: vec3f, albedo: vec3f, f0: vec3f, roughness: f32, metallic: f32) -> vec3f {
    let h = normalize(v + l);
    let ndotl = max(dot(n, l), 0.0);
    let ndotv = max(dot(n, v), 1e-4);
    let ndoth = max(dot(n, h), 0.0);
    let vdoth = max(dot(v, h), 0.0);
    let a2 = pow(roughness, 4.0);
    let dTerm = ndoth * ndoth * (a2 - 1.0) + 1.0;
    let d = a2 / (3.14159265 * dTerm * dTerm + 1e-7);
    var k = roughness + 1.0;
    k = k * k / 8.0;
    let g = (ndotv / (ndotv * (1.0 - k) + k)) * (ndotl / (ndotl * (1.0 - k) + k));
    let f = f0 + (vec3f(1.0) - f0) * pow(clamp(1.0 - vdoth, 0.0, 1.0), 5.0);
    let specular = (d * g * f) / max(4.0 * ndotv * ndotl, 1e-4);
    let diffuseColor = albedo * (1.0 - metallic);
    let kd = vec3f(1.0) - f;
    return (kd * diffuseColor / 3.14159265 + specular) * lightColor * ndotl;
}

@fragment fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let baseColorSample = textureSample(baseColorTex, texSampler, input.uv);
    let albedo = baseColorSample.rgb * u.diffuseColor.rgb;
    let alpha = baseColorSample.a * u.diffuseColor.a;

    let n0 = normalize(input.worldNormal);
    let t0 = normalize(input.worldTangent - n0 * dot(n0, input.worldTangent));
    let b0 = cross(n0, t0) * input.bitangentSign;
    let tbn = mat3x3f(t0, b0, n0);
    let sampledNormal = textureSample(normalTex, texSampler, input.uv).rgb * 2.0 - 1.0;
    let finalNormal = normalize(tbn * sampledNormal);

    let mr = textureSample(metallicRoughnessTex, texSampler, input.uv);
    let roughness = clamp(mr.g * pf.metallicRoughness.y, 0.045, 1.0);
    let metallic = clamp(mr.b * pf.metallicRoughness.x, 0.0, 1.0);

    let eye = normalize(lp.eyePos.xyz - input.worldPos);
    let f0 = mix(vec3f(0.04), albedo, metallic);

    // Same disabled-light NaN guard as lit_textured3d.wgsl: a disabled DirectionalLight forwards
    // Direction=(0,0,0) (only DiffuseColor is zeroed), and normalize() on a true zero vector is
    // undefined and can poison the whole sum with NaN.
    let dir0sq = dot(u.light0DirTexture.xyz, u.light0DirTexture.xyz);
    let dir1sq = dot(lp.light1Dir.xyz, lp.light1Dir.xyz);
    let dir2sq = dot(lp.light2Dir.xyz, lp.light2Dir.xyz);
    let l0 = select(vec3f(0.0), normalize(-u.light0DirTexture.xyz), dir0sq > 0.0);
    let l1 = select(vec3f(0.0), normalize(-lp.light1Dir.xyz), dir1sq > 0.0);
    let l2 = select(vec3f(0.0), normalize(-lp.light2Dir.xyz), dir2sq > 0.0);

    var lo = vec3f(0.0);
    lo += pbrLight(finalNormal, eye, l0, u.light0DiffuseVertexColor.xyz, albedo, f0, roughness, metallic);
    lo += pbrLight(finalNormal, eye, l1, lp.light1Diffuse.xyz, albedo, f0, roughness, metallic);
    lo += pbrLight(finalNormal, eye, l2, lp.light2Diffuse.xyz, albedo, f0, roughness, metallic);

    let occlusion = textureSample(occlusionTex, texSampler, input.uv).r;
    let ambient = u.ambientLighting.xyz * albedo * occlusion;
    let emissive = lp.emissiveColor.xyz * textureSample(emissiveTex, texSampler, input.uv).rgb;

    return vec4f(ambient + lo + emissive, alpha);
}
)WGSL";

        WGPUShaderSourceWGSL wgsl{};
        wgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        wgsl.code = StringView(shaderSource);
        WGPUShaderModuleDescriptor shaderDescriptor{};
        shaderDescriptor.label = StringView("CNA WebGPU Pbr3D WGSL");
        shaderDescriptor.nextInChain = &wgsl.chain;
        pbrShader_ = wgpuDeviceCreateShaderModule(device_, &shaderDescriptor);
        if (pbrShader_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Pbr3D shader");

        std::array<WGPUBindGroupLayoutEntry, 3> uboEntries{};
        uboEntries[0].binding = 0;
        uboEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        uboEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
        uboEntries[0].buffer.minBindingSize = 128;
        uboEntries[1].binding = 1;
        uboEntries[1].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        uboEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
        uboEntries[1].buffer.minBindingSize = 272;
        uboEntries[2].binding = 2;
        uboEntries[2].visibility = WGPUShaderStage_Fragment;
        uboEntries[2].buffer.type = WGPUBufferBindingType_Uniform;
        uboEntries[2].buffer.minBindingSize = 16;
        WGPUBindGroupLayoutDescriptor uboLayoutDescriptor{};
        uboLayoutDescriptor.label = StringView("CNA WebGPU Pbr3D BindGroupLayout0");
        uboLayoutDescriptor.entryCount = uboEntries.size();
        uboLayoutDescriptor.entries = uboEntries.data();
        pbrBindGroupLayout0_ = wgpuDeviceCreateBindGroupLayout(device_, &uboLayoutDescriptor);

        std::array<WGPUBindGroupLayoutEntry, 6> texEntries{};
        texEntries[0].binding = 0;
        texEntries[0].visibility = WGPUShaderStage_Fragment;
        texEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;
        for (std::uint32_t i = 1; i <= 5; ++i)
        {
            texEntries[i].binding = i;
            texEntries[i].visibility = WGPUShaderStage_Fragment;
            texEntries[i].texture.sampleType = WGPUTextureSampleType_Float;
            texEntries[i].texture.viewDimension = WGPUTextureViewDimension_2D;
            texEntries[i].texture.multisampled = false;
        }
        WGPUBindGroupLayoutDescriptor texLayoutDescriptor{};
        texLayoutDescriptor.label = StringView("CNA WebGPU Pbr3D BindGroupLayout1");
        texLayoutDescriptor.entryCount = texEntries.size();
        texLayoutDescriptor.entries = texEntries.data();
        pbrBindGroupLayout1_ = wgpuDeviceCreateBindGroupLayout(device_, &texLayoutDescriptor);

        std::array<WGPUBindGroupLayout, 2> groupLayouts{pbrBindGroupLayout0_, pbrBindGroupLayout1_};
        WGPUPipelineLayoutDescriptor pipelineLayoutDescriptor{};
        pipelineLayoutDescriptor.label = StringView("CNA WebGPU Pbr3D PipelineLayout");
        pipelineLayoutDescriptor.bindGroupLayoutCount = groupLayouts.size();
        pipelineLayoutDescriptor.bindGroupLayouts = groupLayouts.data();
        pbrPipelineLayout_ = wgpuDeviceCreatePipelineLayout(device_, &pipelineLayoutDescriptor);

        if (pbrBindGroupLayout0_ == nullptr || pbrBindGroupLayout1_ == nullptr || pbrPipelineLayout_ == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Pbr3D GPU resources");
    }

    WGPURenderPipeline WebGPUGraphicsBackend::GetOrCreatePipelinePbr3D(WGPUPrimitiveTopology topology,
                                                                         bool depthTest, bool depthWrite,
                                                                         int depthFunc)
    {
        const int key = (static_cast<int>(topology) * 8 + depthFunc) * 4 + (depthTest ? 2 : 0) + (depthWrite ? 1 : 0);
        if (auto it = pbrPipelines_.find(key); it != pbrPipelines_.end())
            return it->second;

        // Matches VertexPositionNormalTangentTexture's 48-byte layout: Position(12) + Normal(12)
        // + Tangent(16, xyz + bitangent-handedness in w) + TextureCoordinate(8).
        struct PbrVertex { float x, y, z, nx, ny, nz, tx, ty, tz, tw, u, v; };
        static_assert(sizeof(PbrVertex) == 48, "PbrVertex must be 48 bytes");
        std::array<WGPUVertexAttribute, 4> attributes{};
        attributes[0].format = WGPUVertexFormat_Float32x3;
        attributes[0].offset = offsetof(PbrVertex, x);
        attributes[0].shaderLocation = 0;
        attributes[1].format = WGPUVertexFormat_Float32x3;
        attributes[1].offset = offsetof(PbrVertex, nx);
        attributes[1].shaderLocation = 1;
        attributes[2].format = WGPUVertexFormat_Float32x4;
        attributes[2].offset = offsetof(PbrVertex, tx);
        attributes[2].shaderLocation = 2;
        attributes[3].format = WGPUVertexFormat_Float32x2;
        attributes[3].offset = offsetof(PbrVertex, u);
        attributes[3].shaderLocation = 3;
        WGPUVertexBufferLayout vertexBufferLayout{};
        vertexBufferLayout.arrayStride = sizeof(PbrVertex);
        vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
        vertexBufferLayout.attributeCount = attributes.size();
        vertexBufferLayout.attributes = attributes.data();

        WGPUColorTargetState target{};
        target.format = surfaceFormat_;
        target.writeMask = WGPUColorWriteMask_All;
        WGPUFragmentState fragment{};
        fragment.module = pbrShader_;
        fragment.entryPoint = StringView("fs_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor pipeline{};
        pipeline.label = StringView("CNA WebGPU Pbr3D Pipeline");
        pipeline.layout = pbrPipelineLayout_;
        pipeline.vertex.module = pbrShader_;
        pipeline.vertex.entryPoint = StringView("vs_main");
        pipeline.vertex.bufferCount = 1;
        pipeline.vertex.buffers = &vertexBufferLayout;
        pipeline.primitive.topology = topology;
        pipeline.primitive.frontFace = WGPUFrontFace_CCW;
        pipeline.primitive.cullMode = WGPUCullMode_None;
        pipeline.multisample.count = 1;
        pipeline.multisample.mask = std::numeric_limits<std::uint32_t>::max();
        pipeline.multisample.alphaToCoverageEnabled = false;
        pipeline.fragment = &fragment;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = depthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = depthTest ? ToWGPUCompareFunction(depthFunc) : WGPUCompareFunction_Always;
        pipeline.depthStencil = &depthStencil;

        WGPURenderPipeline created = wgpuDeviceCreateRenderPipeline(device_, &pipeline);
        if (created == nullptr)
            throw std::runtime_error("CNA WebGPU: failed to create Pbr3D pipeline");
        pbrPipelines_[key] = created;
        return created;
    }

    void WebGPUGraphicsBackend::EnsurePbrDefaultTextures()
    {
        // Mirrors EasyGLGraphicsBackend::EnsureDefaultWhiteTexture()/
        // EnsureDefaultFlatNormalTexture(): a 1x1 flat tangent-space normal (0,0,1), encoded as
        // RGB (128,128,255), so the sampled/decoded (rgb*2-1) normal is exactly the geometric
        // normal (no perturbation) when PbrEffect::NormalMap is unbound. The other 3 PBR map
        // fallbacks (metallic-roughness, emissive, occlusion) all reuse a shared 1x1 white texture
        // -- their respective factor/no-op semantics already make (1,1,1,1) the correct "map
        // absent" value (factor*1.0=factor; emissive tint*1.0=tint; occlusion 1.0=unoccluded).
        if (pbrDefaultWhiteTexture_ == nullptr)
        {
            ImageData white{};
            white.width = 1;
            white.height = 1;
            white.mipLevels = 1;
            white.pixels = {255, 255, 255, 255};
            pbrDefaultWhiteTexture_ = std::make_unique<WebGPUTextureBackend>(*this, white);
        }
        if (pbrDefaultFlatNormalTexture_ == nullptr)
        {
            ImageData flatNormal{};
            flatNormal.width = 1;
            flatNormal.height = 1;
            flatNormal.mipLevels = 1;
            flatNormal.pixels = {128, 128, 255, 255};
            pbrDefaultFlatNormalTexture_ = std::make_unique<WebGPUTextureBackend>(*this, flatNormal);
        }
    }

    void WebGPUGraphicsBackend::QueuePbrDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                              const Matrix& world, const Matrix& view, const Matrix& projection,
                                              PrimitiveType primitive, int primitiveCount,
                                              const GpuDrawParams& params)
    {
        const auto& webgpuVb = static_cast<const WebGPUVertexBufferBackend&>(vb);
        if (webgpuVb.Stride() != 48)
            throw std::invalid_argument("CNA WebGPU: QueuePbrDraw requires a stride-48 "
                                        "(VertexPositionNormalTangentTexture) vertex buffer");
        if (params.texture0 == nullptr)
            throw std::invalid_argument("CNA WebGPU: QueuePbrDraw requires a bound texture0");

        EnsurePbrDefaultTextures();

        PbrDrawCommand command;
        const auto& shadow = webgpuVb.ShadowData();
        const std::size_t byteOffset = static_cast<std::size_t>(params.vertexStart) * 48u;
        if (byteOffset <= shadow.size())
            command.vertexData.assign(shadow.begin() + static_cast<std::ptrdiff_t>(byteOffset), shadow.end());
        command.topology = ToTopology(primitive);
        command.depthTest = depthTestEnabled_;
        command.depthFunc = depthCompareFunction_;
        command.depthWrite = depthWriteEnabled_;
        command.baseColorTexture = static_cast<const WebGPUTextureBackend*>(params.texture0);
        command.normalMap = params.pbrNormalMap != nullptr
            ? static_cast<const WebGPUTextureBackend*>(params.pbrNormalMap)
            : pbrDefaultFlatNormalTexture_.get();
        command.metallicRoughnessMap = params.pbrMetallicRoughnessMap != nullptr
            ? static_cast<const WebGPUTextureBackend*>(params.pbrMetallicRoughnessMap)
            : pbrDefaultWhiteTexture_.get();
        command.emissiveMap = params.pbrEmissiveMap != nullptr
            ? static_cast<const WebGPUTextureBackend*>(params.pbrEmissiveMap)
            : pbrDefaultWhiteTexture_.get();
        command.occlusionMap = params.pbrOcclusionMap != nullptr
            ? static_cast<const WebGPUTextureBackend*>(params.pbrOcclusionMap)
            : pbrDefaultWhiteTexture_.get();

        const Matrix wvp = world * view * projection;
        FillExtUniforms(command.uniforms, wvp, params);
        FillLitLightUniforms(command.lightUniforms, params);
        FillPbrFactors(command.pbrFactors, params);

        if (ib != nullptr)
        {
            const auto& webgpuIb = static_cast<const WebGPUIndexBufferBackend&>(*ib);
            command.indexed = true;
            command.index32 = webgpuIb.IsThirtyTwoBit();
            command.indexData = webgpuIb.ShadowData();
            command.indexCount = static_cast<std::uint32_t>(PrimitiveIndexCount(primitive, primitiveCount));
            command.vertexCount = static_cast<std::uint32_t>(webgpuVb.GetVertexCount()) -
                                  static_cast<std::uint32_t>(params.vertexStart);
        }
        else
        {
            command.vertexCount = static_cast<std::uint32_t>(PrimitiveVertexCount(primitive, primitiveCount));
        }

        pbrDrawCommands_.push_back(std::move(command));
        framePending_ = true;
    }

    void WebGPUGraphicsBackend::RenderPbrDraws(WGPURenderPassEncoder pass)
    {
        for (const PbrDrawCommand& command : pbrDrawCommands_)
        {
            if (command.vertexCount == 0 || command.vertexData.empty() || command.baseColorTexture == nullptr ||
                command.normalMap == nullptr || command.metallicRoughnessMap == nullptr ||
                command.emissiveMap == nullptr || command.occlusionMap == nullptr)
                continue;

            WGPUBufferDescriptor vbDescriptor{};
            vbDescriptor.label = StringView("CNA WebGPU Pbr3D VertexBuffer");
            vbDescriptor.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
            vbDescriptor.size = Align4(command.vertexData.size());
            WGPUBuffer vertexBuffer = wgpuDeviceCreateBuffer(device_, &vbDescriptor);
            wgpuQueueWriteBuffer(queue_, vertexBuffer, 0, command.vertexData.data(), command.vertexData.size());

            WGPUBufferDescriptor uboDescriptor{};
            uboDescriptor.label = StringView("CNA WebGPU Pbr3D UBO");
            uboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            uboDescriptor.size = sizeof(command.uniforms);
            WGPUBuffer uniformBuffer = wgpuDeviceCreateBuffer(device_, &uboDescriptor);
            wgpuQueueWriteBuffer(queue_, uniformBuffer, 0, command.uniforms.data(), sizeof(command.uniforms));

            WGPUBufferDescriptor lightUboDescriptor{};
            lightUboDescriptor.label = StringView("CNA WebGPU Pbr3D LightUBO");
            lightUboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            lightUboDescriptor.size = sizeof(command.lightUniforms);
            WGPUBuffer lightUniformBuffer = wgpuDeviceCreateBuffer(device_, &lightUboDescriptor);
            wgpuQueueWriteBuffer(queue_, lightUniformBuffer, 0, command.lightUniforms.data(), sizeof(command.lightUniforms));

            WGPUBufferDescriptor factorsUboDescriptor{};
            factorsUboDescriptor.label = StringView("CNA WebGPU Pbr3D FactorsUBO");
            factorsUboDescriptor.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            factorsUboDescriptor.size = sizeof(command.pbrFactors);
            WGPUBuffer factorsUniformBuffer = wgpuDeviceCreateBuffer(device_, &factorsUboDescriptor);
            wgpuQueueWriteBuffer(queue_, factorsUniformBuffer, 0, command.pbrFactors.data(), sizeof(command.pbrFactors));

            std::array<WGPUBindGroupEntry, 3> uboEntries{};
            uboEntries[0].binding = 0;
            uboEntries[0].buffer = uniformBuffer;
            uboEntries[0].size = sizeof(command.uniforms);
            uboEntries[1].binding = 1;
            uboEntries[1].buffer = lightUniformBuffer;
            uboEntries[1].size = sizeof(command.lightUniforms);
            uboEntries[2].binding = 2;
            uboEntries[2].buffer = factorsUniformBuffer;
            uboEntries[2].size = sizeof(command.pbrFactors);
            WGPUBindGroupDescriptor uboBindDescriptor{};
            uboBindDescriptor.label = StringView("CNA WebGPU Pbr3D UBO BindGroup");
            uboBindDescriptor.layout = pbrBindGroupLayout0_;
            uboBindDescriptor.entryCount = uboEntries.size();
            uboBindDescriptor.entries = uboEntries.data();
            WGPUBindGroup uboBindGroup = wgpuDeviceCreateBindGroup(device_, &uboBindDescriptor);

            WGPUSampler sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
            std::array<WGPUBindGroupEntry, 6> texEntries{};
            texEntries[0].binding = 0;
            texEntries[0].sampler = sampler;
            texEntries[1].binding = 1;
            texEntries[1].textureView = command.baseColorTexture->View();
            texEntries[2].binding = 2;
            texEntries[2].textureView = command.normalMap->View();
            texEntries[3].binding = 3;
            texEntries[3].textureView = command.metallicRoughnessMap->View();
            texEntries[4].binding = 4;
            texEntries[4].textureView = command.emissiveMap->View();
            texEntries[5].binding = 5;
            texEntries[5].textureView = command.occlusionMap->View();
            WGPUBindGroupDescriptor texBindDescriptor{};
            texBindDescriptor.label = StringView("CNA WebGPU Pbr3D Texture BindGroup");
            texBindDescriptor.layout = pbrBindGroupLayout1_;
            texBindDescriptor.entryCount = texEntries.size();
            texBindDescriptor.entries = texEntries.data();
            WGPUBindGroup texBindGroup = wgpuDeviceCreateBindGroup(device_, &texBindDescriptor);

            WGPURenderPipeline pipe = GetOrCreatePipelinePbr3D(command.topology, command.depthTest,
                                                               command.depthWrite, command.depthFunc);
            wgpuRenderPassEncoderSetPipeline(pass, pipe);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, uboBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, texBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, command.vertexData.size());

            if (command.indexed && !command.indexData.empty())
            {
                WGPUBufferDescriptor ibDescriptor{};
                ibDescriptor.label = StringView("CNA WebGPU Pbr3D IndexBuffer");
                ibDescriptor.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
                ibDescriptor.size = Align4(command.indexData.size());
                WGPUBuffer indexBuffer = wgpuDeviceCreateBuffer(device_, &ibDescriptor);
                wgpuQueueWriteBuffer(queue_, indexBuffer, 0, command.indexData.data(), command.indexData.size());
                wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer,
                    command.index32 ? WGPUIndexFormat_Uint32 : WGPUIndexFormat_Uint16,
                    0, command.indexData.size());
                wgpuRenderPassEncoderDrawIndexed(pass, command.indexCount, 1, 0, 0, 0);
                pendingBufferReleases_.push_back(indexBuffer);
            }
            else
            {
                wgpuRenderPassEncoderDraw(pass, command.vertexCount, 1, 0, 0);
            }

            pendingBindGroupReleases_.push_back(uboBindGroup);
            pendingBindGroupReleases_.push_back(texBindGroup);
            pendingBufferReleases_.push_back(uniformBuffer);
            pendingBufferReleases_.push_back(lightUniformBuffer);
            pendingBufferReleases_.push_back(factorsUniformBuffer);
            pendingBufferReleases_.push_back(vertexBuffer);
        }
        pbrDrawCommands_.clear();
    }
}


namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<WebGPU::WebGPUGraphicsBackend>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode, args.swapInterval);
    }
}
