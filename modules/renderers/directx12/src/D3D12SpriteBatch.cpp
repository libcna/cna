// plans/plan_dx.md Phase DX12 (DX-111/DX-112 follow-up).
#include "CNA/Internal/Renderers/DirectX12/D3D12SpriteBatch.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Textures.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12EffectRenderer.hpp"
#if defined(CNA_DIRECTX12_COMPILED_EFFECTS)
#include "CNA/Internal/Renderers/DirectX12/D3D12CompiledEffect.hpp"
#include "Fna3dStockEffectBlobs.hpp"
#endif
#include "CNA/Internal/Renderers/D3DCommon/D3DShaderCache.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DConstantBuffers.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DFormatMapping.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DStateMapping.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "System/InvalidOperationException.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

namespace CNA::Internal::Renderers::DirectX12
{
    using namespace CNA::Internal::Renderers::D3DCommon;
    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Graphics::Effect;

    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

        /// Same two-concrete-type SRV resolution as DirectX12Renderer.cpp's own (private,
        /// not exported) GetSrvGpuHandleForTextureEXT -- duplicated rather than factored into a
        /// shared header for a few lines of logic, matching D3D11SpriteBatch.cpp's established
        /// precedent of duplicating GetSrvForTextureEXT locally rather than sharing it.
        D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(const ITextureRenderer* tex)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE handle{};
            if (tex == nullptr) return handle;
            if (const auto* t = dynamic_cast<const D3D12TextureRenderer*>(tex))
                return t->GetShaderResourceViewGpuHandleEXT();
            if (const auto* rt = dynamic_cast<const D3D12RenderTargetRenderer*>(tex))
                return rt->GetShaderResourceViewGpuHandleEXT();
            return handle;
        }

    }

    D3D12SpriteBatchRenderer::D3D12SpriteBatchRenderer(DirectX12Renderer* owner)
        : owner_(owner, owner ? owner->GetLifetimeTokenEXT() : std::weak_ptr<void>{},
                 "D3D12SpriteBatchRenderer")
        , device_(owner_->GetDeviceEXT())
        , vb_(owner_.Get(), 256)
        , vb3d_(owner_.Get(), 256)
        , ib_(owner_.Get(), 384, /*thirtyTwoBit=*/false)
    {
        using Microsoft::Xna::Framework::Graphics::VertexDeclaration;
        using Microsoft::Xna::Framework::Graphics::VertexElement;
        using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
        using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
        static const VertexDeclaration kSpriteDeclaration(
            static_cast<int>(sizeof(Sprite2DVertex)),
            {
                VertexElement(static_cast<int>(offsetof(Sprite2DVertex, x)),
                              VertexElementFormat::Vector2, VertexElementUsage::Position, 0),
                VertexElement(static_cast<int>(offsetof(Sprite2DVertex, u)),
                              VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
                VertexElement(static_cast<int>(offsetof(Sprite2DVertex, r)),
                              VertexElementFormat::Vector4, VertexElementUsage::Color, 0),
            });
        vb_.SetVertexDeclaration(kSpriteDeclaration);
        static const VertexDeclaration kSprite3DDeclaration(
            static_cast<int>(sizeof(SpriteVertex)),
            {
                VertexElement(static_cast<int>(offsetof(SpriteVertex, x)),
                              VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
                VertexElement(static_cast<int>(offsetof(SpriteVertex, u)),
                              VertexElementFormat::Vector2,
                              VertexElementUsage::TextureCoordinate, 0),
                VertexElement(static_cast<int>(offsetof(SpriteVertex, r)),
                              VertexElementFormat::Vector4, VertexElementUsage::Color, 0),
            });
        vb3d_.SetVertexDeclaration(kSprite3DDeclaration);
#if defined(CNA_DIRECTX12_COMPILED_EFFECTS)
        const auto& bytes =
            CNA::Internal::Renderers::Fna3d::StockEffectBlobs::kSpriteEffectFxb;
        spriteCompiledEffect_ = std::make_unique<D3D12CompiledEffect>(
            *owner_.Get(), bytes, sizeof(bytes));
        const auto& parameters = spriteCompiledEffect_->GetDescription().parameters;
        const auto matrix = std::find_if(
            parameters.begin(), parameters.end(),
            [](const CompiledEffectParameterDescription& parameter)
            {
                return parameter.name == "MatrixTransform";
            });
        if (matrix == parameters.end())
            throw std::runtime_error(
                "DirectX12 SpriteBatch: embedded SpriteEffect has no MatrixTransform parameter.");
        spriteMatrixParameterIndex_ = matrix->runtimeIndex;
#endif
    }

    D3D12SpriteBatchRenderer::~D3D12SpriteBatchRenderer() = default;

    void D3D12SpriteBatchRenderer::Begin()
    {
        (void) owner_.Get();
        if (begun_) return;
        begun_ = true;
    }

    void D3D12SpriteBatchRenderer::End()
    {
        FlushBatch();
        begun_ = false;
    }

    void D3D12SpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        transform_ = m;
    }

    void D3D12SpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (customEffect_ != effect)
        {
            FlushBatch();
            customEffect_ = effect;
        }
    }

    void D3D12SpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        pendingFilter_ = textureFilter;
    }

    void D3D12SpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        pendingAddressU_ = addressU;
        pendingAddressV_ = addressV;
    }

    void D3D12SpriteBatchRenderer::SetSamplerMipState(int maxMipLevel, float lodBias)
    {
        pendingMaxMipLevel_ = maxMipLevel;
        pendingLodBias_ = lodBias;
    }

    void D3D12SpriteBatchRenderer::SetSamplerAddressW(int addressW)
    {
        pendingAddressW_ = addressW;
    }

    ID3D12RootSignature* D3D12SpriteBatchRenderer::GetOrCreateSprite3DRootSignature()
    {
        ID3D12Device* const device = owner_->GetDeviceEXT();
        if (sprite3DRootSignature_ && sprite3DRootSignatureDevice_ == device)
            return sprite3DRootSignature_.Get();

        // [0] MatrixTransform, b0, vertex stage; [1] ChannelExpansion, b0, pixel stage; [2] t0 and
        // [3] s0 as single-descriptor tables, the shape every other D3D12 draw here binds.
        D3D12_DESCRIPTOR_RANGE1 textureRange{};
        textureRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        textureRange.NumDescriptors = 1;
        textureRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC;
        D3D12_DESCRIPTOR_RANGE1 samplerRange{};
        samplerRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        samplerRange.NumDescriptors = 1;

        D3D12_ROOT_PARAMETER1 parameters[4]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[2].DescriptorTable.NumDescriptorRanges = 1;
        parameters[2].DescriptorTable.pDescriptorRanges = &textureRange;
        parameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[3].DescriptorTable.NumDescriptorRanges = 1;
        parameters[3].DescriptorTable.pDescriptorRanges = &samplerRange;
        parameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC desc{};
        desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        desc.Desc_1_1.NumParameters = static_cast<UINT>(std::size(parameters));
        desc.Desc_1_1.pParameters = parameters;
        desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ComPtr<ID3DBlob> serialized;
        ComPtr<ID3DBlob> error;
        HRESULT hr = D3D12SerializeVersionedRootSignature(&desc, serialized.GetAddressOf(),
                                                          error.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error(
                "D3D12SpriteBatchRenderer: sprite3d root signature serialization failed, hr=" +
                FormatHr(hr) + (error ? std::string(": ") +
                    std::string(static_cast<const char*>(error->GetBufferPointer()),
                                error->GetBufferSize()) : std::string()));
        ComPtr<ID3D12RootSignature> rootSignature;
        hr = device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
                                         IID_PPV_ARGS(rootSignature.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error(
                "D3D12SpriteBatchRenderer: sprite3d CreateRootSignature failed, hr=" + FormatHr(hr));
        sprite3DRootSignature_ = std::move(rootSignature);
        sprite3DRootSignatureDevice_ = device;
        return sprite3DRootSignature_.Get();
    }

    const std::vector<D3D12SpriteBatchRenderer::Sprite2DVertex>&
    D3D12SpriteBatchRenderer::TransformedSprite2DVertices()
    {
        // Exactly what Draw() used to push for these paths: the affine part of the Begin transform,
        // applied in pixel space. They bind a float2 position; layerDepth and W were never theirs.
        transformedVertices_.clear();
        transformedVertices_.reserve(pendingVertices_.size());
        for (const SpriteVertex& vertex : pendingVertices_)
        {
            const Vector2 position = Vector2::Transform(Vector2(vertex.x, vertex.y), transform_);
            transformedVertices_.push_back(
                {position.X, position.Y, vertex.u, vertex.v, vertex.r, vertex.g, vertex.b, vertex.a});
        }
        return transformedVertices_;
    }

    void D3D12SpriteBatchRenderer::FlushBatch()
    {
        if (pendingVertices_.empty()) return;

#if defined(CNA_DIRECTX12_COMPILED_EFFECTS)
        if (customEffect_ != nullptr && customEffect_->GetCompiledRuntimePtr() != nullptr)
        {
            FlushBatchWithCompiledEffect();
            return;
        }
#endif

        if (!owner_->HasBoundColorTargetEXT())
        {
            throw std::runtime_error(
                "D3D12SpriteBatchRenderer::FlushBatch: no off-screen color target bound -- "
                "BindOffscreenColorTargetEXT (plans/plan_dx.md DX-111's own scaffolding; a real D3D12 swap "
                "chain back buffer is unusable under this machine's Wine+vkd3d-proton dev loop, DX-100)");
        }

        const int targetW = owner_->GetBoundColorWidthEXT();
        const int targetH = owner_->GetBoundColorHeightEXT();

        // REMED-GFX-072: the sprite2d ViewportSize projection basis (the pixel->NDC divisor) must be
        // the ACTIVE GraphicsDevice.Viewport's width/height, not the full bound-target size. XNA/FNA
        // build the SpriteBatch ortho from Viewport.Width/Height (CreateOrthographicOffCenter(0,
        // Viewport.Width, Viewport.Height, 0)), so a custom sub-Viewport makes sprite coordinates
        // VIEWPORT-LOCAL. This mirrors D3D11SpriteBatchRenderer, which reads the live viewport size via
        // RSGetViewports. GetEffectiveViewportEXT() (REMED-GFX-064) already returns the custom
        // sub-region when a Viewport was set (else the full target), and it also drives the rasterizer
        // viewport below (RSSetViewports) -- previously only the rasterizer honored it while the
        // projection stayed full-target, squishing the sprite into the sub-region. The default
        // full-target viewport keeps vpW/vpH == the target size, byte-identical to the prior behavior.
        const D3D12_VIEWPORT effectiveViewport = owner_->GetEffectiveViewportEXT();
        float logicalViewportWidth = 0.0f;
        float logicalViewportHeight = 0.0f;
        owner_->GetSpriteViewportSizeEXT(logicalViewportWidth, logicalViewportHeight);
        const int vpW = static_cast<int>(std::lround(logicalViewportWidth));
        const int vpH = static_cast<int>(std::lround(logicalViewportHeight));

        D3D12EffectRenderer* customRenderer = nullptr;
        if (customEffect_)
            customRenderer = dynamic_cast<D3D12EffectRenderer*>(customEffect_->GetEffectRendererPtr());

        ID3D12RootSignature* rootSignature = nullptr;
        ID3D12PipelineState* pso = nullptr;
        float matrixValues[16]{};
        float channelExpansion[8]{};
        bool useStockConstants = false;
        int constantBufferCount = 1;
        int shaderResourceCount = 1;
        int samplerCount = 1;

        if (customRenderer && customRenderer->IsValid())
        {
            customRenderer->SetViewportSizeEXT(static_cast<float>(vpW), static_cast<float>(vpH));
            customEffect_->Apply();

            using Microsoft::Xna::Framework::Graphics::VertexElement;
            using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
            using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
            D3D12PipelineStateDesc state;
            owner_->FillPsoStateFromCurrentEXT(state);
            state.strideInBytes = sizeof(Sprite2DVertex);
            state.vertexInputElements = {
                {VertexElement(0, VertexElementFormat::Vector2,
                               VertexElementUsage::Position, 0), 0, 0, false},
                {VertexElement(8, VertexElementFormat::Vector2,
                               VertexElementUsage::TextureCoordinate, 0), 0, 0, false},
                {VertexElement(16, VertexElementFormat::Vector4,
                               VertexElementUsage::Color, 0), 0, 0, false},
            };
            state.topologyType = static_cast<int>(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
            // Preserve the established SpriteBatch rasterizer contract while the custom shader
            // uses the same blend/depth/stencil state as the stock sprite PSO.
            state.cullMode = 0;
            state.fillMode = 0;
            state.depthBias = 0;
            state.slopeScaleDepthBias = 0.0f;
            pso = customRenderer->GetOrCreatePipelineStateEXT(std::move(state));
            rootSignature = customRenderer->GetRootSignatureEXT();
            constantBufferCount = customRenderer->GetConstantBufferCountEXT();
            shaderResourceCount = customRenderer->GetShaderResourceCountEXT();
            samplerCount = customRenderer->GetSamplerCountEXT();
            if (!pso || !rootSignature)
                throw std::runtime_error(
                    "D3D12SpriteBatchRenderer: custom ShaderEffect PSO creation failed");
        }
        else
        {
            // plans/plan_directx12_parity.md DX12-0013 (DirectX11's WINCLOSE-0014/0019): the stock
            // stage is FNA's SpriteEffect. sprite2d wrote z = 0 and w = 1 from CPU-transformed
            // pixel positions, so layerDepth never reached the depth test and a Begin transform lost
            // its depth, W and perspective; its pixel stage also sampled a one- or two-channel
            // texture the Direct3D 10+ way. The pipeline state now comes from the renderer's shared
            // cache with the device's whole tracked state -- rasterizer included, so a SpriteBatch
            // culls, fills and biases as the RasterizerState passed to Begin asks, as on DirectX11.
            constantBufferCount = 2;
            rootSignature = GetOrCreateSprite3DRootSignature();
            D3D12PipelineStateDesc state;
            owner_->FillPsoStateFromCurrentEXT(state);
            state.variant = D3DShaderVariant::Sprite3d;
            state.strideInBytes = sizeof(SpriteVertex);
            using Microsoft::Xna::Framework::Graphics::VertexElement;
            using Microsoft::Xna::Framework::Graphics::VertexElementFormat;
            using Microsoft::Xna::Framework::Graphics::VertexElementUsage;
            state.vertexInputElements = {
                {VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0), 0, 0, false},
                {VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0), 0, 0, false},
                {VertexElement(20, VertexElementFormat::Vector4, VertexElementUsage::Color, 0), 0, 0, false},
            };
            state.topologyType = static_cast<int>(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
            const auto cached = owner_->psoCache_.GetOrCreate(owner_->GetDeviceEXT(), rootSignature, state);
            pso = cached.Get();
            if (!pso)
                throw std::runtime_error(
                    "D3D12SpriteBatchRenderer: CreateGraphicsPipelineState failed for sprite3d");

            // SpriteBatch.cs: MatrixTransform = transformMatrix * ortho(0, w, h, 0, 0, -1), over the
            // logical viewport size. ToColumnMajor gives the flat layout the row_major field reads.
            const Matrix matrixTransform = transform_ * Matrix::CreateOrthographicOffCenter(
                0.0f, static_cast<float>(vpW), static_cast<float>(vpH), 0.0f, 0.0f, -1.0f);
            matrixTransform.ToColumnMajor(matrixValues);
            D3DCommon::D3D9ChannelExpansion(currentTexture_ != nullptr
                                                ? currentTexture_->GetSurfaceFormatEXT() : 0,
                                            channelExpansion, channelExpansion + 4);
            useStockConstants = true;
        }

        // SpriteBatch replaces both transient streams on every flush. Discard gives each update a
        // fresh range in DX-238's frame-owned upload ring without a submit or fence wait.
        D3D12VertexBufferRenderer& vertexBuffer = useStockConstants ? vb3d_ : vb_;
        if (useStockConstants)
        {
            vb3d_.SetDataWithOptions(pendingVertices_.data(), static_cast<int>(pendingVertices_.size()),
                                     sizeof(SpriteVertex), SetDataOptions::Discard);
        }
        else
        {
            const auto& vertices = TransformedSprite2DVertices();
            vb_.SetDataWithOptions(vertices.data(), static_cast<int>(vertices.size()),
                                   sizeof(Sprite2DVertex), SetDataOptions::Discard);
        }
        ib_.SetData16WithOptions(pendingIndices_.data(), static_cast<int>(pendingIndices_.size()),
                                 SetDataOptions::Discard);

        ID3D12GraphicsCommandList* cmdList = owner_->GetFrameCommandListEXT();
        owner_->RetainFrameObjectEXT(vertexBuffer.GetResourceEXT());
        owner_->RetainFrameObjectEXT(ib_.GetResourceEXT());
        owner_->RetainFrameObjectEXT(rootSignature);
        owner_->RetainFrameObjectEXT(pso);
        if (const auto* texture = dynamic_cast<const D3D12TextureRenderer*>(currentTexture_))
            owner_->RetainFrameObjectEXT(texture->GetResourceEXT());
        else if (const auto* target = dynamic_cast<const D3D12RenderTargetRenderer*>(currentTexture_))
            owner_->RetainFrameObjectEXT(target->GetSampleableColorResourceEXT());

        const D3D12_GPU_VIRTUAL_ADDRESS matrixAddress = useStockConstants
            ? owner_->AllocateFrameConstantDataEXT(matrixValues, sizeof(matrixValues)) : 0;
        const D3D12_GPU_VIRTUAL_ADDRESS channelExpansionAddress = useStockConstants
            ? owner_->AllocateFrameConstantDataEXT(channelExpansion, sizeof(channelExpansion)) : 0;

        owner_->TransitionAndBindRenderTargetsEXT(cmdList);

        // REMED-GFX-064: honor a custom GraphicsDevice.Viewport for sprite draws (the GPU viewport
        // rectangle). REMED-GFX-072: the SAME effective viewport now also drives the ViewportSize
        // projection basis above, so sprite coordinates are viewport-relative.
        // plans/plan_dx.md DX-201: the scissor is the device's effective one, not a hardcoded full-target
        // rectangle. SpriteBatch::Begin(..., RasterizerState with ScissorTestEnable) is the ordinary
        // XNA way to clip a HUD or a pane, and D3D11's sprite path gets the same clipping from its
        // persistent RSSetScissorRects state; hardcoding the full target here is what made it a
        // no-op on D3D12. When no scissor is set, or the test is off, GetEffectiveScissorEXT()
        // returns exactly the full-target rectangle this replaces.
        D3D12_RECT scissor = owner_->GetEffectiveScissorEXT();
        (void)targetW; (void)targetH;
        cmdList->RSSetViewports(1, &effectiveViewport);
        cmdList->RSSetScissorRects(1, &scissor);

        cmdList->SetGraphicsRootSignature(rootSignature);
        cmdList->SetPipelineState(pso);
        // plans/plan_dx.md DX-204: SpriteBatch::Begin(..., BlendState) can select Blend::BlendFactor just as
        // a 3D draw can, so the sprite path records the same device constant.
        cmdList->OMSetBlendFactor(owner_->GetBlendFactorEXT());
        // plans/plan_dx.md DX-210/DX-203: and the stencil reference, for the same reason -- without it a
        // stencil-gated sprite compares against 0 rather than the game's ReferenceStencil, so a
        // stencil-EQUAL mask rejects the whole sprite instead of clipping it. Found exactly that way.
        cmdList->OMSetStencilRef(static_cast<UINT>(owner_->GetReferenceStencilEXT()));
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        D3D12_VERTEX_BUFFER_VIEW vbView = vertexBuffer.GetViewEXT();
        cmdList->IASetVertexBuffers(0, 1, &vbView);
        D3D12_INDEX_BUFFER_VIEW ibView = ib_.GetViewEXT();
        cmdList->IASetIndexBuffer(&ibView);

        if (customRenderer)
        {
            for (int slot = 0; slot < constantBufferCount; ++slot)
            {
                const D3D12_GPU_VIRTUAL_ADDRESS address =
                    customRenderer->GetConstantBufferGpuAddressEXT(slot);
                if (address != 0)
                    cmdList->SetGraphicsRootConstantBufferView(
                        static_cast<UINT>(slot), address);
            }
        }
        else
        {
            cmdList->SetGraphicsRootConstantBufferView(0, matrixAddress);
            cmdList->SetGraphicsRootConstantBufferView(1, channelExpansionAddress);
        }

        // DX-133: pendingFilter_/pendingAddressU_/pendingAddressV_ (set via SetSamplerFilter()/
        // SetSamplerAddressMode(), i.e. the SamplerState passed to SpriteBatch::Begin()) now
        // genuinely drive slot 0's real sampler descriptor via DX-119's dynamic sampler system --
        // mirrors D3D11SpriteBatchRenderer's own exact ApplySamplerState() call, same placement
        // (right before the sampler handle is read below). Before this task, SpriteBatch draws
        // silently used whatever slot 0's sampler happened to already be (or DirectX12Renderer's
        // own pre-DX-119 LINEAR/WRAP fallback if nothing had ever called ApplySamplerState) instead
        // of the SamplerState the game's own SpriteBatch::Begin() call actually requested.
        owner_->ApplySamplerState(0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);
        owner_->ApplySamplerMipState(0, pendingMaxMipLevel_, pendingLodBias_);
        owner_->ApplySamplerAddressW(0, pendingAddressW_);
        std::array<D3D12_GPU_DESCRIPTOR_HANDLE,
                   D3DCommon::D3DProgramReflection::kMaxSamplers> samplerHandles{};
        for (int slot = 0; slot < samplerCount; ++slot)
            (void) owner_->GetSamplerGpuHandleEXT(slot);
        // Allocating a later descriptor may grow and replace the shader-visible heap. Resolve
        // every handle only after all requested descriptors exist in the final heap.
        for (int slot = 0; slot < samplerCount; ++slot)
            samplerHandles[static_cast<std::size_t>(slot)] = owner_->GetSamplerGpuHandleEXT(slot);

        ID3D12DescriptorHeap* heaps[2]{};
        UINT heapCount = 0;
        if (shaderResourceCount > 0)
            heaps[heapCount++] = owner_->GetCbvSrvUavHeapEXT();
        if (samplerCount > 0)
            heaps[heapCount++] = owner_->GetSamplerHeapEXT();
        if (heapCount > 0)
            cmdList->SetDescriptorHeaps(heapCount, heaps);

        for (int slot = 0; slot < shaderResourceCount; ++slot)
        {
            D3D12_GPU_DESCRIPTOR_HANDLE textureHandle{};
            if (customRenderer && customRenderer->HasTextureBindingEXT(slot))
                textureHandle = customRenderer->GetTextureGpuHandleEXT(slot);
            else if (slot == 0)
                textureHandle = GetSrvGpuHandle(currentTexture_);
            if (textureHandle.ptr != 0)
                cmdList->SetGraphicsRootDescriptorTable(
                    static_cast<UINT>(constantBufferCount + slot), textureHandle);
        }
        for (int slot = 0; slot < samplerCount; ++slot)
        {
            const auto handle = samplerHandles[static_cast<std::size_t>(slot)];
            if (handle.ptr != 0)
                cmdList->SetGraphicsRootDescriptorTable(
                    static_cast<UINT>(constantBufferCount + shaderResourceCount + slot), handle);
        }

        cmdList->DrawIndexedInstanced(static_cast<UINT>(pendingIndices_.size()), 1, 0, 0, 0);

        pendingVertices_.clear();
        pendingIndices_.clear();
        currentTexture_ = nullptr;
    }

#if defined(CNA_DIRECTX12_COMPILED_EFFECTS)
    void D3D12SpriteBatchRenderer::ApplyCompiledSpriteVertexShader(
        float viewportWidth, float viewportHeight)
    {
        if (!spriteCompiledEffect_ || customEffect_ == nullptr ||
            viewportWidth <= 0.0f || viewportHeight <= 0.0f)
            throw std::runtime_error(
                "DirectX12 SpriteBatch: compiled stock vertex effect is unavailable.");
        const Matrix projection = Matrix::CreateOrthographicOffCenter(
            0.0f, viewportWidth, viewportHeight, 0.0f, 0.0f, -1.0f);
        float values[16];
        projection.ToColumnMajor(values);
        spriteCompiledEffect_->SetParameterValue(
            spriteMatrixParameterIndex_, values, sizeof(values));
        spriteCompiledEffect_->SetTechnique(0);

        auto& graphicsDevice = customEffect_->getGraphicsDeviceInternal();
        CompiledEffectDeviceState state;
        state.blend = &graphicsDevice.getBlendStateProperty();
        state.depthStencil = &graphicsDevice.getDepthStencilStateProperty();
        state.rasterizer = &graphicsDevice.getRasterizerStateProperty();
        state.samplerStates = &graphicsDevice.getSamplerStatesProperty();
        state.vertexSamplerStates = &graphicsDevice.getVertexSamplerStatesProperty();
        CompiledEffectPassStateChanges ignored;
        spriteCompiledEffect_->ApplyPass(0, state, ignored);
    }

    void D3D12SpriteBatchRenderer::FlushBatchWithCompiledEffect()
    {
        ICompiledEffectRuntime* runtime = customEffect_->GetCompiledRuntimePtr();
        if (runtime == nullptr || currentTexture_ == nullptr)
            throw std::runtime_error(
                "DirectX12 SpriteBatch: compiled-effect batch state is incomplete.");
        if (!owner_->HasBoundColorTargetEXT())
            throw std::runtime_error(
                "DirectX12 SpriteBatch: no render target is bound.");

        const auto& transformedVertices = TransformedSprite2DVertices();
        vb_.SetDataWithOptions(
            transformedVertices.data(), static_cast<int>(transformedVertices.size()),
            sizeof(Sprite2DVertex), SetDataOptions::Discard);
        ib_.SetData16WithOptions(
            pendingIndices_.data(), static_cast<int>(pendingIndices_.size()),
            SetDataOptions::Discard);

        float viewportWidth = 0.0f;
        float viewportHeight = 0.0f;
        owner_->GetSpriteViewportSizeEXT(viewportWidth, viewportHeight);
        owner_->ApplySamplerState(
            0, pendingFilter_, pendingAddressU_, pendingAddressV_, 1);
        owner_->ApplySamplerMipState(0, pendingMaxMipLevel_, pendingLodBias_);
        owner_->ApplySamplerAddressW(0, pendingAddressW_);

        auto* technique = customEffect_->getCurrentTechniqueProperty();
        const int passCount = technique != nullptr
            ? technique->getPassesProperty().getCountProperty() : 0;
        if (passCount == 0)
            throw System::InvalidOperationException(
                "DirectX12 SpriteBatch: compiled Effect needs a technique with a pass.");

        ApplyCompiledSpriteVertexShader(viewportWidth, viewportHeight);
        const auto& textures =
            customEffect_->getGraphicsDeviceInternal().getTexturesProperty();
        GpuDrawParams params;
        for (int pass = 0; pass < passCount; ++pass)
        {
            technique->getPassesProperty()[pass].Apply();
            owner_->RecordCompiledEffectDrawEXT(
                vb_, &ib_, PrimitiveType::TriangleList,
                static_cast<int>(pendingIndices_.size() / 3), 1, params, *runtime,
                currentTexture_, &textures);
        }

        pendingVertices_.clear();
        pendingIndices_.clear();
        currentTexture_ = nullptr;
    }
#endif

    void D3D12SpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const int w = texture.GetWidth();
        const int h = texture.GetHeight();
        Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), w, h), Rectangle(0, 0, w, h),
             Color::White);
    }

    void D3D12SpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0), SpriteEffects::None, 0.0f);
    }

    void D3D12SpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                       const Rectangle& destinationRectangle,
                                       const Rectangle& sourceRectangle,
                                       const Color& color,
                                       float rotation,
                                       const Vector2& origin,
                                       SpriteEffects effects,
                                       float layerDepth)
    {
        if (!begun_) throw std::runtime_error("D3D12SpriteBatchRenderer::Draw called before Begin()");

        // DX12-0013 (WINCLOSE-0014): a batch is flushed on a texture change and also before a quad a
        // 16-bit index could no longer address -- the index base was narrowed to uint16_t
        // unconditionally, so past 65 536 vertices the indices wrapped onto the first quads.
        constexpr std::size_t kMaxBatchVertices = 65536u;
        if (currentTexture_ != nullptr &&
            (currentTexture_ != &texture || pendingVertices_.size() + 4u > kMaxBatchVertices))
            FlushBatch();
        currentTexture_ = &texture;

        const float texW = static_cast<float>(texture.GetWidth());
        const float texH = static_cast<float>(texture.GetHeight());

        // No [0,1] clamp -- matches FNA (SpriteBatch.cs divides straight through), same convention
        // D3D11SpriteBatchRenderer::Draw already established.
        // In the float domain, as FNA does: X + Width in int overflows (undefined behaviour) for a
        // source rectangle near INT_MAX.
        float u1 = static_cast<float>(sourceRectangle.X) / texW;
        float v1 = static_cast<float>(sourceRectangle.Y) / texH;
        float u2 = u1 + static_cast<float>(sourceRectangle.Width)  / texW;
        float v2 = v1 + static_cast<float>(sourceRectangle.Height) / texH;

        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) std::swap(u1, u2);
        if (static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) std::swap(v1, v2);

        const float r = static_cast<float>(color.getRProperty()) / 255.0f;
        const float g = static_cast<float>(color.getGProperty()) / 255.0f;
        const float b = static_cast<float>(color.getBProperty()) / 255.0f;
        const float a = static_cast<float>(color.getAProperty()) / 255.0f;

        const float dx = static_cast<float>(destinationRectangle.X);
        const float dy = static_cast<float>(destinationRectangle.Y);
        const float dw = static_cast<float>(destinationRectangle.Width);
        const float dh = static_cast<float>(destinationRectangle.Height);

        const float sw = static_cast<float>(sourceRectangle.Width);
        const float sh = static_cast<float>(sourceRectangle.Height);

        const float ox = origin.X;
        const float oy = origin.Y;

        const float scaleX = dw / sw;
        const float scaleY = dh / sh;

        const float p0x = (0.0f - ox) * scaleX, p0y = (0.0f - oy) * scaleY;
        const float p1x = (sw   - ox) * scaleX, p1y = (0.0f - oy) * scaleY;
        const float p2x = (sw   - ox) * scaleX, p2y = (sh   - oy) * scaleY;
        const float p3x = (0.0f - ox) * scaleX, p3y = (sh   - oy) * scaleY;

        const float cosR = std::cos(rotation);
        const float sinR = std::sin(rotation);

        auto rotateAndTranslate = [&](float px, float py, float& rx, float& ry)
        {
            rx = dx + px * cosR - py * sinR;
            ry = dy + px * sinR + py * cosR;
        };

        float v0x, v0y, v1x, v1y, v2x, v2y, v3x, v3y;
        rotateAndTranslate(p0x, p0y, v0x, v0y);
        rotateAndTranslate(p1x, p1y, v1x, v1y);
        rotateAndTranslate(p2x, p2y, v2x, v2y);
        rotateAndTranslate(p3x, p3y, v3x, v3y);

        // DX12-0013: untransformed, with layerDepth as z. The stock stage applies the Begin transform
        // on the GPU; the 2D paths get it applied on the CPU at flush (TransformedSprite2DVertices).
        const auto base = static_cast<uint16_t>(pendingVertices_.size());

        pendingVertices_.push_back({v0x, v0y, layerDepth, u1, v1, r, g, b, a});
        pendingVertices_.push_back({v1x, v1y, layerDepth, u2, v1, r, g, b, a});
        pendingVertices_.push_back({v2x, v2y, layerDepth, u2, v2, r, g, b, a});
        pendingVertices_.push_back({v3x, v3y, layerDepth, u1, v2, r, g, b, a});

        pendingIndices_.push_back(base + 0);
        pendingIndices_.push_back(base + 1);
        pendingIndices_.push_back(base + 2);
        pendingIndices_.push_back(base + 2);
        pendingIndices_.push_back(base + 3);
        pendingIndices_.push_back(base + 0);
    }
}
