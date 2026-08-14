// plan_d3d10.md: real Direct3D 10 graphics renderer implementation. See the header's own class-level
// doc comment for the architecture summary; design decisions are recorded in plan_d3d10.md itself.
#include "CNA/Internal/Renderers/DirectX10/DirectX10Renderer.hpp"

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"

#include <windows.h>
#include <d3d10.h>
#include <d3d10misc.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::DirectX10
{
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector2;
    using Microsoft::Xna::Framework::Vector4;
    using Microsoft::Xna::Framework::Graphics::Blend;
    using Microsoft::Xna::Framework::Graphics::CompareFunction;
    using Microsoft::Xna::Framework::Graphics::CullMode;
    using Microsoft::Xna::Framework::Graphics::FillMode;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Graphics::SpriteEffects;
    using Microsoft::Xna::Framework::Graphics::StencilOperation;
    using Microsoft::Xna::Framework::Graphics::TextureAddressMode;
    using Microsoft::Xna::Framework::Graphics::TextureFilter;

    namespace
    {
        [[noreturn]] void ThrowHr(const char* what, HRESULT hr)
        {
            throw std::runtime_error(std::string(what) + " failed: HRESULT=0x" +
                                      std::to_string(static_cast<unsigned long>(hr)));
        }

        // ---- raw XNA int -> real D3D10 enum mapping. Numerically identical to D3D11's own
        // D3DCommon::*ToD3D11 mapping (D3D11 inherited D3D10's state-object model verbatim) --
        // re-expressed against D3D10_* types since this renderer deliberately stays self-contained
        // (plan_d3d10.md: D3DCommon's own headers reference ID3D11 types directly, not reusable
        // as-is without adaptation).
        D3D10_BLEND BlendToD3D10(int blend)
        {
            switch (static_cast<Blend>(blend))
            {
                case Blend::One:                     return D3D10_BLEND_ONE;
                case Blend::Zero:                     return D3D10_BLEND_ZERO;
                case Blend::SourceColor:             return D3D10_BLEND_SRC_COLOR;
                case Blend::InverseSourceColor:      return D3D10_BLEND_INV_SRC_COLOR;
                case Blend::SourceAlpha:             return D3D10_BLEND_SRC_ALPHA;
                case Blend::InverseSourceAlpha:      return D3D10_BLEND_INV_SRC_ALPHA;
                case Blend::DestinationColor:        return D3D10_BLEND_DEST_COLOR;
                case Blend::InverseDestinationColor: return D3D10_BLEND_INV_DEST_COLOR;
                case Blend::DestinationAlpha:        return D3D10_BLEND_DEST_ALPHA;
                case Blend::InverseDestinationAlpha: return D3D10_BLEND_INV_DEST_ALPHA;
                case Blend::BlendFactor:             return D3D10_BLEND_BLEND_FACTOR;
                case Blend::InverseBlendFactor:      return D3D10_BLEND_INV_BLEND_FACTOR;
                case Blend::SourceAlphaSaturation:   return D3D10_BLEND_SRC_ALPHA_SAT;
                default:                               return D3D10_BLEND_ONE;
            }
        }

        D3D10_COMPARISON_FUNC CompareFunctionToD3D10(int compareFunction)
        {
            switch (static_cast<CompareFunction>(compareFunction))
            {
                case CompareFunction::Always:        return D3D10_COMPARISON_ALWAYS;
                case CompareFunction::Never:         return D3D10_COMPARISON_NEVER;
                case CompareFunction::Less:          return D3D10_COMPARISON_LESS;
                case CompareFunction::LessEqual:     return D3D10_COMPARISON_LESS_EQUAL;
                case CompareFunction::Equal:         return D3D10_COMPARISON_EQUAL;
                case CompareFunction::GreaterEqual:  return D3D10_COMPARISON_GREATER_EQUAL;
                case CompareFunction::Greater:       return D3D10_COMPARISON_GREATER;
                case CompareFunction::NotEqual:      return D3D10_COMPARISON_NOT_EQUAL;
                default:                               return D3D10_COMPARISON_ALWAYS;
            }
        }

        D3D10_STENCIL_OP StencilOperationToD3D10(int stencilOperation)
        {
            switch (static_cast<StencilOperation>(stencilOperation))
            {
                case StencilOperation::Keep:                return D3D10_STENCIL_OP_KEEP;
                case StencilOperation::Zero:                 return D3D10_STENCIL_OP_ZERO;
                case StencilOperation::Replace:              return D3D10_STENCIL_OP_REPLACE;
                case StencilOperation::Increment:            return D3D10_STENCIL_OP_INCR;
                case StencilOperation::Decrement:             return D3D10_STENCIL_OP_DECR;
                case StencilOperation::IncrementSaturation:  return D3D10_STENCIL_OP_INCR_SAT;
                case StencilOperation::DecrementSaturation:  return D3D10_STENCIL_OP_DECR_SAT;
                case StencilOperation::Invert:               return D3D10_STENCIL_OP_INVERT;
                default:                                        return D3D10_STENCIL_OP_KEEP;
            }
        }

        // See D3DCommon::CullModeToD3D11's own doc: FrontCounterClockwise=FALSE (D3D10's default,
        // matching D3D's native clockwise-is-front convention) is assumed here too.
        D3D10_CULL_MODE CullModeToD3D10(int cullMode)
        {
            switch (static_cast<CullMode>(cullMode))
            {
                case CullMode::None:                     return D3D10_CULL_NONE;
                case CullMode::CullClockwiseFace:        return D3D10_CULL_FRONT;
                case CullMode::CullCounterClockwiseFace: return D3D10_CULL_BACK;
                default:                                   return D3D10_CULL_NONE;
            }
        }

        D3D10_FILL_MODE FillModeToD3D10(int fillMode)
        {
            switch (static_cast<FillMode>(fillMode))
            {
                case FillMode::Solid:      return D3D10_FILL_SOLID;
                case FillMode::WireFrame:  return D3D10_FILL_WIREFRAME;
                default:                     return D3D10_FILL_SOLID;
            }
        }

        D3D10_TEXTURE_ADDRESS_MODE TextureAddressModeToD3D10(int addressMode)
        {
            switch (static_cast<TextureAddressMode>(addressMode))
            {
                case TextureAddressMode::Wrap:   return D3D10_TEXTURE_ADDRESS_WRAP;
                case TextureAddressMode::Clamp:  return D3D10_TEXTURE_ADDRESS_CLAMP;
                case TextureAddressMode::Mirror: return D3D10_TEXTURE_ADDRESS_MIRROR;
                default:                           return D3D10_TEXTURE_ADDRESS_WRAP;
            }
        }

        D3D10_FILTER TextureFilterToD3D10(int textureFilter)
        {
            switch (static_cast<TextureFilter>(textureFilter))
            {
                case TextureFilter::Linear:                      return D3D10_FILTER_MIN_MAG_MIP_LINEAR;
                case TextureFilter::Point:                       return D3D10_FILTER_MIN_MAG_MIP_POINT;
                case TextureFilter::Anisotropic:                 return D3D10_FILTER_ANISOTROPIC;
                case TextureFilter::LinearMipPoint:               return D3D10_FILTER_MIN_MAG_LINEAR_MIP_POINT;
                case TextureFilter::PointMipLinear:               return D3D10_FILTER_MIN_MAG_POINT_MIP_LINEAR;
                case TextureFilter::MinLinearMagPointMipLinear:  return D3D10_FILTER_MIN_LINEAR_MAG_POINT_MIP_LINEAR;
                case TextureFilter::MinLinearMagPointMipPoint:   return D3D10_FILTER_MIN_LINEAR_MAG_MIP_POINT;
                case TextureFilter::MinPointMagLinearMipLinear:  return D3D10_FILTER_MIN_POINT_MAG_MIP_LINEAR;
                case TextureFilter::MinPointMagLinearMipPoint:   return D3D10_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT;
                default:                                           return D3D10_FILTER_MIN_MAG_MIP_LINEAR;
            }
        }

        // ---- real HLSL shaders (D3DCompile, vs_4_0/ps_4_0 -- D3D10's own shader-model ceiling) ----
        // Matrix convention (matches D3DCommon's own colored3d.vert.hlsl precedent, plan_dx.md
        // design decision 14): cbuffer matrices are row_major so XNA's row-major Matrix uploads
        // unchanged (Matrix::ToColumnMajor, despite its name, flattens M11..M44 in row-major
        // order -- see that method's own body). `mul(v, M)` is the row-vector convention used
        // consistently by every HLSL shader in this project.
        constexpr const char* kColored3DShaderSrc =
            "cbuffer PerDraw : register(b0)\n"
            "{\n"
            "    row_major float4x4 Mvp;\n"
            "};\n"
            "struct VSInput { float3 Position : POSITION0; float4 Color : COLOR0; };\n"
            "struct VSOutput { float4 Position : SV_POSITION; float4 Color : COLOR0; };\n"
            "VSOutput VSMain(VSInput input)\n"
            "{\n"
            "    VSOutput output;\n"
            "    output.Position = mul(float4(input.Position, 1.0), Mvp);\n"
            "    output.Color = input.Color;\n"
            "    return output;\n"
            "}\n"
            "float4 PSMain(VSOutput input) : SV_TARGET\n"
            "{\n"
            "    return input.Color;\n"
            "}\n";

        // SpriteBatch: screen-pixel-space position (NOT NDC) + color + uv. No half-texel
        // correction needed (unlike D3D8/D3D9's own pre-D3D10 pixel-center convention) -- D3D10
        // places texel/pixel centers at pixel CENTERS like every modern API.
        constexpr const char* kSpriteBatchShaderSrc =
            "cbuffer PerFrame : register(b0)\n"
            "{\n"
            "    row_major float4x4 Projection;\n"
            "};\n"
            "struct VSInput { float3 Position : POSITION0; float4 Color : COLOR0; float2 UV : TEXCOORD0; };\n"
            "struct VSOutput { float4 Position : SV_POSITION; float4 Color : COLOR0; float2 UV : TEXCOORD0; };\n"
            "VSOutput VSMain(VSInput input)\n"
            "{\n"
            "    VSOutput output;\n"
            "    output.Position = mul(float4(input.Position, 1.0), Projection);\n"
            "    output.Color = input.Color;\n"
            "    output.UV = input.UV;\n"
            "    return output;\n"
            "}\n"
            "Texture2D tex0 : register(t0);\n"
            "SamplerState samp0 : register(s0);\n"
            "float4 PSMain(VSOutput input) : SV_TARGET\n"
            "{\n"
            "    return tex0.Sample(samp0, input.UV) * input.Color;\n"
            "}\n";

        struct CompiledShader
        {
            ID3D10VertexShader* vs = nullptr;
            ID3D10PixelShader* ps = nullptr;
            ID3D10InputLayout* layout = nullptr;
        };

        void CompileShaderPair(ID3D10Device* device, const char* src, std::size_t srcLen,
                                const D3D10_INPUT_ELEMENT_DESC* elements, UINT elementCount,
                                CompiledShader& out)
        {
            ID3DBlob* vsBlob = nullptr; ID3DBlob* vsErr = nullptr;
            HRESULT hr = D3DCompile(src, srcLen, "d3d10_shader", nullptr, nullptr,
                                     "VSMain", "vs_4_0", 0, 0, &vsBlob, &vsErr);
            if (FAILED(hr))
            {
                std::string msg = "D3DCompile(vs_4_0) failed";
                if (vsErr) { msg += ": "; msg += static_cast<const char*>(vsErr->GetBufferPointer()); vsErr->Release(); }
                throw std::runtime_error(msg);
            }
            ID3DBlob* psBlob = nullptr; ID3DBlob* psErr = nullptr;
            hr = D3DCompile(src, srcLen, "d3d10_shader", nullptr, nullptr,
                             "PSMain", "ps_4_0", 0, 0, &psBlob, &psErr);
            if (FAILED(hr))
            {
                vsBlob->Release();
                std::string msg = "D3DCompile(ps_4_0) failed";
                if (psErr) { msg += ": "; msg += static_cast<const char*>(psErr->GetBufferPointer()); psErr->Release(); }
                throw std::runtime_error(msg);
            }

            hr = device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &out.vs);
            if (FAILED(hr)) { vsBlob->Release(); psBlob->Release(); ThrowHr("ID3D10Device::CreateVertexShader", hr); }
            hr = device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), &out.ps);
            if (FAILED(hr)) { vsBlob->Release(); psBlob->Release(); ThrowHr("ID3D10Device::CreatePixelShader", hr); }
            hr = device->CreateInputLayout(elements, elementCount, vsBlob->GetBufferPointer(),
                                            vsBlob->GetBufferSize(), &out.layout);
            vsBlob->Release();
            psBlob->Release();
            if (FAILED(hr)) ThrowHr("ID3D10Device::CreateInputLayout", hr);
        }

        int VertexCountForPrimitives(PrimitiveType primitive, int primitiveCount)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList:  return primitiveCount * 3;
                case PrimitiveType::TriangleStrip: return primitiveCount + 2;
                case PrimitiveType::LineList:       return primitiveCount * 2;
                case PrimitiveType::LineStrip:      return primitiveCount + 1;
                case PrimitiveType::PointListEXT:   return primitiveCount;
            }
            throw std::runtime_error(
                "DirectX10 renderer does not support the requested PrimitiveType value");
        }

        D3D10_PRIMITIVE_TOPOLOGY ToD3D10Topology(PrimitiveType primitive)
        {
            switch (primitive)
            {
                case PrimitiveType::TriangleList:  return D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
                case PrimitiveType::TriangleStrip: return D3D10_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
                case PrimitiveType::LineList:       return D3D10_PRIMITIVE_TOPOLOGY_LINELIST;
                case PrimitiveType::LineStrip:      return D3D10_PRIMITIVE_TOPOLOGY_LINESTRIP;
                case PrimitiveType::PointListEXT:   return D3D10_PRIMITIVE_TOPOLOGY_POINTLIST;
            }
            throw std::runtime_error(
                "DirectX10 renderer does not support the requested PrimitiveType value");
        }
    }

    // Pure-virtual accessor letting SpriteBatch/3D-draw code sample either a plain texture or a
    // render target polymorphically (mirrors DIRECTX8's own DirectX8TextureOwner pattern).
    class D3D10TextureOwner
    {
    public:
        virtual ~D3D10TextureOwner() = default;
        [[nodiscard]] virtual ID3D10ShaderResourceView* SRV() const = 0;
    };

    // ---- Textures and render targets: real ID3D10Texture2D resources ----

    class D3D10TextureRenderer final : public ITextureRenderer, public D3D10TextureOwner
    {
    public:
        D3D10TextureRenderer(ID3D10Device* device, const ImageData& data)
            : device_(device), width_(data.width), height_(data.height)
        {
            D3D10_TEXTURE2D_DESC desc{};
            desc.Width = static_cast<UINT>(width_);
            desc.Height = static_cast<UINT>(height_);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D10_USAGE_DEFAULT;
            desc.BindFlags = D3D10_BIND_SHADER_RESOURCE;

            D3D10_SUBRESOURCE_DATA init{};
            init.pSysMem = data.pixels.empty() ? nullptr : data.pixels.data();
            init.SysMemPitch = static_cast<UINT>(width_) * 4;

            const HRESULT hr = device_->CreateTexture2D(&desc, data.pixels.empty() ? nullptr : &init, &texture_);
            if (FAILED(hr)) ThrowHr("ID3D10Device::CreateTexture2D", hr);
            CreateSRV();
        }
        ~D3D10TextureRenderer() override
        {
            if (srv_) srv_->Release();
            if (texture_) texture_->Release();
        }

        int GetWidth() const override { return width_; }
        int GetHeight() const override { return height_; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void UpdatePixels(const uint8_t* rgba, int stride) override
        {
            const UINT rowPitch = stride > 0 ? static_cast<UINT>(stride) : static_cast<UINT>(width_) * 4;
            device_->UpdateSubresource(texture_, 0, nullptr, rgba, rowPitch, 0);
        }

        [[nodiscard]] bool GetData(int /*level*/, int x, int y, int w, int h, void* data, int /*dataLength*/) const override
        {
            if (w <= 0 || h <= 0) return true;
            D3D10_TEXTURE2D_DESC stagingDesc{};
            stagingDesc.Width = static_cast<UINT>(width_);
            stagingDesc.Height = static_cast<UINT>(height_);
            stagingDesc.MipLevels = 1;
            stagingDesc.ArraySize = 1;
            stagingDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            stagingDesc.SampleDesc.Count = 1;
            stagingDesc.Usage = D3D10_USAGE_STAGING;
            stagingDesc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
            ID3D10Texture2D* staging = nullptr;
            HRESULT hr = device_->CreateTexture2D(&stagingDesc, nullptr, &staging);
            if (FAILED(hr)) ThrowHr("ID3D10Device::CreateTexture2D(staging)", hr);
            device_->CopyResource(staging, texture_);
            D3D10_MAPPED_TEXTURE2D mapped{};
            hr = staging->Map(0, D3D10_MAP_READ, 0, &mapped);
            if (FAILED(hr)) { staging->Release(); ThrowHr("ID3D10Texture2D::Map(READ)", hr); }
            auto* src = static_cast<const uint8_t*>(mapped.pData);
            auto* dst = static_cast<uint8_t*>(data);
            const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
            for (int row = 0; row < h; ++row)
            {
                std::memcpy(dst + static_cast<std::size_t>(row) * rowBytes,
                            src + static_cast<std::size_t>(y + row) * mapped.RowPitch + static_cast<std::size_t>(x) * 4,
                            rowBytes);
            }
            staging->Unmap(0);
            staging->Release();
            return true;
        }

        [[nodiscard]] ID3D10ShaderResourceView* SRV() const override { return srv_; }

    private:
        void CreateSRV()
        {
            const HRESULT hr = device_->CreateShaderResourceView(texture_, nullptr, &srv_);
            if (FAILED(hr)) ThrowHr("ID3D10Device::CreateShaderResourceView", hr);
        }

        ID3D10Device* device_;
        int width_, height_;
        ID3D10Texture2D* texture_ = nullptr;
        ID3D10ShaderResourceView* srv_ = nullptr;
    };

    class D3D10RenderTargetRenderer final : public IRenderTargetRenderer, public D3D10TextureOwner
    {
    public:
        D3D10RenderTargetRenderer(ID3D10Device* device, int w, int h, bool wantDepthStencil)
            : device_(device), width_(w), height_(h)
        {
            D3D10_TEXTURE2D_DESC desc{};
            desc.Width = static_cast<UINT>(w);
            desc.Height = static_cast<UINT>(h);
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D10_USAGE_DEFAULT;
            desc.BindFlags = D3D10_BIND_RENDER_TARGET | D3D10_BIND_SHADER_RESOURCE;
            HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &texture_);
            if (FAILED(hr)) ThrowHr("ID3D10Device::CreateTexture2D(render target)", hr);
            hr = device_->CreateRenderTargetView(texture_, nullptr, &rtv_);
            if (FAILED(hr)) { texture_->Release(); ThrowHr("ID3D10Device::CreateRenderTargetView", hr); }
            hr = device_->CreateShaderResourceView(texture_, nullptr, &srv_);
            if (FAILED(hr)) { rtv_->Release(); texture_->Release(); ThrowHr("ID3D10Device::CreateShaderResourceView", hr); }

            if (wantDepthStencil)
            {
                D3D10_TEXTURE2D_DESC dsDesc{};
                dsDesc.Width = static_cast<UINT>(w);
                dsDesc.Height = static_cast<UINT>(h);
                dsDesc.MipLevels = 1;
                dsDesc.ArraySize = 1;
                dsDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
                dsDesc.SampleDesc.Count = 1;
                dsDesc.Usage = D3D10_USAGE_DEFAULT;
                dsDesc.BindFlags = D3D10_BIND_DEPTH_STENCIL;
                hr = device_->CreateTexture2D(&dsDesc, nullptr, &depthTexture_);
                if (FAILED(hr)) ThrowHr("ID3D10Device::CreateTexture2D(rt depth-stencil)", hr);
                hr = device_->CreateDepthStencilView(depthTexture_, nullptr, &dsv_);
                if (FAILED(hr)) ThrowHr("ID3D10Device::CreateDepthStencilView(rt)", hr);
            }
        }
        ~D3D10RenderTargetRenderer() override
        {
            if (dsv_) dsv_->Release();
            if (depthTexture_) depthTexture_->Release();
            if (srv_) srv_->Release();
            if (rtv_) rtv_->Release();
            if (texture_) texture_->Release();
        }

        int GetWidth() const override { return width_; }
        int GetHeight() const override { return height_; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t*, int) override {}

        [[nodiscard]] bool GetData(int /*level*/, int x, int y, int w, int h, void* data, int /*dataLength*/) const override
        {
            if (w <= 0 || h <= 0) return true;
            D3D10_TEXTURE2D_DESC stagingDesc{};
            stagingDesc.Width = static_cast<UINT>(width_);
            stagingDesc.Height = static_cast<UINT>(height_);
            stagingDesc.MipLevels = 1;
            stagingDesc.ArraySize = 1;
            stagingDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            stagingDesc.SampleDesc.Count = 1;
            stagingDesc.Usage = D3D10_USAGE_STAGING;
            stagingDesc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
            ID3D10Texture2D* staging = nullptr;
            HRESULT hr = device_->CreateTexture2D(&stagingDesc, nullptr, &staging);
            if (FAILED(hr)) ThrowHr("ID3D10Device::CreateTexture2D(rt staging)", hr);
            device_->CopyResource(staging, texture_);
            D3D10_MAPPED_TEXTURE2D mapped{};
            hr = staging->Map(0, D3D10_MAP_READ, 0, &mapped);
            if (FAILED(hr)) { staging->Release(); ThrowHr("ID3D10Texture2D::Map(READ, rt)", hr); }
            auto* src = static_cast<const uint8_t*>(mapped.pData);
            auto* dst = static_cast<uint8_t*>(data);
            const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
            for (int row = 0; row < h; ++row)
            {
                std::memcpy(dst + static_cast<std::size_t>(row) * rowBytes,
                            src + static_cast<std::size_t>(y + row) * mapped.RowPitch + static_cast<std::size_t>(x) * 4,
                            rowBytes);
            }
            staging->Unmap(0);
            staging->Release();
            return true;
        }

        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        {
            return depthFormatWasRequested && dsv_ != nullptr;
        }

        void BindAsRenderTarget() override {}   // Actual binding happens in SetRenderTarget2D.
        void UnbindAsRenderTarget() override {}

        [[nodiscard]] ID3D10ShaderResourceView* SRV() const override { return srv_; }
        [[nodiscard]] ID3D10RenderTargetView* RTV() const { return rtv_; }
        [[nodiscard]] ID3D10DepthStencilView* DSV() const { return dsv_; }

    private:
        ID3D10Device* device_;
        int width_, height_;
        ID3D10Texture2D* texture_ = nullptr;
        ID3D10RenderTargetView* rtv_ = nullptr;
        ID3D10ShaderResourceView* srv_ = nullptr;
        ID3D10Texture2D* depthTexture_ = nullptr;
        ID3D10DepthStencilView* dsv_ = nullptr;
    };

    class D3D10VertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        D3D10VertexBufferRenderer(ID3D10Device* device, int vertexCapacity)
            : device_(device), capacity_(vertexCapacity)
        {
        }
        ~D3D10VertexBufferRenderer() override { if (buffer_) buffer_->Release(); }

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override
        {
            stride_ = stride_in_bytes;
            count_ = vertex_count;
            const UINT byteWidth = static_cast<UINT>(stride_in_bytes * static_cast<std::size_t>(std::max(vertex_count, 1)));
            if (buffer_ && byteWidth > capacityBytes_)
            {
                buffer_->Release();
                buffer_ = nullptr;
            }
            if (!buffer_)
            {
                D3D10_BUFFER_DESC desc{};
                desc.Usage = D3D10_USAGE_DEFAULT;
                desc.ByteWidth = byteWidth;
                desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
                const HRESULT hr = device_->CreateBuffer(&desc, nullptr, &buffer_);
                if (FAILED(hr)) ThrowHr("ID3D10Device::CreateBuffer(vertex)", hr);
                capacityBytes_ = byteWidth;
            }
            if (vertex_count > 0)
                device_->UpdateSubresource(buffer_, 0, nullptr, data, 0, 0);
        }

        [[nodiscard]] int GetVertexCount() const override { return count_; }
        [[nodiscard]] ID3D10Buffer* GetBufferEXT() const { return buffer_; }
        [[nodiscard]] std::size_t GetStrideEXT() const { return stride_; }

        // REMED-GFX-DECL-GUARD: the draw routes infer attribute byte offsets from the stride
        // alone (REMED-GFX-217), so the declaration is remembered rather than discarded and the
        // Draw*Ex routes refuse one those offsets would silently reinterpret.
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override
        {
            declaration_.Remember(vertexDeclaration);
        }
        /// The declaration this buffer carries, for REMED-GFX-DECL-GUARD's fidelity check.
        [[nodiscard]] const CNA::Internal::Graphics::DeclaredVertexLayout& Declaration() const
        {
            return declaration_;
        }

    private:
        ID3D10Device* device_;
        CNA::Internal::Graphics::DeclaredVertexLayout declaration_;
        int capacity_;
        ID3D10Buffer* buffer_ = nullptr;
        UINT capacityBytes_ = 0;
        int count_ = 0;
        std::size_t stride_ = 0;
    };

    class D3D10IndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        D3D10IndexBufferRenderer(ID3D10Device* device, bool thirtyTwoBit)
            : device_(device), thirtyTwoBit_(thirtyTwoBit)
        {
        }
        ~D3D10IndexBufferRenderer() override { if (buffer_) buffer_->Release(); }

        void SetData16(const void* data, int index_count) override { SetDataImpl(data, index_count, 2); }
        void SetData32(const void* data, int index_count) override { SetDataImpl(data, index_count, 4); }

        [[nodiscard]] int GetIndexCount() const override { return count_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }
        [[nodiscard]] ID3D10Buffer* GetBufferEXT() const { return buffer_; }

    private:
        void SetDataImpl(const void* data, int index_count, int elemSize)
        {
            count_ = index_count;
            const UINT byteWidth = static_cast<UINT>(elemSize) * static_cast<UINT>(std::max(index_count, 1));
            if (buffer_ && byteWidth > capacityBytes_)
            {
                buffer_->Release();
                buffer_ = nullptr;
            }
            if (!buffer_)
            {
                D3D10_BUFFER_DESC desc{};
                desc.Usage = D3D10_USAGE_DEFAULT;
                desc.ByteWidth = byteWidth;
                desc.BindFlags = D3D10_BIND_INDEX_BUFFER;
                const HRESULT hr = device_->CreateBuffer(&desc, nullptr, &buffer_);
                if (FAILED(hr)) ThrowHr("ID3D10Device::CreateBuffer(index)", hr);
                capacityBytes_ = byteWidth;
            }
            if (index_count > 0)
                device_->UpdateSubresource(buffer_, 0, nullptr, data, 0, 0);
        }

        ID3D10Device* device_;
        bool thirtyTwoBit_;
        ID3D10Buffer* buffer_ = nullptr;
        UINT capacityBytes_ = 0;
        int count_ = 0;
    };

    struct SpriteVertex { float x, y, z; float r, g, b, a; float u, v; };

    class D3D10SpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        // viewportWidthPtr/viewportHeightPtr point at the owning DirectX10Renderer::Impl's own
        // currentTargetWidth/Height fields (updated live by SetRenderTarget2D/SetRenderTargets),
        // read at FlushBatch() time -- avoids needing Impl (private) visible here.
        D3D10SpriteBatchRenderer(ID3D10Device* device, const int* viewportWidthPtr, const int* viewportHeightPtr)
            : device_(device), viewportWidthPtr_(viewportWidthPtr), viewportHeightPtr_(viewportHeightPtr)
        {
            static const D3D10_INPUT_ELEMENT_DESC elements[] = {
                { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
                { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0 },
                { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D10_INPUT_PER_VERTEX_DATA, 0 },
            };
            CompileShaderPair(device_, kSpriteBatchShaderSrc, std::strlen(kSpriteBatchShaderSrc),
                               elements, 3, shader_);

            D3D10_BUFFER_DESC cbDesc{};
            cbDesc.Usage = D3D10_USAGE_DEFAULT;
            cbDesc.ByteWidth = 64; // one row_major float4x4
            cbDesc.BindFlags = D3D10_BIND_CONSTANT_BUFFER;
            HRESULT hr = device_->CreateBuffer(&cbDesc, nullptr, &constantBuffer_);
            if (FAILED(hr)) ThrowHr("ID3D10Device::CreateBuffer(sprite PerFrame)", hr);

            D3D10_SAMPLER_DESC sampDesc{};
            sampDesc.Filter = D3D10_FILTER_MIN_MAG_MIP_LINEAR;
            sampDesc.AddressU = D3D10_TEXTURE_ADDRESS_CLAMP;
            sampDesc.AddressV = D3D10_TEXTURE_ADDRESS_CLAMP;
            sampDesc.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
            sampDesc.ComparisonFunc = D3D10_COMPARISON_NEVER;
            sampDesc.MaxLOD = D3D10_FLOAT32_MAX;
            hr = device_->CreateSamplerState(&sampDesc, &sampler_);
            if (FAILED(hr)) ThrowHr("ID3D10Device::CreateSamplerState(sprite default)", hr);

            // Real bug found via CTest: D3D10's default rasterizer state culls back-facing
            // triangles (D3D10_CULL_BACK). A sprite rotated by ~180 degrees flips its screen-space
            // winding order and gets silently culled unless this renderer explicitly disables
            // culling for its own 2D quads -- 2D sprites must never be culled by orientation.
            D3D10_RASTERIZER_DESC rastDesc{};
            rastDesc.CullMode = D3D10_CULL_NONE;
            rastDesc.FillMode = D3D10_FILL_SOLID;
            rastDesc.DepthClipEnable = TRUE;
            hr = device_->CreateRasterizerState(&rastDesc, &cullNoneState_);
            if (FAILED(hr)) ThrowHr("ID3D10Device::CreateRasterizerState(sprite cull-none)", hr);
        }
        ~D3D10SpriteBatchRenderer() override
        {
            if (cullNoneState_) cullNoneState_->Release();
            if (sampler_) sampler_->Release();
            if (constantBuffer_) constantBuffer_->Release();
            if (vertexBuffer_) vertexBuffer_->Release();
            if (indexBuffer_) indexBuffer_->Release();
            if (shader_.layout) shader_.layout->Release();
            if (shader_.ps) shader_.ps->Release();
            if (shader_.vs) shader_.vs->Release();
        }

        void Begin() override
        {
            if (begun_) throw std::runtime_error("D3D10SpriteBatchRenderer::Begin: already begun");
            begun_ = true;
            pendingVertices_.clear();
            pendingIndices_.clear();
            currentTexture_ = nullptr;
            // Real bug found via CTest (not present in DIRECTX8's own Begin(), which this was modeled
            // on): SpriteBatch::Begin()'s own public call order is SetTransformMatrix() THEN
            // Begin() (SpriteBatch.cpp) -- resetting transformMatrix_ here would silently discard
            // whatever the caller's Begin(..., transformMatrix) overload just set.
        }
        void End() override
        {
            if (!begun_) throw std::runtime_error("D3D10SpriteBatchRenderer::End: End() called before Begin()");
            FlushBatch();
            begun_ = false;
        }
        void SetTransformMatrix(const Matrix& m) override { transformMatrix_ = m; }
        void SetSamplerFilter(int textureFilter) override { filter_ = textureFilter; samplerDirty_ = true; }
        void SetSamplerAddressMode(int addressU, int addressV) override
        {
            addressU_ = addressU; addressV_ = addressV; samplerDirty_ = true;
        }

        void Draw(const ITextureRenderer& texture, float x, float y) override
        {
            Draw(texture, Rectangle(static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()),
                 Rectangle(0, 0, texture.GetWidth(), texture.GetHeight()), Microsoft::Xna::Framework::Color(255, 255, 255, 255),
                 0.0f, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);
        }
        void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Microsoft::Xna::Framework::Color& color) override
        {
            Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0.0f, 0.0f),
                 SpriteEffects::None, 0.0f);
        }
        void Draw(const ITextureRenderer& texture, const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle, const Microsoft::Xna::Framework::Color& color, float rotation,
                  const Vector2& origin, SpriteEffects effects, float layerDepth) override
        {
            if (!begun_) throw std::runtime_error("D3D10SpriteBatchRenderer::Draw: Draw() called before Begin()");

            const auto* owner = dynamic_cast<const D3D10TextureOwner*>(&texture);
            if (!owner)
                throw std::runtime_error(
                    "D3D10SpriteBatchRenderer::Draw: texture renderer is not a D3D10 texture");

            if (currentTexture_ != nullptr && currentTexture_ != &texture) FlushBatch();
            currentTexture_ = &texture;

            const int texW = std::max(1, texture.GetWidth());
            const int texH = std::max(1, texture.GetHeight());
            float u1 = static_cast<float>(sourceRectangle.X) / static_cast<float>(texW);
            float v1 = static_cast<float>(sourceRectangle.Y) / static_cast<float>(texH);
            float u2 = static_cast<float>(sourceRectangle.X + sourceRectangle.Width) / static_cast<float>(texW);
            float v2 = static_cast<float>(sourceRectangle.Y + sourceRectangle.Height) / static_cast<float>(texH);
            if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0) std::swap(u1, u2);
            if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0) std::swap(v1, v2);

            const float dx = static_cast<float>(destinationRectangle.X);
            const float dy = static_cast<float>(destinationRectangle.Y);
            const float dw = static_cast<float>(destinationRectangle.Width);
            const float dh = static_cast<float>(destinationRectangle.Height);
            const float sw = static_cast<float>(std::max(1, sourceRectangle.Width));
            const float sh = static_cast<float>(std::max(1, sourceRectangle.Height));
            const float scaleX = dw / sw;
            const float scaleY = dh / sh;
            const float ox = origin.X, oy = origin.Y;

            const float p0x = (0.0f - ox) * scaleX, p0y = (0.0f - oy) * scaleY;
            const float p1x = (sw - ox) * scaleX,   p1y = (0.0f - oy) * scaleY;
            const float p2x = (sw - ox) * scaleX,   p2y = (sh - oy) * scaleY;
            const float p3x = (0.0f - ox) * scaleX, p3y = (sh - oy) * scaleY;

            const float cosR = std::cos(rotation);
            const float sinR = std::sin(rotation);

            const auto placeCorner = [&](float px, float py) -> Vector2
            {
                const float rx = dx + px * cosR - py * sinR;
                const float ry = dy + px * sinR + py * cosR;
                const Vector4 t = Vector4::Transform(Microsoft::Xna::Framework::Vector3(rx, ry, 0.0f), transformMatrix_);
                return Vector2(t.X, t.Y);
            };

            const Vector2 c0 = placeCorner(p0x, p0y);
            const Vector2 c1 = placeCorner(p1x, p1y);
            const Vector2 c2 = placeCorner(p2x, p2y);
            const Vector2 c3 = placeCorner(p3x, p3y);

            const float r = color.getRProperty() / 255.0f;
            const float g = color.getGProperty() / 255.0f;
            const float b = color.getBProperty() / 255.0f;
            const float a = color.getAProperty() / 255.0f;

            const auto baseIdx = static_cast<uint16_t>(pendingVertices_.size());
            pendingVertices_.push_back({c0.X, c0.Y, layerDepth, r, g, b, a, u1, v1});
            pendingVertices_.push_back({c1.X, c1.Y, layerDepth, r, g, b, a, u2, v1});
            pendingVertices_.push_back({c2.X, c2.Y, layerDepth, r, g, b, a, u2, v2});
            pendingVertices_.push_back({c3.X, c3.Y, layerDepth, r, g, b, a, u1, v2});
            pendingIndices_.push_back(baseIdx + 0); pendingIndices_.push_back(baseIdx + 1); pendingIndices_.push_back(baseIdx + 2);
            pendingIndices_.push_back(baseIdx + 0); pendingIndices_.push_back(baseIdx + 2); pendingIndices_.push_back(baseIdx + 3);
        }

    private:
        void EnsureBuffers(std::size_t vertexBytes, std::size_t indexBytes)
        {
            if (!vertexBuffer_ || vertexBytes > vertexCapacityBytes_)
            {
                if (vertexBuffer_) vertexBuffer_->Release();
                D3D10_BUFFER_DESC desc{};
                desc.Usage = D3D10_USAGE_DEFAULT;
                desc.ByteWidth = static_cast<UINT>(vertexBytes);
                desc.BindFlags = D3D10_BIND_VERTEX_BUFFER;
                const HRESULT hr = device_->CreateBuffer(&desc, nullptr, &vertexBuffer_);
                if (FAILED(hr)) ThrowHr("ID3D10Device::CreateBuffer(sprite vertices)", hr);
                vertexCapacityBytes_ = static_cast<UINT>(vertexBytes);
            }
            if (!indexBuffer_ || indexBytes > indexCapacityBytes_)
            {
                if (indexBuffer_) indexBuffer_->Release();
                D3D10_BUFFER_DESC desc{};
                desc.Usage = D3D10_USAGE_DEFAULT;
                desc.ByteWidth = static_cast<UINT>(indexBytes);
                desc.BindFlags = D3D10_BIND_INDEX_BUFFER;
                const HRESULT hr = device_->CreateBuffer(&desc, nullptr, &indexBuffer_);
                if (FAILED(hr)) ThrowHr("ID3D10Device::CreateBuffer(sprite indices)", hr);
                indexCapacityBytes_ = static_cast<UINT>(indexBytes);
            }
        }

        void FlushBatch()
        {
            if (pendingVertices_.empty() || !currentTexture_) { pendingVertices_.clear(); pendingIndices_.clear(); return; }

            const std::size_t vertexBytes = pendingVertices_.size() * sizeof(SpriteVertex);
            const std::size_t indexBytes = pendingIndices_.size() * sizeof(uint16_t);
            EnsureBuffers(vertexBytes, indexBytes);
            device_->UpdateSubresource(vertexBuffer_, 0, nullptr, pendingVertices_.data(), 0, 0);
            device_->UpdateSubresource(indexBuffer_, 0, nullptr, pendingIndices_.data(), 0, 0);

            const int vw = *viewportWidthPtr_;
            const int vh = *viewportHeightPtr_;
            const float proj[16] = {
                2.0f / static_cast<float>(vw), 0.0f, 0.0f, 0.0f,
                0.0f, -2.0f / static_cast<float>(vh), 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                -1.0f, 1.0f, 0.0f, 1.0f,
            };
            device_->UpdateSubresource(constantBuffer_, 0, nullptr, proj, 0, 0);

            if (samplerDirty_)
            {
                if (sampler_) sampler_->Release();
                D3D10_SAMPLER_DESC sampDesc{};
                sampDesc.Filter = TextureFilterToD3D10(filter_);
                sampDesc.AddressU = TextureAddressModeToD3D10(addressU_);
                sampDesc.AddressV = TextureAddressModeToD3D10(addressV_);
                sampDesc.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
                sampDesc.ComparisonFunc = D3D10_COMPARISON_NEVER;
                sampDesc.MaxLOD = D3D10_FLOAT32_MAX;
                device_->CreateSamplerState(&sampDesc, &sampler_);
                samplerDirty_ = false;
            }

            const auto* owner = static_cast<const D3D10TextureOwner*>(
                dynamic_cast<const D3D10TextureOwner*>(currentTexture_));

            const UINT stride = sizeof(SpriteVertex);
            const UINT offset = 0;
            device_->IASetVertexBuffers(0, 1, &vertexBuffer_, &stride, &offset);
            device_->IASetIndexBuffer(indexBuffer_, DXGI_FORMAT_R16_UINT, 0);
            device_->IASetInputLayout(shader_.layout);
            device_->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            device_->VSSetShader(shader_.vs);
            device_->PSSetShader(shader_.ps);
            device_->VSSetConstantBuffers(0, 1, &constantBuffer_);
            ID3D10ShaderResourceView* srv = owner->SRV();
            device_->PSSetShaderResources(0, 1, &srv);
            device_->PSSetSamplers(0, 1, &sampler_);

            // Save/restore whatever rasterizer state 3D draws may have set (design decision 4) --
            // 2D sprites always render with cull-none regardless (see the constructor's own
            // comment: a rotated sprite's winding order must never be culled).
            ID3D10RasterizerState* previousRasterizerState = nullptr;
            device_->RSGetState(&previousRasterizerState);
            device_->RSSetState(cullNoneState_);

            device_->DrawIndexed(static_cast<UINT>(pendingIndices_.size()), 0, 0);

            device_->RSSetState(previousRasterizerState);
            if (previousRasterizerState) previousRasterizerState->Release();

            pendingVertices_.clear();
            pendingIndices_.clear();
        }

        ID3D10Device* device_;
        const int* viewportWidthPtr_;
        const int* viewportHeightPtr_;
        CompiledShader shader_{};
        ID3D10Buffer* constantBuffer_ = nullptr;
        ID3D10SamplerState* sampler_ = nullptr;
        ID3D10RasterizerState* cullNoneState_ = nullptr;
        ID3D10Buffer* vertexBuffer_ = nullptr;
        UINT vertexCapacityBytes_ = 0;
        ID3D10Buffer* indexBuffer_ = nullptr;
        UINT indexCapacityBytes_ = 0;
        bool begun_ = false;
        std::vector<SpriteVertex> pendingVertices_;
        std::vector<uint16_t> pendingIndices_;
        const ITextureRenderer* currentTexture_ = nullptr;
        Matrix transformMatrix_ = Matrix::getIdentityProperty();
        int filter_ = static_cast<int>(TextureFilter::Linear);
        int addressU_ = static_cast<int>(TextureAddressMode::Clamp);
        int addressV_ = static_cast<int>(TextureAddressMode::Clamp);
        bool samplerDirty_ = false;
    };

    struct DirectX10Renderer::Impl
    {
        SDL_Window* window = nullptr;
        HWND hwnd = nullptr;
        ID3D10Device* device = nullptr;
        IDXGISwapChain* swapChain = nullptr;
        ID3D10Texture2D* backBufferTex = nullptr;
        ID3D10RenderTargetView* backBufferRTV = nullptr;
        ID3D10Texture2D* depthStencilTex = nullptr;
        ID3D10DepthStencilView* depthStencilView = nullptr;
        int width = 0, height = 0;               // real physical window/back-buffer size
        int virtualWidth = 0, virtualHeight = 0; // inert bookkeeping only (design decision 6)
        CnaPresentationMode presentationMode = CnaPresentationMode::FixedHeightDynamicWidth;

        // Currently-bound render targets (nullptr entries in colorTargets beyond count mean the
        // default back buffer). Supports real MRT (design decision 3/5).
        static constexpr int kMaxTargets = 8;
        ID3D10RenderTargetView* currentColorRTVs[kMaxTargets] = {};
        int currentRTVCount = 0;
        ID3D10DepthStencilView* currentDSV = nullptr;
        int currentTargetWidth = 0, currentTargetHeight = 0;
        D3D10RenderTargetRenderer* boundSingleTarget = nullptr; // for SetRenderTarget2D(nullptr) restore + ReadBackbuffer sizing

        CompiledShader coloredShader{};
        ID3D10Buffer* coloredConstantBuffer = nullptr;

        ID3D10BlendState* currentBlendState = nullptr;
        ID3D10DepthStencilState* currentDepthStencilState = nullptr;
        ID3D10RasterizerState* currentRasterizerState = nullptr;
        int currentStencilRef = 0;

        void BindDefaultTargets()
        {
            currentColorRTVs[0] = backBufferRTV;
            currentRTVCount = 1;
            currentDSV = depthStencilView;
            currentTargetWidth = width;
            currentTargetHeight = height;
            boundSingleTarget = nullptr;
            device->OMSetRenderTargets(1, &backBufferRTV, depthStencilView);
        }

        ~Impl()
        {
            if (currentRasterizerState) currentRasterizerState->Release();
            if (currentDepthStencilState) currentDepthStencilState->Release();
            if (currentBlendState) currentBlendState->Release();
            if (coloredConstantBuffer) coloredConstantBuffer->Release();
            if (coloredShader.layout) coloredShader.layout->Release();
            if (coloredShader.ps) coloredShader.ps->Release();
            if (coloredShader.vs) coloredShader.vs->Release();
            if (depthStencilView) depthStencilView->Release();
            if (depthStencilTex) depthStencilTex->Release();
            if (backBufferRTV) backBufferRTV->Release();
            if (backBufferTex) backBufferTex->Release();
            if (swapChain) swapChain->Release();
            if (device) device->Release();
        }
    };

    DirectX10Renderer::DirectX10Renderer(const GraphicsRendererCreateArgs& args)
        : impl_(std::make_unique<Impl>())
    {
        if (!args.window) throw std::runtime_error("DirectX10Renderer initialized with null window.");
        impl_->window = args.window;
        impl_->presentationMode = args.presentationMode;
        impl_->virtualWidth = args.virtualWidth;
        impl_->virtualHeight = args.virtualHeight;

        impl_->hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(impl_->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!impl_->hwnd)
            throw std::runtime_error("DirectX10Renderer: could not obtain a real HWND from the SDL window.");

        SDL_GetWindowSizeInPixels(impl_->window, &impl_->width, &impl_->height);
        if (impl_->width <= 0) impl_->width = args.virtualWidth > 0 ? args.virtualWidth : 640;
        if (impl_->height <= 0) impl_->height = args.virtualHeight > 0 ? args.virtualHeight : 480;

        DXGI_SWAP_CHAIN_DESC scd{};
        scd.BufferCount = 1;
        scd.BufferDesc.Width = static_cast<UINT>(impl_->width);
        scd.BufferDesc.Height = static_cast<UINT>(impl_->height);
        scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scd.BufferDesc.RefreshRate.Numerator = 60;
        scd.BufferDesc.RefreshRate.Denominator = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.OutputWindow = impl_->hwnd;
        scd.SampleDesc.Count = 1;
        scd.Windowed = TRUE;
        scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

        HRESULT hr = D3D10CreateDeviceAndSwapChain(nullptr, D3D10_DRIVER_TYPE_HARDWARE, nullptr, 0,
            D3D10_SDK_VERSION, &scd, &impl_->swapChain, &impl_->device);
        if (FAILED(hr)) ThrowHr("D3D10CreateDeviceAndSwapChain", hr);

        hr = impl_->swapChain->GetBuffer(0, __uuidof(ID3D10Texture2D), reinterpret_cast<void**>(&impl_->backBufferTex));
        if (FAILED(hr)) ThrowHr("IDXGISwapChain::GetBuffer", hr);
        hr = impl_->device->CreateRenderTargetView(impl_->backBufferTex, nullptr, &impl_->backBufferRTV);
        if (FAILED(hr)) ThrowHr("ID3D10Device::CreateRenderTargetView(back buffer)", hr);

        D3D10_TEXTURE2D_DESC dsDesc{};
        dsDesc.Width = static_cast<UINT>(impl_->width);
        dsDesc.Height = static_cast<UINT>(impl_->height);
        dsDesc.MipLevels = 1;
        dsDesc.ArraySize = 1;
        dsDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsDesc.SampleDesc.Count = 1;
        dsDesc.Usage = D3D10_USAGE_DEFAULT;
        dsDesc.BindFlags = D3D10_BIND_DEPTH_STENCIL;
        hr = impl_->device->CreateTexture2D(&dsDesc, nullptr, &impl_->depthStencilTex);
        if (FAILED(hr)) ThrowHr("ID3D10Device::CreateTexture2D(depth-stencil)", hr);
        hr = impl_->device->CreateDepthStencilView(impl_->depthStencilTex, nullptr, &impl_->depthStencilView);
        if (FAILED(hr)) ThrowHr("ID3D10Device::CreateDepthStencilView", hr);

        impl_->BindDefaultTargets();

        D3D10_VIEWPORT vp{};
        vp.Width = static_cast<UINT>(impl_->width);
        vp.Height = static_cast<UINT>(impl_->height);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        impl_->device->RSSetViewports(1, &vp);

        // Real vs_4_0/ps_4_0 colored-primitives shader (design decision 3 -- required, matches
        // BasicEffect(VertexColorEnabled=true)).
        static const D3D10_INPUT_ELEMENT_DESC coloredElements[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0 },
        };
        CompileShaderPair(impl_->device, kColored3DShaderSrc, std::strlen(kColored3DShaderSrc),
                           coloredElements, 2, impl_->coloredShader);
        D3D10_BUFFER_DESC cbDesc{};
        cbDesc.Usage = D3D10_USAGE_DEFAULT;
        cbDesc.ByteWidth = 64;
        cbDesc.BindFlags = D3D10_BIND_CONSTANT_BUFFER;
        hr = impl_->device->CreateBuffer(&cbDesc, nullptr, &impl_->coloredConstantBuffer);
        if (FAILED(hr)) ThrowHr("ID3D10Device::CreateBuffer(colored PerDraw)", hr);
    }

    DirectX10Renderer::~DirectX10Renderer() = default;

    void DirectX10Renderer::Clear(float r, float g, float b, float a)
    {
        const float color[4] = { r, g, b, a };
        for (int i = 0; i < impl_->currentRTVCount; ++i)
            if (impl_->currentColorRTVs[i]) impl_->device->ClearRenderTargetView(impl_->currentColorRTVs[i], color);
    }

    void DirectX10Renderer::Present()
    {
        const HRESULT hr = impl_->swapChain->Present(0, 0);
        if (FAILED(hr)) ThrowHr("IDXGISwapChain::Present", hr);
    }

    void DirectX10Renderer::GetViewportSize(int& width, int& height)
    {
        if (impl_->window)
        {
            SDL_GetWindowSizeInPixels(impl_->window, &width, &height);
        }
        else
        {
            width = impl_->width;
            height = impl_->height;
        }
    }

    void DirectX10Renderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (w <= 0 || h <= 0) return;

        ID3D10Resource* source = impl_->boundSingleTarget ? nullptr : nullptr;
        ID3D10Texture2D* sourceTex = impl_->backBufferTex;
        int surfW = impl_->width, surfH = impl_->height;
        if (impl_->boundSingleTarget)
        {
            // No direct texture accessor needed here: staging copy uses CopyResource from the
            // render target's own backing texture via its RTV's parent resource.
        }

        D3D10_TEXTURE2D_DESC stagingDesc{};
        stagingDesc.Width = static_cast<UINT>(surfW);
        stagingDesc.Height = static_cast<UINT>(surfH);
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage = D3D10_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D10_CPU_ACCESS_READ;
        ID3D10Texture2D* staging = nullptr;
        HRESULT hr = impl_->device->CreateTexture2D(&stagingDesc, nullptr, &staging);
        if (FAILED(hr)) ThrowHr("ID3D10Device::CreateTexture2D(readback staging)", hr);
        impl_->device->CopyResource(staging, sourceTex);

        D3D10_MAPPED_TEXTURE2D mapped{};
        hr = staging->Map(0, D3D10_MAP_READ, 0, &mapped);
        if (FAILED(hr)) { staging->Release(); ThrowHr("ID3D10Texture2D::Map(READ, readback)", hr); }
        auto* src = static_cast<const uint8_t*>(mapped.pData);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
        for (int row = 0; row < h; ++row)
        {
            std::memcpy(pixels + static_cast<std::size_t>(row) * rowBytes,
                        src + static_cast<std::size_t>(y + row) * mapped.RowPitch + static_cast<std::size_t>(x) * 4,
                        rowBytes);
        }
        staging->Unmap(0);
        staging->Release();
    }

    void DirectX10Renderer::SetVirtualResolution(int width, int height)
    {
        // Inert bookkeeping (design decision 6) -- the swap chain always matches the real window
        // size, matching DirectX11Renderer's own simpler convention (unlike DIRECTX8's own
        // logical-render-target addition, which D3D10 doesn't need: it has no scaled-blit gap).
        if (width > 0) impl_->virtualWidth = width;
        if (height > 0) impl_->virtualHeight = height;
    }

    void DirectX10Renderer::SetPresentationMode(int mode)
    {
        impl_->presentationMode = static_cast<CnaPresentationMode>(mode);
    }

    SDL_Window* DirectX10Renderer::GetWindowInternal() const { return impl_->window; }

    std::unique_ptr<ITextureRenderer> DirectX10Renderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<D3D10TextureRenderer>(impl_->device, data);
    }

    std::unique_ptr<IRenderTargetRenderer> DirectX10Renderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool /*preserveContents*/, bool /*mipMap*/, int /*multiSampleCount*/)
    {
        const bool wantDepth = depthFormat != 0; // DepthFormat::None == 0
        return std::make_unique<D3D10RenderTargetRenderer>(impl_->device, w, h, wantDepth);
    }

    void DirectX10Renderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (!rt)
        {
            impl_->BindDefaultTargets();
            return;
        }
        auto* d3dRt = dynamic_cast<D3D10RenderTargetRenderer*>(rt);
        if (!d3dRt)
            throw std::runtime_error("DirectX10Renderer::SetRenderTarget2D: not a D3D10 render target");
        impl_->currentColorRTVs[0] = d3dRt->RTV();
        impl_->currentRTVCount = 1;
        impl_->currentDSV = d3dRt->DSV();
        impl_->currentTargetWidth = d3dRt->GetWidth();
        impl_->currentTargetHeight = d3dRt->GetHeight();
        impl_->boundSingleTarget = d3dRt;
        ID3D10RenderTargetView* rtv = d3dRt->RTV();
        impl_->device->OMSetRenderTargets(1, &rtv, d3dRt->DSV());
    }

    void DirectX10Renderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count <= 0) { impl_->BindDefaultTargets(); return; }
        if (count > Impl::kMaxTargets)
            throw std::runtime_error("DirectX10Renderer::SetRenderTargets: too many render targets requested");

        ID3D10RenderTargetView* rtvs[Impl::kMaxTargets] = {};
        for (int i = 0; i < count; ++i)
        {
            if (renderTargets[i].IsRenderTargetCubeFace())
                throw std::runtime_error(
                    "DirectX10Renderer::SetRenderTargets: RenderTargetCube face bindings are not supported");
            auto* d3dRt = dynamic_cast<D3D10RenderTargetRenderer*>(renderTargets[i].GetRenderTarget2D());
            if (!d3dRt)
                throw std::runtime_error("DirectX10Renderer::SetRenderTargets: not a D3D10 render target");
            rtvs[i] = d3dRt->RTV();
            impl_->currentColorRTVs[i] = d3dRt->RTV();
        }
        impl_->currentRTVCount = count;
        impl_->currentDSV = nullptr; // No shared per-target depth for MRT in this v1 (design decision 3).
        auto* firstRt = dynamic_cast<D3D10RenderTargetRenderer*>(renderTargets[0].GetRenderTarget2D());
        impl_->currentTargetWidth = firstRt ? firstRt->GetWidth() : impl_->width;
        impl_->currentTargetHeight = firstRt ? firstRt->GetHeight() : impl_->height;
        impl_->boundSingleTarget = count == 1 ? firstRt : nullptr;
        impl_->device->OMSetRenderTargets(static_cast<UINT>(count), rtvs, nullptr);
    }

    std::unique_ptr<ISpriteBatchRenderer> DirectX10Renderer::CreateSpriteBatch()
    {
        return std::make_unique<D3D10SpriteBatchRenderer>(impl_->device, &impl_->currentTargetWidth,
                                                           &impl_->currentTargetHeight);
    }

    void DirectX10Renderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                               int colorDstBlend, int alphaDstBlend,
                                               int colorBlendFunc, int /*alphaBlendFunc*/,
                                               const BlendWriteState& /*writeState*/)
    {
        // REMED-GFX-077: D3D10_BLEND_DESC does carry a per-target RenderTargetWriteMask[8] and
        // OMSetBlendState takes a sample mask, but wiring them is deferred with the rest of this
        // renderer's owner-confirmed v1 scope; the write state is accepted and ignored -- a
        // documented deferral, not a silent drop the caller was told succeeded in full.
        // Unlike D3D11_BLEND_DESC (per-target full RenderTarget[8] blend options),
        // D3D10_BLEND_DESC shares ONE set of blend factors/op across all 8 targets -- only
        // BlendEnable/RenderTargetWriteMask are per-target arrays (D3D10.1's D3D10_BLEND_DESC1
        // added true per-target blend factors, not used here).
        D3D10_BLEND_DESC desc{};
        desc.AlphaToCoverageEnable = FALSE;
        desc.SrcBlend = BlendToD3D10(colorSrcBlend);
        desc.DestBlend = BlendToD3D10(colorDstBlend);
        desc.BlendOp = colorBlendFunc == 1 ? D3D10_BLEND_OP_SUBTRACT
                     : colorBlendFunc == 2 ? D3D10_BLEND_OP_REV_SUBTRACT
                     : colorBlendFunc == 3 ? D3D10_BLEND_OP_MIN
                     : colorBlendFunc == 4 ? D3D10_BLEND_OP_MAX
                     : D3D10_BLEND_OP_ADD;
        desc.SrcBlendAlpha = BlendToD3D10(alphaSrcBlend);
        desc.DestBlendAlpha = BlendToD3D10(alphaDstBlend);
        desc.BlendOpAlpha = desc.BlendOp;
        for (int i = 0; i < 8; ++i)
        {
            desc.BlendEnable[i] = TRUE;
            desc.RenderTargetWriteMask[i] = D3D10_COLOR_WRITE_ENABLE_ALL;
        }
        ID3D10BlendState* state = nullptr;
        const HRESULT hr = impl_->device->CreateBlendState(&desc, &state);
        if (FAILED(hr)) ThrowHr("ID3D10Device::CreateBlendState", hr);
        const float blendFactor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        impl_->device->OMSetBlendState(state, blendFactor, 0xFFFFFFFF);
        if (impl_->currentBlendState) impl_->currentBlendState->Release();
        impl_->currentBlendState = state;
    }

    void DirectX10Renderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                                       bool stencilEnable, int stencilFunc,
                                                       int stencilPass, int stencilFail, int stencilDepthFail,
                                                       int stencilMask, int stencilWriteMask, int referenceStencil,
                                                       bool /*twoSidedStencilMode*/,
                                                       int /*ccwStencilFunc*/, int /*ccwStencilPass*/,
                                                       int /*ccwStencilFail*/, int /*ccwStencilDepthFail*/)
    {
        D3D10_DEPTH_STENCIL_DESC desc{};
        desc.DepthEnable = depthEnable ? TRUE : FALSE;
        desc.DepthWriteMask = depthWriteEnable ? D3D10_DEPTH_WRITE_MASK_ALL : D3D10_DEPTH_WRITE_MASK_ZERO;
        desc.DepthFunc = CompareFunctionToD3D10(depthFunc);
        desc.StencilEnable = stencilEnable ? TRUE : FALSE;
        desc.StencilReadMask = static_cast<UINT8>(stencilMask);
        desc.StencilWriteMask = static_cast<UINT8>(stencilWriteMask);
        desc.FrontFace.StencilFunc = CompareFunctionToD3D10(stencilFunc);
        desc.FrontFace.StencilPassOp = StencilOperationToD3D10(stencilPass);
        desc.FrontFace.StencilFailOp = StencilOperationToD3D10(stencilFail);
        desc.FrontFace.StencilDepthFailOp = StencilOperationToD3D10(stencilDepthFail);
        desc.BackFace = desc.FrontFace;

        ID3D10DepthStencilState* state = nullptr;
        const HRESULT hr = impl_->device->CreateDepthStencilState(&desc, &state);
        if (FAILED(hr)) ThrowHr("ID3D10Device::CreateDepthStencilState", hr);
        impl_->currentStencilRef = referenceStencil;
        impl_->device->OMSetDepthStencilState(state, static_cast<UINT>(referenceStencil));
        if (impl_->currentDepthStencilState) impl_->currentDepthStencilState->Release();
        impl_->currentDepthStencilState = state;
    }

    void DirectX10Renderer::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                                    float depthBias, float slopeScaleDepthBias)
    {
        D3D10_RASTERIZER_DESC desc{};
        desc.CullMode = CullModeToD3D10(cullMode);
        desc.FillMode = FillModeToD3D10(fillMode);
        desc.FrontCounterClockwise = FALSE;
        desc.DepthClipEnable = TRUE;
        desc.ScissorEnable = scissorTestEnable ? TRUE : FALSE;
        desc.DepthBias = static_cast<INT>(depthBias);
        desc.SlopeScaledDepthBias = slopeScaleDepthBias;

        ID3D10RasterizerState* state = nullptr;
        const HRESULT hr = impl_->device->CreateRasterizerState(&desc, &state);
        if (FAILED(hr)) ThrowHr("ID3D10Device::CreateRasterizerState", hr);
        impl_->device->RSSetState(state);
        if (impl_->currentRasterizerState) impl_->currentRasterizerState->Release();
        impl_->currentRasterizerState = state;
    }

    void DirectX10Renderer::ApplySamplerState(int slot, int filter, int addressU, int addressV,
                                                 int maxAnisotropy)
    {
        D3D10_SAMPLER_DESC desc{};
        desc.Filter = TextureFilterToD3D10(filter);
        desc.AddressU = TextureAddressModeToD3D10(addressU);
        desc.AddressV = TextureAddressModeToD3D10(addressV);
        desc.AddressW = D3D10_TEXTURE_ADDRESS_CLAMP;
        desc.MaxAnisotropy = static_cast<UINT>(std::max(1, maxAnisotropy));
        desc.ComparisonFunc = D3D10_COMPARISON_NEVER;
        desc.MaxLOD = D3D10_FLOAT32_MAX;

        ID3D10SamplerState* state = nullptr;
        const HRESULT hr = impl_->device->CreateSamplerState(&desc, &state);
        if (FAILED(hr)) ThrowHr("ID3D10Device::CreateSamplerState", hr);
        impl_->device->PSSetSamplers(static_cast<UINT>(slot), 1, &state);
        state->Release(); // D3D10 keeps its own internal ref once bound; safe to release ours immediately.
    }

    void DirectX10Renderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        Clear(r, g, b, a);
        if (impl_->currentDSV) impl_->device->ClearDepthStencilView(impl_->currentDSV, D3D10_CLEAR_DEPTH, depth, 0);
    }
    void DirectX10Renderer::ClearDepth(float depth)
    {
        if (impl_->currentDSV) impl_->device->ClearDepthStencilView(impl_->currentDSV, D3D10_CLEAR_DEPTH, depth, 0);
    }
    void DirectX10Renderer::ClearStencil(int stencil)
    {
        if (impl_->currentDSV) impl_->device->ClearDepthStencilView(impl_->currentDSV, D3D10_CLEAR_STENCIL, 1.0f, static_cast<UINT8>(stencil));
    }
    void DirectX10Renderer::ClearDepthAndStencil(float depth, int stencil)
    {
        if (impl_->currentDSV) impl_->device->ClearDepthStencilView(impl_->currentDSV, D3D10_CLEAR_DEPTH | D3D10_CLEAR_STENCIL, depth, static_cast<UINT8>(stencil));
    }
    void DirectX10Renderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        Clear(r, g, b, a);
        if (impl_->currentDSV) impl_->device->ClearDepthStencilView(impl_->currentDSV, D3D10_CLEAR_STENCIL, 1.0f, static_cast<UINT8>(stencil));
    }
    void DirectX10Renderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        Clear(r, g, b, a);
        if (impl_->currentDSV) impl_->device->ClearDepthStencilView(impl_->currentDSV, D3D10_CLEAR_DEPTH | D3D10_CLEAR_STENCIL, depth, static_cast<UINT8>(stencil));
    }

    void DirectX10Renderer::SetDepthTestEnabled(bool /*enabled*/) {}
    void DirectX10Renderer::SetBlendEnabled(bool /*enabled*/) {}
    void DirectX10Renderer::SetDepthWriteEnabled(bool /*enabled*/) {}

    std::unique_ptr<IVertexBufferRenderer> DirectX10Renderer::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<D3D10VertexBufferRenderer>(impl_->device, vertex_capacity);
    }
    std::unique_ptr<IIndexBufferRenderer> DirectX10Renderer::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<D3D10IndexBufferRenderer>(impl_->device, false);
    }
    std::unique_ptr<IIndexBufferRenderer> DirectX10Renderer::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<D3D10IndexBufferRenderer>(impl_->device, true);
    }

    void DirectX10Renderer::DrawColoredPrimitives(
        const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount)
    {
        const auto& d3dVb = static_cast<const D3D10VertexBufferRenderer&>(vb);
        const std::size_t stride = d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;

        const Matrix wvp = world * view * projection;
        float mvp[16];
        wvp.ToColumnMajor(mvp); // row-major flat layout, matches HLSL row_major cbuffer field
        impl_->device->UpdateSubresource(impl_->coloredConstantBuffer, 0, nullptr, mvp, 0, 0);

        ID3D10Buffer* vbRaw = d3dVb.GetBufferEXT();
        const UINT strideU = static_cast<UINT>(stride);
        const UINT offset = 0;
        impl_->device->IASetVertexBuffers(0, 1, &vbRaw, &strideU, &offset);
        impl_->device->IASetInputLayout(impl_->coloredShader.layout);
        impl_->device->IASetPrimitiveTopology(ToD3D10Topology(primitive));
        impl_->device->VSSetShader(impl_->coloredShader.vs);
        impl_->device->PSSetShader(impl_->coloredShader.ps);
        impl_->device->VSSetConstantBuffers(0, 1, &impl_->coloredConstantBuffer);

        const UINT vertexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
        impl_->device->Draw(vertexCount, 0);
    }

    void DirectX10Renderer::DrawIndexedColoredPrimitives(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount)
    {
        const auto& d3dVb = static_cast<const D3D10VertexBufferRenderer&>(vb);
        const auto& d3dIb = static_cast<const D3D10IndexBufferRenderer&>(ib);
        const std::size_t stride = d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;

        const Matrix wvp = world * view * projection;
        float mvp[16];
        wvp.ToColumnMajor(mvp);
        impl_->device->UpdateSubresource(impl_->coloredConstantBuffer, 0, nullptr, mvp, 0, 0);

        ID3D10Buffer* vbRaw = d3dVb.GetBufferEXT();
        const UINT strideU = static_cast<UINT>(stride);
        const UINT offset = 0;
        impl_->device->IASetVertexBuffers(0, 1, &vbRaw, &strideU, &offset);
        impl_->device->IASetIndexBuffer(d3dIb.GetBufferEXT(),
            d3dIb.IsThirtyTwoBit() ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
        impl_->device->IASetInputLayout(impl_->coloredShader.layout);
        impl_->device->IASetPrimitiveTopology(ToD3D10Topology(primitive));
        impl_->device->VSSetShader(impl_->coloredShader.vs);
        impl_->device->PSSetShader(impl_->coloredShader.ps);
        impl_->device->VSSetConstantBuffers(0, 1, &impl_->coloredConstantBuffer);

        const UINT indexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
        impl_->device->DrawIndexed(indexCount, 0, 0);
    }
}

namespace CNA::Internal::Renderers
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<DirectX10::DirectX10Renderer>(args);
    }
}
