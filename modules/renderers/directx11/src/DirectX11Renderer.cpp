// plans/plan_dx.md Phase DIRECTX2/DIRECTX4: D3D11 renderer skeleton + device/swap-chain/back-buffer.
#include "CNA/Logger.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#include "CNA/Internal/Renderers/DirectX11/DirectX11Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11IndirectBuffer.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11ComputeShader.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11Textures.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11Texture2DArray.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11StorageTexture2D.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11OcclusionQuery.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11EffectRenderer.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11SpriteBatch.hpp"
#if defined(CNA_DIRECTX11_COMPILED_EFFECTS)
#include "CNA/Internal/Renderers/DirectX11/D3D11CompiledEffect.hpp"
#endif
#include "CNA/Internal/Renderers/D3DCommon/D3DShaderCache.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DConstantBuffers.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DDebugLayerLog.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DFormatMapping.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DPresentation.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DRasterizationConvention.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DStateMapping.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::DirectX11
{
    namespace
    {
        class D3D11GpuTimerRenderer final : public IGpuTimerRenderer
        {
        public:
            D3D11GpuTimerRenderer(ID3D11Device* device, ID3D11DeviceContext* context)
                : context_(context)
            {
                D3D11_QUERY_DESC description{};
                description.Query = D3D11_QUERY_TIMESTAMP_DISJOINT;
                if (FAILED(device->CreateQuery(&description, disjoint_.GetAddressOf())))
                    throw std::runtime_error("D3D11 GPU timer: disjoint query creation failed");
                description.Query = D3D11_QUERY_TIMESTAMP;
                if (FAILED(device->CreateQuery(&description, start_.GetAddressOf())) ||
                    FAILED(device->CreateQuery(&description, end_.GetAddressOf())))
                    throw std::runtime_error("D3D11 GPU timer: timestamp query creation failed");
            }

            void Begin() override
            {
                ready_ = false;
                ended_ = false;
                context_->Begin(disjoint_.Get());
                context_->End(start_.Get());
            }

            void End() override
            {
                context_->End(end_.Get());
                context_->End(disjoint_.Get());
                ended_ = true;
            }

            [[nodiscard]] bool IsResultAvailable() const override
            {
                if (!ended_) return false;
                if (ready_) return true;
                D3D11_QUERY_DATA_TIMESTAMP_DISJOINT disjoint{};
                UINT64 start = 0;
                UINT64 end = 0;
                constexpr UINT flags = D3D11_ASYNC_GETDATA_DONOTFLUSH;
                if (context_->GetData(disjoint_.Get(), &disjoint, sizeof(disjoint), flags) != S_OK ||
                    context_->GetData(start_.Get(), &start, sizeof(start), flags) != S_OK ||
                    context_->GetData(end_.Get(), &end, sizeof(end), flags) != S_OK)
                    return false;
                nanoseconds_ = disjoint.Disjoint || disjoint.Frequency == 0 || end < start
                    ? 0
                    : static_cast<std::uint64_t>(
                          static_cast<long double>(end - start) * 1.0e9L /
                          static_cast<long double>(disjoint.Frequency));
                ready_ = true;
                return true;
            }

            [[nodiscard]] std::uint64_t ElapsedNanoseconds() const override
            {
                return IsResultAvailable() ? nanoseconds_ : 0;
            }

        private:
            ComPtr<ID3D11DeviceContext> context_;
            ComPtr<ID3D11Query> disjoint_;
            ComPtr<ID3D11Query> start_;
            ComPtr<ID3D11Query> end_;
            bool ended_ = false;
            mutable bool ready_ = false;
            mutable std::uint64_t nanoseconds_ = 0;
        };

        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

        /// Writes two known texels into a @p format texture and reads them back through a staging
        /// copy. CheckFormatSupport is only the driver's claim: VirtualBox's SVGA driver claims
        /// B4G4R4A4_UNORM for 2D, cube and volume textures, accepts every upload, and reads back
        /// zeros -- while B5G6R5 and B5G5R5A1 round-trip exactly on the same device. Every bit
        /// pattern is a valid texel in these UNORM formats, so a faithful device returns the bytes.
        bool FormatKeepsWrittenBytes(
            ID3D11Device* device, ID3D11DeviceContext* context, DXGI_FORMAT format)
        {
            UINT support = 0;
            if (FAILED(device->CheckFormatSupport(format, &support)) ||
                (support & D3D11_FORMAT_SUPPORT_TEXTURE2D) == 0)
                return false;

            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = 2;
            desc.Height = 1;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = format;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            ComPtr<ID3D11Texture2D> texture;
            if (FAILED(device->CreateTexture2D(&desc, nullptr, texture.GetAddressOf())))
                return false;
            desc.Usage = D3D11_USAGE_STAGING;
            desc.BindFlags = 0;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            ComPtr<ID3D11Texture2D> staging;
            if (FAILED(device->CreateTexture2D(&desc, nullptr, staging.GetAddressOf())))
                return false;

            static constexpr std::uint8_t kTexels[4] = {0x24, 0x2F, 0x35, 0x46};
            context->UpdateSubresource(texture.Get(), 0, nullptr, kTexels, sizeof(kTexels), 0);
            context->CopyResource(staging.Get(), texture.Get());
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
                return false;
            const bool kept = std::memcmp(mapped.pData, kTexels, sizeof(kTexels)) == 0;
            context->Unmap(staging.Get(), 0);
            return kept;
        }

        int ClampBackBufferMultiSampleCount(
            ID3D11Device* device, DXGI_FORMAT format, int requestedCount)
        {
            if (device == nullptr || requestedCount <= 1) return 0;

            int candidate = 1;
            while (candidate <= requestedCount / 2) candidate *= 2;
            while (candidate > 1)
            {
                UINT qualityLevels = 0;
                if (SUCCEEDED(device->CheckMultisampleQualityLevels(
                        format, static_cast<UINT>(candidate), &qualityLevels)) &&
                    qualityLevels > 0)
                {
                    return candidate;
                }
                candidate >>= 1;
            }
            return 0;
        }

        /// Same per-PrimitiveType vertex-count formula every other renderer duplicates locally
        /// (Vulkan/EasyGL's own VertexCountForPrimitives) -- not shared via a common header today,
        /// so this follows the existing precedent rather than introducing a new one.
        int VertexCountForPrimitives(PrimitiveType pt, int primitiveCount)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList:      return primitiveCount * 2;
            case PrimitiveType::LineStrip:     return primitiveCount + 1;
            case PrimitiveType::PointListEXT:  return primitiveCount;
            }
            throw std::runtime_error(
                "DirectX11 renderer does not support the requested PrimitiveType value");
        }

        D3D11_PRIMITIVE_TOPOLOGY ToD3D11Topology(PrimitiveType pt)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            case PrimitiveType::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            case PrimitiveType::LineList:      return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
            case PrimitiveType::LineStrip:     return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
            case PrimitiveType::PointListEXT:  return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
            }
            throw std::runtime_error(
                "DirectX11 renderer does not support the requested PrimitiveType value");
        }

        void BuildVertexInputLayout(
            const GpuDrawParams& params, bool includeInstanceStreams,
            std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& combinedElements,
            std::vector<D3DCommon::D3DVertexInputElement>& inputElements)
        {
            combinedElements.clear();
            inputElements.clear();
            const auto streamElements = [](const D3D11VertexBufferRenderer& buffer,
                                           int strideInBytes)
            {
                auto elements = buffer.GetDeclarationEXT().GetElements();
                if (!elements.empty())
                    return elements;
                const auto inferred = CNA::Internal::Graphics::InferredLayoutForStride(
                    strideInBytes,
                    CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
                if (!inferred.known)
                    throw System::NotSupportedException(
                        "DirectX11 cannot infer a vertex declaration for this buffer stride.");
                for (std::size_t index = 0; index < inferred.count; ++index)
                {
                    const auto& input = inferred.elements[index];
                    elements.emplace_back(input.offset, input.format, input.usage,
                                          input.usageIndex);
                }
                return elements;
            };
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency != 0)
                    continue;
                const auto* buffer =
                    static_cast<const D3D11VertexBufferRenderer*>(stream.buffer);
                if (buffer == nullptr)
                    throw System::NotSupportedException(
                        "DirectX11 multi-stream input requires a per-vertex buffer.");
                const auto elements = streamElements(
                    *buffer, stream.strideInBytes > 0 ? stream.strideInBytes
                                                      : static_cast<int>(buffer->GetStrideEXT()));
                for (std::size_t elementIndex = 0; elementIndex < elements.size(); ++elementIndex)
                {
                    auto combined = elements[elementIndex];
                    combined.setOffsetProperty(
                        combined.getOffsetProperty() + stream.combinedByteBase);
                    // WINCLOSE-0033: the shared layer moves a (usage, index) pair that an earlier
                    // bound stream already claimed to that usage's first free index, as XNA and
                    // FNA3D do (REMED-GFX-201/SOFTWARE-320). Using the declaration's own index
                    // instead handed CreateInputLayout two POSITION0 or COLOR0 elements, which it
                    // refuses -- so any draw binding a duplicate-semantic decoy stream failed.
                    if (static_cast<int>(elementIndex) < stream.effectiveUsageIndexCount)
                        combined.setUsageIndexProperty(
                            stream.effectiveUsageIndices[elementIndex]);
                    combinedElements.push_back(combined);
                }
            }

            for (const auto& combined : combinedElements)
            {
                const auto mapped =
                    MapCombinedOffsetToStream(params, combined.getOffsetProperty());
                const auto& stream =
                    params.vertexStreams[static_cast<std::size_t>(mapped.streamIndex)];
                auto local = combined;
                local.setOffsetProperty(mapped.byteOffsetInStream);
                inputElements.push_back({local, stream.slot, 0, false});
            }

            if (!includeInstanceStreams)
                return;
            int instanceColumn = 0;
            for (int i = 0; i < params.vertexStreamCount; ++i)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                if (stream.instanceFrequency <= 0)
                    continue;
                const auto* buffer =
                    static_cast<const D3D11VertexBufferRenderer*>(stream.buffer);
                if (buffer == nullptr)
                    throw System::NotSupportedException(
                        "DirectX11 instancing requires a per-instance buffer.");
                for (const auto& element : streamElements(
                         *buffer, stream.strideInBytes > 0 ? stream.strideInBytes
                                                           : static_cast<int>(buffer->GetStrideEXT())))
                {
                    auto nativeElement = element;
                    const bool worldColumn = instanceColumn < 4;
                    if (worldColumn)
                    {
                        nativeElement.setVertexElementUsageProperty(
                            Microsoft::Xna::Framework::Graphics::VertexElementUsage::TextureCoordinate);
                        nativeElement.setUsageIndexProperty(++instanceColumn);
                    }
                    inputElements.push_back(
                        {nativeElement, stream.slot, stream.instanceFrequency, worldColumn});
                }
            }
        }

        void BindVertexStreams(
            ID3D11DeviceContext* context, const D3D11VertexBufferRenderer& fallback,
            const GpuDrawParams& params, int logicalInstance = -1)
        {
            ID3D11Buffer* buffers[kMaxVertexStreams]{};
            UINT strides[kMaxVertexStreams]{};
            UINT offsets[kMaxVertexStreams]{};
            if (params.vertexStreamCount == 0)
            {
                buffers[0] = fallback.GetBufferEXT();
                strides[0] = static_cast<UINT>(fallback.GetStrideEXT());
            }
            else
            {
                for (int i = 0; i < params.vertexStreamCount; ++i)
                {
                    const auto& stream = params.vertexStreams[static_cast<std::size_t>(i)];
                    if (stream.slot < 0 || stream.slot >= kMaxVertexStreams ||
                        stream.buffer == nullptr)
                        continue;
                    const auto& buffer =
                        *static_cast<const D3D11VertexBufferRenderer*>(stream.buffer);
                    const UINT stride = static_cast<UINT>(stream.strideInBytes > 0
                        ? stream.strideInBytes : buffer.GetStrideEXT());
                    buffers[stream.slot] = buffer.GetBufferEXT();
                    strides[stream.slot] = stride;
                    std::size_t elementOffset = static_cast<std::size_t>(
                        std::max(stream.vertexOffset, 0));
                    if (logicalInstance >= 0 && stream.instanceFrequency > 0)
                        elementOffset += static_cast<std::size_t>(
                            logicalInstance / stream.instanceFrequency);
                    offsets[stream.slot] = static_cast<UINT>(elementOffset * stride);
                }
            }

            // D3D11 IA state survives draw calls. Rebinding all public slots explicitly clears a
            // secondary stream left by a wider preceding draw.
            context->IASetVertexBuffers(
                0, static_cast<UINT>(kMaxVertexStreams), buffers, strides, offsets);
        }

        /// DX-62: resolves the real SRV to bind for a GpuDrawParams texture slot. Two concrete
        /// renderer types can appear here -- a plain D3D11TextureRenderer, or a D3D11RenderTargetRenderer
        /// used as a sampled texture (IRenderTargetRenderer derives ITextureRenderer) -- neither shares
        /// a common "GetShaderResourceViewEXT()" base, so this tries both. Returns nullptr (an
        /// explicit unbind, not an error) if @p tex is null or neither cast matches.
        ID3D11ShaderResourceView* GetSrvForTextureEXT(const ITextureRenderer* tex)
        {
            if (tex == nullptr) return nullptr;
            if (const auto* t = dynamic_cast<const D3D11TextureRenderer*>(tex))
                return t->GetShaderResourceViewEXT();
            if (const auto* rt = dynamic_cast<const D3D11RenderTargetRenderer*>(tex))
                return rt->GetShaderResourceViewEXT();
            return nullptr;
        }

        /// DX-66: same two-concrete-type resolution as GetSrvForTextureEXT above, but for
        /// ITextureCubeRenderer (env_map3d's TextureCube) -- a plain D3D11TextureCubeRenderer, or a
        /// D3D11RenderTargetCubeRenderer used as a sampled cube texture.
        ID3D11ShaderResourceView* GetSrvForTextureCubeEXT(const ITextureCubeRenderer* tex)
        {
            if (tex == nullptr) return nullptr;
            if (const auto* t = dynamic_cast<const D3D11TextureCubeRenderer*>(tex))
                return t->GetShaderResourceViewEXT();
            if (const auto* rt = dynamic_cast<const D3D11RenderTargetCubeRenderer*>(tex))
                return rt->GetShaderResourceViewEXT();
            return nullptr;
        }
    }

    DirectX11Renderer::DirectX11Renderer(const GraphicsRendererCreateArgs& args)
        : surface_(args.surface, "DirectX11Renderer")
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , requestedMultiSampleCount_(args.multiSampleCount)
        , backBufferDepthFormat_(args.depthStencilFormat)
        , contextRecoveryEnabled_(args.contextRecoveryEnabled)
        , deviceEventCallback_(args.deviceEventCallback)
    {
        presentationMode_ = args.presentationMode;
        swapInterval_ = args.swapInterval;
        vsyncEnabled_ = args.swapInterval > 0;

        CNA::Platform::Win32NativeWindow nativeWindow;
        if (!CNA::Platform::TryGetWin32(surface_.GetNativeHandle(), nativeWindow))
            throw std::runtime_error("DirectX11Renderer requires a Win32 native window.");
        hwnd_ = static_cast<HWND>(nativeWindow.hwnd);

        const auto drawableSize = surface_.GetDrawableSize();
        width_ = drawableSize.width;
        height_ = drawableSize.height;
        if (width_ <= 0) width_ = args.virtualWidth > 0 ? args.virtualWidth : 1024;
        if (height_ <= 0) height_ = args.virtualHeight > 0 ? args.virtualHeight : 768;

        CreateDeviceResources();
        CreateSwapChainResources();
        CreateWindowSizeDependentViews();

        CNA::Logger::Info(
            "D3D11 renderer initialised (" + std::to_string(width_) + "x" +
                std::to_string(height_) + "), feature level " + FormatHr(featureLevel_) +
                ", debug layer " + (debugLayerEnabled_ ? "enabled" : "disabled") +
                ", tearing " + (allowTearingSupported_ ? "supported" : "unsupported"),
            CNA::LogCategory::RENDER);
    }

    DirectX11Renderer::~DirectX11Renderer()
    {
        DrainDebugMessagesEXT();
        D3DCommon::D3DDebugLayerLog::UnregisterLiveQueue(this);
        lifetimeToken_.reset();
#if defined(CNA_DIRECTX11_COMPILED_EFFECTS)
        if (mojoShaderContext_ != nullptr)
        {
            MOJOSHADER_d3d11DestroyContext(mojoShaderContext_);
            mojoShaderContext_ = nullptr;
        }
#endif
    }

    void DirectX11Renderer::SetContextRecoveryEnabled(bool enabled)
    {
        contextRecoveryEnabled_ = enabled;
    }

    void DirectX11Renderer::RegisterRecoverableResourceEXT(
        D3DCommon::ID3DDeviceRecoverableEXT* resource)
    {
        if (!contextRecoveryEnabled_ || resource == nullptr)
            return;
        if (std::find(recoverableResources_.begin(), recoverableResources_.end(), resource) ==
            recoverableResources_.end())
        {
            recoverableResources_.push_back(resource);
        }
    }

    void DirectX11Renderer::UnregisterRecoverableResourceEXT(
        D3DCommon::ID3DDeviceRecoverableEXT* resource) noexcept
    {
        std::erase(recoverableResources_, resource);
    }

    void DirectX11Renderer::DebugSimulateContextLoss()
    {
        if (deviceLost_)
            return;
        deviceLost_ = true;
        if (deviceEventCallback_)
            deviceEventCallback_(RendererDeviceEvent::Lost);
    }

    void DirectX11Renderer::DebugRestoreContext()
    {
        if (!deviceLost_)
            return;
        if (deviceEventCallback_)
            deviceEventCallback_(RendererDeviceEvent::Resetting);
        RecreateDeviceEXT();
        deviceLost_ = false;
        if (deviceEventCallback_)
            deviceEventCallback_(RendererDeviceEvent::Reset);
    }

    void DirectX11Renderer::CreateDeviceResources()
    {
        static const D3D_FEATURE_LEVEL kFeatureLevels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0,
        };

        auto tryCreate = [&](UINT flags, const D3D_FEATURE_LEVEL* levels, UINT levelCount) -> HRESULT
        {
            return D3D11CreateDevice(
                nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                levels, levelCount, D3D11_SDK_VERSION,
                device_.ReleaseAndGetAddressOf(), &featureLevel_, context_.ReleaseAndGetAddressOf());
        };

        UINT flags = 0;
