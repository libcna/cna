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

        D3D12_PRIMITIVE_TOPOLOGY_TYPE kSpriteTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }

    D3D12SpriteBatchRenderer::D3D12SpriteBatchRenderer(DirectX12Renderer* owner)
        : owner_(owner, owner ? owner->GetLifetimeTokenEXT() : std::weak_ptr<void>{},
                 "D3D12SpriteBatchRenderer")
        , device_(owner_->GetDeviceEXT())
        , vb_(owner_.Get(), 256)
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

    ID3D12PipelineState* D3D12SpriteBatchRenderer::GetOrCreateSprite2DPso(ID3D12RootSignature* rootSig)
    {
        // plans/plan_dx.md DX-210: the whole tracked pipeline state, not a hand-copied subset of it. This
        // used to list blend and write-mask fields only, which is why the depth and stencil halves
        // were silently absent from both the key AND the descriptor below.
        D3D12PipelineStateDesc state;
        owner_->FillPsoStateFromCurrentEXT(state);
        state.variant = D3DShaderVariant::Sprite2d;
        state.strideInBytes = sizeof(Sprite2DVertex);
        state.topologyType = static_cast<int>(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        const SpritePsoKey key = state.AsCacheKeyEXT();
        if (const auto it = sprite2DPsos_.find(key); it != sprite2DPsos_.end())
            return it->second.Get();

        const uint8_t* vsBytes = nullptr; std::size_t vsSize = 0;
        const uint8_t* psBytes = nullptr; std::size_t psSize = 0;
        GetVertexShaderBytecode(D3DShaderVariant::Sprite2d, vsBytes, vsSize);
        GetPixelShaderBytecode(D3DShaderVariant::Sprite2d, psBytes, psSize);
        if (!vsBytes || !psBytes)
            throw std::runtime_error("D3D12SpriteBatchRenderer: missing sprite2d DXBC bytecode");

        // Fixed Sprite2DVertex contract -- deliberately NOT resolved via
        // D3DVertexFormatHelper::InputElementsForStrideD3D12(32, ...), which would incorrectly
        // return VertexPositionNormalTexture's layout (this file's own header comment explains the
        // real stride-32 collision this sidesteps, matching D3D11's own DX-70 precedent).
        static const D3D12_INPUT_ELEMENT_DESC kElements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 8,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rootSig;
        desc.VS = {vsBytes, vsSize};
        desc.PS = {psBytes, psSize};
        desc.InputLayout = {kElements, static_cast<UINT>(std::size(kElements))};
        desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
        desc.PrimitiveTopologyType = kSpriteTopologyType;
        desc.SampleMask = state.sampleMask;
        desc.SampleDesc.Count = state.sampleCount; // plans/plan_dx.md DX-207
        desc.NodeMask = 0;

        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.FrontCounterClockwise = FALSE;
        desc.RasterizerState.DepthClipEnable = TRUE;

        const bool colorOpaque = owner_->currentColorSrcBlend_ == 0 && owner_->currentColorDstBlend_ == 1;
        const bool alphaOpaque = owner_->currentAlphaSrcBlend_ == 0 && owner_->currentAlphaDstBlend_ == 1;
        desc.BlendState.IndependentBlendEnable =
            (state.colorWriteMasks[1] != state.colorWriteMasks[0]
             || state.colorWriteMasks[2] != state.colorWriteMasks[0]
             || state.colorWriteMasks[3] != state.colorWriteMasks[0]) ? TRUE : FALSE;
        D3D12_RENDER_TARGET_BLEND_DESC rtTemplate{};
        rtTemplate.BlendEnable = !(colorOpaque && alphaOpaque);
        rtTemplate.SrcBlend = static_cast<D3D12_BLEND>(BlendToD3D11(owner_->currentColorSrcBlend_));
        rtTemplate.DestBlend = static_cast<D3D12_BLEND>(BlendToD3D11(owner_->currentColorDstBlend_));
        rtTemplate.BlendOp = static_cast<D3D12_BLEND_OP>(BlendFunctionToD3D11(owner_->currentColorBlendFunc_));
        rtTemplate.SrcBlendAlpha = static_cast<D3D12_BLEND>(BlendToD3D11(owner_->currentAlphaSrcBlend_));
        rtTemplate.DestBlendAlpha = static_cast<D3D12_BLEND>(BlendToD3D11(owner_->currentAlphaDstBlend_));
        rtTemplate.BlendOpAlpha = static_cast<D3D12_BLEND_OP>(BlendFunctionToD3D11(owner_->currentAlphaBlendFunc_));
        rtTemplate.LogicOpEnable = FALSE;
        rtTemplate.LogicOp = D3D12_LOGIC_OP_NOOP;
        for (std::size_t i = 0; i < std::size(desc.BlendState.RenderTarget); ++i)
        {
            desc.BlendState.RenderTarget[i] = rtTemplate;
            desc.BlendState.RenderTarget[i].RenderTargetWriteMask =
                static_cast<UINT8>(state.colorWriteMasks[std::min<std::size_t>(i, 3)] & 0xF);
        }
        desc.SampleMask = owner_->currentSampleMask_;

        // plans/plan_dx.md DX-210: the device's real DepthStencilState, not a hardcoded "off". XNA's
        // SpriteBatch::Begin takes a DepthStencilState, and SpriteSortMode::FrontToBack with
        // DepthStencilState::Default plus stencil-masked sprite UI are ordinary uses of it; both
        // silently lost depth and stencil here. D3D11's sprite path issues no depth state of its
        // own, so whatever GraphicsDevice last applied is in force -- this reaches the same place by
        // building the pipeline state from the same tracked fields. SpriteBatch's own default is
        // DepthStencilState::None, which GraphicsDevice applies before the first sprite draw, so a
        // batch that does not ask for depth still gets none.
        D3D12_DEPTH_STENCIL_DESC& sds = desc.DepthStencilState;
        sds.DepthEnable = state.depthEnable ? TRUE : FALSE;
        sds.DepthWriteMask = state.depthWriteEnable ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        sds.DepthFunc = static_cast<D3D12_COMPARISON_FUNC>(CompareFunctionToD3D11(state.depthFunc));
        sds.StencilEnable = state.stencilEnable ? TRUE : FALSE;
        sds.StencilReadMask = static_cast<UINT8>(state.stencilMask);
        sds.StencilWriteMask = static_cast<UINT8>(state.stencilWriteMask);
        sds.FrontFace.StencilFunc = static_cast<D3D12_COMPARISON_FUNC>(CompareFunctionToD3D11(state.stencilFunc));
        sds.FrontFace.StencilPassOp = static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(state.stencilPass));
        sds.FrontFace.StencilFailOp = static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(state.stencilFail));
        sds.FrontFace.StencilDepthFailOp =
            static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(state.stencilDepthFail));
        if (state.twoSidedStencilMode)
        {
            sds.BackFace.StencilFunc = static_cast<D3D12_COMPARISON_FUNC>(CompareFunctionToD3D11(state.ccwStencilFunc));
            sds.BackFace.StencilPassOp = static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(state.ccwStencilPass));
            sds.BackFace.StencilFailOp = static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(state.ccwStencilFail));
            sds.BackFace.StencilDepthFailOp =
                static_cast<D3D12_STENCIL_OP>(StencilOperationToD3D11(state.ccwStencilDepthFail));
        }
        else
        {
            sds.BackFace = sds.FrontFace;
        }

        desc.NumRenderTargets = std::min<UINT>(
            state.renderTargetCount, D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT);
        for (UINT i = 0; i < desc.NumRenderTargets; ++i)
            desc.RTVFormats[i] = state.renderTargetFormats[i];
        // DX-210: a pipeline state that uses depth or stencil must name the format of the view it
        // will be used with; DXGI_FORMAT_UNKNOWN here is legal only when neither is enabled.
        desc.DSVFormat = state.depthStencilFormat;

        ComPtr<ID3D12PipelineState> pso;
        HRESULT hr = device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(pso.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12SpriteBatchRenderer: CreateGraphicsPipelineState failed, hr=" + FormatHr(hr));
        ID3D12PipelineState* result = pso.Get();
        sprite2DPsos_.emplace(key, std::move(pso));
        return result;
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

        ComPtr<ID3D12RootSignature> stockRootSignature;
        ID3D12RootSignature* rootSignature = nullptr;
        ID3D12PipelineState* pso = nullptr;
        D3DSprite2DConstants stockConstants{};
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
            stockRootSignature = owner_->GetRootSignatureCacheEXT().GetOrCreate(
                device_.Get(), 1, 1, 1);
            rootSignature = stockRootSignature.Get();
            if (!rootSignature)
                throw std::runtime_error(
                    "D3D12SpriteBatchRenderer: failed to create sprite2d root signature");
            pso = GetOrCreateSprite2DPso(rootSignature);
            stockConstants.ViewportSize[0] = static_cast<float>(vpW);
            stockConstants.ViewportSize[1] = static_cast<float>(vpH);
            useStockConstants = true;
        }

        // SpriteBatch replaces both transient streams on every flush. Discard gives each update a
        // fresh range in DX-238's frame-owned upload ring without a submit or fence wait.
        vb_.SetDataWithOptions(pendingVertices_.data(), static_cast<int>(pendingVertices_.size()),
                               sizeof(Sprite2DVertex), SetDataOptions::Discard);
        ib_.SetData16WithOptions(pendingIndices_.data(), static_cast<int>(pendingIndices_.size()),
                                 SetDataOptions::Discard);

        ID3D12GraphicsCommandList* cmdList = owner_->GetFrameCommandListEXT();
        owner_->RetainFrameObjectEXT(vb_.GetResourceEXT());
        owner_->RetainFrameObjectEXT(ib_.GetResourceEXT());
        owner_->RetainFrameObjectEXT(rootSignature);
        owner_->RetainFrameObjectEXT(pso);
        if (const auto* texture = dynamic_cast<const D3D12TextureRenderer*>(currentTexture_))
            owner_->RetainFrameObjectEXT(texture->GetResourceEXT());
        else if (const auto* target = dynamic_cast<const D3D12RenderTargetRenderer*>(currentTexture_))
            owner_->RetainFrameObjectEXT(target->GetSampleableColorResourceEXT());

        const D3D12_GPU_VIRTUAL_ADDRESS stockConstantAddress = useStockConstants
            ? owner_->AllocateFrameConstantDataEXT(&stockConstants, sizeof(stockConstants)) : 0;

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

        D3D12_VERTEX_BUFFER_VIEW vbView = vb_.GetViewEXT();
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
            cmdList->SetGraphicsRootConstantBufferView(0, stockConstantAddress);
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

        vb_.SetDataWithOptions(
            pendingVertices_.data(), static_cast<int>(pendingVertices_.size()),
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
                                       float /*layerDepth*/)
    {
        if (!begun_) throw std::runtime_error("D3D12SpriteBatchRenderer::Draw called before Begin()");

        if (currentTexture_ != nullptr && currentTexture_ != &texture)
            FlushBatch();
        currentTexture_ = &texture;

        const float texW = static_cast<float>(texture.GetWidth());
        const float texH = static_cast<float>(texture.GetHeight());

        // No [0,1] clamp -- matches FNA (SpriteBatch.cs divides straight through), same convention
        // D3D11SpriteBatchRenderer::Draw already established.
        float u1 = static_cast<float>(sourceRectangle.X) / texW;
        float v1 = static_cast<float>(sourceRectangle.Y) / texH;
        float u2 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width)  / texW;
        float v2 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / texH;

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

        // SpriteBatch's own transform matrix is applied here, in pixel space, before upload -- same
        // reasoning D3D11SpriteBatchRenderer's own header comment already documents (sprite2d.vert
        // .hlsl's real contract has no projection-matrix uniform to fold it into GPU-side).
        const Vector2 tv0 = Vector2::Transform(Vector2(v0x, v0y), transform_);
        const Vector2 tv1 = Vector2::Transform(Vector2(v1x, v1y), transform_);
        const Vector2 tv2 = Vector2::Transform(Vector2(v2x, v2y), transform_);
        const Vector2 tv3 = Vector2::Transform(Vector2(v3x, v3y), transform_);

        const auto base = static_cast<uint16_t>(pendingVertices_.size());

        pendingVertices_.push_back({tv0.X, tv0.Y, u1, v1, r, g, b, a});
        pendingVertices_.push_back({tv1.X, tv1.Y, u2, v1, r, g, b, a});
        pendingVertices_.push_back({tv2.X, tv2.Y, u2, v2, r, g, b, a});
        pendingVertices_.push_back({tv3.X, tv3.Y, u1, v2, r, g, b, a});

        pendingIndices_.push_back(base + 0);
        pendingIndices_.push_back(base + 1);
        pendingIndices_.push_back(base + 2);
        pendingIndices_.push_back(base + 2);
        pendingIndices_.push_back(base + 3);
        pendingIndices_.push_back(base + 0);
    }
}