#ifndef NDEBUG
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        // plans/plan_graphics_shared_cleanup.md GSC-0006: CNA_D3D11_DEBUG_LAYER=1 enables the layer in any
        // build type and =0 disables it in a Debug build -- the explicit switch validation runs use, as
        // CNA_D3D12_DEBUG_LAYER is for DirectX12. Unset keeps the build type's default.
        if (const char* debugLayer = std::getenv("CNA_D3D11_DEBUG_LAYER"); debugLayer != nullptr)
        {
            if (std::strcmp(debugLayer, "1") == 0)
                flags |= D3D11_CREATE_DEVICE_DEBUG;
            else if (std::strcmp(debugLayer, "0") == 0)
                flags &= ~static_cast<UINT>(D3D11_CREATE_DEVICE_DEBUG);
            else
                CNA::Logger::Warn(std::string("CNA_D3D11_DEBUG_LAYER='") + debugLayer +
                                      "' is neither 0 nor 1; the build type's default is kept.",
                                  CNA::LogCategory::RENDER);
        }

        HRESULT hr = tryCreate(flags, kFeatureLevels, ARRAYSIZE(kFeatureLevels));

        // design decision 12: the debug layer is best-effort, never a hard requirement.
        if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG) && hr == DXGI_ERROR_SDK_COMPONENT_MISSING)
        {
            CNA::Logger::Warn("D3D11 debug layer unavailable; retrying without it.",
                              CNA::LogCategory::RENDER);
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = tryCreate(flags, kFeatureLevels, ARRAYSIZE(kFeatureLevels));
        }

        // design decision 12: some drivers reject an explicit 11_1 request outright.
        if (hr == E_INVALIDARG)
        {
            CNA::Logger::Warn("D3D11 feature level 11_1 rejected; retrying without it.",
                              CNA::LogCategory::RENDER);
            hr = tryCreate(flags, kFeatureLevels + 1, ARRAYSIZE(kFeatureLevels) - 1);
        }

        if (FAILED(hr))
        {
            throw std::runtime_error("D3D11CreateDevice failed, hr=" + FormatHr(hr));
        }

        context_.As(&annotation_);
        debugLayerEnabled_ = (flags & D3D11_CREATE_DEVICE_DEBUG) != 0;
        if (debugLayerEnabled_ && SUCCEEDED(device_.As(&infoQueue_)))
        {
            // Informational messages carry no verdict and arrive by the thousand (state object
            // creation, every shader); they are dropped at the queue, as DirectX12 does.
            D3D11_MESSAGE_SEVERITY deniedSeverities[] = {
                D3D11_MESSAGE_SEVERITY_INFO, D3D11_MESSAGE_SEVERITY_MESSAGE};
            D3D11_INFO_QUEUE_FILTER filter{};
            filter.DenyList.NumSeverities = static_cast<UINT>(std::size(deniedSeverities));
            filter.DenyList.pSeverityList = deniedSeverities;
            infoQueue_->PushStorageFilter(&filter);
            D3DCommon::D3DDebugLayerLog::RegisterLiveQueue(this, [this] { DrainDebugMessagesEXT(); });
        }

        // design decision 12: negotiation is broad, but acceptance is a hard floor -- Phase DIRECTX8's
        // Shader Model 5 stock shaders need feature level 11.0+.
        if (featureLevel_ < D3D_FEATURE_LEVEL_11_0)
        {
            throw std::runtime_error(
                "CNA's D3D11 renderer requires feature level 11_0+; GPU only reports 0x"
                + FormatHr(featureLevel_));
        }

        b5g6r5KeepsBytes_ = FormatKeepsWrittenBytes(device_.Get(), context_.Get(), DXGI_FORMAT_B5G6R5_UNORM);
        b5g5r5a1KeepsBytes_ = FormatKeepsWrittenBytes(device_.Get(), context_.Get(), DXGI_FORMAT_B5G5R5A1_UNORM);
        b4g4r4a4KeepsBytes_ = FormatKeepsWrittenBytes(device_.Get(), context_.Get(), DXGI_FORMAT_B4G4R4A4_UNORM);
        if (!b4g4r4a4KeepsBytes_)
            CNA::Logger::Warn("D3D11 device does not store B4G4R4A4_UNORM texels it reports as "
                              "supported; SurfaceFormat::Bgra4444 is refused on this device.",
                              CNA::LogCategory::RENDER);

        // DX-22: factory chain + tearing-capability query (device lifetime -- done once).
        ComPtr<IDXGIDevice> dxgiDevice;
        hr = device_.As(&dxgiDevice);
        if (FAILED(hr))
            throw std::runtime_error("QueryInterface(IDXGIDevice) failed, hr=" + FormatHr(hr));

        ComPtr<IDXGIAdapter> adapter;
        hr = dxgiDevice->GetParent(IID_PPV_ARGS(adapter.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("IDXGIDevice::GetParent(IDXGIAdapter) failed, hr=" + FormatHr(hr));

        ComPtr<IDXGIAdapter1> adapter1;
        DXGI_ADAPTER_DESC1 adapterDescription{};
        if (FAILED(adapter.As(&adapter1)) || FAILED(adapter1->GetDesc1(&adapterDescription)))
            throw std::runtime_error("D3D11 could not identify its selected DXGI adapter");
        char adapterIdentity[96];
        std::snprintf(adapterIdentity, sizeof(adapterIdentity),
                      "D3D11 selected DXGI adapter PCI %04X:%04X, software=%u",
                      adapterDescription.VendorId, adapterDescription.DeviceId,
                      (adapterDescription.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 ? 1u : 0u);
        CNA::Logger::Info(adapterIdentity, CNA::LogCategory::RENDER);

        hr = adapter->GetParent(IID_PPV_ARGS(factory_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("IDXGIAdapter::GetParent(IDXGIFactory2) failed, hr=" + FormatHr(hr));

        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory_.As(&factory5)))
        {
            BOOL allowTearing = FALSE;
            if (SUCCEEDED(factory5->CheckFeatureSupport(
                    DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))))
            {
                allowTearingSupported_ = allowTearing != FALSE;
            }
        }
    }

    void DirectX11Renderer::CreateSwapChainResources()
    {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = static_cast<UINT>(width_);
        desc.Height = static_cast<UINT>(height_);
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // maps to XNA SurfaceFormat::Color (DX-11-fmt)
        desc.SampleDesc.Count = 1; // flip-model swap chains are never MSAA directly (DX-45's note)
        desc.SampleDesc.Quality = 0;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Flags = (allowTearingSupported_ && allowTearingRequested_) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        HRESULT hr = factory_->CreateSwapChainForHwnd(
            device_.Get(), hwnd_, &desc, nullptr, nullptr, swapChain_.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("CreateSwapChainForHwnd failed, hr=" + FormatHr(hr));
    }

    void DirectX11Renderer::CreateWindowSizeDependentViews()
    {
        HRESULT hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(backBufferTexture_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("IDXGISwapChain1::GetBuffer failed, hr=" + FormatHr(hr));

        hr = device_->CreateRenderTargetView(
            backBufferTexture_.Get(), nullptr, backBufferRTV_.ReleaseAndGetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("CreateRenderTargetView failed, hr=" + FormatHr(hr));

        RecreateDefaultRenderSurfaces(requestedMultiSampleCount_);
    }

    ID3D11RenderTargetView* DirectX11Renderer::GetBackBufferDrawRtv() const
    {
        return backBufferMsaaRTV_ ? backBufferMsaaRTV_.Get() : backBufferRTV_.Get();
    }

    void DirectX11Renderer::RecreateDefaultRenderSurfaces(int requestedMultiSampleCount)
    {
        const int newSampleCount = ClampBackBufferMultiSampleCount(
            device_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, requestedMultiSampleCount);

        ComPtr<ID3D11Texture2D> newMsaaTexture;
        ComPtr<ID3D11RenderTargetView> newMsaaRtv;
        if (newSampleCount > 0)
        {
            D3D11_TEXTURE2D_DESC colorDesc{};
            colorDesc.Width = static_cast<UINT>(width_);
            colorDesc.Height = static_cast<UINT>(height_);
            colorDesc.MipLevels = 1;
            colorDesc.ArraySize = 1;
            colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            colorDesc.SampleDesc.Count = static_cast<UINT>(newSampleCount);
            colorDesc.Usage = D3D11_USAGE_DEFAULT;
            colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET;

            HRESULT hr = device_->CreateTexture2D(
                &colorDesc, nullptr, newMsaaTexture.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error(
                    "DirectX11Renderer: back-buffer MSAA color creation failed, hr=" +
                    FormatHr(hr));
            hr = device_->CreateRenderTargetView(
                newMsaaTexture.Get(), nullptr, newMsaaRtv.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error(
                    "DirectX11Renderer: back-buffer MSAA RTV creation failed, hr=" + FormatHr(hr));
        }

        // The back buffer's depth resource follows PresentationParameters.DepthStencilFormat, the
        // same way this renderer's render targets already did. It used to be D24S8 whatever was
        // asked for, so DepthFormat::None still depth-tested, Depth24 still carried a usable
        // stencil, and GraphicsDevice -- told the applied format was always Depth24Stencil8 --
        // never refused a depth or stencil clear on a surface that should have had neither.
        // DepthFormat::None allocates nothing; DXGI has no 24-bit depth-only format, so Depth24
        // shares D24S8 storage and RebindDepthStencilState() keeps its stencil out of reach.
        const DXGI_FORMAT depthDxgiFormat = D3DCommon::DepthFormatToDxgi(backBufferDepthFormat_);
        ComPtr<ID3D11Texture2D> newDepthTexture;
        ComPtr<ID3D11DepthStencilView> newDepthView;
        if (depthDxgiFormat != DXGI_FORMAT_UNKNOWN)
        {
            D3D11_TEXTURE2D_DESC depthDesc{};
            depthDesc.Width = static_cast<UINT>(width_);
            depthDesc.Height = static_cast<UINT>(height_);
            depthDesc.MipLevels = 1;
            depthDesc.ArraySize = 1;
            depthDesc.Format = depthDxgiFormat;
            depthDesc.SampleDesc.Count = newSampleCount > 0
                ? static_cast<UINT>(newSampleCount)
                : 1u;
            depthDesc.Usage = D3D11_USAGE_DEFAULT;
            depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

            HRESULT hr =
                device_->CreateTexture2D(&depthDesc, nullptr, newDepthTexture.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("CreateTexture2D(depth) failed, hr=" + FormatHr(hr));

            hr = device_->CreateDepthStencilView(
                newDepthTexture.Get(), nullptr, newDepthView.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("CreateDepthStencilView failed, hr=" + FormatHr(hr));
        }

        context_->OMSetRenderTargets(0, nullptr, nullptr);
        backBufferMsaaTexture_ = std::move(newMsaaTexture);
        backBufferMsaaRTV_ = std::move(newMsaaRtv);
        depthStencilTexture_ = std::move(newDepthTexture);
        depthStencilView_ = std::move(newDepthView);
        appliedMultiSampleCount_ = newSampleCount;
        appliedBackBufferDepthFormat_ = backBufferDepthFormat_;

        ID3D11RenderTargetView* rtv = GetBackBufferDrawRtv();
        context_->OMSetRenderTargets(1, &rtv, depthStencilView_.Get());

        // ResizeBuffers recreates the back buffer after GraphicsDevice's window-size
        // notification. Restore the presentation rectangle here as well: the logical
        // viewport can remain 320x240 while a resized window needs new letterbox bars.
        const auto geometry = D3DCommon::ComputeD3DPresentationGeometry(
            width_, height_, virtualWidth_, virtualHeight_, presentationMode_);
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = static_cast<float>(std::lround(geometry.x));
        vp.TopLeftY = static_cast<float>(std::lround(geometry.y));
        vp.Width = static_cast<float>(std::lround(geometry.width));
        vp.Height = static_cast<float>(std::lround(geometry.height));
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        context_->RSSetViewports(1, &vp);

        // Phase DIRECTX6: Clear()/ClearX target whatever's tracked here -- initialise/reset it to the
        // back buffer every time this is (re)created (construction, and DX-29 resize).
        currentColorRTVs_[0] = rtv;
        currentRTVCount_ = 1;
        currentDSV_ = depthStencilView_.Get();
        RebindRasterizerState();
        RebindDepthStencilStateIfApplied();
    }

    void DirectX11Renderer::ResolveBackBufferMsaa()
    {
        if (!backBufferMsaaTexture_ || !backBufferTexture_) return;
        context_->ResolveSubresource(
            backBufferTexture_.Get(), 0, backBufferMsaaTexture_.Get(), 0,
            DXGI_FORMAT_R8G8B8A8_UNORM);
    }

    void DirectX11Renderer::ReleaseWindowSizeDependentViews()
    {
        ID3D11RenderTargetView* nullRtv[] = { nullptr };
        context_->OMSetRenderTargets(1, nullRtv, nullptr);
        depthStencilView_.Reset();
        depthStencilTexture_.Reset();
        backBufferMsaaRTV_.Reset();
        backBufferMsaaTexture_.Reset();
        backBufferRTV_.Reset();
        backBufferTexture_.Reset();
        appliedMultiSampleCount_ = 0;
    }

    void DirectX11Renderer::EnsureSwapChainSize()
    {
        if (!swapChain_) return;

        const auto drawableSize = surface_.GetDrawableSize();
        const int w = drawableSize.width;
        const int h = drawableSize.height;
        if (w <= 0 || h <= 0) return;
        if (w == width_ && h == height_) return;

        // DX-29: only the window-size group is torn down here -- device_/context_/factory_
        // (DX-20/21/22) and the swap chain object itself (DX-23) are untouched.
        ReleaseWindowSizeDependentViews();
        context_->Flush();

        const UINT flags =
            (allowTearingSupported_ && allowTearingRequested_) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        HRESULT hr = swapChain_->ResizeBuffers(
            0, static_cast<UINT>(w), static_cast<UINT>(h), DXGI_FORMAT_UNKNOWN, flags);
        if (FAILED(hr))
        {
            CheckDeviceRemoved(hr);
            throw std::runtime_error("IDXGISwapChain1::ResizeBuffers failed, hr=" + FormatHr(hr));
        }

        width_ = w;
        height_ = h;
        CreateWindowSizeDependentViews();
    }

    void DirectX11Renderer::DrainDebugMessagesEXT()
    {
        if (!infoQueue_)
            return;
        const UINT64 count = infoQueue_->GetNumStoredMessagesAllowedByRetrievalFilter();
        for (UINT64 i = 0; i < count; ++i)
        {
            SIZE_T length = 0;
            if (FAILED(infoQueue_->GetMessage(i, nullptr, &length)) || length == 0)
                continue;
            std::vector<char> storage(length);
            auto* message = reinterpret_cast<D3D11_MESSAGE*>(storage.data());
            if (FAILED(infoQueue_->GetMessage(i, message, &length)))
                continue;
            // Stored before the filter was pushed (device creation), like DirectX12's startup notices.
            if (message->Severity == D3D11_MESSAGE_SEVERITY_INFO ||
                message->Severity == D3D11_MESSAGE_SEVERITY_MESSAGE)
                continue;
            D3DCommon::D3DDebugLayerLog::Record(
                {D3DCommon::D3DDebugLayerApi::Direct3D11, static_cast<int>(message->Severity),
                 static_cast<int>(message->ID),
                 message->pDescription != nullptr ? std::string(message->pDescription) : std::string()});
        }
        infoQueue_->ClearStoredMessages();
    }

    void DirectX11Renderer::CheckDeviceRemoved(HRESULT hr)
    {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            const HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : hr;
            CNA::Logger::Error("D3D11 device removed/reset; reason=" + FormatHr(reason),
                               CNA::LogCategory::RENDER);
            DrainDebugMessagesEXT();
            if (!deviceLost_)
            {
                deviceLost_ = true;
                if (deviceEventCallback_)
                    deviceEventCallback_(RendererDeviceEvent::Lost);
            }
        }
    }

    void DirectX11Renderer::RecreateDeviceEXT()
    {
        const auto resources = recoverableResources_;
        if (context_)
        {
            context_->ClearState();
            context_->Flush();
        }
        for (D3DCommon::ID3DDeviceRecoverableEXT* resource : resources)
        {
            if (resource != nullptr)
                resource->ReleaseDeviceResourcesEXT();
        }
#if defined(CNA_DIRECTX11_COMPILED_EFFECTS)
        if (mojoShaderContext_ != nullptr)
        {
            MOJOSHADER_d3d11DestroyContext(mojoShaderContext_);
            mojoShaderContext_ = nullptr;
        }
#endif
        ReleaseWindowSizeDependentViews();
        swapChain_.Reset();

        inputLayoutCache_ = D3D11InputLayoutCache();
        samplerCache_ = D3D11SamplerCache();
        blendStateCache_ = D3D11BlendStateCache();
        depthStencilStateCache_ = D3D11DepthStencilStateCache();
        rasterizerStateCache_ = D3D11RasterizerStateCache();
        currentBlendState_.Reset();
        currentDepthStencilState_.Reset();
        perDrawConstantBuffer_.Reset();
        logicalInstanceIdBuffer_.Reset();
        logicalInstanceIdCapacity_ = 0;
        drawStorageBuffers_.fill(nullptr);
        fogConstantBuffer_.Reset();
        lightingConstantBuffer_.Reset();
        alphaTestConstantBuffer_.Reset();
        dualTexFogConstantBuffer_.Reset();
        envMapPerDrawConstantBuffer_.Reset();
        envMapConstantBuffer_.Reset();
        boneConstantBuffer_.Reset();
        skinnedExtraConstantBuffer_.Reset();
        pbrPerDrawConstantBuffer_.Reset();
        pbrLightsConstantBuffer_.Reset();
        shadowConstantBuffer_.Reset();
        iblConstantBuffer_.Reset();
        defaultWhiteSrv_.Reset();
        defaultWhiteTexture_.Reset();
        defaultFlatNormalSrv_.Reset();
        defaultFlatNormalTexture_.Reset();
        defaultOpaqueBlackSrv_.Reset();
        defaultOpaqueBlackTexture_.Reset();
        defaultOpaqueBlackCubeSrv_.Reset();
        defaultOpaqueBlackCubeTexture_.Reset();
        currentCustomRT_ = nullptr;
        currentCubeRT_ = nullptr;
        currentMRTCount_ = 0;
        for (auto& target : currentMRTTargets_)
            target = nullptr;
        for (auto& target : currentMRTCubes_)
            target = nullptr;
        currentRTVCount_ = 0;
        for (auto& rtv : currentColorRTVs_)
            rtv = nullptr;
        currentDSV_ = nullptr;

        DrainDebugMessagesEXT();
        D3DCommon::D3DDebugLayerLog::UnregisterLiveQueue(this);
        infoQueue_.Reset();
        annotation_.Reset();
        context_.Reset();
        device_.Reset();
        factory_.Reset();
        stockVertexShaders_.clear();
        stockPixelShaders_.clear();
        debugLayerEnabled_ = false;
        allowTearingSupported_ = false;

        CreateDeviceResources();
        CreateSwapChainResources();
        CreateWindowSizeDependentViews();

        std::vector<std::string> failures;
        for (D3DCommon::ID3DDeviceRecoverableEXT* resource : resources)
        {
            if (resource == nullptr)
                continue;
            try
            {
                resource->RecreateDeviceResourcesEXT();
            }
            catch (const std::exception& error)
            {
                failures.emplace_back(error.what());
            }
        }
        if (!failures.empty())
        {
            throw std::runtime_error(
                "D3D11 device recovery failed to recreate " +
                std::to_string(failures.size()) + " resource(s); first failure: " +
                failures.front());
        }
    }

    void DirectX11Renderer::Clear(float r, float g, float b, float a)
    {
        const float color[4] = { r, g, b, a };
        // Phase DIRECTX6: clears whatever's currently bound (custom render target(s) or MRT set), not
        // always the back buffer -- matches every other renderer's "clear the active target(s)"
        // semantics once SetRenderTarget2D/SetRenderTargets has bound something.
        for (int i = 0; i < currentRTVCount_; ++i)
        {
            if (currentColorRTVs_[i]) context_->ClearRenderTargetView(currentColorRTVs_[i], color);
        }
    }

    void DirectX11Renderer::Present()
    {
        EnsureSwapChainSize();
        ResolveBackBufferMsaa();

        const bool mayTear =
            allowTearingSupported_ && allowTearingRequested_ && !vsyncEnabled_ && !exclusiveFullscreen_;
        const UINT syncInterval = static_cast<UINT>(std::max(0, swapInterval_));
        const UINT flags = mayTear ? DXGI_PRESENT_ALLOW_TEARING : 0;

        const HRESULT hr = swapChain_->Present(syncInterval, flags);
        DrainDebugMessagesEXT();
        if (FAILED(hr))
        {
            CheckDeviceRemoved(hr);
            return;
        }

        // The swap chain is DXGI_SWAP_EFFECT_FLIP_DISCARD (DX-45), and a flip-model Present unbinds
        // the back buffer from the output merger. Clear() never noticed, because it names its
        // render target view explicitly; every draw renders into whatever OMSetRenderTargets last
        // bound, which after a Present is nothing. So from the second frame on, a game showed a
        // correct clear colour and none of its geometry -- sprites and primitives alike -- and no
        // test caught it, because every readback test reads the back buffer before its first
        // Present. Rebind exactly the tracked set rather than restoring the back buffer: that
        // leaves the viewport, and any render target the game bound itself, as they were.
        if (currentRTVCount_ > 0)
        {
            context_->OMSetRenderTargets(static_cast<UINT>(currentRTVCount_), currentColorRTVs_,
                                         currentDSV_);
        }
    }

    void DirectX11Renderer::GetViewportSize(int& width, int& height)
    {
        const auto drawableSize = surface_.GetDrawableSize();
        const int physicalWidth = drawableSize.width > 0 ? drawableSize.width : width_;
        const int physicalHeight = drawableSize.height > 0 ? drawableSize.height : height_;
        const auto geometry = D3DCommon::ComputeD3DPresentationGeometry(
            physicalWidth, physicalHeight, virtualWidth_, virtualHeight_, presentationMode_);
        width = static_cast<int>(std::lround(geometry.logicalWidth));
        height = static_cast<int>(std::lround(geometry.logicalHeight));
    }

    void DirectX11Renderer::GetDefaultViewportRect(int& x, int& y, int& width, int& height)
    {
        const auto drawableSize = surface_.GetDrawableSize();
        const int physicalWidth = drawableSize.width > 0 ? drawableSize.width : width_;
        const int physicalHeight = drawableSize.height > 0 ? drawableSize.height : height_;
        const auto geometry = D3DCommon::ComputeD3DPresentationGeometry(
            physicalWidth, physicalHeight, virtualWidth_, virtualHeight_, presentationMode_);
        x = static_cast<int>(std::lround(geometry.x));
        y = static_cast<int>(std::lround(geometry.y));
        width = static_cast<int>(std::lround(geometry.width));
        height = static_cast<int>(std::lround(geometry.height));
    }

    void DirectX11Renderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        surface_.Update(surface);
    }

    void DirectX11Renderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    int DirectX11Renderer::ApplyMultiSampleCount(int requestedMultiSampleCount)
    {
        requestedMultiSampleCount_ = requestedMultiSampleCount;
        EnsureSwapChainSize();
        const int clamped = ClampBackBufferMultiSampleCount(
            device_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, requestedMultiSampleCount_);
        // appliedBackBufferDepthFormat_ stands in for the old `!depthStencilTexture_` test, which
        // meant "no default surfaces yet" and is no longer that once DepthFormat::None legitimately
        // leaves the depth texture null.
        if (clamped != appliedMultiSampleCount_ || appliedBackBufferDepthFormat_ != backBufferDepthFormat_)
            RecreateDefaultRenderSurfaces(requestedMultiSampleCount_);
        return appliedMultiSampleCount_;
    }

    int DirectX11Renderer::GetAppliedBackBufferFormatEXT(int requestedFormat) const
    {
        (void) requestedFormat;
        return static_cast<int>(Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color);
    }

    int DirectX11Renderer::GetAppliedDepthStencilFormatEXT(int requestedFormat) const
    {
        // The back buffer now allocates what was requested (RecreateDefaultRenderSurfaces), so the
        // applied format is the request -- except an ordinal DXGI has no depth format for, which
        // allocates nothing and is reported as exactly that.
        return D3DCommon::DepthFormatToDxgi(requestedFormat) == DXGI_FORMAT_UNKNOWN
            ? static_cast<int>(Microsoft::Xna::Framework::Graphics::DepthFormat::None)
            : requestedFormat;
    }

    void DirectX11Renderer::UpdatePresentationFormatEXT(
        int backBufferFormat, int depthStencilFormat, bool isFullScreen)
    {
        // The colour format is fixed (GetAppliedBackBufferFormatEXT) and full-screen state has its
        // own path; only the depth format changes a resource here. Recreated at once when the
        // surfaces already exist, so the depth buffer a Reset asked for is the one the next draw
        // gets even if the multisample count did not change with it.
        (void) backBufferFormat;
        (void) isFullScreen;
        if (depthStencilFormat == backBufferDepthFormat_)
            return;
        backBufferDepthFormat_ = depthStencilFormat;
        if (swapChain_ && appliedBackBufferDepthFormat_ >= 0)
            RecreateDefaultRenderSurfaces(requestedMultiSampleCount_);
    }

    void DirectX11Renderer::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void DirectX11Renderer::SetSwapInterval(int interval)
    {
        // DX-26: sync interval is renderer state applied at the next Present(), not a direct
        // D3D11 API call -- there is no "set swap interval" entry point to call ahead of time.
        swapInterval_ = interval;
        vsyncEnabled_ = interval > 0;
    }

    bool DirectX11Renderer::TransformWindowToLogical(
        float windowX, float windowY, float& logX, float& logY) const
    {
        const auto drawableSize = surface_.GetDrawableSize();
        const int physicalWidth = drawableSize.width > 0 ? drawableSize.width : width_;
        const int physicalHeight = drawableSize.height > 0 ? drawableSize.height : height_;
        const auto geometry = D3DCommon::ComputeD3DPresentationGeometry(
            physicalWidth, physicalHeight, virtualWidth_, virtualHeight_, presentationMode_);
        return D3DCommon::MapDrawableToLogical(
            geometry, surface_.WindowToDrawable(windowX), surface_.WindowToDrawable(windowY),
            logX, logY);
    }

    bool DirectX11Renderer::TransformLogicalToWindow(
        float logX, float logY, float& windowX, float& windowY) const
    {
        const auto drawableSize = surface_.GetDrawableSize();
        const int physicalWidth = drawableSize.width > 0 ? drawableSize.width : width_;
        const int physicalHeight = drawableSize.height > 0 ? drawableSize.height : height_;
        const auto geometry = D3DCommon::ComputeD3DPresentationGeometry(
            physicalWidth, physicalHeight, virtualWidth_, virtualHeight_, presentationMode_);
        float drawableX = 0.0f;
        float drawableY = 0.0f;
        if (!D3DCommon::MapLogicalToDrawable(
                geometry, logX, logY, drawableX, drawableY))
            return false;
        windowX = surface_.DrawableToWindow(drawableX);
        windowY = surface_.DrawableToWindow(drawableY);
        return true;
    }

    void DirectX11Renderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        // plans/plan_dx.md DX-205: this used to be one `return` covering both conditions, which meant a
        // device with no back buffer left GraphicsDevice's zero-initialised scratch buffer to be
        // converted and handed to the caller -- a complete, uniformly transparent-black frame,
        // reported as a successful read. That is the fabricate-rather-than-refuse anti-pattern
        // REMED-GFX-127 removed from ITextureRenderer::GetData, and it is exactly the failure mode
        // a readback oracle must never have. An empty rectangle stays a plain no-op (there is
        // nothing to write and nothing was promised); a missing back buffer is now named.
        if (w <= 0 || h <= 0)
            return;
        if (!backBufferTexture_)
        {
            throw System::NotSupportedException(
                "DirectX11Renderer::ReadBackbuffer: this device has no back buffer to read. D3D11 "
                "creates its swap chain in the constructor, so reaching this means device creation "
                "did not complete; PresentationParameters::HeadlessEXT is not supported by this "
                "renderer (see its own documentation).");
        }

        ResolveBackBufferMsaa();

        D3D11_TEXTURE2D_DESC desc{};
        backBufferTexture_->GetDesc(&desc);

        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> staging;
        HRESULT hr = device_->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("ReadBackbuffer: staging texture creation failed, hr=" + FormatHr(hr));

        context_->CopyResource(staging.Get(), backBufferTexture_.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        hr = context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr))
            throw std::runtime_error("ReadBackbuffer: Map failed, hr=" + FormatHr(hr));

        // DX-28: must honor RowPitch per row -- the mapped rows are not guaranteed tightly packed.
        const int srcW = static_cast<int>(desc.Width);
        const int srcH = static_cast<int>(desc.Height);

        // The request is in the game's logical back buffer space, but this renderer draws straight
        // into the physical swap chain: GraphicsDevice maps every viewport and scissor through
        // GetDefaultViewportRect(), so when the logical buffer is letterboxed or scaled inside the
        // window, logical (x, y) is not physical (x, y). Reading physical (x, y) returned the
        // letterbox bars -- a 16x16 back buffer, which Windows can only show in a window at least
        // ~120 px wide, read back as black wherever the game had drawn. Sample the same geometry
        // the draws used, at logical pixel centres. When logical and physical agree the mapping is
        // the identity and the direct copy below runs exactly as before.
        int logicalW = 0;
        int logicalH = 0;
        GetViewportSize(logicalW, logicalH);
        int presentX = 0;
        int presentY = 0;
        int presentW = 0;
        int presentH = 0;
        GetDefaultViewportRect(presentX, presentY, presentW, presentH);
        const bool presentationIsIdentity =
            logicalW <= 0 || logicalH <= 0 || presentW <= 0 || presentH <= 0 ||
            (presentX == 0 && presentY == 0 && presentW == logicalW && presentH == logicalH);
        if (!presentationIsIdentity)
        {
            const double scaleX = static_cast<double>(presentW) / static_cast<double>(logicalW);
            const double scaleY = static_cast<double>(presentH) / static_cast<double>(logicalH);
            for (int row = 0; row < h; ++row)
            {
                uint8_t* dst =
                    pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4;
                const int srcY = presentY + static_cast<int>(
                    std::floor((static_cast<double>(y + row) + 0.5) * scaleY));
                for (int column = 0; column < w; ++column)
                {
                    const int srcX = presentX + static_cast<int>(
                        std::floor((static_cast<double>(x + column) + 0.5) * scaleX));
                    uint8_t* texel = dst + static_cast<std::size_t>(column) * 4;
                    if (srcX < 0 || srcX >= srcW || srcY < 0 || srcY >= srcH)
                    {
                        std::memset(texel, 0, 4);
                        continue;
                    }
                    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData)
                                        + static_cast<std::size_t>(srcY) * mapped.RowPitch
                                        + static_cast<std::size_t>(srcX) * 4;
                    std::memcpy(texel, src, 4);
                }
            }
            context_->Unmap(staging.Get(), 0);
            return;
        }

        for (int row = 0; row < h; ++row)
        {
            uint8_t* dst = pixels + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4;
            const int srcY = y + row;
            if (srcY < 0 || srcY >= srcH || x >= srcW)
            {
                std::memset(dst, 0, static_cast<std::size_t>(w) * 4);
                continue;
            }
            const int copyW = std::max(0, std::min(w, srcW - std::max(x, 0)));
            const int srcX = std::max(x, 0);
            const uint8_t* src = static_cast<const uint8_t*>(mapped.pData)
                                + static_cast<std::size_t>(srcY) * mapped.RowPitch
                                + static_cast<std::size_t>(srcX) * 4;
            std::memcpy(dst, src, static_cast<std::size_t>(copyW) * 4);
            if (copyW < w)
                std::memset(dst + static_cast<std::size_t>(copyW) * 4, 0,
                            static_cast<std::size_t>(w - copyW) * 4);
        }

        context_->Unmap(staging.Get(), 0);
    }

    void DirectX11Renderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        Clear(r, g, b, a);
        if (currentDSV_) context_->ClearDepthStencilView(currentDSV_, D3D11_CLEAR_DEPTH, depth, 0);
    }

    void DirectX11Renderer::ClearDepth(float depth)
    {
        if (currentDSV_) context_->ClearDepthStencilView(currentDSV_, D3D11_CLEAR_DEPTH, depth, 0);
    }

    void DirectX11Renderer::ClearStencil(int stencil)
    {
        if (currentDSV_)
            context_->ClearDepthStencilView(
                currentDSV_, D3D11_CLEAR_STENCIL, 1.0f, static_cast<UINT8>(stencil));
    }

    void DirectX11Renderer::ClearDepthAndStencil(float depth, int stencil)
    {
        if (currentDSV_)
            context_->ClearDepthStencilView(
                currentDSV_, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, static_cast<UINT8>(stencil));
    }

    void DirectX11Renderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        Clear(r, g, b, a);
        if (currentDSV_)
            context_->ClearDepthStencilView(
                currentDSV_, D3D11_CLEAR_STENCIL, 1.0f, static_cast<UINT8>(stencil));
    }

    void DirectX11Renderer::ClearColorDepthAndStencil(
        float r, float g, float b, float a, float depth, int stencil)
    {
        Clear(r, g, b, a);
        if (currentDSV_)
            context_->ClearDepthStencilView(
                currentDSV_, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, depth, static_cast<UINT8>(stencil));
    }

    // State setters: stored/applied for real once Phase DIRECTX7 (state objects) lands. Storing them
    // as no-ops now (rather than throwing) matches this project's own precedent (SOFTWARE-3/
    // HEADLESS-3) of a skeleton that's honest about what's not real yet without crashing normal
    // GraphicsDevice construction, which applies default state on every renderer.
    // These carry a single bool each, so they rebuild the tracked depth-stencil state with just that
    // one field changed (see the ds*_ fields' own comment). They were silent no-ops before: a game
    // calling GraphicsDevice::SetDepthTestEnabled(true) on D3D11 got no depth test at all, while
    // EasyGL honoured it -- a real, silent cross-renderer behavior divergence, found while wiring
    // D3D12's own equivalents (which were worse still: they threw).
    void DirectX11Renderer::SetDepthTestEnabled(bool enabled)
    {
        if (dsDepthEnable_ == enabled) return;
        dsDepthEnable_ = enabled;
        RebindDepthStencilState();
    }

    void DirectX11Renderer::SetDepthWriteEnabled(bool enabled)
    {
        if (dsDepthWriteEnable_ == enabled) return;
        dsDepthWriteEnable_ = enabled;
        RebindDepthStencilState();
    }

    // Deliberate no-op, matching D3D12's own equivalent: a bare "enable blending" has no defined
    // blend factors in XNA -- real blend configuration always arrives via ApplyBlendState().
    void DirectX11Renderer::SetBlendEnabled(bool enabled) { (void)enabled; }

    RendererFormatVerdict DirectX11Renderer::ClassifySurfaceFormatEXT(int surfaceFormat) const
    {
        if (D3DCommon::IsXnaBlockCompressedSurfaceFormat(surfaceFormat))
            return RendererFormatVerdict::Supported;
        if (D3DCommon::IsXnaUncompressedSurfaceFormat(surfaceFormat))
        {
            // Asked of the device, not assumed. Every mapping here is faithful, but not every DXGI
            // format is required of every adapter -- B4G4R4A4_UNORM is optional -- and answering
            // Supported regardless told GraphicsDevice a texture could be made that CreateTexture2D
            // would then refuse. The render-target classifier below has always asked; this one
            // now asks the one question this verdict answers -- can the texture be created. Not
            // SHADER_SAMPLE: linear filtering of 32-bit float formats is optional where creating
            // and point-sampling them is not, and XNA requires point filtering for them anyway.
            if (!device_)
                return RendererFormatVerdict::Supported;
            UINT support = 0;
            constexpr UINT required = D3D11_FORMAT_SUPPORT_TEXTURE2D;
            const DXGI_FORMAT format = D3DCommon::SurfaceFormatToDxgi(surfaceFormat);
            if (FAILED(device_->CheckFormatSupport(format, &support)) ||
                (support & required) != required || !DeviceKeepsFormatBytesEXT(format))
                return RendererFormatVerdict::Unsupported;
            return RendererFormatVerdict::Supported;
        }
        if (D3DCommon::SurfaceFormatToDxgi(surfaceFormat) != DXGI_FORMAT_UNKNOWN)
            return RendererFormatVerdict::Unsupported;
        return RendererFormatVerdict::Defer;
    }

    bool DirectX11Renderer::DeviceKeepsFormatBytesEXT(DXGI_FORMAT format) const
    {
        switch (format)
        {
            case DXGI_FORMAT_B5G6R5_UNORM:   return b5g6r5KeepsBytes_;
            case DXGI_FORMAT_B5G5R5A1_UNORM: return b5g5r5a1KeepsBytes_;
            case DXGI_FORMAT_B4G4R4A4_UNORM: return b4g4r4a4KeepsBytes_;
            default:                         return true;
        }
    }

    RendererFormatVerdict DirectX11Renderer::ClassifyTexture3DFormatEXT(int surfaceFormat) const
    {
        // WINCLOSE-0013: the default defers to the framework's Color-only volume rule, so every
        // other volume format was refused ("SurfaceFormat 12 is not implemented") although
        // D3D11Texture3DRenderer has stored them in their declared DXGI format since DX-225. The
        // set is Software's (SoftwareRenderer2DState.cpp), the renderer the volume format tests
        // were written against; each entry is still asked of the device, since not every DXGI
        // format is required as a 3D texture.
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            case SurfaceFormat::Color:
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::Rgba1010102:
            case SurfaceFormat::Rg32:
            case SurfaceFormat::Rgba64:
            case SurfaceFormat::Alpha8:
            case SurfaceFormat::Single:
            case SurfaceFormat::Vector2:
            case SurfaceFormat::Vector4:
            case SurfaceFormat::HalfSingle:
            case SurfaceFormat::HalfVector2:
            case SurfaceFormat::HalfVector4:
            case SurfaceFormat::HdrBlendable:
            {
                if (!device_)
                    return RendererFormatVerdict::Supported;
                UINT support = 0;
                const DXGI_FORMAT format = D3DCommon::SurfaceFormatToDxgi(surfaceFormat);
                if (FAILED(device_->CheckFormatSupport(format, &support)) ||
                    (support & D3D11_FORMAT_SUPPORT_TEXTURE3D) == 0 ||
                    !DeviceKeepsFormatBytesEXT(format))
                    return RendererFormatVerdict::Unsupported;
                return RendererFormatVerdict::Supported;
            }
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
            case SurfaceFormat::NormalizedByte2:
            case SurfaceFormat::NormalizedByte4:
                return RendererFormatVerdict::Unsupported;
            default:
                return RendererFormatVerdict::Defer;
        }
    }

    RendererFormatVerdict DirectX11Renderer::ClassifyRenderTargetFormatEXT(int surfaceFormat) const
    {
        const DXGI_FORMAT format = D3DCommon::SurfaceFormatToDxgi(surfaceFormat);
        if (!D3DCommon::IsXnaRenderTargetSurfaceFormat(surfaceFormat))
            return format == DXGI_FORMAT_UNKNOWN
                ? RendererFormatVerdict::Defer
                : RendererFormatVerdict::Unsupported;
        if (!device_) return RendererFormatVerdict::Unsupported;

        UINT support = 0;
        constexpr UINT required = D3D11_FORMAT_SUPPORT_TEXTURE2D |
                                  D3D11_FORMAT_SUPPORT_RENDER_TARGET |
                                  D3D11_FORMAT_SUPPORT_SHADER_SAMPLE;
        if (FAILED(device_->CheckFormatSupport(format, &support)) || (support & required) != required)
            return RendererFormatVerdict::Unsupported;
        return RendererFormatVerdict::Supported;
    }

    RendererFormatVerdict DirectX11Renderer::ClassifyColorTransferFormatEXT(int surfaceFormat) const
    {
        if (surfaceFormat == 0) return RendererFormatVerdict::Supported;
        if (D3DCommon::SurfaceFormatToDxgi(surfaceFormat) != DXGI_FORMAT_UNKNOWN)
            return RendererFormatVerdict::Unsupported;
        return RendererFormatVerdict::Defer;
    }

    bool DirectX11Renderer::IsCompressedTransferFormatEXT(int surfaceFormat) const
    {
        return D3DCommon::IsXnaBlockCompressedSurfaceFormat(surfaceFormat);
    }

    bool DirectX11Renderer::IsCompressedCubeTransferFormatEXT(int surfaceFormat) const
    {
        return D3DCommon::IsXnaBlockCompressedSurfaceFormat(surfaceFormat);
    }

    bool DirectX11Renderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

        switch (capability)
        {
        case CNA::GraphicsCapability::ThreeD:
        case CNA::GraphicsCapability::DepthStencilBuffer:
        case CNA::GraphicsCapability::MultipleRenderTargets:
        case CNA::GraphicsCapability::AnisotropicFiltering:
        case CNA::GraphicsCapability::WireFrame:
        case CNA::GraphicsCapability::OcclusionQuery:
        case CNA::GraphicsCapability::CustomEffects:
        case CNA::GraphicsCapability::Texture3D:
        case CNA::GraphicsCapability::MultiStreamVertexInput:
        case CNA::GraphicsCapability::Instancing:
        case CNA::GraphicsCapability::StencilBuffer:
        case CNA::GraphicsCapability::AdditiveBlending:
            return true;
        case CNA::GraphicsCapability::MultiSampleAntiAliasing:
            return ClampBackBufferMultiSampleCount(
                device_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, 4) > 1;
        case CNA::GraphicsCapability::CompiledEffects:
            return SupportsCompiledEffects();
        case CNA::GraphicsCapability::FloatRenderTargets:
            return ClassifyRenderTargetFormatEXT(static_cast<int>(SurfaceFormat::Vector4)) ==
                   RendererFormatVerdict::Supported;
        case CNA::GraphicsCapability::HalfFloatRenderTargets:
            return ClassifyRenderTargetFormatEXT(static_cast<int>(SurfaceFormat::HdrBlendable)) ==
                   RendererFormatVerdict::Supported;
        case CNA::GraphicsCapability::HalfFloatTextureLinearFiltering:
            return SupportsHalfFloatTextureLinearFilteringEXT();
        case CNA::GraphicsCapability::ComputeShaders:
            return SupportsComputeShadersEXT();
        case CNA::GraphicsCapability::IndirectDraw:
            return SupportsIndirectDrawEXT();
        }
        return false;
    }

    ID3D11VertexShader* DirectX11Renderer::GetStockVertexShaderEXT(D3DCommon::D3DShaderVariant variant)
    {
        const int key = static_cast<int>(variant);
        const auto found = stockVertexShaders_.find(key);
        if (found != stockVertexShaders_.end())
            return found->second.Get();
        ComPtr<ID3D11VertexShader> shader = D3DCommon::CreateVertexShaderForVariant(device_.Get(), variant);
        if (!shader)
            return nullptr;
        return stockVertexShaders_.emplace(key, std::move(shader)).first->second.Get();
    }

    ID3D11PixelShader* DirectX11Renderer::GetStockPixelShaderEXT(D3DCommon::D3DShaderVariant variant)
    {
        const int key = static_cast<int>(variant);
        const auto found = stockPixelShaders_.find(key);
        if (found != stockPixelShaders_.end())
            return found->second.Get();
        ComPtr<ID3D11PixelShader> shader = D3DCommon::CreatePixelShaderForVariant(device_.Get(), variant);
        if (!shader)
            return nullptr;
        return stockPixelShaders_.emplace(key, std::move(shader)).first->second.Get();
    }

    bool DirectX11Renderer::SupportsHalfFloatTextureLinearFilteringEXT() const
    {
        if (!device_)
            return false;
        UINT support = 0;
        return SUCCEEDED(device_->CheckFormatSupport(DXGI_FORMAT_R16G16B16A16_FLOAT, &support)) &&
               (support & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0;
    }

    bool DirectX11Renderer::SupportsTexture3DSamplingEXT() const
    {
        if (!device_) return false;
        UINT support = 0;
        return SUCCEEDED(device_->CheckFormatSupport(DXGI_FORMAT_R8G8B8A8_UNORM, &support)) &&
            (support & (D3D11_FORMAT_SUPPORT_TEXTURE3D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE)) ==
                (D3D11_FORMAT_SUPPORT_TEXTURE3D | D3D11_FORMAT_SUPPORT_SHADER_SAMPLE);
    }

    bool DirectX11Renderer::SupportsShaderLanguageEXT(int language, int stage) const
    {
        return device_ != nullptr &&
               language == static_cast<int>(CNA::ShaderLanguageEXT::Hlsl) &&
               (stage == static_cast<int>(CNA::ShaderStageEXT::Vertex) ||
                stage == static_cast<int>(CNA::ShaderStageEXT::Fragment) ||
                (stage == static_cast<int>(CNA::ShaderStageEXT::Compute) &&
                 SupportsComputeShadersEXT()));
    }

    bool DirectX11Renderer::SupportsGpuTimerEXT() const
    {
        return device_ != nullptr && featureLevel_ >= D3D_FEATURE_LEVEL_10_0;
    }

    std::unique_ptr<IGpuTimerRenderer> DirectX11Renderer::CreateGpuTimerEXT()
    {
        if (!SupportsGpuTimerEXT()) return nullptr;
        return std::make_unique<D3D11GpuTimerRenderer>(device_.Get(), context_.Get());
    }

    void DirectX11Renderer::SetStringMarkerEXT(const char* marker)
    {
        if (marker == nullptr || marker[0] == '\0' || !annotation_)
            return;
        const int length = MultiByteToWideChar(CP_UTF8, 0, marker, -1, nullptr, 0);
        if (length <= 1) return;
        std::wstring wide(static_cast<std::size_t>(length), L'\0');
        if (MultiByteToWideChar(CP_UTF8, 0, marker, -1, wide.data(), length) != length)
            return;
        annotation_->SetMarker(wide.c_str());
    }

    bool DirectX11Renderer::SupportsIndirectDrawEXT() const
    {
        return device_ != nullptr && featureLevel_ >= D3D_FEATURE_LEVEL_11_0;
    }

    bool DirectX11Renderer::SupportsBaseInstanceDrawingEXT() const
    {
        return device_ != nullptr && context_ != nullptr &&
               featureLevel_ >= D3D_FEATURE_LEVEL_11_0;
    }

    int DirectX11Renderer::GetMaxVertexShaderStorageBlocksEXT() const
    {
        return device_ != nullptr && featureLevel_ >= D3D_FEATURE_LEVEL_11_0
            ? static_cast<int>(drawStorageBuffers_.size()) : 0;
    }

    void DirectX11Renderer::BindStorageBufferForDrawEXT(
        int binding, const IStorageBufferRenderer& buffer)
    {
        const auto* native = dynamic_cast<const D3D11IndirectBuffer*>(&buffer);
        if (binding < 0 || binding >= GetMaxVertexShaderStorageBlocksEXT() ||
            native == nullptr || native->GetDeviceEXT() != device_.Get() ||
            native->GetShaderResourceViewEXT() == nullptr)
            throw System::NotSupportedException(
                "DirectX11 draw storage binding needs a same-device raw storage buffer "
                "and a t-register from zero through fifteen.");
        drawStorageBuffers_[static_cast<std::size_t>(binding)] =
            buffer.shared_from_this();
    }

    void DirectX11Renderer::BindStorageInputsForEffectEXT(
        const D3D11EffectRenderer& effect)
    {
        const std::uint32_t vertex = effect.GetVertexStorageSlotsEXT();
        const std::uint32_t pixel = effect.GetPixelStorageSlotsEXT();
        for (std::size_t slot = 0; slot < drawStorageBuffers_.size(); ++slot)
        {
            const std::uint32_t bit = UINT32_C(1) << slot;
            if (((vertex | pixel) & bit) == 0)
                continue;
            const auto* native = dynamic_cast<const D3D11IndirectBuffer*>(
                drawStorageBuffers_[slot].get());
            if (native == nullptr || native->GetShaderResourceViewEXT() == nullptr)
                throw System::NotSupportedException(
                    "DirectX11 ShaderEffect uses an unbound raw storage t-register " +
                    std::to_string(slot) + '.');
            ID3D11ShaderResourceView* srv = native->GetShaderResourceViewEXT();
            if ((vertex & bit) != 0)
                context_->VSSetShaderResources(static_cast<UINT>(slot), 1, &srv);
            if ((pixel & bit) != 0)
                context_->PSSetShaderResources(static_cast<UINT>(slot), 1, &srv);
        }
    }

    void DirectX11Renderer::ClearStorageInputsAfterEffectEXT(
        const D3D11EffectRenderer& effect)
    {
        ID3D11ShaderResourceView* empty[16]{};
        context_->VSSetShaderResources(0, 16, empty);
        const std::uint32_t pixel = effect.GetPixelStorageSlotsEXT();
        for (UINT slot = 0; slot < 16; ++slot)
            if ((pixel & (UINT32_C(1) << slot)) != 0)
                context_->PSSetShaderResources(slot, 1, empty);
    }

    std::unique_ptr<IStorageBufferRenderer> DirectX11Renderer::CreateStorageBufferEXT(
        std::size_t byteSize, std::uint32_t usage, std::uint32_t cpuAccess)
    {
        constexpr std::uint32_t supportedUsage = UINT32_C(0x4F);
        if (usage == 0 || (usage & ~supportedUsage) != 0 ||
            (cpuAccess & ~UINT32_C(0x03)) != 0 || byteSize == 0 ||
            byteSize > static_cast<std::size_t>(std::numeric_limits<UINT>::max() - 15) ||
            ((usage & UINT32_C(0x01)) != 0 &&
             (!SupportsComputeShadersEXT() || byteSize > GetMaxStorageBufferBytesEXT())) ||
            ((usage & UINT32_C(0x08)) != 0 && !SupportsIndirectDrawEXT()) ||
            ((usage & UINT32_C(0x40)) != 0 &&
             byteSize > GetMaxUniformBufferBytesEXT()))
            return nullptr;
        return std::make_unique<D3D11IndirectBuffer>(
            device_.Get(), context_.Get(), byteSize, usage, cpuAccess);
    }

    bool DirectX11Renderer::SupportsComputeShadersEXT() const
    {
        return device_ != nullptr && featureLevel_ >= D3D_FEATURE_LEVEL_11_0;
    }

    bool DirectX11Renderer::SupportsComputeImageBindingEXT() const
    {
        if (!SupportsComputeShadersEXT()) return false;
        const auto support = GetSurfaceFormatUsageSupportEXT(static_cast<int>(
            Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color));
        constexpr std::uint32_t required =
            static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageRead) |
            static_cast<std::uint32_t>(CNA::RendererFormatUsage::StorageWrite);
        return (support.supportedUsages & required) == required;
    }

    std::unique_ptr<IComputeShaderRenderer> DirectX11Renderer::CreateComputeShader(
        const std::string& computeSrc)
    {
        if (!SupportsComputeShadersEXT()) return nullptr;
        auto shader = std::make_unique<D3D11ComputeShader>(device_.Get(), context_.Get());
        shader->CompileProgram(computeSrc);
        return shader;
    }

    std::unique_ptr<IStorageBufferRenderer> DirectX11Renderer::CreateStorageBuffer(
        std::size_t byteSize)
    {
        if (!SupportsComputeShadersEXT()) return nullptr;
        return CreateStorageBufferEXT(byteSize, UINT32_C(0x0F), UINT32_C(0x03));
    }

    void DirectX11Renderer::DispatchCompute(
        IComputeShaderRenderer* shader, int groupsX, int groupsY, int groupsZ)
    {
        auto* native = dynamic_cast<D3D11ComputeShader*>(shader);
        if (!native || groupsX <= 0 || groupsY <= 0 || groupsZ <= 0 ||
            groupsX > GetMaxComputeWorkGroupCountEXT(0) ||
            groupsY > GetMaxComputeWorkGroupCountEXT(1) ||
            groupsZ > GetMaxComputeWorkGroupCountEXT(2))
            throw std::invalid_argument("D3D11 compute dispatch has an invalid program or size");
        native->Dispatch(groupsX, groupsY, groupsZ);
    }

    int DirectX11Renderer::GetMaxComputeWorkGroupCountEXT(int axis) const
    {
        return SupportsComputeShadersEXT() && axis >= 0 && axis < 3
            ? D3D11_CS_DISPATCH_MAX_THREAD_GROUPS_PER_DIMENSION : 0;
    }

    int DirectX11Renderer::GetMaxComputeWorkGroupSizeEXT(int axis) const
    {
        if (!SupportsComputeShadersEXT()) return 0;
        switch (axis)
        {
        case 0: return D3D11_CS_THREAD_GROUP_MAX_X;
        case 1: return D3D11_CS_THREAD_GROUP_MAX_Y;
        case 2: return D3D11_CS_THREAD_GROUP_MAX_Z;
        default: return 0;
        }
    }

    int DirectX11Renderer::GetMaxComputeWorkGroupInvocationsEXT() const
    {
        return SupportsComputeShadersEXT()
            ? D3D11_CS_THREAD_GROUP_MAX_THREADS_PER_GROUP : 0;
    }

    std::uint64_t DirectX11Renderer::GetMaxStorageBufferBytesEXT() const
    {
        return SupportsComputeShadersEXT() ? UINT64_C(128) * 1024 * 1024 : 0;
    }

    std::uint64_t DirectX11Renderer::GetMaxUniformBufferBytesEXT() const
    {
        return device_ != nullptr
            ? static_cast<std::uint64_t>(D3D11_REQ_CONSTANT_BUFFER_ELEMENT_COUNT) * 16 : 0;
    }

    int DirectX11Renderer::GetMaxComputeStorageBufferBindingsEXT() const
    {
        return SupportsComputeShadersEXT() ? D3D11_PS_CS_UAV_REGISTER_COUNT : 0;
    }

    std::uint64_t DirectX11Renderer::GetMinStorageBufferOffsetAlignmentEXT() const
    {
        return SupportsComputeShadersEXT() ? 4 : 0;
    }

    std::uint64_t DirectX11Renderer::GetMinUniformBufferOffsetAlignmentEXT() const
    {
        return device_ != nullptr ? 16 : 0;
    }

    bool DirectX11Renderer::LoadsCompressedContentNativelyEXT() const
    {
        return true;
    }

    std::unique_ptr<ITextureRenderer> DirectX11Renderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<D3D11TextureRenderer>(this, data);
    }

    std::unique_ptr<ITexture2DArrayRenderer> DirectX11Renderer::CreateTexture2DArrayEXT(
        int width, int height, int layerCount, int mipLevelCount,
        int surfaceFormat, std::uint32_t usage)
    {
        if (!device_ || !context_ || width <= 0 || height <= 0 ||
            width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
            height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
            layerCount <= 0 || layerCount > GetMaxTextureArrayLayersEXT() ||
            mipLevelCount <= 0 || mipLevelCount > D3D11_REQ_MIP_LEVELS ||
            ClassifySurfaceFormatEXT(surfaceFormat) != RendererFormatVerdict::Supported)
            return nullptr;
        const auto support = GetSurfaceFormatUsageSupportEXT(surfaceFormat);
        constexpr std::uint32_t sampled = static_cast<std::uint32_t>(
            CNA::RendererFormatUsage::Sampled);
        constexpr std::uint32_t filterable = static_cast<std::uint32_t>(
            CNA::RendererFormatUsage::Filterable);
        if ((support.supportedUsages & sampled) == 0 ||
            ((usage & UINT32_C(0x02)) != 0 &&
             (support.supportedUsages & filterable) == 0))
            return nullptr;
        return std::make_unique<D3D11Texture2DArray>(
            device_.Get(), context_.Get(), width, height, layerCount,
            mipLevelCount, surfaceFormat, usage);
    }

    std::unique_ptr<IStorageTexture2DRenderer> DirectX11Renderer::CreateStorageTexture2DEXT(
        int width, int height, int mipLevelCount, int surfaceFormat, std::uint32_t usage)
    {
        using CNA::RendererFormatUsage;
        if (!device_ || !context_ || !SupportsComputeShadersEXT() ||
            !D3DCommon::IsXnaUncompressedSurfaceFormat(surfaceFormat) ||
            D3DCommon::SurfaceFormatBytesPerTexel(surfaceFormat) <= 0 ||
            width <= 0 || height <= 0 ||
            width > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
            height > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION ||
            mipLevelCount <= 0 || mipLevelCount > D3D11_REQ_MIP_LEVELS ||
            (usage & UINT32_C(0x03)) == 0 || (usage & ~UINT32_C(0x3F)) != 0)
            return nullptr;
        const auto support = GetSurfaceFormatUsageSupportEXT(surfaceFormat);
        std::uint32_t required = 0;
        if (usage & UINT32_C(0x01))
            required |= static_cast<std::uint32_t>(RendererFormatUsage::StorageRead);
        if (usage & UINT32_C(0x02))
            required |= static_cast<std::uint32_t>(RendererFormatUsage::StorageWrite);
        if (usage & UINT32_C(0x04))
            required |= static_cast<std::uint32_t>(RendererFormatUsage::Sampled);
        if (usage & UINT32_C(0x08))
            required |= static_cast<std::uint32_t>(RendererFormatUsage::Filterable);
        if (usage & UINT32_C(0x10))
            required |= static_cast<std::uint32_t>(RendererFormatUsage::TransferSource);
        if (usage & UINT32_C(0x20))
            required |= static_cast<std::uint32_t>(RendererFormatUsage::TransferDestination);
        if (mipLevelCount > 1)
            required |= static_cast<std::uint32_t>(RendererFormatUsage::Mipmapped);
        if ((support.supportedUsages & required) != required)
            return nullptr;
        return std::make_unique<D3D11StorageTexture2D>(
            device_.Get(), context_.Get(), width, height, mipLevelCount,
            surfaceFormat, usage);
    }

    int DirectX11Renderer::GetMaxTextureArrayLayersEXT() const
    {
        if (!device_) return 0;
        if (featureLevel_ >= D3D_FEATURE_LEVEL_11_0)
            return D3D11_REQ_TEXTURE2D_ARRAY_AXIS_DIMENSION;
        return featureLevel_ >= D3D_FEATURE_LEVEL_10_0 ? 512 : 0;
    }

    int DirectX11Renderer::GetMaxSampledTexturesPerShaderStageEXT() const
    {
        return device_ ? D3DCommon::D3DProgramReflection::kMaxShaderResources : 0;
    }

    int DirectX11Renderer::GetMaxStorageImagesPerShaderStageEXT() const
    {
        if (!SupportsComputeShadersEXT()) return 0;
        const auto support = GetSurfaceFormatUsageSupportEXT(static_cast<int>(
            Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color));
        return (support.supportedUsages & static_cast<std::uint32_t>(
            CNA::RendererFormatUsage::StorageWrite)) != 0
            ? D3D11_PS_CS_UAV_REGISTER_COUNT : 0;
    }

    CNA::RendererFormatSupport DirectX11Renderer::GetSurfaceFormatUsageSupportEXT(
        int surfaceFormat) const
    {
        using CNA::RendererFormatUsage;
        constexpr std::uint32_t classified =
            static_cast<std::uint32_t>(RendererFormatUsage::Sampled) |
            static_cast<std::uint32_t>(RendererFormatUsage::Filterable) |
            static_cast<std::uint32_t>(RendererFormatUsage::StorageRead) |
            static_cast<std::uint32_t>(RendererFormatUsage::StorageWrite) |
            static_cast<std::uint32_t>(RendererFormatUsage::TransferSource) |
            static_cast<std::uint32_t>(RendererFormatUsage::TransferDestination) |
            static_cast<std::uint32_t>(RendererFormatUsage::Mipmapped);
        if (!device_ ||
            ClassifySurfaceFormatEXT(surfaceFormat) != RendererFormatVerdict::Supported)
            return {classified, 0};
        const DXGI_FORMAT format = D3DCommon::SurfaceFormatToDxgi(surfaceFormat);
        UINT native = 0;
        // A format without SHADER_SAMPLE still permits point-only sampling;
        // that bit decides Filterable below rather than basic Sampled support.
        constexpr UINT required = D3D11_FORMAT_SUPPORT_TEXTURE2D;
        if (format == DXGI_FORMAT_UNKNOWN ||
            FAILED(device_->CheckFormatSupport(format, &native)) ||
            (native & required) != required || !DeviceKeepsFormatBytesEXT(format))
            return {classified, 0};
        std::uint32_t supported =
            static_cast<std::uint32_t>(RendererFormatUsage::Sampled) |
            static_cast<std::uint32_t>(RendererFormatUsage::TransferSource) |
            static_cast<std::uint32_t>(RendererFormatUsage::TransferDestination);
        if ((native & D3D11_FORMAT_SUPPORT_MIP) != 0)
            supported |= static_cast<std::uint32_t>(RendererFormatUsage::Mipmapped);
        if ((native & D3D11_FORMAT_SUPPORT_SHADER_SAMPLE) != 0)
            supported |= static_cast<std::uint32_t>(RendererFormatUsage::Filterable);
        if (D3DCommon::IsXnaUncompressedSurfaceFormat(surfaceFormat) &&
            SupportsComputeShadersEXT() &&
            (native & D3D11_FORMAT_SUPPORT_TYPED_UNORDERED_ACCESS_VIEW) != 0)
        {
            D3D11_FEATURE_DATA_FORMAT_SUPPORT2 typed{};
            typed.InFormat = format;
            if (SUCCEEDED(device_->CheckFeatureSupport(
                    D3D11_FEATURE_FORMAT_SUPPORT2, &typed, sizeof(typed))))
            {
                if ((typed.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0)
                    supported |= static_cast<std::uint32_t>(RendererFormatUsage::StorageRead);
                if ((typed.OutFormatSupport2 & D3D11_FORMAT_SUPPORT2_UAV_TYPED_STORE) != 0)
                    supported |= static_cast<std::uint32_t>(RendererFormatUsage::StorageWrite);
            }
        }
        return {classified, supported};
    }

    std::unique_ptr<ITexture3DRenderer> DirectX11Renderer::CreateTexture3D(
        int w, int h, int depth, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<D3D11Texture3DRenderer>(device_.Get(), context_.Get(), w, h, depth, mipMap, surfaceFormat);
    }

    std::unique_ptr<ITextureCubeRenderer> DirectX11Renderer::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<D3D11TextureCubeRenderer>(device_.Get(), context_.Get(), size, mipMap, surfaceFormat);
    }

    std::unique_ptr<IRenderTargetRenderer> DirectX11Renderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        return CreateRenderTarget2DEXT(
            w, h, depthFormat, preserveContents, mipMap, multiSampleCount, 0);
    }

    std::unique_ptr<IRenderTargetRenderer> DirectX11Renderer::CreateRenderTarget2DEXT(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap,
        int multiSampleCount, int surfaceFormat)
    {
        (void)preserveContents; // D3D11_USAGE_DEFAULT + ResolveSubresource-on-unbind already always
                                 // preserves prior contents across binds (no "discard on bind" path
                                 // exists in this renderer) -- matches EasyGL/Vulkan's own honoring
                                 // of RenderTargetUsage as a hint GraphicsDevice.SetRenderTarget()
                                 // itself acts on (an explicit Clear() call), not something the
                                 // renderer needs to special-case at creation time.
        return std::make_unique<DirectX11::D3D11RenderTargetRenderer>(
            this, device_.Get(), context_.Get(), w, h, depthFormat, mipMap, multiSampleCount,
            surfaceFormat);
    }

    void DirectX11Renderer::FlushPendingMRTResolveEXT()
    {
        if (currentMRTCount_ <= 0) return;
        RestoreBackBufferRenderTargetEXT();
        for (int i = 0; i < currentMRTCount_; ++i)
        {
            if (currentMRTTargets_[i]) currentMRTTargets_[i]->ResolveAndGenerateMipsEXT();
            if (currentMRTCubes_[i]) currentMRTCubes_[i]->ResolveAndGenerateMipsEXT();
            currentMRTTargets_[i] = nullptr;
            currentMRTCubes_[i] = nullptr;
        }
        currentMRTCount_ = 0;
    }

    // REMED-GFX-134: a bound RenderTargetCube face was never tracked, so its
    // UnbindAsRenderTarget() -- where this renderer's per-face ResolveSubresource() and
    // GenerateMips() live -- was unreachable from the public SetRenderTarget/SetRenderTargets path.
    // A multisampled cube target's resolve texture therefore stayed empty and a mipMap=true cube
    // target's levels above 0 were never regenerated; both read back (and sampled) as untouched
    // memory. Called at the top of every binding change, exactly like FlushPendingMRTResolveEXT().
    void DirectX11Renderer::FlushPendingCubeResolveEXT()
    {
        if (!currentCubeRT_) return;
        D3D11RenderTargetCubeRenderer* cube = currentCubeRT_;
        currentCubeRT_ = nullptr;
        cube->UnbindAsRenderTarget();
    }

    void DirectX11Renderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face)
    {
        FlushPendingMRTResolveEXT();
        FlushPendingCubeResolveEXT();
        if (!rt)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        if (currentCustomRT_)
        {
            currentCustomRT_->UnbindAsRenderTarget();
            currentCustomRT_ = nullptr;
        }
        rt->BindAsRenderTargetFace(face);
        currentCubeRT_ = static_cast<D3D11RenderTargetCubeRenderer*>(rt);
    }

    void DirectX11Renderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        // DX-143: finalize (MSAA resolve + mip regen) any MRT set that was previously bound via
        // SetRenderTargets() before switching to a single target or the back buffer -- the real
        // gap this task closes (an MRT set's own per-target finalize never ran before this).
        FlushPendingMRTResolveEXT();
        FlushPendingCubeResolveEXT();
        if (currentCustomRT_) currentCustomRT_->UnbindAsRenderTarget();
        if (!rt)
        {
            currentCustomRT_ = nullptr;
            return;
        }
        auto* d3drt = static_cast<D3D11RenderTargetRenderer*>(rt);
        currentCustomRT_ = d3drt;
        d3drt->BindAsRenderTarget();
    }

    std::unique_ptr<IRenderTargetCubeRenderer> DirectX11Renderer::CreateRenderTargetCube(
        int size, int depthFormat, bool preserveContents, bool mipMap, int multiSampleCount)
    {
        return CreateRenderTargetCubeEXT(
            size, depthFormat, preserveContents, mipMap, multiSampleCount, 0);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> DirectX11Renderer::CreateRenderTargetCubeEXT(
        int size, int depthFormat, bool preserveContents, bool mipMap,
        int multiSampleCount, int surfaceFormat)
    {
        // DX-152: multiSampleCount is now honored -- device-queried and clamped to 0 (off) by
        // D3D11RenderTargetCubeRenderer's own ClampMultiSampleCount() when unsupported.
        // REMED-GFX-136: preserveContents is consumed by being deliberately unused, for the same
        // reason CreateRenderTarget2D above states -- OMSetRenderTargets has no load action, so a
        // bound cube face keeps whatever is in it until something explicitly clears or draws over
        // it, and the only unrequested clear is GraphicsDevice::SetRenderTargets' DiscardContents
        // one.
        (void) preserveContents;
        return std::make_unique<DirectX11::D3D11RenderTargetCubeRenderer>(
            this, device_.Get(), context_.Get(), size, depthFormat, mipMap, multiSampleCount,
            surfaceFormat);
    }

    void DirectX11Renderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        // DX-143: finalize (MSAA resolve + mip regen) any PRIOR MRT set before doing anything else
        // -- handles MRT->MRT, MRT->single-target (via the currentCustomRT_ branch below not
        // applying), and MRT->back-buffer (the count<=0 branch below) transitions all in one place.
        FlushPendingMRTResolveEXT();
        // REMED-GFX-134: and the same for a prior cube-face bind, whose own finalize was never
        // reached at all. SetRenderTargetCubeFace() below re-tracks the new one.
        FlushPendingCubeResolveEXT();
        if (currentCustomRT_)
        {
            currentCustomRT_->UnbindAsRenderTarget();
            currentCustomRT_ = nullptr;
        }
        if (!renderTargets || count <= 0)
        {
            // Phase DIRECTX8 bugfix (found via DX-61's first real draw-call test): a prior MRT bind
            // (this same method, count>0 below) deliberately never sets currentCustomRT_ -- the
            // comment at the bottom of this function explains why -- so the branch above only
            // restores the back buffer when the prior bind went through SetRenderTarget2D().
            // Unconditionally (and idempotently -- harmless if it's already the back buffer)
            // restore here too, so unbinding after an MRT bind doesn't leave OMSetRenderTargets
            // (and this renderer's own currentColorRTVs_/currentDSV_ tracking) pointing at render
            // target views the caller may be about to destroy (dangling GPU state).
            RestoreBackBufferRenderTargetEXT();
            return;
        }
        if (count == 1 && renderTargets[0].IsRenderTargetCubeFace())
        {
            SetRenderTargetCubeFace(
                renderTargets[0].GetRenderTargetCube(),
                renderTargets[0].GetCubeFace());
            return;
        }
        const int n = std::min(count, static_cast<int>(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT));
        ID3D11RenderTargetView* rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
        ID3D11DepthStencilView* dsv = nullptr;
        for (int i = 0; i < n; ++i)
        {
            if (renderTargets[i].IsRenderTargetCubeFace())
            {
                auto* cube = static_cast<D3D11RenderTargetCubeRenderer*>(
                    renderTargets[i].GetRenderTargetCube());
                rtvs[i] = cube ? cube->PrepareMRTFaceEXT(renderTargets[i].GetCubeFace()) : nullptr;
                if (i == 0) dsv = cube ? cube->GetDSVEXT() : nullptr;
            }
            else
            {
                auto* target = static_cast<D3D11RenderTargetRenderer*>(
                    renderTargets[i].GetRenderTarget2D());
                rtvs[i] = target ? target->GetRTVEXT() : nullptr;
                if (i == 0) dsv = target ? target->GetDSVEXT() : nullptr;
            }
            if (!rtvs[i])
                throw std::runtime_error("DirectX11Renderer::SetRenderTargets: missing color attachment view.");
        }

        const int w = renderTargets[0].GetWidth();
        const int h = renderTargets[0].GetHeight();

        UnbindOutputAliasesEXT(rtvs, n, dsv);
        context_->OMSetRenderTargets(static_cast<UINT>(n), rtvs, dsv);

        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(w);
        vp.Height = static_cast<float>(h);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        context_->RSSetViewports(1, &vp);

        TrackCurrentRenderTargetEXT(rtvs, n, dsv);
        // DX-143: MRT targets are still NOT tracked in currentCustomRT_ (a single pointer can't
        // represent N targets) -- instead tracked in currentMRTTargets_/currentMRTCount_, finalized
        // by FlushPendingMRTResolveEXT() the next time SetRenderTarget2D()/SetRenderTargets() is
        // called (this task's own real fix: every individual target's MSAA resolve / mip regen now
        // genuinely runs when this MRT set is replaced/unbound, not silently skipped).
        currentMRTCount_ = n;
        for (int i = 0; i < n; ++i)
        {
            currentMRTTargets_[i] = renderTargets[i].IsRenderTargetCubeFace()
                ? nullptr : static_cast<D3D11RenderTargetRenderer*>(renderTargets[i].GetRenderTarget2D());
            currentMRTCubes_[i] = renderTargets[i].IsRenderTargetCubeFace()
                ? static_cast<D3D11RenderTargetCubeRenderer*>(renderTargets[i].GetRenderTargetCube())
                : nullptr;
        }
    }

    void DirectX11Renderer::ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy)
    {
        if (slot < 0 || slot >= kMaxSamplerSlotsEXT) return;
        samplerFilter_[slot] = filter;
        samplerAddressU_[slot] = addressU;
        samplerAddressV_[slot] = addressV;
        samplerMaxAnisotropy_[slot] = maxAnisotropy;
        // plans/plan_dx.md DX-216: XNA applies SamplerState as one object, so ApplySamplerState is also the
        // point at which the W axis and the mip controls revert to their defaults unless the caller
        // sets them again -- GraphicsDevice calls ApplySamplerAddressW/ApplySamplerMipState right
        // after this for a state that carries non-default values. Resetting them here is what makes
        // a slot's sampler state a state rather than an accumulation of every value ever set on it.
        samplerAddressW_[slot] = addressV;
        samplerMaxMipLevel_[slot] = 0;
        samplerLodBias_[slot] = 0.0f;
        RebindSamplerEXT(slot);
    }

    void DirectX11Renderer::ApplySamplerMipState(int slot, int maxMipLevel, float lodBias)
    {
        // DX-216: SamplerState.MaxMipLevel and MipMapLevelOfDetailBias. Tracked per slot exactly as
        // the fields above, and applied by rebuilding this slot's sampler -- D3D11 bakes all of it
        // into one immutable ID3D11SamplerState, so there is nothing finer-grained to set.
        if (slot < 0 || slot >= kMaxSamplerSlotsEXT) return;
        samplerMaxMipLevel_[slot] = maxMipLevel;
        samplerLodBias_[slot] = lodBias;
        RebindSamplerEXT(slot);
    }

    void DirectX11Renderer::ApplySamplerAddressW(int slot, int addressW)
    {
        // DX-216: the third addressing axis, which decides how a Texture3D sampled by a
        // ShaderEffect wraps on W. Until this override existed the cache mirrored AddressV, which
        // the interface documentation explicitly calls out as the thing a renderer must not do.
        if (slot < 0 || slot >= kMaxSamplerSlotsEXT) return;
        samplerAddressW_[slot] = addressW;
        RebindSamplerEXT(slot);
    }

    void DirectX11Renderer::RebindSamplerEXT(int slot)
    {
        if (slot < 0 || slot >= kMaxSamplerSlotsEXT || !device_ || !context_) return;
        auto sampler = samplerCache_.GetOrCreate(
            device_.Get(), samplerFilter_[slot], samplerAddressU_[slot], samplerAddressV_[slot],
            samplerMaxAnisotropy_[slot], samplerAddressW_[slot], samplerMaxMipLevel_[slot],
            samplerLodBias_[slot]);
        ID3D11SamplerState* raw = sampler.Get();
        context_->PSSetSamplers(static_cast<UINT>(slot), 1, &raw);
        context_->VSSetSamplers(static_cast<UINT>(slot), 1, &raw);
    }

    std::unique_ptr<IOcclusionQueryRenderer> DirectX11Renderer::CreateOcclusionQuery()
    {
        return std::make_unique<D3D11OcclusionQueryRenderer>(device_.Get(), context_.Get());
    }

    std::unique_ptr<IEffectRenderer> DirectX11Renderer::CreateEffectRenderer(
        const std::string& vertSrc, const std::string& fragSrc)
    {
        auto renderer = std::make_unique<D3D11EffectRenderer>(device_.Get(), context_.Get());
        if (!vertSrc.empty() && !fragSrc.empty())
            renderer->CompileProgram(vertSrc, fragSrc);
        return renderer;
    }

    void DirectX11Renderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                               int colorDstBlend, int alphaDstBlend,
                                               int colorBlendFunc, int alphaBlendFunc,
                                               const BlendWriteState& writeState)
    {
        // REMED-GFX-077: the four per-RT colour write masks are part of the ID3D11BlendState object
        // (static → cache key). The MultiSampleMask is NOT part of the object — it is the dynamic
        // third argument to OMSetBlendState, applied at bind time (and re-applied verbatim by
        // SetBlendFactor's re-bind), so it is captured on the renderer rather than keyed.
        currentSampleMask_ = writeState.multiSampleMask;
        currentBlendState_ = blendStateCache_.GetOrCreate(
            device_.Get(), colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
            colorBlendFunc, alphaBlendFunc,
            writeState.colorWriteChannels[0], writeState.colorWriteChannels[1],
            writeState.colorWriteChannels[2], writeState.colorWriteChannels[3]);
        context_->OMSetBlendState(currentBlendState_.Get(), currentBlendFactor_, currentSampleMask_);
    }

    void DirectX11Renderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                                       int depthFunc,
                                                       bool stencilEnable, int stencilFunc,
                                                       int stencilPass, int stencilFail, int stencilDepthFail,
                                                       int stencilMask, int stencilWriteMask, int referenceStencil,
                                                       bool twoSidedStencilMode,
                                                       int ccwStencilFunc, int ccwStencilPass,
                                                       int ccwStencilFail, int ccwStencilDepthFail)
    {
        dsDepthEnable_ = depthEnable;
        dsDepthWriteEnable_ = depthWriteEnable;
        dsDepthFunc_ = depthFunc;
        dsStencilEnable_ = stencilEnable;
        dsStencilFunc_ = stencilFunc;
        dsStencilPass_ = stencilPass;
        dsStencilFail_ = stencilFail;
        dsStencilDepthFail_ = stencilDepthFail;
        dsStencilMask_ = stencilMask;
        dsStencilWriteMask_ = stencilWriteMask;
        dsTwoSidedStencilMode_ = twoSidedStencilMode;
        dsCcwStencilFunc_ = ccwStencilFunc;
        dsCcwStencilPass_ = ccwStencilPass;
        dsCcwStencilFail_ = ccwStencilFail;
        dsCcwStencilDepthFail_ = ccwStencilDepthFail;
        currentReferenceStencil_ = referenceStencil;
        RebindDepthStencilState();
    }

    void DirectX11Renderer::RebindDepthStencilStateIfApplied()
    {
        if (depthStencilStateApplied_)
            RebindDepthStencilState();
    }

    void DirectX11Renderer::RebindDepthStencilState()
    {
        depthStencilStateApplied_ = true;
        // A back buffer declared Depth24 is backed by D24S8 (DXGI has no 24-bit depth-only
        // format), and XNA's Depth24 has no stencil at all. So while that depth view is bound the
        // stencil test is switched off rather than left to operate on storage the game never asked
        // for. Keyed on the bound view because the same state must still enable stencil for a
        // render target that declared Depth24Stencil8; this is re-evaluated on every target change.
        const bool backBufferStencilHidden =
            currentDSV_ != nullptr && currentDSV_ == depthStencilView_.Get() &&
            backBufferDepthFormat_ != static_cast<int>(
                Microsoft::Xna::Framework::Graphics::DepthFormat::Depth24Stencil8);
        const bool effectiveStencilEnable = dsStencilEnable_ && !backBufferStencilHidden;
        currentDepthStencilState_ = depthStencilStateCache_.GetOrCreate(
            device_.Get(), dsDepthEnable_, dsDepthWriteEnable_, dsDepthFunc_,
            effectiveStencilEnable, dsStencilFunc_, dsStencilPass_, dsStencilFail_, dsStencilDepthFail_,
            dsStencilMask_, dsStencilWriteMask_,
            dsTwoSidedStencilMode_, dsCcwStencilFunc_, dsCcwStencilPass_, dsCcwStencilFail_,
            dsCcwStencilDepthFail_);
        context_->OMSetDepthStencilState(currentDepthStencilState_.Get(),
                                         static_cast<UINT>(currentReferenceStencil_));
    }

    void DirectX11Renderer::ApplyRasterizerState(int cullMode, int fillMode,
                                                    bool scissorTestEnable,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        rsCullMode_ = cullMode;
        rsFillMode_ = fillMode;
        rsScissorTestEnable_ = scissorTestEnable;
        rsDepthBias_ = depthBias;
        rsSlopeScaleDepthBias_ = slopeScaleDepthBias;
        rasterizerStateApplied_ = true;
        RebindRasterizerState();
    }

    void DirectX11Renderer::RebindRasterizerState()
    {
        if (!rasterizerStateApplied_)
            return;

        DXGI_FORMAT depthFormat = DXGI_FORMAT_UNKNOWN;
        if (currentDSV_ != nullptr)
        {
            D3D11_DEPTH_STENCIL_VIEW_DESC desc{};
            currentDSV_->GetDesc(&desc);
            depthFormat = desc.Format;
        }
        const int nativeDepthBias = D3DCommon::XnaDepthBiasToD3D(rsDepthBias_, depthFormat);
        auto state = rasterizerStateCache_.GetOrCreate(
            device_.Get(), rsCullMode_, rsFillMode_, rsScissorTestEnable_,
            nativeDepthBias, rsSlopeScaleDepthBias_);
        context_->RSSetState(state.Get());
    }

    void DirectX11Renderer::SetBlendFactor(float r, float g, float b, float a)
    {
        currentBlendFactor_[0] = r;
        currentBlendFactor_[1] = g;
        currentBlendFactor_[2] = b;
        currentBlendFactor_[3] = a;
        // Task 870/319 (mirrored from DX-51's own SetReferenceStencil below): BlendFactor is a
        // real, independent device property -- it must take effect immediately even if no new
        // ApplyBlendState() call happens, by re-binding whatever blend state is already current
        // with the new factor. If no blend state has been applied yet, there is nothing to
        // re-bind (the next ApplyBlendState() call will pick up currentBlendFactor_ itself).
        if (currentBlendState_)
            context_->OMSetBlendState(currentBlendState_.Get(), currentBlendFactor_, currentSampleMask_);
    }

    void DirectX11Renderer::SetReferenceStencil(int value)
    {
        currentReferenceStencil_ = value;
        // Same standalone-property discipline as SetBlendFactor above (Task 870/319): re-bind the
        // current depth-stencil state object with the new reference value.
        if (currentDepthStencilState_)
            context_->OMSetDepthStencilState(currentDepthStencilState_.Get(),
                                             static_cast<UINT>(currentReferenceStencil_));
    }

    void DirectX11Renderer::SetScissorRect(int x, int y, int w, int h)
    {
        D3D11_RECT rect{};
        rect.left = x;
        rect.top = y;
        rect.right = x + std::max(0, w);
        rect.bottom = y + std::max(0, h);
        context_->RSSetScissorRects(1, &rect);
    }

    void DirectX11Renderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        D3D11_VIEWPORT vp{};
        vp.TopLeftX = static_cast<float>(x);
        vp.TopLeftY = static_cast<float>(y);
        vp.Width = static_cast<float>(std::max(0, w));
        vp.Height = static_cast<float>(std::max(0, h));
        vp.MinDepth = minDepth;
        vp.MaxDepth = maxDepth;
        context_->RSSetViewports(1, &vp);
    }

    void DirectX11Renderer::GetSpriteViewportSizeEXT(float& width, float& height) const
    {
        UINT count = 1;
        D3D11_VIEWPORT viewport{};
        context_->RSGetViewports(&count, &viewport);
        width = viewport.Width;
        height = viewport.Height;

        const bool backBufferBound = currentRTVCount_ == 1 &&
            currentColorRTVs_[0] == GetBackBufferDrawRtv();
        if (!backBufferBound)
            return;

        const auto drawableSize = surface_.GetDrawableSize();
        const int physicalWidth = drawableSize.width > 0 ? drawableSize.width : width_;
        const int physicalHeight = drawableSize.height > 0 ? drawableSize.height : height_;
        const auto geometry = D3DCommon::ComputeD3DPresentationGeometry(
            physicalWidth, physicalHeight, virtualWidth_, virtualHeight_, presentationMode_);
        const int logicalWidth = static_cast<int>(std::lround(geometry.logicalWidth));
        const int logicalHeight = static_cast<int>(std::lround(geometry.logicalHeight));
        const int presentationWidth = static_cast<int>(std::lround(geometry.width));
        const int presentationHeight = static_cast<int>(std::lround(geometry.height));
        if (presentationWidth > 0 && presentationHeight > 0)
        {
            width = viewport.Width * static_cast<float>(logicalWidth) /
                static_cast<float>(presentationWidth);
            height = viewport.Height * static_cast<float>(logicalHeight) /
                static_cast<float>(presentationHeight);
        }
    }

    void DirectX11Renderer::UnbindOutputAliasesEXT(
        ID3D11RenderTargetView* const* rtvs, int count, ID3D11DepthStencilView* dsv)
    {
        std::vector<ID3D11Resource*> outputs;
        outputs.reserve(static_cast<std::size_t>(std::max(0, count)) + (dsv ? 1u : 0u));
        for (int i = 0; i < count; ++i)
        {
            if (!rtvs[i]) continue;
            ID3D11Resource* resource = nullptr;
            rtvs[i]->GetResource(&resource);
            outputs.push_back(resource);
        }
        if (dsv)
        {
            ID3D11Resource* resource = nullptr;
            dsv->GetResource(&resource);
            outputs.push_back(resource);
        }
        if (outputs.empty()) return;

        const auto clearStage = [&](auto getViews, auto setViews)
        {
            ID3D11ShaderResourceView* views[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
            (context_.Get()->*getViews)(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, views);
            for (UINT slot = 0; slot < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; ++slot)
            {
                if (!views[slot]) continue;
                ID3D11Resource* sampled = nullptr;
                views[slot]->GetResource(&sampled);
                const bool alias = std::find(outputs.begin(), outputs.end(), sampled) != outputs.end();
                sampled->Release();
                views[slot]->Release();
                if (alias)
                {
                    ID3D11ShaderResourceView* empty = nullptr;
                    (context_.Get()->*setViews)(slot, 1, &empty);
                }
            }
        };
        clearStage(&ID3D11DeviceContext::VSGetShaderResources,
                   &ID3D11DeviceContext::VSSetShaderResources);
        clearStage(&ID3D11DeviceContext::PSGetShaderResources,
                   &ID3D11DeviceContext::PSSetShaderResources);
        clearStage(&ID3D11DeviceContext::GSGetShaderResources,
                   &ID3D11DeviceContext::GSSetShaderResources);
        clearStage(&ID3D11DeviceContext::HSGetShaderResources,
                   &ID3D11DeviceContext::HSSetShaderResources);
        clearStage(&ID3D11DeviceContext::DSGetShaderResources,
                   &ID3D11DeviceContext::DSSetShaderResources);
        clearStage(&ID3D11DeviceContext::CSGetShaderResources,
                   &ID3D11DeviceContext::CSSetShaderResources);
        for (ID3D11Resource* resource : outputs) resource->Release();
    }

    void DirectX11Renderer::TrackCurrentRenderTargetEXT(
        ID3D11RenderTargetView* const* rtvs, int count, ID3D11DepthStencilView* dsv)
    {
        currentRTVCount_ = std::clamp(count, 0, static_cast<int>(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT));
        for (int i = 0; i < currentRTVCount_; ++i) currentColorRTVs_[i] = rtvs[i];
        for (int i = currentRTVCount_; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) currentColorRTVs_[i] = nullptr;
        currentDSV_ = dsv;
        RebindRasterizerState();
        RebindDepthStencilStateIfApplied();
    }

    bool DirectX11Renderer::IsRenderTargetActiveEXT(
        const D3D11RenderTargetRenderer* target) const noexcept
    {
        if (target == nullptr) return false;
        if (currentCustomRT_ == target) return true;
        for (int i = 0; i < currentMRTCount_; ++i)
            if (currentMRTTargets_[i] == target) return true;
        return false;
    }

    void DirectX11Renderer::RestoreBackBufferRenderTargetEXT()
    {
        ID3D11RenderTargetView* rtv = GetBackBufferDrawRtv();
        context_->OMSetRenderTargets(1, &rtv, depthStencilView_.Get());

        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(width_);
        vp.Height = static_cast<float>(height_);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        context_->RSSetViewports(1, &vp);

        currentColorRTVs_[0] = rtv;
        currentRTVCount_ = 1;
        for (int i = 1; i < D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT; ++i) currentColorRTVs_[i] = nullptr;
        currentDSV_ = depthStencilView_.Get();
        RebindRasterizerState();
        RebindDepthStencilStateIfApplied();
    }

    void DirectX11Renderer::NotifyRenderTargetDestroyedEXT(
        D3D11RenderTargetRenderer* target) noexcept
    {
        bool wasBound = currentCustomRT_ == target;
        if (wasBound) currentCustomRT_ = nullptr;
        for (int i = 0; i < currentMRTCount_; ++i)
        {
            if (currentMRTTargets_[i] == target)
            {
                currentMRTTargets_[i] = nullptr;
                wasBound = true;
            }
        }
        if (wasBound)
        {
            try { RestoreBackBufferRenderTargetEXT(); }
            catch (...)
            {
                currentRTVCount_ = 0;
                for (auto& rtv : currentColorRTVs_) rtv = nullptr;
                currentDSV_ = nullptr;
            }
        }
    }

    void DirectX11Renderer::NotifyRenderTargetCubeDestroyedEXT(
        D3D11RenderTargetCubeRenderer* target) noexcept
    {
        bool wasBound = currentCubeRT_ == target;
        if (wasBound) currentCubeRT_ = nullptr;
        for (int i = 0; i < currentMRTCount_; ++i)
        {
            if (currentMRTCubes_[i] == target)
            {
                currentMRTCubes_[i] = nullptr;
                wasBound = true;
            }
        }
        if (!wasBound) return;
        try { RestoreBackBufferRenderTargetEXT(); }
        catch (...)
        {
            currentRTVCount_ = 0;
            for (auto& rtv : currentColorRTVs_) rtv = nullptr;
            currentDSV_ = nullptr;
        }
    }

    std::unique_ptr<ISpriteBatchRenderer> DirectX11Renderer::CreateSpriteBatch()
    {
        return std::make_unique<D3D11SpriteBatchRenderer>(this);
    }

    std::unique_ptr<IVertexBufferRenderer> DirectX11Renderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<D3D11VertexBufferRenderer>(this, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferRenderer> DirectX11Renderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<D3D11IndexBufferRenderer>(this, index_capacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> DirectX11Renderer::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<D3D11IndexBufferRenderer>(this, index_capacity, true);
    }

    ID3D11Buffer* DirectX11Renderer::GetOrCreatePerDrawConstantBufferEXT()
    {
        if (!perDrawConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DPerDrawConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, perDrawConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: PerDraw constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return perDrawConstantBuffer_.Get();
    }

    ID3D11Buffer* DirectX11Renderer::GetOrCreateFogConstantBufferEXT()
    {
        if (!fogConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DFogConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, fogConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: Fog constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return fogConstantBuffer_.Get();
    }

    void DirectX11Renderer::UpdateDynamicConstantBufferEXT(
        ID3D11Buffer* buffer, const void* data, std::size_t byteCount)
    {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        const HRESULT hr = context_->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
        if (FAILED(hr))
            throw std::runtime_error("DirectX11Renderer: constant buffer Map failed, hr=" + FormatHr(hr));
        std::memcpy(mapped.pData, data, byteCount);
        context_->Unmap(buffer, 0);
    }

    Matrix DirectX11Renderer::ApplyXnaPixelCenterEXT(const Matrix& transform) const
    {
        D3D11_VIEWPORT viewport{};
        UINT viewportCount = 1;
        context_->RSGetViewports(&viewportCount, &viewport);

        bool multisampledDestination = false;
        if (currentRTVCount_ > 0 && currentColorRTVs_[0] != nullptr)
        {
            ComPtr<ID3D11Resource> resource;
            currentColorRTVs_[0]->GetResource(resource.GetAddressOf());
            ComPtr<ID3D11Texture2D> texture;
            if (resource && SUCCEEDED(resource.As(&texture)))
            {
                D3D11_TEXTURE2D_DESC desc{};
                texture->GetDesc(&desc);
                multisampledDestination = desc.SampleDesc.Count > 1;
            }
        }

        return D3DCommon::ApplyXnaPixelCenter(
            transform, viewport.Width, viewport.Height, multisampledDestination);
    }

    void DirectX11Renderer::DrawColoredPrimitives(
        const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount)
    {
        // DX-61: colored3d (stride 16, unlit vertex-color) is the only real draw pipeline so far --
        // matches this method's own header doc ("equivalent to BasicEffect with
        // VertexColorEnabled = true"). Other strides/variants still throw via DrawPrimitivesEx's
        // default fallback until Phase DIRECTX8's remaining tasks (DX-62 onward) land them.
        const auto& d3dVb = static_cast<const D3D11VertexBufferRenderer&>(vb);
        const std::size_t stride = d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;
        if (stride != 16)
        {
            throw std::runtime_error(
                "DirectX11Renderer::DrawColoredPrimitives: only stride-16 (VertexPositionColor) "
                "is implemented so far (plans/plan_dx.md DX-61); other strides land in DX-62 onward");
        }

        constexpr auto variant = D3DCommon::D3DShaderVariant::Colored3d;
        ID3D11VertexShader* vs = GetStockVertexShaderEXT(variant);
        ID3D11PixelShader* ps = GetStockPixelShaderEXT(variant);
        if (!vs || !ps)
            throw std::runtime_error("DrawColoredPrimitives: failed to create colored3d shader objects");

        auto layout = inputLayoutCache_.GetOrCreate(
            device_.Get(), variant, stride, d3dVb.GetDeclarationEXT().GetElements());
        if (!layout)
            throw std::runtime_error("DrawColoredPrimitives: failed to create colored3d input layout");

        // DX-60: PerDraw (b0) -- historical "raw vertex color" convention for this no-GpuDrawParams
        // legacy path (diffuseColor=white, vertexColorEnabled=true), matching every other renderer's
        // own DrawColoredPrimitives behavior (Task 364).
        D3DCommon::D3DPerDrawConstants perDraw{};
        const Matrix wvp = ApplyXnaPixelCenterEXT(world * view * projection);
        wvp.ToColumnMajor(perDraw.Mvp); // row-major flat layout, matches HLSL row_major cbuffer field
        perDraw.DiffuseColor[0] = perDraw.DiffuseColor[1] = perDraw.DiffuseColor[2] = perDraw.DiffuseColor[3] = 1.0f;
        perDraw.VertexColorEnabled = 1.0f;

        // FogParams (b1) -- fog disabled; this legacy path carries no GpuDrawParams to enable it.
        D3DCommon::D3DFogConstants fog{};
        // REMED-GFX-005/010/061: the zero-initialized FogVector authoritatively disables fog;
        // no separate enable scalar or explicit default is needed.

        ID3D11Buffer* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
        ID3D11Buffer* fogCB = GetOrCreateFogConstantBufferEXT();
        UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));
        UpdateDynamicConstantBufferEXT(fogCB, &fog, sizeof(fog));

        ID3D11Buffer* vbRaw = d3dVb.GetBufferEXT();
        const UINT strideU = static_cast<UINT>(stride);
        const UINT offset = 0;
        context_->IASetVertexBuffers(0, 1, &vbRaw, &strideU, &offset);
        context_->IASetInputLayout(layout.Get());
        context_->IASetPrimitiveTopology(ToD3D11Topology(primitive));
        context_->VSSetShader(vs, nullptr, 0);
        context_->PSSetShader(ps, nullptr, 0);

        ID3D11Buffer* cbs[2] = { perDrawCB, fogCB };
        context_->VSSetConstantBuffers(0, 2, cbs);
        context_->PSSetConstantBuffers(0, 2, cbs);

        const UINT vertexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
        context_->Draw(vertexCount, 0);
    }

    void DirectX11Renderer::DrawIndexedColoredPrimitives(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount)
    {
        // DX-61: indexed counterpart of DrawColoredPrimitives above -- same colored3d-only scope.
        const auto& d3dVb = static_cast<const D3D11VertexBufferRenderer&>(vb);
        const auto& d3dIb = static_cast<const D3D11IndexBufferRenderer&>(ib);
        const std::size_t stride = d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;
        if (stride != 16)
        {
            throw std::runtime_error(
                "DirectX11Renderer::DrawIndexedColoredPrimitives: only stride-16 "
                "(VertexPositionColor) is implemented so far (plans/plan_dx.md DX-61)");
        }

        constexpr auto variant = D3DCommon::D3DShaderVariant::Colored3d;
        ID3D11VertexShader* vs = GetStockVertexShaderEXT(variant);
        ID3D11PixelShader* ps = GetStockPixelShaderEXT(variant);
        if (!vs || !ps)
            throw std::runtime_error("DrawIndexedColoredPrimitives: failed to create colored3d shader objects");

        auto layout = inputLayoutCache_.GetOrCreate(
            device_.Get(), variant, stride, d3dVb.GetDeclarationEXT().GetElements());
        if (!layout)
            throw std::runtime_error("DrawIndexedColoredPrimitives: failed to create colored3d input layout");

        D3DCommon::D3DPerDrawConstants perDraw{};
        const Matrix wvp = ApplyXnaPixelCenterEXT(world * view * projection);
        wvp.ToColumnMajor(perDraw.Mvp);
        perDraw.DiffuseColor[0] = perDraw.DiffuseColor[1] = perDraw.DiffuseColor[2] = perDraw.DiffuseColor[3] = 1.0f;
        perDraw.VertexColorEnabled = 1.0f;

        D3DCommon::D3DFogConstants fog{};
        // REMED-GFX-005/010/061: the zero-initialized FogVector authoritatively disables fog;
        // no separate enable scalar or explicit default is needed.

        ID3D11Buffer* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
        ID3D11Buffer* fogCB = GetOrCreateFogConstantBufferEXT();
        UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));
        UpdateDynamicConstantBufferEXT(fogCB, &fog, sizeof(fog));

        ID3D11Buffer* vbRaw = d3dVb.GetBufferEXT();
        const UINT strideU = static_cast<UINT>(stride);
        const UINT offset = 0;
        context_->IASetVertexBuffers(0, 1, &vbRaw, &strideU, &offset);
        context_->IASetIndexBuffer(d3dIb.GetBufferEXT(), d3dIb.GetFormatEXT(), 0);
        context_->IASetInputLayout(layout.Get());
        context_->IASetPrimitiveTopology(ToD3D11Topology(primitive));
        context_->VSSetShader(vs, nullptr, 0);
        context_->PSSetShader(ps, nullptr, 0);

        ID3D11Buffer* cbs[2] = { perDrawCB, fogCB };
        context_->VSSetConstantBuffers(0, 2, cbs);
        context_->PSSetConstantBuffers(0, 2, cbs);

        const UINT indexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
        context_->DrawIndexed(indexCount, 0, 0);
    }

    ID3D11Buffer* DirectX11Renderer::GetOrCreateLightingConstantBufferEXT()
    {
        if (!lightingConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DLightingConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, lightingConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: Lighting constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return lightingConstantBuffer_.Get();
    }

    ID3D11Buffer* DirectX11Renderer::GetOrCreateAlphaTestConstantBufferEXT()
    {
        if (!alphaTestConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DAlphaTestConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, alphaTestConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: AlphaTest constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return alphaTestConstantBuffer_.Get();
    }

    ID3D11Buffer* DirectX11Renderer::GetOrCreateDualTexFogConstantBufferEXT()
    {
        if (!dualTexFogConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DFogConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, dualTexFogConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: DualTex Fog constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return dualTexFogConstantBuffer_.Get();
    }

    ID3D11Buffer* DirectX11Renderer::GetOrCreateEnvMapPerDrawConstantBufferEXT()
    {
        if (!envMapPerDrawConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DEnvMapPerDrawConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, envMapPerDrawConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: EnvMap PerDraw constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return envMapPerDrawConstantBuffer_.Get();
    }

    ID3D11Buffer* DirectX11Renderer::GetOrCreateEnvMapConstantBufferEXT()
    {
        if (!envMapConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DEnvMapConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, envMapConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: EnvMap constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return envMapConstantBuffer_.Get();
    }

    ID3D11Buffer* DirectX11Renderer::GetOrCreateBoneConstantBufferEXT()
    {
        if (!boneConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DBoneConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, boneConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: Bone constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return boneConstantBuffer_.Get();
    }

    ID3D11Buffer* DirectX11Renderer::GetOrCreateSkinnedExtraConstantBufferEXT()
    {
        if (!skinnedExtraConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DSkinnedExtraConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, skinnedExtraConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: Skinned Extra constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return skinnedExtraConstantBuffer_.Get();
    }

    ID3D11Buffer* DirectX11Renderer::GetOrCreatePbrPerDrawConstantBufferEXT()
    {
        if (!pbrPerDrawConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DPbrPerDrawConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, pbrPerDrawConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: Pbr PerDraw constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return pbrPerDrawConstantBuffer_.Get();
    }

    ID3D11Buffer* DirectX11Renderer::GetOrCreatePbrLightsConstantBufferEXT()
    {
        if (!pbrLightsConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DPbrLightConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(&desc, nullptr, pbrLightsConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: Pbr Lights constant buffer creation failed, hr=" + FormatHr(hr));
        }
        return pbrLightsConstantBuffer_.Get();
    }

    ID3D11Buffer* DirectX11Renderer::GetOrCreateShadowConstantBufferEXT()
    {
        if (!shadowConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DShadowConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(
                &desc, nullptr, shadowConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error(
                    "DirectX11Renderer: shadow constant buffer creation failed, hr=" +
                    FormatHr(hr));
        }
        return shadowConstantBuffer_.Get();
    }

    ID3D11Buffer* DirectX11Renderer::GetOrCreateIblConstantBufferEXT()
    {
        if (!iblConstantBuffer_)
        {
            D3D11_BUFFER_DESC desc{};
            desc.ByteWidth = sizeof(D3DCommon::D3DIblConstants);
            desc.Usage = D3D11_USAGE_DYNAMIC;
            desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            const HRESULT hr = device_->CreateBuffer(
                &desc, nullptr, iblConstantBuffer_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error(
                    "DirectX11Renderer: IBL constant buffer creation failed, hr=" +
                    FormatHr(hr));
        }
        return iblConstantBuffer_.Get();
    }

    ID3D11ShaderResourceView* DirectX11Renderer::GetOrCreateDefaultWhiteSrvEXT()
    {
        if (!defaultWhiteSrv_)
        {
            const uint8_t white[4] = {255, 255, 255, 255};
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = 1;
            desc.Height = 1;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA initData{};
            initData.pSysMem = white;
            initData.SysMemPitch = 4;

            HRESULT hr = device_->CreateTexture2D(&desc, &initData, defaultWhiteTexture_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: default white texture creation failed, hr=" + FormatHr(hr));
            hr = device_->CreateShaderResourceView(defaultWhiteTexture_.Get(), nullptr, defaultWhiteSrv_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: default white SRV creation failed, hr=" + FormatHr(hr));
        }
        return defaultWhiteSrv_.Get();
    }

    ID3D11ShaderResourceView* DirectX11Renderer::GetOrCreateDefaultFlatNormalSrvEXT()
    {
        if (!defaultFlatNormalSrv_)
        {
            // plans/plan_cnj.md CNB-58 follow-up: a "flat" tangent-space normal (0,0,1) encoded as RGB
            // (128,128,255), matching EasyGLRenderer::EnsureDefaultFlatNormalTexture()
            // exactly -- so the sampled/decoded (rgb*2-1) normal is exactly the geometric normal
            // (no perturbation) when PbrEffect::NormalMap is unbound.
            const uint8_t flatNormal[4] = {128, 128, 255, 255};
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = 1;
            desc.Height = 1;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA initData{};
            initData.pSysMem = flatNormal;
            initData.SysMemPitch = 4;

            HRESULT hr = device_->CreateTexture2D(&desc, &initData, defaultFlatNormalTexture_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: default flat-normal texture creation failed, hr=" + FormatHr(hr));
            hr = device_->CreateShaderResourceView(defaultFlatNormalTexture_.Get(), nullptr, defaultFlatNormalSrv_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: default flat-normal SRV creation failed, hr=" + FormatHr(hr));
        }
        return defaultFlatNormalSrv_.Get();
    }

    ID3D11ShaderResourceView* DirectX11Renderer::GetOrCreateDefaultOpaqueBlackSrvEXT()
    {
        if (!defaultOpaqueBlackSrv_)
        {
            const uint8_t opaqueBlack[4] = {0, 0, 0, 255};
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = 1;
            desc.Height = 1;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA initData{};
            initData.pSysMem = opaqueBlack;
            initData.SysMemPitch = 4;

            HRESULT hr = device_->CreateTexture2D(&desc, &initData, defaultOpaqueBlackTexture_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: default opaque-black texture creation failed, hr=" + FormatHr(hr));
            hr = device_->CreateShaderResourceView(defaultOpaqueBlackTexture_.Get(), nullptr, defaultOpaqueBlackSrv_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: default opaque-black SRV creation failed, hr=" + FormatHr(hr));
        }
        return defaultOpaqueBlackSrv_.Get();
    }

    ID3D11ShaderResourceView* DirectX11Renderer::GetOrCreateDefaultOpaqueBlackCubeSrvEXT()
    {
        if (!defaultOpaqueBlackCubeSrv_)
        {
            const uint8_t opaqueBlack[4] = {0, 0, 0, 255};
            D3D11_TEXTURE2D_DESC desc{};
            desc.Width = 1;
            desc.Height = 1;
            desc.MipLevels = 1;
            desc.ArraySize = 6;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

            D3D11_SUBRESOURCE_DATA faces[6]{};
            for (D3D11_SUBRESOURCE_DATA& face : faces)
            {
                face.pSysMem = opaqueBlack;
                face.SysMemPitch = 4;
            }

            HRESULT hr = device_->CreateTexture2D(&desc, faces, defaultOpaqueBlackCubeTexture_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: default opaque-black cube creation failed, hr=" + FormatHr(hr));
            hr = device_->CreateShaderResourceView(defaultOpaqueBlackCubeTexture_.Get(), nullptr, defaultOpaqueBlackCubeSrv_.ReleaseAndGetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("DirectX11Renderer: default opaque-black cube SRV creation failed, hr=" + FormatHr(hr));
        }
        return defaultOpaqueBlackCubeSrv_.Get();
    }

    void DirectX11Renderer::DrawPrimitivesExImpl(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params,
        ID3D11Buffer* indirectArguments, UINT indirectByteOffset)
    {
        // DX-62/DX-63/DX-64/DX-65/DX-66/DX-67: real effect-aware variant dispatch.
        const auto& d3dVb = static_cast<const D3D11VertexBufferRenderer&>(vb);
        const std::size_t fallbackStride =
            d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;
        const bool multiStream = HasMultipleVertexStreams(params);
        std::vector<Microsoft::Xna::Framework::Graphics::VertexElement> combinedElements;
        std::vector<D3DCommon::D3DVertexInputElement> inputElements;
        if (multiStream)
            BuildVertexInputLayout(params, false, combinedElements, inputElements);
        const std::size_t stride = CombinedVertexStrideOr(params, fallbackStride);
        const auto& vertexElements = multiStream
            ? combinedElements : d3dVb.GetDeclarationEXT().GetElements();

#if defined(CNA_DIRECTX11_COMPILED_EFFECTS)
        if (params.compiledEffectRuntime != nullptr)
        {
            BindCompiledEffectForDrawEXT(
                d3dVb, params, *params.compiledEffectRuntime);
            BindVertexStreams(context_.Get(), d3dVb, params);
            if (ib != nullptr)
            {
                const auto& d3dIb = static_cast<const D3D11IndexBufferRenderer&>(*ib);
                context_->IASetIndexBuffer(d3dIb.GetBufferEXT(), d3dIb.GetFormatEXT(), 0);
            }
            context_->IASetPrimitiveTopology(ToD3D11Topology(primitive));
            const UINT elementCount = static_cast<UINT>(
                VertexCountForPrimitives(primitive, primitiveCount));
            if (indirectArguments != nullptr)
            {
                if (ib != nullptr)
                    context_->DrawIndexedInstancedIndirect(indirectArguments, indirectByteOffset);
                else
                    context_->DrawInstancedIndirect(indirectArguments, indirectByteOffset);
            }
            else if (ib != nullptr)
                context_->DrawIndexed(elementCount, static_cast<UINT>(params.startIndex),
                                      static_cast<INT>(params.baseVertex));
            else
                context_->Draw(elementCount, static_cast<UINT>(params.vertexStart));
            return;
        }
#endif

        if (params.customEffectRequested)
        {
            auto* customEffect = dynamic_cast<D3D11EffectRenderer*>(params.customEffectRenderer);
            if (customEffect == nullptr || !customEffect->IsValid())
                throw System::NotSupportedException(
                    "DirectX11 custom 3D drawing requires a valid D3D11 ShaderEffect.");

            float worldValues[16];
            float viewValues[16];
            float projectionValues[16];
            world.ToColumnMajor(worldValues);
            view.ToColumnMajor(viewValues);
            projection.ToColumnMajor(projectionValues);
            customEffect->SetUniformMat4("World", worldValues);
            customEffect->SetUniformMat4("View", viewValues);
            customEffect->SetUniformMat4("Projection", projectionValues);
            if (!customEffect->BindForDrawEXT(vertexElements, inputElements))
                throw System::NotSupportedException(
                    "DirectX11 could not match the ShaderEffect vertex signature to the bound "
                    "VertexDeclaration.");
            BindStorageInputsForEffectEXT(*customEffect);

            BindVertexStreams(context_.Get(), d3dVb, params);
            if (ib != nullptr)
            {
                const auto& d3dIb = static_cast<const D3D11IndexBufferRenderer&>(*ib);
                context_->IASetIndexBuffer(d3dIb.GetBufferEXT(), d3dIb.GetFormatEXT(), 0);
            }
            context_->IASetPrimitiveTopology(ToD3D11Topology(primitive));
            const UINT elementCount = static_cast<UINT>(
                VertexCountForPrimitives(primitive, primitiveCount));
            if (indirectArguments != nullptr)
            {
                if (ib != nullptr)
                    context_->DrawIndexedInstancedIndirect(indirectArguments, indirectByteOffset);
                else
                    context_->DrawInstancedIndirect(indirectArguments, indirectByteOffset);
            }
            else if (ib != nullptr)
                context_->DrawIndexed(elementCount, static_cast<UINT>(params.startIndex),
                                      static_cast<INT>(params.baseVertex));
            else
                context_->Draw(elementCount, static_cast<UINT>(params.vertexStart));
            ClearStorageInputsAfterEffectEXT(*customEffect);
            return;
        }

        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
        const bool hasDeclaration = !vertexElements.empty();
        const auto hasElement = [&](VertexElementUsage usage, int usageIndex = 0)
        {
            return D3DCommon::DeclarationHasElement(vertexElements, usage, usageIndex);
        };
        const bool hasNormal = hasDeclaration ? hasElement(VertexElementUsage::Normal)
                                              : stride == 32;
        const bool hasColor = hasDeclaration ? hasElement(VertexElementUsage::Color)
                                             : stride == 16 || stride == 24;
        const bool hasTexCoord = hasDeclaration ? hasElement(VertexElementUsage::TextureCoordinate)
                                                : stride == 20 || stride == 24 || stride == 32;
        const bool hasTexCoord1 = hasDeclaration &&
                                  hasElement(VertexElementUsage::TextureCoordinate, 1);

        // A PBR MASK draw keeps the PBR shader and evaluates alpha coverage there.
        const bool needsPbr         = params.pbr;
        const bool needsAlphaTest   = !needsPbr &&
                                      (params.alphaTest[3] < 0.0f || params.alphaTest[2] < 0.0f);
        const bool needsDualTex     = params.dualTexture && !needsAlphaTest;
        const bool needsEnvMap      = params.envMapping  && !needsAlphaTest && !needsDualTex;
        // plans/plan_cnj.md CNB-58 follow-up: PbrEffect/SkinnedPbrEffect -- `params.skinned` further
        // selects Pbr3d vs. PbrSkinned3d below, mirroring EasyGLRenderer::SelectProgram()'s
        // own `if (params.pbr && params.skinned) ... else if (params.pbr) ...` priority (PBR takes
        // precedence over the plain-skinned bucket so SkinnedPbrEffect draws don't fall through to
        // skinned3d's non-PBR shader).
        const bool needsSkinned     = params.skinned && !needsPbr
                                     && !needsAlphaTest && !needsDualTex && !needsEnvMap;
        // A declared normal selects BasicEffect's lit family regardless of the record's byte size.
        // The stride-only rule remains solely for internal buffers that carry no declaration.
        const bool needsLitTextured = hasNormal && !needsAlphaTest && !needsDualTex
                                     && !needsEnvMap && !needsPbr && !needsSkinned;
        const bool haveIbl = needsPbr && params.iblEnabled &&
            params.iblIrradiance != nullptr &&
            params.iblPrefilteredSpecular != nullptr && params.iblBrdfLut != nullptr;
        const bool useModernLightingShader =
            (needsLitTextured || needsSkinned || needsPbr) &&
            ((params.shadowsEnabled && params.shadowMap != nullptr) ||
             params.punctualKind != 0 || haveIbl);

        if (needsAlphaTest && params.texture0 != nullptr && !hasTexCoord)
            throw std::runtime_error(
                "DirectX11Renderer::DrawPrimitivesEx: AlphaTestEffect requires TEXCOORD0 when "
                "a real texture is bound");
        if (needsDualTex && !hasTexCoord)
            throw std::runtime_error(
                "DirectX11Renderer::DrawPrimitivesEx: DualTextureEffect requires TEXCOORD0");
        // DX-66: env_map3d.vert.hlsl's VSInput is Position+Normal+UV (32 bytes).
        if (needsEnvMap && stride != 32)
            throw std::runtime_error(
                "DirectX11Renderer::DrawPrimitivesEx: EnvironmentMapEffect (env_map3d) requires "
                "stride 32 (VertexPositionNormalTexture)");
        // A declared skinned stream is identified by semantics, not by one packed record size:
        // XNA content may legally spell BLENDINDICES as either Byte4 (stride 52) or Vector4
        // (stride 64). Keep the legacy stride rule only for declaration-less internal buffers.
        bool usesFloatBoneIndices = false;
        bool hasSupportedBoneIndices = false;
        for (const auto& element : vertexElements)
        {
            if (element.getVertexElementUsageProperty() != VertexElementUsage::BlendIndices
                || element.getUsageIndexProperty() != 0)
                continue;

            const auto format = element.getVertexElementFormatProperty();
            usesFloatBoneIndices = format == VertexElementFormat::Vector4;
            hasSupportedBoneIndices = usesFloatBoneIndices || format == VertexElementFormat::Byte4;
            break;
        }
        const bool hasSkinnedElements = hasElement(VertexElementUsage::Position) && hasNormal && hasTexCoord
            && hasElement(VertexElementUsage::BlendWeight)
            && hasSupportedBoneIndices;
        if (needsSkinned && ((hasDeclaration && !hasSkinnedElements)
                            || (!hasDeclaration && stride != 52 && stride != 56)))
            throw std::runtime_error(
                "DirectX11Renderer::DrawPrimitivesEx: SkinnedEffect requires POSITION0, NORMAL0, "
                "TEXCOORD0, BLENDWEIGHT0 and Byte4 or Vector4 BLENDINDICES0");
        // plans/plan_cnj.md CNB-58 follow-up: pbr3d.vert.hlsl (unskinned) is stride 48
        // (VertexPositionNormalTangentTexture); pbr_skinned3d.vert.hlsl (SkinnedPbrEffect) is
        // stride 68 (VertexPositionNormalTangentTextureSkinned).
        if (needsPbr && !params.skinned && stride != 48 && stride != 60)
            throw std::runtime_error(
                "DirectX11Renderer::DrawPrimitivesEx: PbrEffect (pbr3d) requires stride 48 or 60 "
                "(VertexPositionNormalTangentTexture with optional TEXCOORD_1)");
        if (needsPbr && params.skinned && stride != 68 && stride != 76 && stride != 80)
            throw std::runtime_error(
                "DirectX11Renderer::DrawPrimitivesEx: SkinnedPbrEffect (pbr_skinned3d) requires "
                "stride 68, 76 or 80 (VertexPositionNormalTangentTextureSkinned with optional "
                "TEXCOORD_1 and COLOR_0)");

        D3DCommon::D3DShaderVariant variant;
        if (needsAlphaTest)
            variant = hasColor
                ? (hasTexCoord ? D3DCommon::D3DShaderVariant::AlphaTestColored3d
                               : D3DCommon::D3DShaderVariant::AlphaTestUntexturedColored3d)
                : (hasTexCoord ? D3DCommon::D3DShaderVariant::AlphaTest3d
                               : D3DCommon::D3DShaderVariant::AlphaTestUntextured3d);
        else if (needsDualTex)
            variant = hasColor
                ? (hasTexCoord1 ? D3DCommon::D3DShaderVariant::DualTextureColoredDualUv3d
                                : D3DCommon::D3DShaderVariant::DualTextureColored3d)
                : (hasTexCoord1 ? D3DCommon::D3DShaderVariant::DualTextureDualUv3d
                                : D3DCommon::D3DShaderVariant::DualTexture3d);
        else if (needsEnvMap)
            variant = D3DCommon::D3DShaderVariant::EnvMap3d;
        else if (needsPbr)
            // plans/plan_gltf.md GLTF-463: stride 80 is the skinned dual-UV record with a packed COLOR_0
            // appended, so it needs the variant that declares the colour input; stride 76 has no
            // colour slot to bind. The rigid dual-UV variant always declares one because every
            // stride-60 record carries the slot (GLTF-462).
            variant = params.skinned
                ? ((stride == 80) ? D3DCommon::D3DShaderVariant::PbrSkinned3dDualUvColor
                : (stride == 76)  ? D3DCommon::D3DShaderVariant::PbrSkinned3dDualUv
                                  : D3DCommon::D3DShaderVariant::PbrSkinned3d)
                : ((stride == 60) ? D3DCommon::D3DShaderVariant::Pbr3dDualUv
                                  : D3DCommon::D3DShaderVariant::Pbr3d);
        else if (needsSkinned)
        {
            // plans/plan_graphics.md Phase 80 (Task 1106): real XNA renders SkinnedEffect's lit path
            // per-vertex by default (PreferPerPixelLighting == false), not per-pixel. CNB-67:
            // A COLOR0 declaration routes to the *Colored sibling independently of record stride.
            // Declaration-less legacy buffers retain the canonical stride-56 fallback.
            const bool colored = hasDeclaration ? hasColor : stride == 56;
            const bool vertexLit = params.lightingEnabled && !params.preferPerPixelLighting
                                   && !useModernLightingShader;
            if (usesFloatBoneIndices)
                variant = colored
                    ? (vertexLit ? D3DCommon::D3DShaderVariant::Skinned3dVertexLitColoredFloatIndices
                                 : D3DCommon::D3DShaderVariant::Skinned3dColoredFloatIndices)
                    : (vertexLit ? D3DCommon::D3DShaderVariant::Skinned3dVertexLitFloatIndices
                                 : D3DCommon::D3DShaderVariant::Skinned3dFloatIndices);
            else
                variant = colored
                    ? (vertexLit ? D3DCommon::D3DShaderVariant::Skinned3dVertexLitColored
                                 : D3DCommon::D3DShaderVariant::Skinned3dColored)
                    : (vertexLit ? D3DCommon::D3DShaderVariant::Skinned3dVertexLit
                                 : D3DCommon::D3DShaderVariant::Skinned3d);
        }
        else if (needsLitTextured)
            // Same real-default fix for BasicEffect's lit-textured bucket.
            variant = hasTexCoord
                ? (hasColor
                    ? ((params.lightingEnabled && !params.preferPerPixelLighting && !useModernLightingShader)
                        ? D3DCommon::D3DShaderVariant::LitTextured3dVertexLitColored
                        : D3DCommon::D3DShaderVariant::LitTextured3dColored)
                    : ((params.lightingEnabled && !params.preferPerPixelLighting && !useModernLightingShader)
                        ? D3DCommon::D3DShaderVariant::LitTextured3dVertexLit
                        : D3DCommon::D3DShaderVariant::LitTextured3d))
                : ((params.lightingEnabled && !params.preferPerPixelLighting && !useModernLightingShader)
                    ? D3DCommon::D3DShaderVariant::LitUntextured3dVertexLit
                    : D3DCommon::D3DShaderVariant::LitUntextured3d);
        else
        {
            if (hasTexCoord && hasColor)
                variant = D3DCommon::D3DShaderVariant::ColoredTextured3d;
            else if (hasTexCoord)
                variant = D3DCommon::D3DShaderVariant::Textured3d;
            else if (hasColor)
                variant = D3DCommon::D3DShaderVariant::Colored3d;
            else if (hasDeclaration && !params.vertexColorEnabled)
                // WINCLOSE-0015: XNA draws BasicEffect over POSITION0 alone in DiffuseColor.
                // Colored3d's signature names COLOR0, so this declaration never got a layout.
                variant = D3DCommon::D3DShaderVariant::Colored3dPositionOnly;
            else if (hasDeclaration)
                throw std::runtime_error(
                    "DirectX11Renderer::DrawPrimitivesEx: VertexColorEnabled requires COLOR0 in the "
                    "vertex declaration");
            else
                throw std::runtime_error(
                    "DirectX11Renderer::DrawPrimitivesEx: unsupported vertex stride " +
                    std::to_string(stride) + " for the colored/textured bundle (plans/plan_dx.md DX-62)");
        }

        if (useModernLightingShader)
        {
            using D3DCommon::D3DShaderVariant;
            switch (variant)
            {
                case D3DShaderVariant::LitTextured3d: variant = D3DShaderVariant::LitTextured3dShadow; break;
                case D3DShaderVariant::LitTextured3dColored: variant = D3DShaderVariant::LitTextured3dColoredShadow; break;
                case D3DShaderVariant::LitUntextured3d: variant = D3DShaderVariant::LitUntextured3dShadow; break;
                case D3DShaderVariant::Skinned3d: variant = D3DShaderVariant::Skinned3dShadow; break;
                case D3DShaderVariant::Skinned3dFloatIndices: variant = D3DShaderVariant::Skinned3dFloatIndicesShadow; break;
                case D3DShaderVariant::Skinned3dColored: variant = D3DShaderVariant::Skinned3dColoredShadow; break;
                case D3DShaderVariant::Skinned3dColoredFloatIndices: variant = D3DShaderVariant::Skinned3dColoredFloatIndicesShadow; break;
                case D3DShaderVariant::Pbr3d: variant = D3DShaderVariant::Pbr3dShadow; break;
                case D3DShaderVariant::Pbr3dDualUv: variant = D3DShaderVariant::Pbr3dDualUvShadow; break;
                case D3DShaderVariant::PbrSkinned3d: variant = D3DShaderVariant::PbrSkinned3dShadow; break;
                case D3DShaderVariant::PbrSkinned3dDualUv: variant = D3DShaderVariant::PbrSkinned3dDualUvShadow; break;
                case D3DShaderVariant::PbrSkinned3dDualUvColor: variant = D3DShaderVariant::PbrSkinned3dDualUvColorShadow; break;
                default: throw std::logic_error("DirectX11 shadow shader has no stock variant");
            }
        }

        ID3D11VertexShader* vs = GetStockVertexShaderEXT(variant);
        ID3D11PixelShader* ps = GetStockPixelShaderEXT(variant);
        if (!vs || !ps)
            throw std::runtime_error("DrawPrimitivesEx: failed to create shader objects for the selected variant");

        auto layout = inputLayoutCache_.GetOrCreate(
            device_.Get(), variant, stride, vertexElements, inputElements);
        if (!layout)
            throw std::runtime_error("DrawPrimitivesEx: failed to create input layout for the selected variant/stride");

        const Matrix wvp = ApplyXnaPixelCenterEXT(world * view * projection);

        // DX-65/DX-66: dual_texture3d needs t0+t1 (both Texture2D); env_map3d needs t0 (Texture2D)
        // + t1 (TextureCube). PBR needs seven slots: the five core maps plus KHR_materials_specular
        // strength/colour at t5/t6. Every other variant only ever binds t0 -- higher entries stay
        // null, which is harmless for a shader that does not declare them. Always bind the full
        // thirteen-wide range (unused slots explicitly null) so no variant can see a stale SRV
        // left by a previous, differently-shaped draw call (same discipline as cbs[5] below).
        ID3D11ShaderResourceView* srvs[13] = {};
        if (needsDualTex)
        {
            // WINCLOSE-0018: XNA samples an unbound slot as opaque black, not white.
            srvs[0] = params.texture0 ? GetSrvForTextureEXT(params.texture0)
                                      : GetOrCreateDefaultOpaqueBlackSrvEXT();
            srvs[1] = params.texture1 ? GetSrvForTextureEXT(params.texture1)
                                      : GetOrCreateDefaultOpaqueBlackSrvEXT();
        }
        else if (needsEnvMap)
        {
            // plans/plan_graphics_shared_cleanup.md GSC-0004: XNA samples both unbound slots as opaque
            // black (tools/xna-oracle/reference/null-texture/envmap_*_null.png); a null view sampled
            // transparent black, which also zeroed the output alpha.
            srvs[0] = params.texture0 ? GetSrvForTextureEXT(params.texture0)
                                      : GetOrCreateDefaultOpaqueBlackSrvEXT();
            srvs[1] = params.envMap ? GetSrvForTextureCubeEXT(params.envMap)
                                    : GetOrCreateDefaultOpaqueBlackCubeSrvEXT();
        }
        else if (needsAlphaTest)
        {
            // plans/plan_directx12_parity.md DX12-0021: XNA samples an unbound AlphaTestEffect texture
            // as opaque black; a null SRV here sampled as the diffuse colour's untextured result.
            srvs[0] = params.texture0 ? GetSrvForTextureEXT(params.texture0)
                                      : GetOrCreateDefaultOpaqueBlackSrvEXT();
        }
        else if (needsPbr)
        {
            // plans/plan_cnj.md CNB-58 follow-up: when a given PBR map is unbound, fall back to a 1x1
            // neutral-value texture instead of an unbound SRV -- matches
            // EasyGLRenderer::EnsurePbrProgram()'s own fallback-texture convention exactly
            // (flat tangent-space normal for the normal map; factor-only/no-emissive/fully-lit
            // white for the other three), so "map absent" reads as the correct BRDF input here too
            // rather than sampling an unbound (all-zero) shader resource.
            // GLTF-386: glTF baseColorTexture is optional. Sampling an unbound D3D11 SRV returns
            // transparent black, which zeroed the material alpha and made every factor-only PBR
            // primitive disappear from transparent reference captures. Match the other four PBR
            // slots and every other full PBR renderer: an absent base-colour map is opaque white.
            srvs[0] = params.texture0 ? GetSrvForTextureEXT(params.texture0)
                                      : GetOrCreateDefaultWhiteSrvEXT();
            srvs[1] = params.pbrNormalMap ? GetSrvForTextureEXT(params.pbrNormalMap) : GetOrCreateDefaultFlatNormalSrvEXT();
            srvs[2] = params.pbrMetallicRoughnessMap ? GetSrvForTextureEXT(params.pbrMetallicRoughnessMap) : GetOrCreateDefaultWhiteSrvEXT();
            srvs[3] = params.pbrEmissiveMap ? GetSrvForTextureEXT(params.pbrEmissiveMap) : GetOrCreateDefaultWhiteSrvEXT();
            srvs[4] = params.pbrOcclusionMap ? GetSrvForTextureEXT(params.pbrOcclusionMap) : GetOrCreateDefaultWhiteSrvEXT();
            srvs[5] = params.pbrSpecularMap ? GetSrvForTextureEXT(params.pbrSpecularMap) : GetOrCreateDefaultWhiteSrvEXT();
            srvs[6] = params.pbrSpecularColorMap ? GetSrvForTextureEXT(params.pbrSpecularColorMap) : GetOrCreateDefaultWhiteSrvEXT();
        }
        else
        {
            // plans/plan_graphics_shared_cleanup.md GSC-0004: SkinnedEffect and BasicEffect with
            // TextureEnabled sample an unbound texture as opaque black in XNA
            // (tools/xna-oracle/reference/null-texture/, measured through the real XNA 4.0 runtime), as
            // every other classic stock effect does. GLTF-386 had bound white here for untextured glTF
            // skins; the glTF importer now supplies glTF's own white base-colour texture instead
            // (ContentManager), so the stock effect keeps XNA's meaning. BasicEffect without
            // TextureEnabled runs shaders that gate the sample on TextureEnabled.
            srvs[0] = params.texture0 ? GetSrvForTextureEXT(params.texture0)
                                      : GetOrCreateDefaultOpaqueBlackSrvEXT();
        }

        if (useModernLightingShader)
        {
            srvs[7] = params.shadowsEnabled ? GetSrvForTextureEXT(params.shadowMap) : nullptr;
            srvs[8] = params.punctualKind == 1
                ? GetSrvForTextureCubeEXT(params.punctualShadowCube) : nullptr;
            srvs[9] = params.punctualKind == 2
                ? GetSrvForTextureEXT(params.punctualShadowMap) : nullptr;
        }
        if (haveIbl)
        {
            srvs[10] = GetSrvForTextureCubeEXT(params.iblIrradiance);
            srvs[11] = GetSrvForTextureCubeEXT(params.iblPrefilteredSpecular);
            srvs[12] = GetSrvForTextureEXT(params.iblBrdfLut);
        }

        // 5 contiguous slots (b0 through b4) always fully rebound below (unused slots explicitly null)
        // so no variant can see a stale buffer left bound by a previous, different-shaped draw.
        ID3D11Buffer* cbs[5] = {};

        if (needsAlphaTest)
        {
            // DX-64: alpha_test3d's single combined PerDraw (b0) cbuffer -- see
            // D3DAlphaTestConstants's own doc comment for why this isn't D3DPerDrawConstants.
            D3DCommon::D3DAlphaTestConstants c{};
            wvp.ToColumnMajor(c.Mvp);
            c.DiffuseColor[0] = params.diffuseColor[0];
            c.DiffuseColor[1] = params.diffuseColor[1];
            c.DiffuseColor[2] = params.diffuseColor[2];
            c.DiffuseColor[3] = params.diffuseColor[3];
            c.AlphaRef           = params.alphaTest[0];
            c.AlphaTol           = params.alphaTest[1];
            c.AlphaPassW         = params.alphaTest[2];
            c.AlphaFailW         = params.alphaTest[3];
            c.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;
            // REMED-GFX-005/010: FNA view-space fog vector into the restructured cbuffer slot.
            c.FogVector[0] = params.fogVector[0];
            c.FogVector[1] = params.fogVector[1];
            c.FogVector[2] = params.fogVector[2];
            c.FogVector[3] = params.fogVector[3];
            c.FogColor[0] = params.fogColor[0];
            c.FogColor[1] = params.fogColor[1];
            c.FogColor[2] = params.fogColor[2];

            ID3D11Buffer* cb = GetOrCreateAlphaTestConstantBufferEXT();
            // WINCLOSE-0026: Direct3D 9 channel expansion of the sampled texture.
            if (params.texture0 != nullptr)
                D3DCommon::D3D9ChannelExpansion(params.texture0->GetSurfaceFormatEXT(),
                                                c.Texture0ChannelMask, c.Texture0ChannelFill);
            UpdateDynamicConstantBufferEXT(cb, &c, sizeof(c));
            cbs[0] = cb;
        }
        else if (needsDualTex)
        {
            // DX-65: dual_texture3d's PerDraw (b0) is the same shape as D3DPerDrawConstants; its
            // FogParams cbuffer is at register(b2) instead of (b1) -- t0/s0 and t1/s1 are already
            // the two texture samplers, so fog moved to the next free slot (DX-13-hlsl's own note).
            D3DCommon::D3DPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            perDraw.DiffuseColor[0] = params.diffuseColor[0];
            perDraw.DiffuseColor[1] = params.diffuseColor[1];
            perDraw.DiffuseColor[2] = params.diffuseColor[2];
            perDraw.DiffuseColor[3] = params.diffuseColor[3];
            perDraw.TextureEnabled = params.textureEnabled ? 1.0f : 0.0f;
            perDraw.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;

            D3DCommon::D3DFogConstants fog{};
            fog.FogColor[0] = params.fogColor[0];
            fog.FogColor[1] = params.fogColor[1];
            fog.FogColor[2] = params.fogColor[2];
            // REMED-GFX-005/010/061: upload the authoritative FNA view-space fog vector; the
            // shader dots it with the object or post-skin position.
            fog.FogVector[0] = params.fogVector[0];
            fog.FogVector[1] = params.fogVector[1];
            fog.FogVector[2] = params.fogVector[2];
            fog.FogVector[3] = params.fogVector[3];

            ID3D11Buffer* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
            ID3D11Buffer* fogCB = GetOrCreateDualTexFogConstantBufferEXT();
            // WINCLOSE-0026: Direct3D 9 channel expansion of both sampled textures.
            if (params.texture0 != nullptr)
                D3DCommon::D3D9ChannelExpansion(params.texture0->GetSurfaceFormatEXT(),
                                                fog.Texture0ChannelMask, fog.Texture0ChannelFill);
            if (params.texture1 != nullptr)
                D3DCommon::D3D9ChannelExpansion(params.texture1->GetSurfaceFormatEXT(),
                                                fog.Texture1ChannelMask, fog.Texture1ChannelFill);
            UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));
            UpdateDynamicConstantBufferEXT(fogCB, &fog, sizeof(fog));
            cbs[0] = perDrawCB;
            cbs[2] = fogCB;
        }
        else if (needsEnvMap)
        {
            // DX-66: env_map3d's own PerDraw (b0) is Mvp+World only (D3DEnvMapPerDrawConstants) --
            // a genuinely different shape from D3DPerDrawConstants; material/lighting/fog live in
            // EnvMapParams (b2) instead (D3DEnvMapConstants), field-for-field matching
            // env_map3d.vert.hlsl/.frag.hlsl's real cbuffer declaration.
            D3DCommon::D3DEnvMapPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            world.ToColumnMajor(perDraw.World);

            D3DCommon::D3DEnvMapConstants c{};
            c.EyePosition[0] = params.eyePositionWorld[0];
            c.EyePosition[1] = params.eyePositionWorld[1];
            c.EyePosition[2] = params.eyePositionWorld[2];
            c.DiffuseColor[0] = params.diffuseColor[0];
            c.DiffuseColor[1] = params.diffuseColor[1];
            c.DiffuseColor[2] = params.diffuseColor[2];
            c.DiffuseColor[3] = params.diffuseColor[3];
            c.EmissiveAmount[0] = params.emissiveColor[0];
            c.EmissiveAmount[1] = params.emissiveColor[1];
            c.EmissiveAmount[2] = params.emissiveColor[2];
            c.EmissiveAmount[3] = params.envMapAmount;
            c.Light0Dir[0] = params.light0Dir[0];
            c.Light0Dir[1] = params.light0Dir[1];
            c.Light0Dir[2] = params.light0Dir[2];
            c.Light0DiffuseFresnel[0] = params.light0Diffuse[0];
            c.Light0DiffuseFresnel[1] = params.light0Diffuse[1];
            c.Light0DiffuseFresnel[2] = params.light0Diffuse[2];
            c.Light0DiffuseFresnel[3] = params.fresnelEnabled ? 1.0f : 0.0f;
            c.EnvMapSpecularFresnel[0] = params.envMapSpecular[0];
            c.EnvMapSpecularFresnel[1] = params.envMapSpecular[1];
            c.EnvMapSpecularFresnel[2] = params.envMapSpecular[2];
            c.EnvMapSpecularFresnel[3] = params.fresnelFactor;
            c.FogColor[0] = params.fogColor[0];
            c.FogColor[1] = params.fogColor[1];
            c.FogColor[2] = params.fogColor[2];
            // REMED-GFX-005/010/061: upload the authoritative FNA view-space fog vector; the
            // shader dots it with the object or post-skin position.
            c.FogVector[0] = params.fogVector[0];
            c.FogVector[1] = params.fogVector[1];
            c.FogVector[2] = params.fogVector[2];
            c.FogVector[3] = params.fogVector[3];
            c.Light1Dir[0] = params.light1Dir[0];
            c.Light1Dir[1] = params.light1Dir[1];
            c.Light1Dir[2] = params.light1Dir[2];
            c.Light1Diffuse[0] = params.light1Diffuse[0];
            c.Light1Diffuse[1] = params.light1Diffuse[1];
            c.Light1Diffuse[2] = params.light1Diffuse[2];
            c.Light2Dir[0] = params.light2Dir[0];
            c.Light2Dir[1] = params.light2Dir[1];
            c.Light2Dir[2] = params.light2Dir[2];
            c.Light2Diffuse[0] = params.light2Diffuse[0];
            c.Light2Diffuse[1] = params.light2Diffuse[1];
            c.Light2Diffuse[2] = params.light2Diffuse[2];
            if (params.envMap != nullptr)
                D3DCommon::D3D9ChannelExpansion(params.envMap->GetSurfaceFormatEXT(),
                                                c.EnvMapChannelMask, c.EnvMapChannelFill);

            if (params.texture0 != nullptr)
                D3DCommon::D3D9ChannelExpansion(params.texture0->GetSurfaceFormatEXT(),
                                                c.Texture0ChannelMask, c.Texture0ChannelFill);

            ID3D11Buffer* perDrawCB = GetOrCreateEnvMapPerDrawConstantBufferEXT();
            ID3D11Buffer* envCB = GetOrCreateEnvMapConstantBufferEXT();
            UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));
            UpdateDynamicConstantBufferEXT(envCB, &c, sizeof(c));
            cbs[0] = perDrawCB;
            cbs[2] = envCB;
        }
        else if (needsPbr)
        {
            // plans/plan_cnj.md CNB-58 follow-up: pbr3d/pbr_skinned3d's shared PerDraw (b0,
            // D3DPbrPerDrawConstants) -- transform + material constants. PbrLights lives at (b1)
            // for the unskinned Pbr3d variant, or (b2) for PbrSkinned3d (BoneBlock claims b1
            // there instead), matching skinned3d's own "next free slot" precedent.
            D3DCommon::D3DPbrPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            world.ToColumnMajor(perDraw.World);
            perDraw.DiffuseColor[0] = params.diffuseColor[0];
            perDraw.DiffuseColor[1] = params.diffuseColor[1];
            perDraw.DiffuseColor[2] = params.diffuseColor[2];
            perDraw.DiffuseColor[3] = params.diffuseColor[3];
            perDraw.AmbientMetallic[0] = params.ambientColor[0];
            perDraw.AmbientMetallic[1] = params.ambientColor[1];
            perDraw.AmbientMetallic[2] = params.ambientColor[2];
            perDraw.AmbientMetallic[3] = params.pbrMetallicFactor;
            perDraw.EmissiveRoughness[0] = params.emissiveColor[0];
            perDraw.EmissiveRoughness[1] = params.emissiveColor[1];
            perDraw.EmissiveRoughness[2] = params.emissiveColor[2];
            perDraw.EmissiveRoughness[3] = params.pbrRoughnessFactor;
            perDraw.AlphaTest[0] = params.alphaTest[0];
            perDraw.AlphaTest[1] = params.alphaTest[1];
            perDraw.AlphaTest[2] = params.alphaTest[2];
            perDraw.AlphaTest[3] = params.alphaTest[3];
            perDraw.PbrMapScales[0] = params.pbrNormalScale;
            perDraw.PbrMapScales[1] = params.pbrOcclusionStrength;
            perDraw.PbrMapScales[2] = params.pbrBaseColorTextureIsSrgb ? 1.0f : 0.0f;
            perDraw.PbrMapScales[3] = params.pbrEmissiveTextureIsSrgb ? 1.0f : 0.0f;
            perDraw.DielectricFresnel[0] = params.pbrDielectricF0[0];
            perDraw.DielectricFresnel[1] = params.pbrDielectricF0[1];
            perDraw.DielectricFresnel[2] = params.pbrDielectricF0[2];
            perDraw.DielectricFresnel[3] = params.pbrDielectricF90;
            std::memcpy(perDraw.TextureTransformRows, params.pbrTextureTransformRows,
                        sizeof(perDraw.TextureTransformRows));
            perDraw.TextureCoordinateSets[0] =
                static_cast<float>(params.pbrTextureCoordinateSetMask & 0x7fu);
            perDraw.SpecularFresnelInputs[0] = params.pbrDielectricF0Unclamped[0];
            perDraw.SpecularFresnelInputs[1] = params.pbrDielectricF0Unclamped[1];
            perDraw.SpecularFresnelInputs[2] = params.pbrDielectricF0Unclamped[2];
            perDraw.SpecularFresnelInputs[3] = params.pbrSpecularFactor;
            perDraw.SpecularMapFlags[0] =
                params.pbrSpecularColorTextureIsSrgb ? 1.0f : 0.0f;
            std::memcpy(perDraw.SpecularTextureTransformRows,
                        params.pbrSpecularTextureTransformRows,
                        sizeof(perDraw.SpecularTextureTransformRows));
            // plans/plan_gltf.md GLTF-465: §3.9.2's COLOR_0 multiplier switch. Only the stride-60 and
            // stride-80 variants declare a colour input, so this is inert for the others.
            perDraw.VertexColorFlags[0] = params.vertexColorEnabled ? 1.0f : 0.0f;

            D3DCommon::D3DPbrLightConstants lights{};
            lights.EyePosWeights[0] = params.eyePositionWorld[0];
            lights.EyePosWeights[1] = params.eyePositionWorld[1];
            lights.EyePosWeights[2] = params.eyePositionWorld[2];
            // Task 895 convention (skinned3d's own D3DSkinnedExtraConstants::EyePosition.w):
            // WeightsPerVertex, meaningless/left 0 for the unskinned Pbr3d variant.
            lights.EyePosWeights[3] = params.skinned ? static_cast<float>(params.weightsPerVertex) : 0.0f;
            lights.Light0Dir[0] = params.light0Dir[0];
            lights.Light0Dir[1] = params.light0Dir[1];
            lights.Light0Dir[2] = params.light0Dir[2];
            lights.Light0Diffuse[0] = params.light0Diffuse[0];
            lights.Light0Diffuse[1] = params.light0Diffuse[1];
            lights.Light0Diffuse[2] = params.light0Diffuse[2];
            lights.Light1Dir[0] = params.light1Dir[0];
            lights.Light1Dir[1] = params.light1Dir[1];
            lights.Light1Dir[2] = params.light1Dir[2];
            lights.Light1Diffuse[0] = params.light1Diffuse[0];
            lights.Light1Diffuse[1] = params.light1Diffuse[1];
            lights.Light1Diffuse[2] = params.light1Diffuse[2];
            lights.Light2Dir[0] = params.light2Dir[0];
            lights.Light2Dir[1] = params.light2Dir[1];
            lights.Light2Dir[2] = params.light2Dir[2];
            lights.Light2Diffuse[0] = params.light2Diffuse[0];
            lights.Light2Diffuse[1] = params.light2Diffuse[1];
            lights.Light2Diffuse[2] = params.light2Diffuse[2];
            lights.FogColor[0] = params.fogColor[0];
            lights.FogColor[1] = params.fogColor[1];
            lights.FogColor[2] = params.fogColor[2];
            lights.FogColor[3] = params.pbrEncodeOutputToSrgb ? 1.0f : 0.0f;
            // REMED-GFX-005/010/061: upload the authoritative FNA view-space fog vector; the
            // shader dots it with the object or post-skin position.
            lights.FogVector[0] = params.fogVector[0];
            lights.FogVector[1] = params.fogVector[1];
            lights.FogVector[2] = params.fogVector[2];
            lights.FogVector[3] = params.fogVector[3];

            ID3D11Buffer* perDrawCB = GetOrCreatePbrPerDrawConstantBufferEXT();
            ID3D11Buffer* lightsCB = GetOrCreatePbrLightsConstantBufferEXT();
            UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));
            UpdateDynamicConstantBufferEXT(lightsCB, &lights, sizeof(lights));
            cbs[0] = perDrawCB;
            if (params.skinned)
            {
                // PbrSkinned3d: BoneBlock at (b1) -- reuses the same D3DBoneConstants shape/buffer
                // skinned3d's own BoneBlock already uses (DX-60a), PbrLights moves to (b2).
                D3DCommon::D3DBoneConstants bones{};
                const int boneCount = std::min(params.boneCount, 72);
                if (boneCount > 0)
                    std::memcpy(bones.Bones, params.boneTransforms,
                               static_cast<std::size_t>(boneCount) * 16u * sizeof(float));
                ID3D11Buffer* boneCB = GetOrCreateBoneConstantBufferEXT();
                UpdateDynamicConstantBufferEXT(boneCB, &bones, sizeof(bones));
                cbs[1] = boneCB;
                cbs[2] = lightsCB;
            }
            else
            {
                cbs[1] = lightsCB;
            }
        }
        else if (needsSkinned)
        {
            // DX-67: skinned3d's PerDraw (b0) is the same shape as D3DPerDrawConstants; BoneBlock
            // (b1, D3DBoneConstants, DX-60a) holds the 72-matrix array; FogParams (b2,
            // D3DSkinnedExtraConstants) carries fog + DirectionalLight1/2 + World + EyePosition +
            // specular (the 128-byte PerDraw buffer has no spare room for those, same reasoning
            // D3DLightingConstants documents for lit_textured3d).
            D3DCommon::D3DPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            perDraw.DiffuseColor[0] = params.diffuseColor[0];
            perDraw.DiffuseColor[1] = params.diffuseColor[1];
            perDraw.DiffuseColor[2] = params.diffuseColor[2];
            perDraw.DiffuseColor[3] = params.diffuseColor[3];
            perDraw.AmbientColor[0] = params.ambientColor[0];
            perDraw.AmbientColor[1] = params.ambientColor[1];
            perDraw.AmbientColor[2] = params.ambientColor[2];
            perDraw.LightingEnabled = params.lightingEnabled ? 1.0f : 0.0f;
            perDraw.Light0Dir[0] = params.light0Dir[0];
            perDraw.Light0Dir[1] = params.light0Dir[1];
            perDraw.Light0Dir[2] = params.light0Dir[2];
            perDraw.TextureEnabled = params.textureEnabled ? 1.0f : 0.0f;
            perDraw.Light0Diffuse[0] = params.light0Diffuse[0];
            perDraw.Light0Diffuse[1] = params.light0Diffuse[1];
            perDraw.Light0Diffuse[2] = params.light0Diffuse[2];
            perDraw.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;

            // DX-60a/DX-67: params.boneTransforms is filled via Matrix::ToColumnMajor() at the
            // XNA-API call site (SkinnedEffect::SetBoneTransforms, SkinnedEffect.cpp:383) -- the
            // SAME function DX-60/DX-61's own report established emits raw row-major M11..M44
            // bytes, exactly what BoneBlock's `row_major float4x4 Bones[72]` wants unchanged. A
            // straight memcpy is therefore correct here, no per-matrix transpose needed.
            D3DCommon::D3DBoneConstants bones{};
            const int boneCount = std::min(params.boneCount, 72);
            if (boneCount > 0)
                std::memcpy(bones.Bones, params.boneTransforms,
                           static_cast<std::size_t>(boneCount) * 16u * sizeof(float));

            D3DCommon::D3DSkinnedExtraConstants extra{};
            extra.FogColor[0] = params.fogColor[0];
            extra.FogColor[1] = params.fogColor[1];
            extra.FogColor[2] = params.fogColor[2];
            // REMED-GFX-005/010/061: upload the authoritative FNA view-space fog vector; the
            // shader dots it with the object or post-skin position.
            extra.FogVector[0] = params.fogVector[0];
            extra.FogVector[1] = params.fogVector[1];
            extra.FogVector[2] = params.fogVector[2];
            extra.FogVector[3] = params.fogVector[3];
            extra.Light1Dir[0] = params.light1Dir[0];
            extra.Light1Dir[1] = params.light1Dir[1];
            extra.Light1Dir[2] = params.light1Dir[2];
            extra.Light1Diffuse[0] = params.light1Diffuse[0];
            extra.Light1Diffuse[1] = params.light1Diffuse[1];
            extra.Light1Diffuse[2] = params.light1Diffuse[2];
            extra.Light2Dir[0] = params.light2Dir[0];
            extra.Light2Dir[1] = params.light2Dir[1];
            extra.Light2Dir[2] = params.light2Dir[2];
            extra.Light2Diffuse[0] = params.light2Diffuse[0];
            extra.Light2Diffuse[1] = params.light2Diffuse[1];
            extra.Light2Diffuse[2] = params.light2Diffuse[2];
            world.ToColumnMajor(extra.World);
            extra.EyePosition[0] = params.eyePositionWorld[0];
            extra.EyePosition[1] = params.eyePositionWorld[1];
            extra.EyePosition[2] = params.eyePositionWorld[2];
            extra.EyePosition[3] = static_cast<float>(params.weightsPerVertex); // DX-67/Task 895
            extra.SpecularColorPower[0] = params.specularColor[0];
            extra.SpecularColorPower[1] = params.specularColor[1];
            extra.SpecularColorPower[2] = params.specularColor[2];
            extra.SpecularColorPower[3] = params.specularPower;
            extra.Light0Specular[0] = params.light0Specular[0];
            extra.Light0Specular[1] = params.light0Specular[1];
            extra.Light0Specular[2] = params.light0Specular[2];
            extra.Light1Specular[0] = params.light1Specular[0];
            extra.Light1Specular[1] = params.light1Specular[1];
            extra.Light1Specular[2] = params.light1Specular[2];
            extra.Light2Specular[0] = params.light2Specular[0];
            extra.Light2Specular[1] = params.light2Specular[1];
            extra.Light2Specular[2] = params.light2Specular[2];
            // REMED-GFX-008: pre-folded (emissive + ambient*diffuse)*alpha. The skinned shaders add
            // this AFTER lightSum*DiffuseColor; previously the skinned frag/vert-lit shaders read the
            // always-zero AmbientColor and never added emissive, dropping both.
            extra.EmissiveColor[0] = params.emissiveColor[0];
            extra.EmissiveColor[1] = params.emissiveColor[1];
            extra.EmissiveColor[2] = params.emissiveColor[2];

            ID3D11Buffer* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
            ID3D11Buffer* boneCB = GetOrCreateBoneConstantBufferEXT();
            ID3D11Buffer* extraCB = GetOrCreateSkinnedExtraConstantBufferEXT();
            UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));
            UpdateDynamicConstantBufferEXT(boneCB, &bones, sizeof(bones));
            // WINCLOSE-0026: Direct3D 9 channel expansion of the sampled texture.
            if (params.texture0 != nullptr)
                D3DCommon::D3D9ChannelExpansion(params.texture0->GetSurfaceFormatEXT(),
                                                extra.Texture0ChannelMask, extra.Texture0ChannelFill);
            UpdateDynamicConstantBufferEXT(extraCB, &extra, sizeof(extra));
            cbs[0] = perDrawCB;
            cbs[1] = boneCB;
            cbs[2] = extraCB;
        }
        else if (needsLitTextured)
        {
            // DX-63: PerDraw (b0) + LitLightParams (b1) -- full per-light Blinn-Phong data, field-
            // for-field matching lit_textured3d.vert.hlsl/.frag.hlsl's real cbuffer declarations
            // (see D3DLightingConstants's own doc comment for the offset table).
            D3DCommon::D3DPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            perDraw.DiffuseColor[0] = params.diffuseColor[0];
            perDraw.DiffuseColor[1] = params.diffuseColor[1];
            perDraw.DiffuseColor[2] = params.diffuseColor[2];
            perDraw.DiffuseColor[3] = params.diffuseColor[3];
            perDraw.AmbientColor[0] = params.ambientColor[0];
            perDraw.AmbientColor[1] = params.ambientColor[1];
            perDraw.AmbientColor[2] = params.ambientColor[2];
            perDraw.LightingEnabled = params.lightingEnabled ? 1.0f : 0.0f;
            perDraw.Light0Dir[0] = params.light0Dir[0];
            perDraw.Light0Dir[1] = params.light0Dir[1];
            perDraw.Light0Dir[2] = params.light0Dir[2];
            perDraw.TextureEnabled = params.textureEnabled ? 1.0f : 0.0f;
            perDraw.Light0Diffuse[0] = params.light0Diffuse[0];
            perDraw.Light0Diffuse[1] = params.light0Diffuse[1];
            perDraw.Light0Diffuse[2] = params.light0Diffuse[2];
            perDraw.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;

            D3DCommon::D3DLightingConstants lighting{};
            lighting.Light1Dir[0] = params.light1Dir[0];
            lighting.Light1Dir[1] = params.light1Dir[1];
            lighting.Light1Dir[2] = params.light1Dir[2];
            lighting.Light1Diffuse[0] = params.light1Diffuse[0];
            lighting.Light1Diffuse[1] = params.light1Diffuse[1];
            lighting.Light1Diffuse[2] = params.light1Diffuse[2];
            lighting.Light2Dir[0] = params.light2Dir[0];
            lighting.Light2Dir[1] = params.light2Dir[1];
            lighting.Light2Dir[2] = params.light2Dir[2];
            lighting.Light2Diffuse[0] = params.light2Diffuse[0];
            lighting.Light2Diffuse[1] = params.light2Diffuse[1];
            lighting.Light2Diffuse[2] = params.light2Diffuse[2];
            lighting.EmissiveColor[0] = params.emissiveColor[0];
            lighting.EmissiveColor[1] = params.emissiveColor[1];
            lighting.EmissiveColor[2] = params.emissiveColor[2];
            world.ToColumnMajor(lighting.World);
            lighting.EyePosition[0] = params.eyePositionWorld[0];
            lighting.EyePosition[1] = params.eyePositionWorld[1];
            lighting.EyePosition[2] = params.eyePositionWorld[2];
            lighting.Light0Specular[0] = params.light0Specular[0];
            lighting.Light0Specular[1] = params.light0Specular[1];
            lighting.Light0Specular[2] = params.light0Specular[2];
            lighting.Light1Specular[0] = params.light1Specular[0];
            lighting.Light1Specular[1] = params.light1Specular[1];
            lighting.Light1Specular[2] = params.light1Specular[2];
            lighting.Light2Specular[0] = params.light2Specular[0];
            lighting.Light2Specular[1] = params.light2Specular[1];
            lighting.Light2Specular[2] = params.light2Specular[2];
            lighting.SpecularColorPower[0] = params.specularColor[0];
            lighting.SpecularColorPower[1] = params.specularColor[1];
            lighting.SpecularColorPower[2] = params.specularColor[2];
            lighting.SpecularColorPower[3] = params.specularPower;
            lighting.FogColor[0] = params.fogColor[0];
            lighting.FogColor[1] = params.fogColor[1];
            lighting.FogColor[2] = params.fogColor[2];
            // REMED-GFX-005/010/061: upload the authoritative FNA view-space fog vector; the
            // shader dots it with the object or post-skin position.
            lighting.FogVector[0] = params.fogVector[0];
            lighting.FogVector[1] = params.fogVector[1];
            lighting.FogVector[2] = params.fogVector[2];
            lighting.FogVector[3] = params.fogVector[3];

            ID3D11Buffer* perDrawCB  = GetOrCreatePerDrawConstantBufferEXT();
            ID3D11Buffer* lightingCB = GetOrCreateLightingConstantBufferEXT();
            UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));
            // WINCLOSE-0026: Direct3D 9 channel expansion of the sampled texture.
            if (params.texture0 != nullptr)
                D3DCommon::D3D9ChannelExpansion(params.texture0->GetSurfaceFormatEXT(),
                                                lighting.Texture0ChannelMask, lighting.Texture0ChannelFill);
            UpdateDynamicConstantBufferEXT(lightingCB, &lighting, sizeof(lighting));
            cbs[0] = perDrawCB;
            cbs[1] = lightingCB;
        }
        else
        {
            // DX-62: colored3d/textured3d/colored_textured3d bundle -- PerDraw (b0) + FogParams (b1),
            // real GpuDrawParams values this time (unlike DrawColoredPrimitives' hardcoded legacy path).
            D3DCommon::D3DPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            perDraw.DiffuseColor[0] = params.diffuseColor[0];
            perDraw.DiffuseColor[1] = params.diffuseColor[1];
            perDraw.DiffuseColor[2] = params.diffuseColor[2];
            perDraw.DiffuseColor[3] = params.diffuseColor[3];
            perDraw.TextureEnabled = params.textureEnabled ? 1.0f : 0.0f;
            perDraw.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;

            D3DCommon::D3DFogConstants fog{};
            fog.FogColor[0] = params.fogColor[0];
            fog.FogColor[1] = params.fogColor[1];
            fog.FogColor[2] = params.fogColor[2];
            // REMED-GFX-005/010/061: upload the authoritative FNA view-space fog vector; the
            // shader dots it with the object or post-skin position.
            fog.FogVector[0] = params.fogVector[0];
            fog.FogVector[1] = params.fogVector[1];
            fog.FogVector[2] = params.fogVector[2];
            fog.FogVector[3] = params.fogVector[3];

            // WINCLOSE-0026: Direct3D 9 channel expansion of the sampled texture.
            if (params.texture0 != nullptr)
                D3DCommon::D3D9ChannelExpansion(params.texture0->GetSurfaceFormatEXT(),
                                                fog.Texture0ChannelMask, fog.Texture0ChannelFill);

            ID3D11Buffer* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
            ID3D11Buffer* fogCB     = GetOrCreateFogConstantBufferEXT();
            UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));
            UpdateDynamicConstantBufferEXT(fogCB, &fog, sizeof(fog));
            cbs[0] = perDrawCB;
            cbs[1] = fogCB;
        }

        if (useModernLightingShader)
        {
            const bool haveDirectional = params.shadowsEnabled && params.shadowMap != nullptr;
            const int cascadeCount = haveDirectional && params.cascadeCount > 0
                ? std::min(params.cascadeCount, 4) : 0;
            const int punctualKind = params.punctualKind >= 1 && params.punctualKind <= 2
                ? params.punctualKind : 0;
            const bool havePoint = punctualKind == 1 && params.punctualShadowCube != nullptr;
            const bool haveSpot = punctualKind == 2 && params.punctualShadowMap != nullptr;

            D3DCommon::D3DShadowConstants shadow{};
            std::copy_n(params.lightViewProjColMajor, 16, shadow.LightViewProj);
            std::copy_n(params.cascadeMatricesColMajor, 64, shadow.CascadeMatrices);
            std::copy_n(params.punctualViewProjColMajor, 16, shadow.PunctualViewProj);
            shadow.Directional[0] = haveDirectional ? 1.0f : 0.0f;
            shadow.Directional[1] = params.shadowDepthBias;
            shadow.Directional[2] = static_cast<float>(std::clamp(params.shadowPcfRadius, 0, 2));
            shadow.Directional[3] = static_cast<float>(cascadeCount);
            const int shadowWidth = haveDirectional ? params.shadowMap->GetWidth() : 1;
            const int shadowHeight = haveDirectional ? params.shadowMap->GetHeight() : 1;
            shadow.ShadowTexelBlendDebug[0] = shadowWidth > 0 ? 1.0f / static_cast<float>(shadowWidth) : 0.0f;
            shadow.ShadowTexelBlendDebug[1] = shadowHeight > 0 ? 1.0f / static_cast<float>(shadowHeight) : 0.0f;
            shadow.ShadowTexelBlendDebug[2] = params.cascadeBlendBand;
            shadow.ShadowTexelBlendDebug[3] = params.cascadeDebugTint ? 1.0f : 0.0f;
            std::copy_n(params.cascadeSplits, 4, shadow.CascadeSplits);
            std::copy_n(params.cascadeViewZRow, 4, shadow.CascadeViewZ);
            std::copy_n(params.punctualPosition, 3, shadow.PunctualPositionRange);
            shadow.PunctualPositionRange[3] = params.punctualRange > 0.0f ? params.punctualRange : 1.0f;
            std::copy_n(params.punctualDirection, 3, shadow.PunctualDirectionKind);
            shadow.PunctualDirectionKind[3] = static_cast<float>(punctualKind);
            std::copy_n(params.punctualDiffuse, 3, shadow.PunctualDiffuseHasShadow);
            shadow.PunctualDiffuseHasShadow[3] = (havePoint || haveSpot) ? 1.0f : 0.0f;
            shadow.PunctualConeBiasTexelX[0] = params.punctualCosInner;
            shadow.PunctualConeBiasTexelX[1] = params.punctualCosOuter;
            shadow.PunctualConeBiasTexelX[2] = params.punctualShadowBias;
            const int spotWidth = haveSpot ? params.punctualShadowMap->GetWidth() : 1;
            const int spotHeight = haveSpot ? params.punctualShadowMap->GetHeight() : 1;
            shadow.PunctualConeBiasTexelX[3] = spotWidth > 0 ? 1.0f / static_cast<float>(spotWidth) : 0.0f;
            shadow.PunctualTexelY[0] = spotHeight > 0 ? 1.0f / static_cast<float>(spotHeight) : 0.0f;

            cbs[3] = GetOrCreateShadowConstantBufferEXT();
            UpdateDynamicConstantBufferEXT(cbs[3], &shadow, sizeof(shadow));
            RebindSamplerEXT(7);
            RebindSamplerEXT(8);
            RebindSamplerEXT(9);
        }
        if (needsPbr && useModernLightingShader)
        {
            D3DCommon::D3DIblConstants ibl{};
            ibl.Enabled = haveIbl ? 1.0f : 0.0f;
            ibl.PrefilteredMipCount = static_cast<float>(
                params.iblPrefilteredMipCount > 0 ? params.iblPrefilteredMipCount : 1);
            ibl.Intensity = params.iblIntensity;
            cbs[4] = GetOrCreateIblConstantBufferEXT();
            UpdateDynamicConstantBufferEXT(cbs[4], &ibl, sizeof(ibl));
            if (haveIbl)
            {
                RebindSamplerEXT(10);
                RebindSamplerEXT(11);
                RebindSamplerEXT(12);
            }
        }

        BindVertexStreams(context_.Get(), d3dVb, params);
        if (ib != nullptr)
        {
            const auto& d3dIb = static_cast<const D3D11IndexBufferRenderer&>(*ib);
            context_->IASetIndexBuffer(d3dIb.GetBufferEXT(), d3dIb.GetFormatEXT(), 0);
        }
        context_->IASetInputLayout(layout.Get());
        context_->IASetPrimitiveTopology(ToD3D11Topology(primitive));
        context_->VSSetShader(vs, nullptr, 0);
        context_->PSSetShader(ps, nullptr, 0);

        // Always rebind the full 5-slot-cbuffer/13-slot-SRV range (unused slots explicitly null,
        // see cbs'/srvs' own declaration comments above) -- no variant can see a stale binding
        // left by whatever differently-shaped draw call ran immediately before this one.
        context_->VSSetConstantBuffers(0, 5, cbs);
        context_->PSSetConstantBuffers(0, 5, cbs);
        context_->PSSetShaderResources(0, 13, srvs);

        if (ib != nullptr)
        {
            // REMED-GFX-020: honor the XNA DrawIndexedPrimitives startIndex/baseVertex offsets --
            // StartIndexLocation (elements into the index buffer) and BaseVertexLocation (added to
            // each fetched index). Previously both were hardcoded 0, so any indexed draw at a
            // non-zero offset silently read from the start of the buffers.
            const UINT indexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
            if (indirectArguments != nullptr)
                context_->DrawIndexedInstancedIndirect(indirectArguments, indirectByteOffset);
            else
                context_->DrawIndexed(indexCount, static_cast<UINT>(params.startIndex),
                                      static_cast<INT>(params.baseVertex));
        }
        else
        {
            // REMED-GFX-020: honor the XNA DrawPrimitives vertexStart offset -- StartVertexLocation
            // (vertices to skip before the first primitive). Was hardcoded 0, so a draw at a
            // non-zero startVertex silently re-rendered vertices from index 0 (the root cause of the
            // DirectX11_Pbr_VertexColor quad-D no-draw: DrawPrimitives(..., 6, 2) drew vertices 0..5).
            const UINT vertexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
            if (indirectArguments != nullptr)
                context_->DrawInstancedIndirect(indirectArguments, indirectByteOffset);
            else
                context_->Draw(vertexCount, static_cast<UINT>(params.vertexStart));
        }
    }

    void DirectX11Renderer::DrawPrimitivesEx(
        const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        DrawPrimitivesExImpl(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
    }

    void DirectX11Renderer::DrawIndexedPrimitivesEx(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        DrawPrimitivesExImpl(vb, &ib, world, view, projection, primitive, primitiveCount, params);
    }

    void DirectX11Renderer::DrawPrimitivesIndirectEXT(
        const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
        const Matrix& projection, PrimitiveType primitive,
        const IStorageBufferRenderer& argumentBuffer, int argumentByteOffset,
        const GpuDrawParams& params)
    {
        const auto* d3dBuffer = dynamic_cast<const D3D11IndirectBuffer*>(&argumentBuffer);
        if (d3dBuffer == nullptr || d3dBuffer->GetDeviceEXT() != device_.Get() ||
            argumentByteOffset < 0)
            throw System::NotSupportedException("DirectX11 indirect draw needs a native argument buffer.");
        DrawPrimitivesExImpl(vb, nullptr, world, view, projection, primitive, 0, params,
                             d3dBuffer->GetBufferEXT(), static_cast<UINT>(argumentByteOffset));
    }

    void DirectX11Renderer::DrawIndexedPrimitivesIndirectEXT(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, const IStorageBufferRenderer& argumentBuffer,
        int argumentByteOffset, const GpuDrawParams& params)
    {
        const auto* d3dBuffer = dynamic_cast<const D3D11IndirectBuffer*>(&argumentBuffer);
        if (d3dBuffer == nullptr || d3dBuffer->GetDeviceEXT() != device_.Get() ||
            argumentByteOffset < 0)
            throw System::NotSupportedException("DirectX11 indexed indirect draw needs a native argument buffer.");
        DrawPrimitivesExImpl(vb, &ib, world, view, projection, primitive, 0, params,
                             d3dBuffer->GetBufferEXT(), static_cast<UINT>(argumentByteOffset));
    }

    void DirectX11Renderer::DrawInstancedPrimitivesEx(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, int instanceCount, const GpuDrawParams& params)
    {
        // REMED-GFX-202: the per-instance stream is now the lowest-slot entry of the shared
        // GpuVertexStreamBinding array whose InstanceFrequency is greater than zero.
        const auto* instanceStream = FirstInstanceStream(params);
        if (params.firstInstance < 0)
            throw System::NotSupportedException(
                "DirectX11 instancing requires a non-negative first instance.");
        const auto& d3dVb = static_cast<const D3D11VertexBufferRenderer&>(vb);
        const auto& d3dIb = static_cast<const D3D11IndexBufferRenderer&>(ib);
        bool logicalIdStreamActive = false;
        const auto drawWithFirstInstance = [&](const UINT indexCount)
        {
            const UINT startIndex = static_cast<UINT>(params.startIndex);
            const INT baseVertex = static_cast<INT>(params.baseVertex);
            if (instanceStream == nullptr || params.firstInstance == 0)
            {
                context_->DrawIndexedInstanced(
                    indexCount, static_cast<UINT>(std::max(1, instanceCount)),
                    startIndex, baseVertex,
                    logicalIdStreamActive ? 0u : static_cast<UINT>(params.firstInstance));
                return;
            }

            // D3D11 applies StartInstanceLocation after InstanceDataStepRate. Rebase every
            // stream at a logical index that aligns to all its frequencies, then submit the
            // entire remaining range in one draw. Only the leading unaligned instances need
            // individual draws; this also keeps frequency-one streams on the fast path.
            for (int instance = 0; instance < instanceCount; ++instance)
            {
                const int logicalInstance = params.firstInstance + instance;
                bool allStreamsAligned = true;
                for (int i = 0; i < params.vertexStreamCount; ++i)
                {
                    const int frequency = params.vertexStreams[
                        static_cast<std::size_t>(i)].instanceFrequency;
                    if (frequency > 0 && logicalInstance % frequency != 0)
                    {
                        allStreamsAligned = false;
                        break;
                    }
                }
                BindVertexStreams(
                    context_.Get(), d3dVb, params, logicalInstance);
                if (logicalIdStreamActive)
                {
                    ID3D11Buffer* idBuffer = logicalInstanceIdBuffer_.Get();
                    const UINT stride = sizeof(UINT);
                    const UINT offset = static_cast<UINT>(instance) * stride;
                    context_->IASetVertexBuffers(
                        static_cast<UINT>(kMaxVertexStreams), 1,
                        &idBuffer, &stride, &offset);
                }
                context_->DrawIndexedInstanced(
                    indexCount,
                    allStreamsAligned
                        ? static_cast<UINT>(instanceCount - instance) : 1u,
                    startIndex, baseVertex, 0);
                if (allStreamsAligned)
                    break;
            }
        };
#if defined(CNA_DIRECTX11_COMPILED_EFFECTS)
        if (params.compiledEffectRuntime != nullptr)
        {
            BindCompiledEffectForDrawEXT(
                d3dVb, params, *params.compiledEffectRuntime);
            BindVertexStreams(context_.Get(), d3dVb, params);
            context_->IASetIndexBuffer(d3dIb.GetBufferEXT(), d3dIb.GetFormatEXT(), 0);
            context_->IASetPrimitiveTopology(ToD3D11Topology(primitive));
            drawWithFirstInstance(static_cast<UINT>(
                VertexCountForPrimitives(primitive, primitiveCount)));
            return;
        }
#endif
        if (params.customEffectRequested)
        {
            auto* customEffect = dynamic_cast<D3D11EffectRenderer*>(params.customEffectRenderer);
            if (customEffect == nullptr || !customEffect->IsValid())
                throw System::NotSupportedException(
                    "DirectX11 custom instancing requires a valid D3D11 ShaderEffect.");

            std::vector<Microsoft::Xna::Framework::Graphics::VertexElement> combinedElements;
            std::vector<D3DCommon::D3DVertexInputElement> inputElements;
            BuildVertexInputLayout(
                params, instanceStream != nullptr, combinedElements, inputElements);
            const auto& declaration = combinedElements.empty()
                ? d3dVb.GetDeclarationEXT().GetElements() : combinedElements;
            float worldValues[16];
            float viewValues[16];
            float projectionValues[16];
            world.ToColumnMajor(worldValues);
            view.ToColumnMajor(viewValues);
            projection.ToColumnMajor(projectionValues);
            customEffect->SetUniformMat4("World", worldValues);
            customEffect->SetUniformMat4("View", viewValues);
            customEffect->SetUniformMat4("Projection", projectionValues);
            if (params.firstInstance > 0
                    ? !customEffect->BindForBaseInstanceDrawEXT(
                          declaration, inputElements, logicalIdStreamActive)
                    : !customEffect->BindForDrawEXT(declaration, inputElements))
                throw System::NotSupportedException(
                    "DirectX11 could not match the instanced ShaderEffect vertex signature "
                    "to the bound vertex streams: " + customEffect->GetCompileError());
            BindStorageInputsForEffectEXT(*customEffect);

            if (logicalIdStreamActive)
            {
                const UINT requested = static_cast<UINT>(instanceCount);
                constexpr UINT maxCapacity =
                    (std::numeric_limits<UINT>::max)() / sizeof(UINT);
                if (requested > maxCapacity)
                    throw System::NotSupportedException(
                        "DirectX11 base-instance ID stream exceeds the D3D11 buffer size.");
                if (logicalInstanceIdCapacity_ < requested)
                {
                    const UINT doubled = logicalInstanceIdCapacity_ <= maxCapacity / 2
                        ? logicalInstanceIdCapacity_ * 2u : maxCapacity;
                    const UINT capacity = (std::max)(requested, doubled);
                    D3D11_BUFFER_DESC desc{};
                    desc.ByteWidth = capacity * sizeof(UINT);
                    desc.Usage = D3D11_USAGE_DYNAMIC;
                    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
                    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                    ComPtr<ID3D11Buffer> buffer;
                    if (FAILED(device_->CreateBuffer(&desc, nullptr, buffer.GetAddressOf())))
                        throw std::runtime_error(
                            "DirectX11 could not allocate the logical instance ID stream.");
                    logicalInstanceIdBuffer_ = std::move(buffer);
                    logicalInstanceIdCapacity_ = capacity;
                }
                D3D11_MAPPED_SUBRESOURCE mapped{};
                if (FAILED(context_->Map(logicalInstanceIdBuffer_.Get(), 0,
                                         D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
                    throw std::runtime_error(
                        "DirectX11 could not update the logical instance ID stream.");
                auto* ids = static_cast<UINT*>(mapped.pData);
                for (UINT instance = 0; instance < requested; ++instance)
                    ids[instance] = static_cast<UINT>(params.firstInstance) + instance;
                context_->Unmap(logicalInstanceIdBuffer_.Get(), 0);
            }

            BindVertexStreams(context_.Get(), d3dVb, params);
            if (logicalIdStreamActive)
            {
                ID3D11Buffer* idBuffer = logicalInstanceIdBuffer_.Get();
                const UINT stride = sizeof(UINT);
                const UINT offset = 0;
                context_->IASetVertexBuffers(
                    static_cast<UINT>(kMaxVertexStreams), 1,
                    &idBuffer, &stride, &offset);
            }
            context_->IASetIndexBuffer(d3dIb.GetBufferEXT(), d3dIb.GetFormatEXT(), 0);
            context_->IASetPrimitiveTopology(ToD3D11Topology(primitive));
            drawWithFirstInstance(static_cast<UINT>(
                VertexCountForPrimitives(primitive, primitiveCount)));
            ClearStorageInputsAfterEffectEXT(*customEffect);
            return;
        }
        if (instanceStream == nullptr)
        {
            // With no per-instance data the stock shader still needs one submission per
            // requested instance for blend/stencil side effects.
            for (int instance = 0; instance < instanceCount; ++instance)
                DrawIndexedPrimitivesEx(
                    vb, ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        const bool stockEffectNeedsFullShader = params.textureEnabled || params.texture0 != nullptr ||
            params.lightingEnabled || params.fogEnabled || params.dualTexture ||
            params.envMapping || params.skinned || params.pbr ||
            params.alphaTest[3] < 0.0f || params.alphaTest[2] < 0.0f;
        if (stockEffectNeedsFullShader)
        {
            struct InstanceColumn
            {
                const GpuVertexStreamBinding* stream;
                const D3D11VertexBufferRenderer* buffer;
                int byteOffset;
            };
            std::array<InstanceColumn, 4> columns{};
            int columnCount = 0;
            GpuDrawParams ordinary = params;
            ordinary.instanceCount = 1;
            ordinary.vertexStreamCount = 0;
            for (int streamIndex = 0; streamIndex < params.vertexStreamCount; ++streamIndex)
            {
                const auto& stream = params.vertexStreams[static_cast<std::size_t>(streamIndex)];
                if (stream.instanceFrequency == 0)
                {
                    ordinary.vertexStreams[static_cast<std::size_t>(ordinary.vertexStreamCount++)] = stream;
                    continue;
                }
                const auto* buffer = static_cast<const D3D11VertexBufferRenderer*>(stream.buffer);
                if (buffer == nullptr)
                    throw System::NotSupportedException("DirectX11 instancing requires a per-instance buffer.");
                const auto& elements = buffer->GetDeclarationEXT().GetElements();
                if (elements.empty())
                {
                    if (stream.strideInBytes != 64)
                        throw System::NotSupportedException(
                            "DirectX11 cannot infer four instance-matrix columns from this stride.");
                    for (int index = 0; index < 4 && columnCount < 4; ++index)
                        columns[static_cast<std::size_t>(columnCount++)] = {&stream, buffer, index * 16};
                }
                else
                {
                    for (const auto& element : elements)
                    {
                        if (columnCount == 4)
                            break;
                        if (element.getVertexElementFormatProperty() !=
                            Microsoft::Xna::Framework::Graphics::VertexElementFormat::Vector4)
                            throw System::NotSupportedException(
                                "DirectX11 instance-matrix columns must be Vector4 elements.");
                        columns[static_cast<std::size_t>(columnCount++)] = {
                            &stream, buffer, element.getOffsetProperty()};
                    }
                }
            }
            if (columnCount != 4 || ordinary.vertexStreamCount == 0)
                throw System::NotSupportedException(
                    "DirectX11 stock instancing requires four instance-matrix columns and a vertex stream.");
            for (int instance = 0; instance < instanceCount; ++instance)
            {
                std::array<float, 16> matrixValues{};
                for (int column = 0; column < 4; ++column)
                {
                    const auto& entry = columns[static_cast<std::size_t>(column)];
                    const int divisor = entry.stream->instanceFrequency;
                    const int record = entry.stream->vertexOffset +
                        (params.firstInstance + instance) / divisor;
                    const int stride = entry.stream->strideInBytes;
                    if (record < 0 || record >= entry.buffer->GetVertexCount() || stride <= 0)
                        throw System::NotSupportedException(
                            "DirectX11 instance-matrix read exceeds the bound vertex buffer.");
                    const std::size_t byteOffset = static_cast<std::size_t>(record) * stride +
                        static_cast<std::size_t>(entry.byteOffset);
                    const auto& bytes = entry.buffer->GetCpuDataEXT();
                    if (byteOffset + sizeof(float) * 4 > bytes.size())
                        throw System::NotSupportedException(
                            "DirectX11 instance-matrix column exceeds the upload shadow.");
                    std::memcpy(matrixValues.data() + column * 4, bytes.data() + byteOffset,
                                sizeof(float) * 4);
                }
                const Matrix instanceWorld(
                    matrixValues[0], matrixValues[1], matrixValues[2], matrixValues[3],
                    matrixValues[4], matrixValues[5], matrixValues[6], matrixValues[7],
                    matrixValues[8], matrixValues[9], matrixValues[10], matrixValues[11],
                    matrixValues[12], matrixValues[13], matrixValues[14], matrixValues[15]);
                DrawPrimitivesExImpl(vb, &ib, instanceWorld * world, view, projection,
                                     primitive, primitiveCount, ordinary);
            }
            return;
        }
        std::vector<Microsoft::Xna::Framework::Graphics::VertexElement> combinedElements;
        std::vector<D3DCommon::D3DVertexInputElement> inputElements;
        BuildVertexInputLayout(params, true, combinedElements, inputElements);
        // The stock shader reads four INSTANCEWORLD columns. CNA's instance-matrix convention
        // assigns them by declaration order, so the public semantics may vary between callers.
        {
            bool instanceColumns[4] = {false, false, false, false};
            for (const auto& input : inputElements)
            {
                const int usageIndex = input.element.getUsageIndexProperty();
                if (input.instanceWorldSemantic &&
                    input.element.getVertexElementUsageProperty() ==
                        Microsoft::Xna::Framework::Graphics::VertexElementUsage::TextureCoordinate &&
                    input.element.getVertexElementFormatProperty() ==
                        Microsoft::Xna::Framework::Graphics::VertexElementFormat::Vector4 &&
                    usageIndex >= 1 && usageIndex <= 4)
                    instanceColumns[usageIndex - 1] = true;
            }
            if (!(instanceColumns[0] && instanceColumns[1] && instanceColumns[2] && instanceColumns[3]))
                throw System::NotSupportedException(
                    "DirectX11Renderer::DrawInstancedPrimitivesEx: the stock instanced effect reads each "
                    "instance's World matrix from four Vector4 columns in the per-instance "
                    "streams, and the bound declarations do not provide all four");
        }
        const bool hasColor = D3DCommon::DeclarationHasElement(
            combinedElements,
            Microsoft::Xna::Framework::Graphics::VertexElementUsage::Color);
        const auto variant = hasColor
            ? D3DCommon::D3DShaderVariant::InstancedColored3d
            : D3DCommon::D3DShaderVariant::Instanced3d;
        ID3D11VertexShader* vs = GetStockVertexShaderEXT(variant);
        ID3D11PixelShader* ps = GetStockPixelShaderEXT(variant);
        if (!vs || !ps)
            throw std::runtime_error("DrawInstancedPrimitivesEx: failed to create instanced3d shader objects");

        const std::size_t combinedStride = CombinedVertexStrideOr(
            params, d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16);
        auto layout = inputLayoutCache_.GetOrCreate(
            device_.Get(), variant, combinedStride, combinedElements, inputElements);
        if (!layout)
            throw std::runtime_error("DrawInstancedPrimitivesEx: failed to create instanced3d input layout");

        // instanced3d.vert.hlsl's PerDraw (b0) is byte-identical to D3DPerDrawConstants -- its
        // first field is named "Vp" (view*projection only, world comes from the per-instance
        // buffer instead) rather than "Mvp", same struct reused for the byte layout only.
        D3DCommon::D3DPerDrawConstants perDraw{};
        // WINCLOSE-0034: the effect's World composes AFTER each instance's own matrix --
        // instanceWorld * World * View * Projection, the order EasyGL, Software and Vulkan apply
        // (InstancedVertexColorTest.EffectWorldComposesAfterTheInstanceWorld). This used to upload
        // View * Projection alone, so an Effect.World set on an instanced draw was ignored.
        const Matrix vp = ApplyXnaPixelCenterEXT(world * view * projection);
        vp.ToColumnMajor(perDraw.Mvp);
        perDraw.DiffuseColor[0] = params.diffuseColor[0];
        perDraw.DiffuseColor[1] = params.diffuseColor[1];
        perDraw.DiffuseColor[2] = params.diffuseColor[2];
        perDraw.DiffuseColor[3] = params.diffuseColor[3];
        perDraw.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;

        ID3D11Buffer* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
        UpdateDynamicConstantBufferEXT(perDrawCB, &perDraw, sizeof(perDraw));

        BindVertexStreams(context_.Get(), d3dVb, params);
        context_->IASetIndexBuffer(d3dIb.GetBufferEXT(), d3dIb.GetFormatEXT(), 0);
        context_->IASetInputLayout(layout.Get());
        context_->IASetPrimitiveTopology(ToD3D11Topology(primitive));
        context_->VSSetShader(vs, nullptr, 0);
        context_->PSSetShader(ps, nullptr, 0);
        context_->VSSetConstantBuffers(0, 1, &perDrawCB);
        context_->PSSetConstantBuffers(0, 1, &perDrawCB);

        const UINT indexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
        // REMED-GFX-123: honor the public startIndex/baseVertex on the instanced path too (both
        // were hardcoded to zero). The per-instance stream's VertexOffset is applied at
        // IASetVertexBuffers; StartInstanceLocation is a separate logical instance index.
        drawWithFirstInstance(indexCount);
    }
}

namespace CNA::Internal::Renderers
{
    // plans/plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace DirectX11 { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> DirectX11::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<DirectX11::DirectX11Renderer>(args);
    }
}
