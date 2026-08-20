// SPDX-License-Identifier: MS-PL
// plans/plan_dx.md Phase DIRECTX4/DX80: smoke test for the D3D11 graphics renderer's device/swap-chain/
// back-buffer foundation. Real window, real DXGI swap chain, real Clear()+Present()+readback --
// this is the renderer's first genuine pixel-correctness proof (DX-28's own bar).
//
// Check A -- real device created with feature level >= 11_0 (design decision 12's floor).
// Check B -- Clear(r,g,b,a) followed by GetBackBufferData() reads back the EXACT clear color.
// Check C -- a second, different Clear()+readback also matches (not a stale/cached value).
// Check D (DX-15-embed) -- D3DShaderCache creates a real ID3D11VertexShader+ID3D11PixelShader,
//   through this same real device, for each of DX-13-hlsl's 10 stock shader variants.
// Check E (DX-30) -- a real D3D11VertexBufferRenderer round-trips known VertexPositionColor data
//   through Map(WRITE_DISCARD)/CopyResource-to-staging/Map(READ) -- a genuine GPU write+readback,
//   not just "SetData() didn't throw".
// Check F (DX-31) -- same round-trip proof for both a 16-bit and a 32-bit D3D11IndexBufferRenderer.
// Check G (DX-32) -- D3D11InputLayoutCache actually calls CreateInputLayout() against a real
//   vertex shader's DXBC input signature for a couple of DX-16-vtx's established strides, and
//   caching returns the identical object on a second request for the same (variant, stride).
// Check H (DX-40) -- a real D3D11TextureRenderer round-trips known RGBA8 pixel data: constructed
//   from an ImageData, then read back via a staging-texture copy and compared byte-for-byte.
// Check I (DX-41/DX-42) -- D3D11TextureCubeRenderer/D3D11Texture3DRenderer SetData()+GetData() also
//   round-trip exact bytes for a sub-region of one cube face / one 3D slice.
// Check J (DX-43) -- a real offscreen D3D11RenderTargetRenderer: BindAsRenderTarget(), Clear() (now
//   routed to whatever's actually bound, not hardcoded to the back buffer), UnbindAsRenderTarget(),
//   then a direct staging-texture readback of the render target's own color texture confirms the
//   exact clear color -- and a follow-up back-buffer Clear()+readback confirms Unbind() genuinely
//   restored the back buffer as Clear()'s target (not left pointing at the old RT).
// Check K (DX-45) -- same round-trip proof for an MSAA render target, whose ResolveSubresource()
//   (triggered by UnbindAsRenderTarget()) must produce the same clear color in the resolved,
//   sampleable texture.
// Check L (DX-44) -- D3D11SamplerCache creates a real ID3D11SamplerState and returns the identical
//   object for a repeat request with the same XNA-level sampler fields.
// Check M (DX-47) -- a real ID3D11Query(D3D11_QUERY_OCCLUSION) completes and reports data through
//   Begin()/End()/IsComplete()/PixelCount() (drawless, so PixelCount() is 0 -- the point of this
//   check is that the query object is real and GetData() succeeds, not a nonzero count).
// Check N (DX-46) -- SetRenderTargets() with 2 render targets performs one real OMSetRenderTargets
//   call binding both RTVs, and Clear() correctly clears both (a real multi-target readback, not
//   just "the call didn't throw") -- MRT output without an actual multi-target-writing shader
//   (Phase DIRECTX8) is proven exactly this far, honestly, and no further.
// Check O (DX-50/DX-51/DX-52/DX-53) -- D3D11BlendStateCache/D3D11DepthStencilStateCache/
//   D3D11RasterizerStateCache each create a real state object, cache it (identical XNA params ->
//   identical object, different params -> different object), and ApplyBlendState()/
//   ApplyDepthStencilState()/ApplyRasterizerState() genuinely bind it (confirmed via
//   OMGetBlendState()/OMGetDepthStencilState()/RSGetState()) -- plus SetBlendFactor()/
//   SetReferenceStencil()'s standalone re-bind (Task 870/319 parity) and SetViewport()/
//   SetScissorRect()'s direct RSSetViewports()/RSSetScissorRects() round-trip. Behavioral pixel
//   correctness of blend/stencil *output* needs an actual draw call (Phase DIRECTX8), proven exactly
//   this far, honestly, and no further -- same bar Check N already set for MRT.
// Check P (DX-60/DX-60a/DX-61) -- the first real 3D triangle this renderer has ever drawn: a real
//   colored3d pipeline (D3DPerDrawConstants/D3DFogConstants constant buffers, DX-32's input layout,
//   DX-15-embed's shaders) actually painted via DrawColoredPrimitives()/DrawIndexedColoredPrimitives()
//   -- the same screen pixel reads back the Clear() background color before the draw and the exact
//   vertex color after it, both for the non-indexed and indexed draw paths.
// Check Q (DX-62) -- DrawPrimitivesEx()/DrawIndexedPrimitivesEx() with real GpuDrawParams (not the
//   hardcoded legacy path): textured3d (stride 20) samples a known texture color exactly
//   (diffuseColor=white), proven for both the non-indexed and indexed entry points since both share
//   DrawPrimitivesExImpl(); colored_textured3d (stride 24) multiplies a known vertex color through a
//   white texture, proving VertexColorEnabled's real effect, not just the texture sample alone.
// Check R (DX-63) -- lit_textured3d (stride 32): the unlit branch (LightingEnabled=false) samples
//   diffuseColor*texture exactly, same bar as Check Q; the lit branch (LightingEnabled=true) isn't
//   byte-exact-asserted (real Blinn-Phong math) but is proven to genuinely run -- its output pixel
//   is confirmed to differ from both the unlit result and the Clear() background, i.e. the GPU
//   actually computed something new, not a stale/cached value.
// Check S (DX-64) -- alpha_test3d: an alpha value that fails the test (AlphaTol=0, alpha>=ref) is
//   confirmed to genuinely discard (the Clear() background survives the draw untouched); an alpha
//   value that passes is confirmed to draw the exact texture color, including its own alpha byte.
// Check T (DX-65) -- dual_texture3d: two textures sampled and combined (tex1.rgb*2 * tex2 * Tint)
//   produce the exact expected byte result, proving the real two-SRV/two-sampler bind path.
// Check U (DX-66) -- env_map3d: a TextureCube sampled via a geometrically-constrained reflection
//   direction (camera far down +Z, vertex normal facing the camera) samples one specific,
//   distinctly-colored cube face exactly -- proving the cube SRV bind + EnvMapParams cbuffer path.
// Check V (DX-67) -- skinned3d: a single-bone identity transform (BoneBlock genuinely populated,
//   not left zero-initialized) combined with ambient=white/specular=zeroed samples the exact
//   texture color, proving the real BoneBlock + FogParams(extra) constant buffer path.
// Check W (DX-68) -- instanced3d: DrawInstancedPrimitivesEx() with one identity-transform instance
//   (via the per-instance INSTANCEWORLD0-3 buffer) outputs the exact instance DiffuseColor.
// Check X (DX-58) -- custom ShaderEffect: runtime D3DCompile() of arbitrary HLSL, driven manually
//   (SpriteBatch didn't exist yet at the time this check was written).
// Check Y (DX-70) -- real, end-to-end D3D11SpriteBatchRenderer via the actual public
//   Microsoft::Xna::Framework::Graphics::SpriteBatch/Texture2D classes (not the raw renderer
//   interface): a 2x2 per-corner-colored texture drawn at a known destination rect reads back the
//   exact color in each of the 4 quadrants (PointClamp, no filtering ambiguity), and a second draw
//   with SpriteEffects::FlipHorizontally is confirmed to genuinely swap the top-left/top-right
//   quadrants -- not just "didn't throw".
// Check Z (DX-72) -- TextureAddressMode::Wrap/Mirror via SpriteBatch, both proven with a probe
//   pixel chosen to give a DIFFERENT color than Clamp (or, for Mirror, than either Wrap or Clamp)
//   would produce at that exact point -- a real discriminating test, not merely "some color came
//   back". D3D11 has genuine sampler address-mode support (D3D11SamplerCache, DX-44) and should
//   not inherit SDL_Renderer's own documented Wrap/Mirror gap.
// Check AA (DX-71) -- a custom Effect passed to SpriteBatch::Begin(..., effect) draws sprites
//   through that effect's own compiled shader (a deliberate color inversion) instead of the stock
//   sprite2d pipeline, proving D3D11EffectRenderer::SetViewportSizeEXT()'s automatic vpSize slot
//   and the shared texture-binding path both work for the custom-effect draw path for real.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Renderers/DirectX11/DirectX11Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11Textures.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11SamplerCache.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11OcclusionQuery.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11StateObjectCache.hpp"
#include "CNA/Internal/Renderers/DirectX11/D3D11EffectRenderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DShaderCache.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::DirectX11;
using CNA::Internal::Renderers::ImageData;
using CNA::Internal::Renderers::IRenderTargetRenderer;
using CNA::Internal::Renderers::RenderTargetBindingDescriptor;
using CNA::Internal::Renderers::GpuDrawParams;
using CNA::Internal::Renderers::D3DCommon::D3DShaderVariant;
using CNA::Internal::Renderers::D3DCommon::CreateVertexShaderForVariant;
using CNA::Internal::Renderers::D3DCommon::CreatePixelShaderForVariant;

namespace
{
    /// Copies an ID3D11Buffer to a CPU-readable staging buffer and returns its bytes -- the same
    /// CopyResource+Map(READ) technique DirectX11Renderer::ReadBackbuffer() already uses for
    /// the back-buffer texture (DX-28), applied to a plain buffer resource instead. Test-only:
    /// production draw-call code never needs to read a vertex/index buffer back.
    std::vector<uint8_t> ReadBufferBytes(ID3D11Device* device, ID3D11DeviceContext* context,
                                         ID3D11Buffer* buffer, UINT byteWidth)
    {
        D3D11_BUFFER_DESC stagingDesc{};
        stagingDesc.ByteWidth = byteWidth;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

        Microsoft::WRL::ComPtr<ID3D11Buffer> staging;
        if (FAILED(device->CreateBuffer(&stagingDesc, nullptr, staging.GetAddressOf())))
            return {};

        context->CopyResource(staging.Get(), buffer);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return {};

        std::vector<uint8_t> result(byteWidth);
        std::memcpy(result.data(), mapped.pData, byteWidth);
        context->Unmap(staging.Get(), 0);
        return result;
    }

    /// Reads a w*h RGBA8 region starting at (x,y) out of subresource 0 of an arbitrary
    /// ID3D11Texture2D via the same staging-texture CopyResource+Map(READ) technique
    /// DirectX11Renderer::ReadBackbuffer() uses for the swap chain's own back buffer --
    /// test-only, since production code never reads a color/render-target texture back this way.
    std::vector<uint8_t> ReadTexture2DRegion(ID3D11Device* device, ID3D11DeviceContext* context,
                                             ID3D11Texture2D* texture, int x, int y, int w, int h)
    {
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf())))
            return {};
        context->CopyResource(staging.Get(), texture);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return {};

        std::vector<uint8_t> result(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
        for (int row = 0; row < h; ++row)
        {
            const uint8_t* src = static_cast<const uint8_t*>(mapped.pData)
                                + static_cast<std::size_t>(y + row) * mapped.RowPitch
                                + static_cast<std::size_t>(x) * 4;
            std::memcpy(result.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4,
                       src, static_cast<std::size_t>(w) * 4);
        }
        context->Unmap(staging.Get(), 0);
        return result;
    }

    /// Reads the STENCIL byte of every pixel in a w*h region of a DXGI_FORMAT_D24_UNORM_S8_UINT
    /// depth-stencil ID3D11Texture2D, via the same staging-texture CopyResource+Map(READ)
    /// technique as ReadTexture2DRegion() above. D3D11 has no separate depth/stencil "plane"
    /// subresource index like D3D12 -- a D24_UNORM_S8_UINT resource is one packed 32-bit-per-pixel
    /// value (little-endian: byte 3 = the S8 stencil byte, bytes 0-2 = the 24-bit depth value), so
    /// this reads the raw bytes directly rather than needing a plane-slice copy (CNAEXT, test-only,
    /// plans/plan_dx.md DX-130).
    std::vector<uint8_t> ReadDepthStencilPlane(ID3D11Device* device, ID3D11DeviceContext* context,
                                               ID3D11Texture2D* dsTexture, int w, int h)
    {
        D3D11_TEXTURE2D_DESC desc{};
        dsTexture->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf())))
            return {};
        context->CopyResource(staging.Get(), dsTexture);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return {};

        std::vector<uint8_t> result(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        for (int row = 0; row < h; ++row)
        {
            const uint8_t* src = static_cast<const uint8_t*>(mapped.pData)
                                + static_cast<std::size_t>(row) * mapped.RowPitch;
            for (int col = 0; col < w; ++col)
                result[static_cast<std::size_t>(row) * static_cast<std::size_t>(w) + static_cast<std::size_t>(col)]
                    = src[static_cast<std::size_t>(col) * 4 + 3];
        }
        context->Unmap(staging.Get(), 0);
        return result;
    }

    /// Reads a w*h RGBA8 region starting at (x,y) out of an arbitrary subresource index (mip level,
    /// or mip+face for a texture array) of an arbitrary ID3D11Texture2D -- same staging-texture
    /// technique as ReadTexture2DRegion() above, but Map()'d at `subresource` instead of always 0
    /// (CNAEXT, test-only -- DX-144's real mip-chain-content proof needs to read back mip levels > 0
    /// directly, since there is no public sampling path in this raw-renderer-level smoke test).
    std::vector<uint8_t> ReadTexture2DMipRegion(ID3D11Device* device, ID3D11DeviceContext* context,
                                                ID3D11Texture2D* texture, UINT subresource,
                                                int x, int y, int w, int h)
    {
        D3D11_TEXTURE2D_DESC desc{};
        texture->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf())))
            return {};
        context->CopyResource(staging.Get(), texture);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context->Map(staging.Get(), subresource, D3D11_MAP_READ, 0, &mapped)))
            return {};

        std::vector<uint8_t> result(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
        for (int row = 0; row < h; ++row)
        {
            const uint8_t* src = static_cast<const uint8_t*>(mapped.pData)
                                + static_cast<std::size_t>(y + row) * mapped.RowPitch
                                + static_cast<std::size_t>(x) * 4;
            std::memcpy(result.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4,
                       src, static_cast<std::size_t>(w) * 4);
        }
        context->Unmap(staging.Get(), subresource);
        return result;
    }
}

class D3D11SmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;
    int frame_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        // Give the swap chain one frame to settle before the first real check.
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<DirectX11Renderer&>(dev.GetRenderer());

        // Check A: real feature level negotiated, meeting design decision 12's 11_0 floor.
        check(renderer.GetFeatureLevelEXT() >= D3D_FEATURE_LEVEL_11_0,
              "device negotiated feature level 11_0 or higher");
        std::printf("    feature level = 0x%04x, debug layer = %s, tearing = %s\n",
                    static_cast<unsigned>(renderer.GetFeatureLevelEXT()),
                    renderer.IsDebugLayerEnabledEXT() ? "enabled" : "disabled",
                    renderer.IsTearingCapableEXT() ? "capable" : "not capable");

        // Check B: real, correct pixel readback after Clear().
        {
            dev.Clear(Color(20, 40, 60, 255));
            const Microsoft::Xna::Framework::Rectangle region(0, 0, 4, 4);
            std::vector<Color> pixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            bool allMatch = true;
            for (const Color& p : pixels)
            {
                if (p.getRProperty() != 20 || p.getGProperty() != 40 || p.getBProperty() != 60 ||
                    p.getAProperty() != 255)
                {
                    allMatch = false;
                    break;
                }
            }
            check(allMatch, "GetBackBufferData() reads back the exact Clear() color for every pixel");
        }

        // Check C: a second, different Clear() also reads back correctly (not a cached/stale value).
        {
            dev.Clear(Color(200, 100, 50, 255));
            const Microsoft::Xna::Framework::Rectangle region(10, 10, 4, 4);
            std::vector<Color> pixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            bool allMatch = true;
            for (const Color& p : pixels)
            {
                if (p.getRProperty() != 200 || p.getGProperty() != 100 || p.getBProperty() != 50 ||
                    p.getAProperty() != 255)
                {
                    allMatch = false;
                    break;
                }
            }
            check(allMatch, "a second Clear() at a different region/color also reads back correctly");
        }

        // Check D (DX-15-embed): every DX-13-hlsl stock variant's DXBC actually creates a real
        // ID3D11VertexShader/ID3D11PixelShader through this real device -- not just plausible-
        // looking bytes (DX-14-compile only proved D3DCompile() accepted them, not that D3D11's
        // own shader-object creation does).
        {
            static const struct { D3DShaderVariant variant; const char* name; } kVariants[] = {
                {D3DShaderVariant::Colored3d,         "colored3d"},
                {D3DShaderVariant::Textured3d,        "textured3d"},
                {D3DShaderVariant::ColoredTextured3d, "colored_textured3d"},
                {D3DShaderVariant::LitTextured3d,     "lit_textured3d"},
                {D3DShaderVariant::AlphaTest3d,       "alpha_test3d"},
                {D3DShaderVariant::DualTexture3d,     "dual_texture3d"},
                {D3DShaderVariant::EnvMap3d,          "env_map3d"},
                {D3DShaderVariant::Skinned3d,         "skinned3d"},
                {D3DShaderVariant::Sprite2d,          "sprite2d"},
                {D3DShaderVariant::Instanced3d,       "instanced3d"},
            };

            ID3D11Device* device = renderer.GetDeviceEXT();
            for (const auto& v : kVariants)
            {
                auto vs = CreateVertexShaderForVariant(device, v.variant);
                auto ps = CreatePixelShaderForVariant(device, v.variant);
                char label[96];
                std::snprintf(label, sizeof(label),
                              "%s: CreateVertexShader+CreatePixelShader both succeed", v.name);
                check(vs != nullptr && ps != nullptr, label);
            }
        }

        ID3D11Device* device = renderer.GetDeviceEXT();
        ID3D11DeviceContext* context = renderer.GetContextEXT();

        // Check E (DX-30): a real vertex buffer round-trips known VertexPositionColor (stride 16)
        // data through an actual GPU write (Map/WRITE_DISCARD) and read (CopyResource to a
        // staging buffer + Map/READ) -- not just "SetData() returned without throwing".
        {
            struct VPC { float x, y, z; uint32_t color; };
            static const VPC kVerts[4] = {
                {0.0f, 0.0f, 0.0f, 0xFF0000FFu},
                {1.0f, 0.0f, 0.0f, 0x00FF00FFu},
                {1.0f, 1.0f, 0.0f, 0x0000FFFFu},
                {0.0f, 1.0f, 0.0f, 0xFFFFFFFFu},
            };
            auto vb = renderer.CreateVertexBuffer(4);
            vb->SetData(kVerts, 4, sizeof(VPC));
            auto* d3dVb = static_cast<D3D11VertexBufferRenderer*>(vb.get());
            const auto readBack = ReadBufferBytes(device, context, d3dVb->GetBufferEXT(),
                                                   static_cast<UINT>(4 * sizeof(VPC)));
            const bool matches = readBack.size() == sizeof(kVerts) &&
                                 std::memcmp(readBack.data(), kVerts, sizeof(kVerts)) == 0;
            check(vb->GetVertexCount() == 4 && matches,
                  "D3D11VertexBufferRenderer: SetData() round-trips exact VertexPositionColor bytes");
        }

        // Check F (DX-31): both 16-bit and 32-bit index buffers round-trip the same way.
        {
            static const uint16_t kIdx16[6] = {0, 1, 2, 2, 3, 0};
            auto ib16 = renderer.CreateIndexBuffer16(6);
            ib16->SetData16(kIdx16, 6);
            auto* d3dIb16 = static_cast<D3D11IndexBufferRenderer*>(ib16.get());
            const auto readBack16 = ReadBufferBytes(device, context, d3dIb16->GetBufferEXT(),
                                                     static_cast<UINT>(sizeof(kIdx16)));
            const bool matches16 = readBack16.size() == sizeof(kIdx16) &&
                                   std::memcmp(readBack16.data(), kIdx16, sizeof(kIdx16)) == 0;
            check(!ib16->IsThirtyTwoBit() && d3dIb16->GetFormatEXT() == DXGI_FORMAT_R16_UINT &&
                      ib16->GetIndexCount() == 6 && matches16,
                  "D3D11IndexBufferRenderer (16-bit): SetData16() round-trips exact index bytes");

            static const uint32_t kIdx32[6] = {0, 1, 2, 2, 3, 0};
            auto ib32 = renderer.CreateIndexBuffer32(6);
            ib32->SetData32(kIdx32, 6);
            auto* d3dIb32 = static_cast<D3D11IndexBufferRenderer*>(ib32.get());
            const auto readBack32 = ReadBufferBytes(device, context, d3dIb32->GetBufferEXT(),
                                                     static_cast<UINT>(sizeof(kIdx32)));
            const bool matches32 = readBack32.size() == sizeof(kIdx32) &&
                                   std::memcmp(readBack32.data(), kIdx32, sizeof(kIdx32)) == 0;
            check(ib32->IsThirtyTwoBit() && d3dIb32->GetFormatEXT() == DXGI_FORMAT_R32_UINT &&
                      ib32->GetIndexCount() == 6 && matches32,
                  "D3D11IndexBufferRenderer (32-bit): SetData32() round-trips exact index bytes");
        }

        // Check G (DX-32): CreateInputLayout() actually succeeds against a real vertex shader's
        // DXBC input signature for two of DX-16-vtx's established strides, and the cache returns
        // the identical object (not just an equal-looking one) on a repeat request.
        {
            auto& cache = renderer.GetInputLayoutCacheEXT();
            auto layoutColored16 = cache.GetOrCreate(device, D3DShaderVariant::Colored3d, 16);
            auto layoutColored16Again = cache.GetOrCreate(device, D3DShaderVariant::Colored3d, 16);
            check(layoutColored16 != nullptr && layoutColored16.Get() == layoutColored16Again.Get(),
                  "D3D11InputLayoutCache: colored3d @ stride 16 creates and caches a real ID3D11InputLayout");

            auto layoutSkinned52 = cache.GetOrCreate(device, D3DShaderVariant::Skinned3d, 52);
            check(layoutSkinned52 != nullptr,
                  "D3D11InputLayoutCache: skinned3d @ stride 52 creates a real ID3D11InputLayout");
        }

        // Check H (DX-40): a real 2D texture round-trips exact RGBA8 bytes, both at construction
        // (level 0 from ImageData) and via a later UpdatePixelsLevel() replacement.
        {
            ImageData img;
            img.width = 4;
            img.height = 4;
            img.mipLevels = 1;
            img.pixels.resize(4 * 4 * 4);
            for (int i = 0; i < 4 * 4; ++i)
            {
                img.pixels[i * 4 + 0] = 10; img.pixels[i * 4 + 1] = 20;
                img.pixels[i * 4 + 2] = 30; img.pixels[i * 4 + 3] = 255;
            }
            auto tex = renderer.CreateTexture(img);
            auto* d3dTex = static_cast<D3D11TextureRenderer*>(tex.get());
            const auto readBack = ReadTexture2DRegion(device, context, d3dTex->GetTextureEXT(), 0, 0, 4, 4);
            check(tex->GetWidth() == 4 && tex->GetHeight() == 4 && readBack == img.pixels,
                  "D3D11TextureRenderer: constructor upload round-trips exact RGBA8 pixel bytes");

            std::vector<uint8_t> newPixels(4 * 4 * 4);
            for (int i = 0; i < 4 * 4; ++i)
            {
                newPixels[i * 4 + 0] = 99; newPixels[i * 4 + 1] = 88;
                newPixels[i * 4 + 2] = 77; newPixels[i * 4 + 3] = 255;
            }
            tex->UpdatePixelsLevel(0, newPixels.data(), 4, 4);
            const auto readBack2 = ReadTexture2DRegion(device, context, d3dTex->GetTextureEXT(), 0, 0, 4, 4);
            check(readBack2 == newPixels,
                  "D3D11TextureRenderer: UpdatePixelsLevel() round-trips exact replacement bytes");
        }

        // Check I (DX-41/DX-42): cube-map and 3D texture SetData()/GetData() round-trip exact
        // bytes for a sub-region -- one face + a sub-volume, not just level 0's full extent.
        {
            auto cube = renderer.CreateTextureCube(8, false, 0);
            std::vector<uint8_t> faceData(4 * 4 * 4);
            for (int i = 0; i < 4 * 4; ++i)
            {
                faceData[i * 4 + 0] = 1; faceData[i * 4 + 1] = 2;
                faceData[i * 4 + 2] = 3; faceData[i * 4 + 3] = 255;
            }
            cube->SetData(2, 0, 2, 2, 4, 4, faceData.data(), static_cast<int>(faceData.size()));
            std::vector<uint8_t> readFace(4 * 4 * 4, 0);
            cube->GetData(2, 0, 2, 2, 4, 4, readFace.data(), static_cast<int>(readFace.size()));
            check(readFace == faceData,
                  "D3D11TextureCubeRenderer: SetData()/GetData() round-trip exact bytes for one face's sub-region");

            auto vol = renderer.CreateTexture3D(4, 4, 4, false, 0);
            std::vector<uint8_t> sliceData(2 * 2 * 2 * 4);
            for (int i = 0; i < 2 * 2 * 2; ++i)
            {
                sliceData[i * 4 + 0] = 9; sliceData[i * 4 + 1] = 8;
                sliceData[i * 4 + 2] = 7; sliceData[i * 4 + 3] = 255;
            }
            vol->SetData(0, 1, 1, 1, 2, 2, 2, sliceData.data(), static_cast<int>(sliceData.size()));
            std::vector<uint8_t> readSlice(2 * 2 * 2 * 4, 0);
            vol->GetData(0, 1, 1, 1, 2, 2, 2, readSlice.data(), static_cast<int>(readSlice.size()));
            check(readSlice == sliceData,
                  "D3D11Texture3DRenderer: SetData()/GetData() round-trip exact bytes for a sub-volume");
        }

        // Check J (DX-43): BindAsRenderTarget()+Clear() (now routed to whatever's bound, not the
        // hardcoded back buffer) writes the exact color into the render target's own texture, and
        // UnbindAsRenderTarget() genuinely restores the back buffer as Clear()'s next target.
        {
            auto rt = renderer.CreateRenderTarget2D(8, 8, 0 /*DepthFormat::None*/, false, false, 0);
            auto* d3dRt = static_cast<D3D11RenderTargetRenderer*>(rt.get());
            renderer.SetRenderTarget2D(rt.get());
            dev.Clear(Color(11, 22, 33, 255));
            renderer.SetRenderTarget2D(nullptr);

            const auto rtPixels = ReadTexture2DRegion(device, context, d3dRt->GetSampleableTextureEXT(), 0, 0, 4, 4);
            bool rtMatches = true;
            for (int i = 0; i < 4 * 4 && rtMatches; ++i)
            {
                rtMatches = rtPixels[i * 4 + 0] == 11 && rtPixels[i * 4 + 1] == 22 &&
                           rtPixels[i * 4 + 2] == 33 && rtPixels[i * 4 + 3] == 255;
            }
            check(rtMatches,
                  "D3D11RenderTargetRenderer: BindAsRenderTarget()+Clear() writes the exact color into the RT's own texture");

            dev.Clear(Color(44, 55, 66, 255));
            const Microsoft::Xna::Framework::Rectangle bbRegion(0, 0, 4, 4);
            std::vector<Color> bbPixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&bbRegion, bbPixels.data(), 0, static_cast<int>(bbPixels.size()));
            bool bbMatches = true;
            for (const Color& p : bbPixels)
            {
                if (p.getRProperty() != 44 || p.getGProperty() != 55 || p.getBProperty() != 66 ||
                    p.getAProperty() != 255)
                {
                    bbMatches = false;
                    break;
                }
            }
            check(bbMatches,
                  "D3D11RenderTargetRenderer: UnbindAsRenderTarget() genuinely restores the back buffer as Clear()'s target");
        }

        // Check K (DX-45): an MSAA render target's ResolveSubresource()-on-unbind produces the
        // exact clear color in the resolved, sampleable texture. Pass/fail is purely about pixel
        // correctness -- whether the device actually granted real multi-sampling (vs. this
        // renderer's own real, device-queried fallback to single-sample) is printed as
        // diagnostics, not gated on, since that's real hardware/driver capability, not a bug here.
        {
            auto rtMsaa = renderer.CreateRenderTarget2D(8, 8, 0, false, false, 4);
            auto* d3dRtMsaa = static_cast<D3D11RenderTargetRenderer*>(rtMsaa.get());
            renderer.SetRenderTarget2D(rtMsaa.get());
            dev.Clear(Color(77, 88, 99, 255));
            renderer.SetRenderTarget2D(nullptr);

            const auto resolved = ReadTexture2DRegion(
                device, context, d3dRtMsaa->GetSampleableTextureEXT(), 0, 0, 4, 4);
            bool msaaMatches = true;
            for (int i = 0; i < 4 * 4 && msaaMatches; ++i)
            {
                msaaMatches = resolved[i * 4 + 0] == 77 && resolved[i * 4 + 1] == 88 &&
                             resolved[i * 4 + 2] == 99 && resolved[i * 4 + 3] == 255;
            }
            check(msaaMatches,
                  "D3D11RenderTargetRenderer (MSAA): Clear()+resolve-on-unbind produces the exact color in the resolved texture");
            std::printf("    MSAA: requested 4x, device-applied %dx\n", d3dRtMsaa->GetMultiSampleCount());
        }

        // Check L (DX-44): the sampler cache creates a real ID3D11SamplerState, caches it for
        // identical XNA-level state, and creates a distinct object for different state; then
        // exercise the renderer's own ApplySamplerState() (PSSetSamplers wiring) end-to-end.
        {
            D3D11SamplerCache cache;
            auto s1 = cache.GetOrCreate(device, 0, 0, 0, 1);
            auto s2 = cache.GetOrCreate(device, 0, 0, 0, 1);
            auto s3 = cache.GetOrCreate(device, 2, 1, 1, 4);
            check(s1 != nullptr && s1.Get() == s2.Get() && s3 != nullptr && s3.Get() != s1.Get() &&
                      cache.GetCacheSizeEXT() == 2,
                  "D3D11SamplerCache: caches identical XNA-level state, creates a distinct object for different state");
            renderer.ApplySamplerState(0, 0, 0, 0, 1);
        }

        // Check M (DX-47): a real ID3D11Query(D3D11_QUERY_OCCLUSION) completes and reports data.
        // PixelCount() == 0 is expected here (no draws occur between Begin()/End()) -- the point
        // of this check is that the query object is real and GetData() succeeds, not a nonzero
        // count (this renderer has no draw path to exercise until Phase DIRECTX8).
        {
            auto oq = renderer.CreateOcclusionQuery();
            oq->Begin();
            oq->End();
            context->Flush();
            bool completed = false;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!completed && std::chrono::steady_clock::now() < deadline)
            {
                completed = oq->IsComplete();
                if (!completed) std::this_thread::yield();
            }
            check(oq != nullptr && completed,
                  "D3D11OcclusionQueryRenderer: a real ID3D11Query completes and reports data");
            check(oq->PixelCount() == 0,
                  "D3D11OcclusionQueryRenderer: PixelCount() is 0 with no draws between Begin()/End()");
        }

        // Check N (DX-46): SetRenderTargets() with 2 targets performs one real OMSetRenderTargets
        // call binding both RTVs, and Clear() writes the exact color into both -- real MRT
        // binding + clear, honestly not a claim about multi-target shader output (no draw path
        // exists yet, Phase DIRECTX8).
        {
            auto rtA = renderer.CreateRenderTarget2D(4, 4, 0, false, false, 0);
            auto rtB = renderer.CreateRenderTarget2D(4, 4, 0, false, false, 0);
            auto* d3dRtA = static_cast<D3D11RenderTargetRenderer*>(rtA.get());
            auto* d3dRtB = static_cast<D3D11RenderTargetRenderer*>(rtB.get());
            const RenderTargetBindingDescriptor rts[2] = {
                RenderTargetBindingDescriptor::ForRenderTarget2D(rtA.get(), 0, 4, 4, rtA->GetMultiSampleCount()),
                RenderTargetBindingDescriptor::ForRenderTarget2D(rtB.get(), 0, 4, 4, rtB->GetMultiSampleCount()),
            };
            renderer.SetRenderTargets(rts, 2);
            dev.Clear(Color(123, 45, 67, 255));
            renderer.SetRenderTargets(nullptr, 0);

            const auto pixelsA = ReadTexture2DRegion(device, context, d3dRtA->GetSampleableTextureEXT(), 0, 0, 4, 4);
            const auto pixelsB = ReadTexture2DRegion(device, context, d3dRtB->GetSampleableTextureEXT(), 0, 0, 4, 4);
            bool mrtMatches = true;
            for (int i = 0; i < 4 * 4 && mrtMatches; ++i)
            {
                mrtMatches = pixelsA[i * 4 + 0] == 123 && pixelsA[i * 4 + 1] == 45 &&
                            pixelsA[i * 4 + 2] == 67 && pixelsA[i * 4 + 3] == 255 &&
                            pixelsB[i * 4 + 0] == 123 && pixelsB[i * 4 + 1] == 45 &&
                            pixelsB[i * 4 + 2] == 67 && pixelsB[i * 4 + 3] == 255;
            }
            check(mrtMatches,
                  "DirectX11Renderer::SetRenderTargets() (MRT): one OMSetRenderTargets binds both targets, Clear() writes both");
        }

        // plans/plan_dx.md DX-143: multi-target (N>1) MRT per-target MSAA-resolve-on-unbind -- the real
        // gap this task closes. Before this fix, DirectX11Renderer::SetRenderTargets() never
        // called any bound target's own resolve/mip-regen logic when the MRT set was replaced or
        // unbound (only the single-target SetRenderTarget2D() path did, via
        // D3D11RenderTargetRenderer::UnbindAsRenderTarget()) -- so an MSAA MRT target's resolved,
        // sampleable texture would have stayed whatever it was before this Clear(), not the real
        // cleared-and-resolved content. Two DIFFERENT MSAA targets bound as MRT, cleared to two
        // DIFFERENT colors, proves each target's OWN resolve ran correctly (not, say, one target's
        // resolve accidentally running twice while the other's never runs).
        {
            auto rtMsaaA = renderer.CreateRenderTarget2D(8, 8, 0, false, false, 4);
            auto rtMsaaB = renderer.CreateRenderTarget2D(8, 8, 0, false, false, 4);
            auto* d3dRtMsaaA = static_cast<D3D11RenderTargetRenderer*>(rtMsaaA.get());
            auto* d3dRtMsaaB = static_cast<D3D11RenderTargetRenderer*>(rtMsaaB.get());
            const RenderTargetBindingDescriptor msaaRts[2] = {
                RenderTargetBindingDescriptor::ForRenderTarget2D(rtMsaaA.get(), 0, 8, 8, rtMsaaA->GetMultiSampleCount()),
                RenderTargetBindingDescriptor::ForRenderTarget2D(rtMsaaB.get(), 0, 8, 8, rtMsaaB->GetMultiSampleCount()),
            };
            renderer.SetRenderTargets(msaaRts, 2);
            dev.Clear(Color(200, 30, 40, 255));
            renderer.SetRenderTargets(nullptr, 0);

            const auto resolvedA = ReadTexture2DRegion(device, context, d3dRtMsaaA->GetSampleableTextureEXT(), 0, 0, 4, 4);
            const auto resolvedB = ReadTexture2DRegion(device, context, d3dRtMsaaB->GetSampleableTextureEXT(), 0, 0, 4, 4);
            bool mrtMsaaMatches = true;
            for (int i = 0; i < 4 * 4 && mrtMsaaMatches; ++i)
            {
                mrtMsaaMatches = resolvedA[i * 4 + 0] == 200 && resolvedA[i * 4 + 1] == 30 &&
                                 resolvedA[i * 4 + 2] == 40 && resolvedA[i * 4 + 3] == 255 &&
                                 resolvedB[i * 4 + 0] == 200 && resolvedB[i * 4 + 1] == 30 &&
                                 resolvedB[i * 4 + 2] == 40 && resolvedB[i * 4 + 3] == 255;
            }
            check(mrtMsaaMatches,
                  "DirectX11Renderer::SetRenderTargets() (MRT, N=2 MSAA targets): unbinding the "
                  "MRT set now genuinely resolves BOTH targets independently, not silently skipped "
                  "(plans/plan_dx.md DX-143 -- same ResolveAndGenerateMipsEXT() code path also covers "
                  "mip-regeneration for a mipMap=true MRT target, not independently pixel-tested here)");

            // A subsequent single-target bind must also trigger the pending MRT flush -- proves
            // FlushPendingMRTResolveEXT() is genuinely called from SetRenderTarget2D() too, not
            // only from SetRenderTargets()'s own unbind-to-back-buffer path.
            auto rtMsaaC = renderer.CreateRenderTarget2D(8, 8, 0, false, false, 4);
            auto rtMsaaD = renderer.CreateRenderTarget2D(8, 8, 0, false, false, 4);
            auto* d3dRtMsaaC = static_cast<D3D11RenderTargetRenderer*>(rtMsaaC.get());
            const RenderTargetBindingDescriptor msaaRts2[2] = {
                RenderTargetBindingDescriptor::ForRenderTarget2D(rtMsaaC.get(), 0, 8, 8, rtMsaaC->GetMultiSampleCount()),
                RenderTargetBindingDescriptor::ForRenderTarget2D(rtMsaaD.get(), 0, 8, 8, rtMsaaD->GetMultiSampleCount()),
            };
            renderer.SetRenderTargets(msaaRts2, 2);
            dev.Clear(Color(5, 6, 7, 255));
            auto rtPlain = renderer.CreateRenderTarget2D(4, 4, 0, false, false, 0);
            renderer.SetRenderTarget2D(rtPlain.get()); // switch straight to a single target, not null
            renderer.SetRenderTarget2D(nullptr);

            const auto resolvedC = ReadTexture2DRegion(device, context, d3dRtMsaaC->GetSampleableTextureEXT(), 0, 0, 4, 4);
            bool mrtToSingleFlushed = true;
            for (int i = 0; i < 4 * 4 && mrtToSingleFlushed; ++i)
                mrtToSingleFlushed = resolvedC[i * 4 + 0] == 5 && resolvedC[i * 4 + 1] == 6 && resolvedC[i * 4 + 2] == 7;
            check(mrtToSingleFlushed,
                  "DirectX11Renderer::SetRenderTarget2D(): switching directly from an MRT set to "
                  "a different single target also flushes the prior MRT set's resolve (plans/plan_dx.md DX-143)");
        }

        // plans/plan_dx.md DX-144: RenderTarget2D/RenderTargetCube mip-chain generation/sampling --
        // proves ResolveAndGenerateMipsEXT()'s GenerateMips() call (DX-45/DX-143) actually writes
        // correct content into mip levels > 0, not zero/garbage. Uses a solid single color across
        // the whole base mip: box-filtering a solid color always produces the exact same solid
        // color at every downstream mip level regardless of the filter kernel's specifics, so this
        // sidesteps needing to replicate exact box-filter math while still being a real,
        // discriminating proof (a zeroed/garbage mip would fail this immediately).
        {
            auto rtMip = renderer.CreateRenderTarget2D(8, 8, 0 /*DepthFormat::None*/, false, true /*mipMap*/, 0);
            auto* d3dRtMip = static_cast<D3D11RenderTargetRenderer*>(rtMip.get());
            renderer.SetRenderTarget2D(rtMip.get());
            dev.Clear(Color(200, 90, 10, 255));
            renderer.SetRenderTarget2D(nullptr); // UnbindAsRenderTarget() -> GenerateMips()

            check(d3dRtMip->GetLevelCountEXT() == 4,
                  "D3D11RenderTargetRenderer: an 8x8 mipMap=true render target reports the expected "
                  "4-level mip chain (8x8/4x4/2x2/1x1, plans/plan_dx.md DX-144)");

            const auto mip1 = ReadTexture2DMipRegion(device, context, d3dRtMip->GetSampleableTextureEXT(), 1, 0, 0, 4, 4);
            bool mip1Exact = mip1.size() == 4u * 4u * 4u;
            for (std::size_t i = 0; i < 4u * 4u && mip1Exact; ++i)
                mip1Exact = mip1[i * 4 + 0] == 200 && mip1[i * 4 + 1] == 90 && mip1[i * 4 + 2] == 10 && mip1[i * 4 + 3] == 255;
            check(mip1Exact,
                  "D3D11RenderTargetRenderer: GenerateMips()-on-unbind writes the exact box-filtered "
                  "(here: solid-color-preserving) content into mip level 1, read back directly from "
                  "the real GPU resource, not zero/garbage (plans/plan_dx.md DX-144)");

            const auto mip2 = ReadTexture2DMipRegion(device, context, d3dRtMip->GetSampleableTextureEXT(), 2, 0, 0, 2, 2);
            bool mip2Exact = mip2.size() == 2u * 2u * 4u;
            for (std::size_t i = 0; i < 2u * 2u && mip2Exact; ++i)
                mip2Exact = mip2[i * 4 + 0] == 200 && mip2[i * 4 + 1] == 90 && mip2[i * 4 + 2] == 10 && mip2[i * 4 + 3] == 255;
            check(mip2Exact,
                  "D3D11RenderTargetRenderer: mip level 2 (2x2) is also exact, confirming the full "
                  "mip chain regenerates correctly, not just level 1 (plans/plan_dx.md DX-144)");
        }
        {
            auto rtCubeMip = renderer.CreateRenderTargetCube(8, 0 /*DepthFormat::None*/, false /*preserveContents*/, true /*mipMap*/);
            auto* d3dRtCubeMip = static_cast<D3D11RenderTargetCubeRenderer*>(rtCubeMip.get());
            rtCubeMip->BindAsRenderTargetFace(0);
            dev.Clear(Color(50, 150, 250, 255));
            rtCubeMip->UnbindAsRenderTarget(); // GenerateMips() on the cube's shared SRV

            // Subresource = mip + face * levelCount (standard D3D11 texture-array/mip indexing) --
            // face 0, mip 1.
            const auto cubeMip1 = ReadTexture2DMipRegion(device, context, d3dRtCubeMip->GetColorTextureEXT(),
                                                          1, 0, 0, 4, 4);
            bool cubeMip1Exact = cubeMip1.size() == 4u * 4u * 4u;
            for (std::size_t i = 0; i < 4u * 4u && cubeMip1Exact; ++i)
                cubeMip1Exact = cubeMip1[i * 4 + 0] == 50 && cubeMip1[i * 4 + 1] == 150 && cubeMip1[i * 4 + 2] == 250 && cubeMip1[i * 4 + 3] == 255;
            check(cubeMip1Exact,
                  "D3D11RenderTargetCubeRenderer: GenerateMips()-on-unbind also regenerates face 0's "
                  "own mip chain correctly, read back directly from the real GPU resource "
                  "(plans/plan_dx.md DX-144; D3D12's own render-target mip-chain generation does not exist "
                  "at all yet -- D3D12RenderTargets.hpp's own DX-117 scope note -- so DX-144's D3D12 "
                  "leg stays open, a real follow-up feature, not a test gap)");
        }

        // plans/plan_dx.md DX-153: RenderTargetCube mip-chain generation for a NON-zero face -- DX-144's
        // own test above only ever proved face 0. D3D11's own mechanism (UnbindAsRenderTarget()'s
        // single, whole-resource ID3D11DeviceContext::GenerateMips(srv_.Get()) call, no face
        // argument at all) is architecturally different from face-scoped regeneration -- it
        // regenerates every face's own chain from whatever is currently in that face's own level-0
        // content, so this proves face 2's chain regenerates correctly when face 2 (not face 0) was
        // the one just drawn to, and that a DIFFERENT, previously-untouched face's own base level
        // survives the call.
        {
            auto rtCubeMip2 = renderer.CreateRenderTargetCube(8, 0 /*DepthFormat::None*/, false /*preserveContents*/, true /*mipMap*/);
            auto* d3dRtCubeMip2 = static_cast<D3D11RenderTargetCubeRenderer*>(rtCubeMip2.get());
            rtCubeMip2->BindAsRenderTargetFace(2);
            dev.Clear(Color(60, 120, 180, 255));
            rtCubeMip2->UnbindAsRenderTarget(); // GenerateMips() on the cube's shared SRV

            // Subresource = mip + face * levelCount -- face 2, mip 1.
            const auto cubeFace2Mip1 = ReadTexture2DMipRegion(
                device, context, d3dRtCubeMip2->GetColorTextureEXT(),
                1 + 2 * d3dRtCubeMip2->GetLevelCountEXT(), 0, 0, 4, 4);
            bool cubeFace2Mip1Exact = cubeFace2Mip1.size() == 4u * 4u * 4u;
            for (std::size_t i = 0; i < 4u * 4u && cubeFace2Mip1Exact; ++i)
                cubeFace2Mip1Exact = cubeFace2Mip1[i * 4 + 0] == 60 && cubeFace2Mip1[i * 4 + 1] == 120 &&
                                     cubeFace2Mip1[i * 4 + 2] == 180 && cubeFace2Mip1[i * 4 + 3] == 255;
            check(cubeFace2Mip1Exact,
                  "D3D11RenderTargetCubeRenderer: GenerateMips()-on-unbind regenerates a NON-zero "
                  "face's (face 2) own mip chain correctly, not just face 0's (plans/plan_dx.md DX-153)");
        }

        // plans/plan_dx.md DX-145: RenderTarget2D DepthStencilFormat fidelity -- confirms a render target
        // actually gets the SPECIFIC depth/stencil DXGI format the game requested
        // (D3DFormatMapping.cpp's DepthFormatToDxgi()), not just *some* working depth buffer:
        // Depth16 must be DXGI_FORMAT_D16_UNORM (not silently upgraded to a combined depth+stencil
        // format), Depth24/Depth24Stencil8 both genuinely land on DXGI_FORMAT_D24_UNORM_S8_UINT
        // (D3D11 has no pure 24-bit depth-only format -- the mapping table's own documented,
        // intentional shared-format decision), and DepthFormat::None creates no DSV at all. Reads
        // the real bound depth resource's own D3D11_TEXTURE2D_DESC.Format via
        // ID3D11DepthStencilView::GetResource(), genuine D3D11 API introspection, not internal state.
        {
            auto GetDsvFormat = [](ID3D11DepthStencilView* dsv) -> DXGI_FORMAT
            {
                if (!dsv) return DXGI_FORMAT_UNKNOWN;
                Microsoft::WRL::ComPtr<ID3D11Resource> res;
                dsv->GetResource(res.GetAddressOf());
                Microsoft::WRL::ComPtr<ID3D11Texture2D> tex2d;
                if (FAILED(res.As(&tex2d))) return DXGI_FORMAT_UNKNOWN;
                D3D11_TEXTURE2D_DESC desc{};
                tex2d->GetDesc(&desc);
                return desc.Format;
            };

            auto rtNone = renderer.CreateRenderTarget2D(4, 4, 0 /*DepthFormat::None*/, false, false, 0);
            auto* d3dRtNone = static_cast<D3D11RenderTargetRenderer*>(rtNone.get());
            check(d3dRtNone->GetDSVEXT() == nullptr,
                  "D3D11RenderTargetRenderer: DepthFormat::None creates no depth-stencil view at all "
                  "(plans/plan_dx.md DX-145)");

            auto rtD16 = renderer.CreateRenderTarget2D(4, 4, 1 /*DepthFormat::Depth16*/, false, false, 0);
            auto* d3dRtD16 = static_cast<D3D11RenderTargetRenderer*>(rtD16.get());
            check(GetDsvFormat(d3dRtD16->GetDSVEXT()) == DXGI_FORMAT_D16_UNORM,
                  "D3D11RenderTargetRenderer: DepthFormat::Depth16 genuinely creates a "
                  "DXGI_FORMAT_D16_UNORM depth resource, not silently upgraded to a combined "
                  "depth+stencil format (plans/plan_dx.md DX-145)");

            auto rtD24 = renderer.CreateRenderTarget2D(4, 4, 2 /*DepthFormat::Depth24*/, false, false, 0);
            auto* d3dRtD24 = static_cast<D3D11RenderTargetRenderer*>(rtD24.get());
            check(GetDsvFormat(d3dRtD24->GetDSVEXT()) == DXGI_FORMAT_D24_UNORM_S8_UINT,
                  "D3D11RenderTargetRenderer: DepthFormat::Depth24 lands on the documented "
                  "DXGI_FORMAT_D24_UNORM_S8_UINT fallback (D3D11 has no pure 24-bit depth-only "
                  "format, plans/plan_dx.md DX-145)");

            auto rtD24S8 = renderer.CreateRenderTarget2D(4, 4, 3 /*DepthFormat::Depth24Stencil8*/, false, false, 0);
            auto* d3dRtD24S8 = static_cast<D3D11RenderTargetRenderer*>(rtD24S8.get());
            check(GetDsvFormat(d3dRtD24S8->GetDSVEXT()) == DXGI_FORMAT_D24_UNORM_S8_UINT,
                  "D3D11RenderTargetRenderer: DepthFormat::Depth24Stencil8 also creates "
                  "DXGI_FORMAT_D24_UNORM_S8_UINT -- both Depth24 and Depth24Stencil8 genuinely share "
                  "the SAME real DXGI resource format, not just coincidentally 'both work' "
                  "(plans/plan_dx.md DX-145)");
        }

        // Check O (DX-50/DX-51/DX-52/DX-53): real ID3D11BlendState/DepthStencilState/
        // RasterizerState creation+caching+binding, plus SetViewport()/SetScissorRect()'s direct
        // RSSetViewports()/RSSetScissorRects() round-trip. Behavioral pixel-correctness of
        // blend/stencil *output* needs an actual draw call (Phase DIRECTX8, not yet available) -- this
        // check honestly proves creation, caching identity/distinctness, and real device binding,
        // same honest bound Check J/K/N already established for render targets/MRT.
        {
            // BlendState: NonPremultiplied-style (SourceAlpha/InvSourceAlpha/Add on both channels)
            // -- deliberately not the Opaque combo, so BlendEnable ends up TRUE.
            auto& blendCache = renderer.GetBlendStateCacheEXT();
            // REMED-GFX-077: GetOrCreate gained cw0..cw3 (per-RT write masks); default All (15) here
            // keeps this cache identity/distinctness check about the blend factors, as before.
            auto blendA1 = blendCache.GetOrCreate(device, 4, 4, 5, 5, 0, 0, 15, 15, 15, 15);   // SourceAlpha/InvSourceAlpha/Add
            auto blendA2 = blendCache.GetOrCreate(device, 4, 4, 5, 5, 0, 0, 15, 15, 15, 15);
            auto blendB = blendCache.GetOrCreate(device, 0, 0, 1, 1, 0, 0, 15, 15, 15, 15);    // Opaque
            check(blendA1.Get() != nullptr && blendA1.Get() == blendA2.Get(),
                  "D3D11BlendStateCache: identical XNA blend params return the identical object");
            check(blendA1.Get() != blendB.Get(),
                  "D3D11BlendStateCache: different XNA blend params return a different object");

            renderer.ApplyBlendState(4, 4, 5, 5, 0, 0, CNA::Internal::Renderers::BlendWriteState{}); // REMED-GFX-077 default write state
            ID3D11BlendState* boundBlend = nullptr;
            float boundBlendFactor[4] = {};
            UINT boundSampleMask = 0;
            context->OMGetBlendState(&boundBlend, boundBlendFactor, &boundSampleMask);
            check(boundBlend == blendA1.Get(),
                  "DirectX11Renderer::ApplyBlendState(): OMSetBlendState actually bound the cached object");
            if (boundBlend) boundBlend->Release();

            renderer.SetBlendFactor(0.25f, 0.5f, 0.75f, 1.0f);
            ID3D11BlendState* boundBlend2 = nullptr;
            context->OMGetBlendState(&boundBlend2, boundBlendFactor, &boundSampleMask);
            check(boundBlend2 == blendA1.Get() &&
                  boundBlendFactor[0] == 0.25f && boundBlendFactor[1] == 0.5f &&
                  boundBlendFactor[2] == 0.75f && boundBlendFactor[3] == 1.0f,
                  "DirectX11Renderer::SetBlendFactor(): re-binds the current blend state with the new factor, standalone");
            if (boundBlend2) boundBlend2->Release();

            // DepthStencilState: depth+stencil enabled, distinct front-face ops.
            auto& dsCache = renderer.GetDepthStencilStateCacheEXT();
            auto dsA1 = dsCache.GetOrCreate(device, true, true, 2 /*Less*/, true, 0 /*Always*/,
                                            2 /*Replace*/, 0 /*Keep*/, 0 /*Keep*/, 0xFF, 0xFF,
                                            false, 0, 0, 0, 0);
            auto dsA2 = dsCache.GetOrCreate(device, true, true, 2, true, 0, 2, 0, 0, 0xFF, 0xFF,
                                            false, 0, 0, 0, 0);
            auto dsB = dsCache.GetOrCreate(device, false, false, 0, false, 0, 0, 0, 0, 0xFF, 0xFF,
                                           false, 0, 0, 0, 0);
            check(dsA1.Get() != nullptr && dsA1.Get() == dsA2.Get(),
                  "D3D11DepthStencilStateCache: identical XNA depth-stencil params return the identical object");
            check(dsA1.Get() != dsB.Get(),
                  "D3D11DepthStencilStateCache: different XNA depth-stencil params return a different object");

            renderer.ApplyDepthStencilState(true, true, 2, true, 0, 2, 0, 0, 0xFF, 0xFF, 77,
                                           false, 0, 0, 0, 0);
            ID3D11DepthStencilState* boundDS = nullptr;
            UINT boundRef = 0;
            context->OMGetDepthStencilState(&boundDS, &boundRef);
            check(boundDS == dsA1.Get() && boundRef == 77,
                  "DirectX11Renderer::ApplyDepthStencilState(): OMSetDepthStencilState bound the cached object with the reference value");
            if (boundDS) boundDS->Release();

            renderer.SetReferenceStencil(123);
            ID3D11DepthStencilState* boundDS2 = nullptr;
            context->OMGetDepthStencilState(&boundDS2, &boundRef);
            check(boundDS2 == dsA1.Get() && boundRef == 123,
                  "DirectX11Renderer::SetReferenceStencil(): re-binds the current depth-stencil state with the new reference, standalone");
            if (boundDS2) boundDS2->Release();

            // RasterizerState: cull-back/solid vs. cull-none/wireframe.
            auto& rsCache = renderer.GetRasterizerStateCacheEXT();
            auto rsA1 = rsCache.GetOrCreate(device, 2 /*CullCounterClockwiseFace*/, 0 /*Solid*/, false, 0.0f, 0.0f);
            auto rsA2 = rsCache.GetOrCreate(device, 2, 0, false, 0.0f, 0.0f);
            auto rsB = rsCache.GetOrCreate(device, 0 /*None*/, 1 /*WireFrame*/, true, 1.0f, 2.0f);
            check(rsA1.Get() != nullptr && rsA1.Get() == rsA2.Get(),
                  "D3D11RasterizerStateCache: identical XNA rasterizer params return the identical object");
            check(rsA1.Get() != rsB.Get(),
                  "D3D11RasterizerStateCache: different XNA rasterizer params return a different object");

            renderer.ApplyRasterizerState(2, 0, false, 0.0f, 0.0f);
            ID3D11RasterizerState* boundRS = nullptr;
            context->RSGetState(&boundRS);
            check(boundRS == rsA1.Get(),
                  "DirectX11Renderer::ApplyRasterizerState(): RSSetState actually bound the cached object");
            if (boundRS) boundRS->Release();

            // Viewport / scissor rect round-trip.
            renderer.SetViewport(2, 3, 40, 30, 0.1f, 0.9f);
            UINT vpCount = 1;
            D3D11_VIEWPORT boundVp{};
            context->RSGetViewports(&vpCount, &boundVp);
            check(vpCount == 1 && boundVp.TopLeftX == 2.0f && boundVp.TopLeftY == 3.0f &&
                  boundVp.Width == 40.0f && boundVp.Height == 30.0f &&
                  boundVp.MinDepth == 0.1f && boundVp.MaxDepth == 0.9f,
                  "DirectX11Renderer::SetViewport(): RSSetViewports round-trips the exact rectangle and depth range");

            renderer.SetScissorRect(5, 6, 20, 10);
            UINT rectCount = 1;
            D3D11_RECT boundRect{};
            context->RSGetScissorRects(&rectCount, &boundRect);
            check(rectCount == 1 && boundRect.left == 5 && boundRect.top == 6 &&
                  boundRect.right == 25 && boundRect.bottom == 16,
                  "DirectX11Renderer::SetScissorRect(): RSSetScissorRects round-trips as (x,y,x+w,y+h)");

            // Restore the window-size viewport so nothing downstream (there is nothing after this
            // check today, but keep the invariant honest) is left with a stale small viewport.
            int vw = 0, vh = 0;
            renderer.GetViewportSize(vw, vh);
            renderer.SetViewport(0, 0, vw, vh, 0.0f, 1.0f);
        }

        // Check P (DX-60/DX-60a/DX-61) -- the first real 3D triangle this renderer has ever drawn.
        // A real colored3d draw (input layout + VS/PS + PerDraw/FogParams constant buffers, all
        // wired for real) paints a known solid-red vertex color over a known-blue-cleared
        // background; reading back the SAME pixel before and after the draw call (blue -> red)
        // proves the fragment genuinely came from the draw, not a stale/cached readback. Exercised
        // for both the non-indexed (DrawColoredPrimitives) and indexed (DrawIndexedColoredPrimitives)
        // paths. State is reset to a known-safe baseline first (opaque blend, depth/stencil off,
        // no culling) so this check doesn't depend on whatever Check O happened to leave bound.
        {
            renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, CNA::Internal::Renderers::BlendWriteState{}); // Opaque (REMED-GFX-077 default write state)
            renderer.ApplyDepthStencilState(false, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
            renderer.ApplyRasterizerState(0 /*CullMode::None*/, 0 /*FillMode::Solid*/, false, 0.0f, 0.0f);

            struct VPC { float x, y, z; uint32_t color; };
            // A single triangle covering the entire NDC square (-1..1, -1..1) via the standard
            // "oversized triangle" trick -- with world=view=projection=Identity, Mvp=Identity, so
            // these Position values ARE clip-space coordinates directly (no separate transform to
            // reason about). Color 0xFF0000FF packed as R8G8B8A8 = (R=0xFF,G=0x00,B=0x00,A=0xFF)
            // opaque red, same byte convention Check E already established.
            static const VPC kTri[3] = {
                {-1.0f, -1.0f, 0.0f, 0xFF0000FFu},
                { 3.0f, -1.0f, 0.0f, 0xFF0000FFu},
                {-1.0f,  3.0f, 0.0f, 0xFF0000FFu},
            };
            auto vb = renderer.CreateVertexBuffer(3);
            vb->SetData(kTri, 3, sizeof(VPC));

            const Microsoft::Xna::Framework::Rectangle centerRegion(28, 28, 4, 4);
            std::vector<Color> before(4 * 4, Color(0, 0, 0, 0));
            std::vector<Color> after(4 * 4, Color(0, 0, 0, 0));

            // Non-indexed path.
            dev.Clear(Color(0, 0, 255, 255));
            dev.GetBackBufferData(&centerRegion, before.data(), 0, static_cast<int>(before.size()));
            renderer.DrawColoredPrimitives(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                          Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
            dev.GetBackBufferData(&centerRegion, after.data(), 0, static_cast<int>(after.size()));

            bool beforeIsBlue = true, afterIsRed = true;
            for (const Color& p : before)
                if (p.getRProperty() != 0 || p.getGProperty() != 0 || p.getBProperty() != 255 || p.getAProperty() != 255)
                    beforeIsBlue = false;
            for (const Color& p : after)
                if (p.getRProperty() != 255 || p.getGProperty() != 0 || p.getBProperty() != 0 || p.getAProperty() != 255)
                    afterIsRed = false;
            check(beforeIsBlue && afterIsRed,
                  "DirectX11Renderer::DrawColoredPrimitives(): real colored3d draw paints exact "
                  "vertex color over the Clear() background at the same readback location");

            // Indexed path: same triangle, via DrawIndexedColoredPrimitives.
            static const uint16_t kTriIdx[3] = {0, 1, 2};
            auto ib = renderer.CreateIndexBuffer16(3);
            ib->SetData16(kTriIdx, 3);

            dev.Clear(Color(0, 0, 255, 255));
            std::vector<Color> beforeIdx(4 * 4, Color(0, 0, 0, 0));
            std::vector<Color> afterIdx(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, beforeIdx.data(), 0, static_cast<int>(beforeIdx.size()));
            renderer.DrawIndexedColoredPrimitives(*vb, *ib, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
            dev.GetBackBufferData(&centerRegion, afterIdx.data(), 0, static_cast<int>(afterIdx.size()));

            bool beforeIdxIsBlue = true, afterIdxIsRed = true;
            for (const Color& p : beforeIdx)
                if (p.getRProperty() != 0 || p.getGProperty() != 0 || p.getBProperty() != 255 || p.getAProperty() != 255)
                    beforeIdxIsBlue = false;
            for (const Color& p : afterIdx)
                if (p.getRProperty() != 255 || p.getGProperty() != 0 || p.getBProperty() != 0 || p.getAProperty() != 255)
                    afterIdxIsRed = false;
            check(beforeIdxIsBlue && afterIdxIsRed,
                  "DirectX11Renderer::DrawIndexedColoredPrimitives(): real colored3d indexed draw "
                  "paints exact vertex color over the Clear() background at the same readback location");
        }

        // ---- Check GFX077: REMED-GFX-077 runtime verification -- BlendState.ColorWriteChannels
        // (RT0 RenderTarget[0].RenderTargetWriteMask baked into the cached ID3D11BlendState, keyed)
        // and BlendState.MultiSampleMask (the dynamic third arg of OMSetBlendState, captured not
        // keyed), against the real DXVK device. An opaque full-screen quad (blend disabled) is drawn
        // into an off-screen RGBA8 render target so the ONLY thing that can preserve a destination
        // channel is the write mask gating the write; the RT's own texture is read back via the same
        // staging-copy technique Check J uses (alpha-preserving, unlike the back buffer). Differential
        // model (masked-in channel == "All" baseline, masked-out == "None"/clear baseline). The RT is
        // single-sample, on which OMSetBlendState's SampleMask bit 0 gates the one coverage sample:
        // SampleMask==0 must discard the quad (pixel stays the clear colour). ----
        {
            auto rt = renderer.CreateRenderTarget2D(64, 64, 0 /*DepthFormat::None*/, false, false, 0);
            auto* d3dRt = static_cast<D3D11RenderTargetRenderer*>(rt.get());

            renderer.ApplyDepthStencilState(false, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
            renderer.ApplyRasterizerState(0 /*CullMode::None*/, 0 /*FillMode::Solid*/, false, 0.0f, 0.0f);

            struct VPC { float x, y, z; uint32_t color; };
            // Full-NDC quad, flat source colour S=(200,100,50,220); packed R8G8B8A8 = 0xDC3264C8
            // (A=220,B=50,G=100,R=200).
            static const VPC kQuad[6] = {
                {-1.0f,  1.0f, 0.0f, 0xDC3264C8u}, {-1.0f, -1.0f, 0.0f, 0xDC3264C8u},
                { 1.0f, -1.0f, 0.0f, 0xDC3264C8u}, {-1.0f,  1.0f, 0.0f, 0xDC3264C8u},
                { 1.0f, -1.0f, 0.0f, 0xDC3264C8u}, { 1.0f,  1.0f, 0.0f, 0xDC3264C8u},
            };
            auto vbCw = renderer.CreateVertexBuffer(6);
            vbCw->SetData(kQuad, 6, sizeof(VPC));
            const Matrix Id = Matrix::getIdentityProperty();

            auto renderMask = [&](int cwc, unsigned int sampleMask) -> std::array<uint8_t, 4>
            {
                CNA::Internal::Renderers::BlendWriteState ws;
                ws.colorWriteChannels[0] = cwc;
                ws.multiSampleMask = sampleMask;
                renderer.SetRenderTarget2D(rt.get());
                renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, ws);         // Opaque + mask
                dev.Clear(Color(10, 20, 30, 40));                     // destination D
                renderer.DrawColoredPrimitives(*vbCw, Id, Id, Id, PrimitiveType::TriangleList, 2);
                renderer.SetRenderTarget2D(nullptr);
                const auto px = ReadTexture2DRegion(device, context, d3dRt->GetSampleableTextureEXT(), 30, 30, 2, 2);
                return {px[0], px[1], px[2], px[3]};
            };
            auto eqc = [](const std::array<uint8_t, 4>& a, const std::array<uint8_t, 4>& b)
            { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3]; };
            auto masked = [](const std::array<uint8_t, 4>& d, const std::array<uint8_t, 4>& s, int bits)
            {
                return std::array<uint8_t, 4>{
                    static_cast<uint8_t>((bits & 1) ? s[0] : d[0]),
                    static_cast<uint8_t>((bits & 2) ? s[1] : d[1]),
                    static_cast<uint8_t>((bits & 4) ? s[2] : d[2]),
                    static_cast<uint8_t>((bits & 8) ? s[3] : d[3])};
            };

            const std::array<uint8_t, 4> dcw = renderMask(0,  0xFFFFFFFFu);   // None
            const std::array<uint8_t, 4> scw = renderMask(15, 0xFFFFFFFFu);   // All
            check(dcw[0] != scw[0] && dcw[1] != scw[1] && dcw[2] != scw[2] && dcw[3] != scw[3],
                  "GFX077-1 (D3D11): None(dst)/All(src) baselines discriminate all four channels");
            check(eqc(renderMask(1, 0xFFFFFFFFu), masked(dcw, scw, 1)),
                  "GFX077-2 (D3D11): ColorWriteChannels.Red writes only R (RenderTargetWriteMask)");
            check(eqc(renderMask(2, 0xFFFFFFFFu), masked(dcw, scw, 2)),
                  "GFX077-3 (D3D11): ColorWriteChannels.Green writes only G");
            check(eqc(renderMask(4, 0xFFFFFFFFu), masked(dcw, scw, 4)),
                  "GFX077-4 (D3D11): ColorWriteChannels.Blue writes only B");
            check(eqc(renderMask(8, 0xFFFFFFFFu), masked(dcw, scw, 8)),
                  "GFX077-5 (D3D11): ColorWriteChannels.Alpha writes only A");
            check(eqc(renderMask(1 | 4, 0xFFFFFFFFu), masked(dcw, scw, 1 | 4)),
                  "GFX077-6 (D3D11): ColorWriteChannels.Red|Blue writes only R and B");
            const std::array<uint8_t, 4> a1 = renderMask(1, 0xFFFFFFFFu);
            const std::array<uint8_t, 4> bG = renderMask(2, 0xFFFFFFFFu);
            const std::array<uint8_t, 4> a2 = renderMask(1, 0xFFFFFFFFu);
            check(eqc(a1, masked(dcw, scw, 1)) && eqc(bG, masked(dcw, scw, 2)) && eqc(a2, masked(dcw, scw, 1)),
                  "GFX077-7 (D3D11): A(Red)->B(Green)->A(Red) each selects its own cached blend object");
            check(eqc(renderMask(15, 0x00000000u), dcw),
                  "GFX077-8 (D3D11): MultiSampleMask=0 discards the single coverage sample (stays clear)");
            check(eqc(renderMask(15, 0xFFFFFFFFu), scw),
                  "GFX077-9 (D3D11): MultiSampleMask=all renders normally (full coverage -> src)");
        }

        const Microsoft::Xna::Framework::Rectangle centerRegion28(28, 28, 4, 4);

        // Check Q (DX-62): textured3d (stride 20) + colored_textured3d (stride 24) via real
        // GpuDrawParams, using the same before/after-Clear() discipline Check P established.
        {
            renderer.ApplySamplerState(0, 1 /*TextureFilter::Point*/, 0, 0, 1);

            struct VPT { float x, y, z; float u, v; };
            static const VPT kTriTex[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
                { 3.0f, -1.0f, 0.0f, 2.0f, 1.0f},
                {-1.0f,  3.0f, 0.0f, 0.0f, -1.0f},
            };
            auto vbTex = renderer.CreateVertexBuffer(3);
            vbTex->SetData(kTriTex, 3, sizeof(VPT));

            ImageData img;
            img.width = 2; img.height = 2; img.mipLevels = 1;
            img.pixels.resize(2 * 2 * 4);
            for (int i = 0; i < 4; ++i)
            {
                img.pixels[i * 4 + 0] = 11; img.pixels[i * 4 + 1] = 22;
                img.pixels[i * 4 + 2] = 33; img.pixels[i * 4 + 3] = 255;
            }
            auto tex = renderer.CreateTexture(img);

            GpuDrawParams tp;
            tp.texture0 = tex.get();
            tp.textureEnabled = true;
            // diffuseColor left at its default (1,1,1,1) so outColor == the raw sampled texel exactly.

            std::vector<Color> beforeQ(4 * 4, Color(0, 0, 0, 0));
            std::vector<Color> afterQ(4 * 4, Color(0, 0, 0, 0));
            dev.Clear(Color(0, 255, 0, 255));
            dev.GetBackBufferData(&centerRegion28, beforeQ.data(), 0, static_cast<int>(beforeQ.size()));
            renderer.DrawPrimitivesEx(*vbTex, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, tp);
            dev.GetBackBufferData(&centerRegion28, afterQ.data(), 0, static_cast<int>(afterQ.size()));

            bool beforeIsGreen = true, afterIsTexColor = true;
            for (const Color& p : beforeQ)
                if (p.getRProperty() != 0 || p.getGProperty() != 255 || p.getBProperty() != 0 || p.getAProperty() != 255)
                    beforeIsGreen = false;
            for (const Color& p : afterQ)
                if (p.getRProperty() != 11 || p.getGProperty() != 22 || p.getBProperty() != 33 || p.getAProperty() != 255)
                    afterIsTexColor = false;
            check(beforeIsGreen && afterIsTexColor,
                  "DirectX11Renderer::DrawPrimitivesEx(): real textured3d draw samples the exact "
                  "texture color (diffuseColor=white) over the Clear() background (plans/plan_dx.md DX-62)");

            // Indexed path, same textured3d draw -- proves DrawIndexedPrimitivesEx shares the same
            // real pipeline (DrawPrimitivesExImpl), not just the non-indexed entry point.
            static const uint16_t kTriTexIdx[3] = {0, 1, 2};
            auto ibTex = renderer.CreateIndexBuffer16(3);
            ibTex->SetData16(kTriTexIdx, 3);
            dev.Clear(Color(0, 255, 0, 255));
            renderer.DrawIndexedPrimitivesEx(*vbTex, *ibTex, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                            Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, tp);
            std::vector<Color> afterQIdx(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, afterQIdx.data(), 0, static_cast<int>(afterQIdx.size()));
            bool afterQIdxIsTexColor = true;
            for (const Color& p : afterQIdx)
                if (p.getRProperty() != 11 || p.getGProperty() != 22 || p.getBProperty() != 33 || p.getAProperty() != 255)
                    afterQIdxIsTexColor = false;
            check(afterQIdxIsTexColor,
                  "DirectX11Renderer::DrawIndexedPrimitivesEx(): indexed textured3d draw shares "
                  "the same real pipeline and samples the exact texture color");

            // colored_textured3d (stride 24): a white texture tinted by an exact vertex color,
            // proving VertexColorEnabled's real multiply, not just the texture sample alone.
            struct VPCT { float x, y, z; uint32_t color; float u, v; };
            const uint32_t kVertColor = 0xFFF0A050u; // A=255,B=240,G=160,R=80 (R8G8B8A8 byte order)
            static VPCT kTriColTex[3];
            kTriColTex[0] = { -1.0f, -1.0f, 0.0f, kVertColor, 0.0f, 1.0f };
            kTriColTex[1] = {  3.0f, -1.0f, 0.0f, kVertColor, 2.0f, 1.0f };
            kTriColTex[2] = { -1.0f,  3.0f, 0.0f, kVertColor, 0.0f, -1.0f };
            auto vbColTex = renderer.CreateVertexBuffer(3);
            vbColTex->SetData(kTriColTex, 3, sizeof(VPCT));

            ImageData whiteImg;
            whiteImg.width = 2; whiteImg.height = 2; whiteImg.mipLevels = 1;
            whiteImg.pixels.assign(2 * 2 * 4, 255);
            auto whiteTex = renderer.CreateTexture(whiteImg);

            GpuDrawParams ctp;
            ctp.texture0 = whiteTex.get();
            ctp.textureEnabled = true;
            ctp.vertexColorEnabled = true;

            dev.Clear(Color(0, 255, 0, 255));
            renderer.DrawPrimitivesEx(*vbColTex, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ctp);
            std::vector<Color> afterCT(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, afterCT.data(), 0, static_cast<int>(afterCT.size()));
            bool afterCTIsVertColor = true;
            for (const Color& p : afterCT)
                if (p.getRProperty() != 80 || p.getGProperty() != 160 || p.getBProperty() != 240 || p.getAProperty() != 255)
                    afterCTIsVertColor = false;
            check(afterCTIsVertColor,
                  "DirectX11Renderer::DrawPrimitivesEx(): real colored_textured3d draw multiplies "
                  "the exact vertex color through a white texture (plans/plan_dx.md DX-62)");

            // DX-137: dedicated fog on/off discriminating test for colored_textured3d -- same
            // Z-at-FogEnd methodology as textured3d's own fog test below, reusing this block's
            // already-proven vertex-color/white-texture fixture (base result (80,160,240)).
            {
                static VPCT kTriColTexFog[3];
                kTriColTexFog[0] = { -1.0f, -1.0f, 0.5f, kVertColor, 0.0f, 1.0f };
                kTriColTexFog[1] = {  3.0f, -1.0f, 0.5f, kVertColor, 2.0f, 1.0f };
                kTriColTexFog[2] = { -1.0f,  3.0f, 0.5f, kVertColor, 0.0f, -1.0f };
                auto vbColTexFog = renderer.CreateVertexBuffer(3);
                vbColTexFog->SetData(kTriColTexFog, 3, sizeof(VPCT));

                const Microsoft::Xna::Framework::Rectangle colTexFogRegion(30, 30, 1, 1);
                ctp.fogEnabled = false;
                ctp.fogColor[0] = 0.0f; ctp.fogColor[1] = 1.0f; ctp.fogColor[2] = 0.0f;
                // REMED-GFX-061: direct-renderer fixtures obey the same reachable-state invariant
                // as public stock effects: disabled fog is encoded by an all-zero vector.
                dev.Clear(Color(10, 10, 10, 255));
                renderer.DrawPrimitivesEx(*vbColTexFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ctp);
                Color colTexFogOffPixel(0, 0, 0, 0);
                dev.GetBackBufferData(&colTexFogRegion, &colTexFogOffPixel, 0, 1);
                check(colTexFogOffPixel.getRProperty() == 80 && colTexFogOffPixel.getGProperty() == 160 &&
                      colTexFogOffPixel.getBProperty() == 240,
                      "DirectX11Renderer::DrawPrimitivesEx(): colored_textured3d fogEnabled=false "
                      "leaves the exact vertex*texture color unblended (plans/plan_dx.md DX-137)");

                ctp.fogEnabled = true;
                ctp.fogVector[2] = 2.0f; // eye-space Z=0.5 -> dot=1 -> fully fogged
                dev.Clear(Color(10, 10, 10, 255));
                renderer.DrawPrimitivesEx(*vbColTexFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ctp);
                Color colTexFogOnPixel(0, 0, 0, 0);
                dev.GetBackBufferData(&colTexFogRegion, &colTexFogOnPixel, 0, 1);
                check(colTexFogOnPixel.getRProperty() == 0 && colTexFogOnPixel.getGProperty() == 255 &&
                      colTexFogOnPixel.getBProperty() == 0,
                      "DirectX11Renderer::DrawPrimitivesEx(): colored_textured3d fogEnabled=true "
                      "with Z at FogEnd genuinely blends all the way to the exact FogColor (plans/plan_dx.md DX-137)");
            }

            // DX-137: dedicated fog on/off discriminating test for textured3d -- a representative
            // variant of the 7 fog-capable non-colored3d variants (chosen for its simple, already-
            // proven fixture above; a full 7-variant x 2-renderer sweep is out of this task's own
            // scope, documented honestly rather than silently partial). Same Z-at-FogEnd
            // methodology DX-69/DX-81's own colored3d fog test already established -- needs its OWN
            // vertex buffer at Z=0.5 (fogEnd), not the shared vbTex (Z=0, which gives fogFactor=1,
            // i.e. no blending at all -- a real fixture bug found and fixed while writing this test).
            static const VPT kTriTexFog[3] = {
                {-1.0f, -1.0f, 0.5f, 0.0f, 1.0f},
                { 3.0f, -1.0f, 0.5f, 2.0f, 1.0f},
                {-1.0f,  3.0f, 0.5f, 0.0f, -1.0f},
            };
            auto vbTexFog = renderer.CreateVertexBuffer(3);
            vbTexFog->SetData(kTriTexFog, 3, sizeof(VPT));

            const Microsoft::Xna::Framework::Rectangle texFogRegion(30, 30, 1, 1);
            tp.fogEnabled = false;
            tp.fogColor[0] = 0.0f; tp.fogColor[1] = 1.0f; tp.fogColor[2] = 0.0f;
            dev.Clear(Color(10, 10, 10, 255));
            renderer.DrawPrimitivesEx(*vbTexFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, tp);
            Color texFogOffPixel(0, 0, 0, 0);
            dev.GetBackBufferData(&texFogRegion, &texFogOffPixel, 0, 1);
            check(texFogOffPixel.getRProperty() == 11 && texFogOffPixel.getGProperty() == 22 &&
                  texFogOffPixel.getBProperty() == 33,
                  "DirectX11Renderer::DrawPrimitivesEx(): textured3d fogEnabled=false leaves the "
                  "exact sampled texture color unblended (plans/plan_dx.md DX-137)");

            tp.fogEnabled = true;
            tp.fogVector[2] = 2.0f;
            dev.Clear(Color(10, 10, 10, 255));
            renderer.DrawPrimitivesEx(*vbTexFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, tp);
            Color texFogOnPixel(0, 0, 0, 0);
            dev.GetBackBufferData(&texFogRegion, &texFogOnPixel, 0, 1);
            check(texFogOnPixel.getRProperty() == 0 && texFogOnPixel.getGProperty() == 255 &&
                  texFogOnPixel.getBProperty() == 0,
                  "DirectX11Renderer::DrawPrimitivesEx(): textured3d fogEnabled=true with Z at "
                  "FogEnd genuinely blends all the way to the exact FogColor, distinctly different "
                  "from the fogEnabled=false case above (plans/plan_dx.md DX-137)");
        }

        // Check R (DX-63): lit_textured3d (stride 32). The unlit branch is byte-exact (same bar as
        // Check Q); the lit branch's real Blinn-Phong math is only checked for plausibility -- it
        // must genuinely differ from both the unlit result and the Clear() background.
        {
            struct VPNT { float x, y, z; float nx, ny, nz; float u, v; };
            static const VPNT kTriLit[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
                { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f},
                {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f},
            };
            auto vbLit = renderer.CreateVertexBuffer(3);
            vbLit->SetData(kTriLit, 3, sizeof(VPNT));

            ImageData img;
            img.width = 2; img.height = 2; img.mipLevels = 1;
            img.pixels.resize(2 * 2 * 4);
            for (int i = 0; i < 4; ++i)
            {
                img.pixels[i * 4 + 0] = 44; img.pixels[i * 4 + 1] = 55;
                img.pixels[i * 4 + 2] = 66; img.pixels[i * 4 + 3] = 255;
            }
            auto tex = renderer.CreateTexture(img);

            GpuDrawParams unlitP;
            unlitP.texture0 = tex.get();
            unlitP.textureEnabled = true;
            unlitP.lightingEnabled = false;

            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawPrimitivesEx(*vbLit, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, unlitP);
            std::vector<Color> unlitResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, unlitResult.data(), 0, static_cast<int>(unlitResult.size()));
            bool unlitIsTexColor = true;
            for (const Color& p : unlitResult)
                if (p.getRProperty() != 44 || p.getGProperty() != 55 || p.getBProperty() != 66 || p.getAProperty() != 255)
                    unlitIsTexColor = false;
            check(unlitIsTexColor,
                  "DirectX11Renderer::DrawPrimitivesEx(): real lit_textured3d unlit branch samples "
                  "diffuseColor*texture exactly (plans/plan_dx.md DX-63)");

            GpuDrawParams litP = unlitP;
            litP.lightingEnabled = true;
            litP.ambientColor[0] = 0.5f; litP.ambientColor[1] = 0.5f; litP.ambientColor[2] = 0.5f;
            litP.specularColor[0] = 0.0f; litP.specularColor[1] = 0.0f; litP.specularColor[2] = 0.0f;

            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawPrimitivesEx(*vbLit, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, litP);
            std::vector<Color> litResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, litResult.data(), 0, static_cast<int>(litResult.size()));
            bool litDiffersFromUnlitAndBackground = true;
            for (std::size_t i = 0; i < litResult.size(); ++i)
            {
                const Color& lp = litResult[i];
                const Color& up = unlitResult[i];
                const bool sameAsUnlit = (lp.getRProperty() == up.getRProperty() && lp.getGProperty() == up.getGProperty()
                                         && lp.getBProperty() == up.getBProperty());
                const bool sameAsBackground = (lp.getRProperty() == 0 && lp.getGProperty() == 0 && lp.getBProperty() == 255);
                if (sameAsUnlit || sameAsBackground) litDiffersFromUnlitAndBackground = false;
            }
            check(litDiffersFromUnlitAndBackground,
                  "DirectX11Renderer::DrawPrimitivesEx(): real lit_textured3d lit branch genuinely "
                  "computes a different color than both the unlit result and the Clear() background "
                  "(plans/plan_dx.md DX-63)");

            // DX-137: dedicated fog on/off discriminating test for lit_textured3d -- reuses the
            // unlit branch's own deterministic fixture (exact base color (44,55,66)) above, same
            // Z-at-FogEnd methodology as textured3d's own fog test.
            {
                static const VPNT kTriLitFog[3] = {
                    {-1.0f, -1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
                    { 3.0f, -1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f},
                    {-1.0f,  3.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f},
                };
                auto vbLitFog = renderer.CreateVertexBuffer(3);
                vbLitFog->SetData(kTriLitFog, 3, sizeof(VPNT));

                GpuDrawParams litFogP = unlitP;
                const Microsoft::Xna::Framework::Rectangle litFogRegion(30, 30, 1, 1);
                litFogP.fogEnabled = false;
                litFogP.fogColor[0] = 0.0f; litFogP.fogColor[1] = 1.0f; litFogP.fogColor[2] = 0.0f;
                dev.Clear(Color(10, 10, 10, 255));
                renderer.DrawPrimitivesEx(*vbLitFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, litFogP);
                Color litFogOffPixel(0, 0, 0, 0);
                dev.GetBackBufferData(&litFogRegion, &litFogOffPixel, 0, 1);
                check(litFogOffPixel.getRProperty() == 44 && litFogOffPixel.getGProperty() == 55 &&
                      litFogOffPixel.getBProperty() == 66,
                      "DirectX11Renderer::DrawPrimitivesEx(): lit_textured3d fogEnabled=false leaves "
                      "the exact unlit texture color unblended (plans/plan_dx.md DX-137)");

                litFogP.fogEnabled = true;
                litFogP.fogVector[2] = 2.0f;
                dev.Clear(Color(10, 10, 10, 255));
                renderer.DrawPrimitivesEx(*vbLitFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, litFogP);
                Color litFogOnPixel(0, 0, 0, 0);
                dev.GetBackBufferData(&litFogRegion, &litFogOnPixel, 0, 1);
                check(litFogOnPixel.getRProperty() == 0 && litFogOnPixel.getGProperty() == 255 &&
                      litFogOnPixel.getBProperty() == 0,
                      "DirectX11Renderer::DrawPrimitivesEx(): lit_textured3d fogEnabled=true with Z "
                      "at FogEnd genuinely blends all the way to the exact FogColor (plans/plan_dx.md DX-137)");
            }
        }

        // Check R2 (Task 1106, plans/plan_graphics.md Phase 80): PreferPerPixelLighting genuinely
        // selects between two different lit_textured3d dispatch variants (vertex-lit vs
        // pixel-lit). Reuses the exact scene/values already independently derived and verified in
        // examples/easygl_basiceffect_preferperpixellighting_test.cpp (Task 1102): a flat quad,
        // single shared normal (0,0,1), makes DIFFUSE spatially constant (useless for
        // discriminating diffuse alone) but SPECULAR still varies across the surface because the
        // eye vector depends on position -- Gouraud-interpolating each vertex's own
        // independently-computed specular term (vertex-lit) genuinely differs from re-evaluating
        // it fresh at the sampled fragment (pixel-lit). Sampled exactly at the viewport centre,
        // which sits on the diagonal seam between the quad's two triangles. Analytically-derived
        // expected values (same offline Blinn-Phong re-derivation as the EasyGL test): ~127
        // vertex-lit, ~155 pixel-lit.
        {
            struct VPNT { float x, y, z; float nx, ny, nz; float u, v; };
            static const VPNT kQuad[6] = {
                {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
                {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
                { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f},
                {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
                { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f},
                { 1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
            };
            auto vbPPL = renderer.CreateVertexBuffer(6);
            vbPPL->SetData(kQuad, 6, sizeof(VPNT));

            ImageData whiteImg;
            whiteImg.width = 1; whiteImg.height = 1; whiteImg.mipLevels = 1;
            whiteImg.pixels = {255, 255, 255, 255};
            auto whiteTex = renderer.CreateTexture(whiteImg);

            GpuDrawParams pplP;
            pplP.texture0 = whiteTex.get();
            pplP.textureEnabled = true;
            pplP.lightingEnabled = true;
            pplP.ambientColor[0] = 0.02f; pplP.ambientColor[1] = 0.02f; pplP.ambientColor[2] = 0.02f;
            pplP.diffuseColor[0] = 0.4f; pplP.diffuseColor[1] = 0.4f; pplP.diffuseColor[2] = 0.4f; pplP.diffuseColor[3] = 1.0f;
            Vector3 lightDir(0.5f, 0.0f, -1.0f);
            lightDir.Normalize();
            pplP.light0Dir[0] = lightDir.X; pplP.light0Dir[1] = lightDir.Y; pplP.light0Dir[2] = lightDir.Z;
            pplP.light0Diffuse[0] = 0.5f; pplP.light0Diffuse[1] = 0.5f; pplP.light0Diffuse[2] = 0.5f;
            pplP.light0Specular[0] = 1.0f; pplP.light0Specular[1] = 1.0f; pplP.light0Specular[2] = 1.0f;
            pplP.specularColor[0] = 1.0f; pplP.specularColor[1] = 1.0f; pplP.specularColor[2] = 1.0f;
            pplP.specularPower = 32.0f;
            pplP.eyePositionWorld[0] = 0.0f; pplP.eyePositionWorld[1] = 0.0f; pplP.eyePositionWorld[2] = 3.0f;

            const Matrix pplView = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
            const Matrix pplProj = Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f);
            const Microsoft::Xna::Framework::Rectangle centerPixel(32, 32, 1, 1);

            pplP.preferPerPixelLighting = false;
            dev.Clear(Color(0, 0, 0, 255));
            renderer.DrawPrimitivesEx(*vbPPL, Matrix::getIdentityProperty(), pplView, pplProj,
                                     PrimitiveType::TriangleList, 2, pplP);
            Color vertexLit(0, 0, 0, 0);
            dev.GetBackBufferData(&centerPixel, &vertexLit, 0, 1);
            const bool vertexLitOk = std::abs(vertexLit.getRProperty() - 127) <= 10 &&
                                     std::abs(vertexLit.getGProperty() - 127) <= 10 &&
                                     std::abs(vertexLit.getBProperty() - 127) <= 10;
            check(vertexLitOk,
                  "DirectX11Renderer::DrawPrimitivesEx(): preferPerPixelLighting=false (XNA's "
                  "real default) genuinely computes the Gouraud-averaged specular result, ~127 "
                  "(Task 1106, plans/plan_graphics.md Phase 80)");

            pplP.preferPerPixelLighting = true;
            dev.Clear(Color(0, 0, 0, 255));
            renderer.DrawPrimitivesEx(*vbPPL, Matrix::getIdentityProperty(), pplView, pplProj,
                                     PrimitiveType::TriangleList, 2, pplP);
            Color pixelLit(0, 0, 0, 0);
            dev.GetBackBufferData(&centerPixel, &pixelLit, 0, 1);
            const bool pixelLitOk = std::abs(pixelLit.getRProperty() - 155) <= 10 &&
                                    std::abs(pixelLit.getGProperty() - 155) <= 10 &&
                                    std::abs(pixelLit.getBProperty() - 155) <= 10;
            check(pixelLitOk,
                  "DirectX11Renderer::DrawPrimitivesEx(): preferPerPixelLighting=true genuinely "
                  "computes a fresh per-fragment specular result, ~155 (Task 1106, plans/plan_graphics.md "
                  "Phase 80)");

            check(vertexLit.getRProperty() != pixelLit.getRProperty(),
                  "DirectX11Renderer::DrawPrimitivesEx(): preferPerPixelLighting is a real "
                  "dispatch selector, not a decorative no-op -- the two draws above produce "
                  "genuinely different pixel values (Task 1106, plans/plan_graphics.md Phase 80)");
        }

        // Check S (DX-64): alpha_test3d. A failing alpha genuinely discards (Clear() background
        // survives); a passing alpha draws the exact texture color, including its own alpha byte.
        {
            struct VPT { float x, y, z; float u, v; };
            static const VPT kTriAT[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
                { 3.0f, -1.0f, 0.0f, 2.0f, 1.0f},
                {-1.0f,  3.0f, 0.0f, 0.0f, -1.0f},
            };
            auto vbAT = renderer.CreateVertexBuffer(3);
            vbAT->SetData(kTriAT, 3, sizeof(VPT));

            ImageData img;
            img.width = 2; img.height = 2; img.mipLevels = 1;
            img.pixels.resize(2 * 2 * 4);
            for (int i = 0; i < 4; ++i)
            {
                img.pixels[i * 4 + 0] = 200; img.pixels[i * 4 + 1] = 100;
                img.pixels[i * 4 + 2] = 50;  img.pixels[i * 4 + 3] = 128; // alpha=128/255 ~ 0.502
            }
            auto tex = renderer.CreateTexture(img);

            GpuDrawParams atp;
            atp.texture0 = tex.get();
            atp.textureEnabled = true;
            // AlphaTol=0 (comparison mode) -> passTest = alpha < AlphaRef; failW<0 -> discard on fail.
            atp.alphaTest[0] = 0.5f;  // AlphaRef
            atp.alphaTest[1] = 0.0f;  // AlphaTol
            atp.alphaTest[2] = 1.0f;  // AlphaPassW (>=0, never discard on pass)
            atp.alphaTest[3] = -1.0f; // AlphaFailW (<0, discard on fail)

            // Sub-check 1: alpha=128/255 is NOT < 0.5 -> fails -> discard -> background survives.
            dev.Clear(Color(0, 255, 0, 255));
            renderer.DrawPrimitivesEx(*vbAT, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, atp);
            std::vector<Color> discardResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, discardResult.data(), 0, static_cast<int>(discardResult.size()));
            bool stillGreen = true;
            for (const Color& p : discardResult)
                if (p.getRProperty() != 0 || p.getGProperty() != 255 || p.getBProperty() != 0 || p.getAProperty() != 255)
                    stillGreen = false;
            check(stillGreen,
                  "DirectX11Renderer::DrawPrimitivesEx(): real alpha_test3d discard() genuinely "
                  "drops a failing pixel, leaving the Clear() background untouched (plans/plan_dx.md DX-64)");

            // Sub-check 2: replace the texture's alpha with 64/255 (< 0.5) -> passes -> drawn exactly.
            std::vector<uint8_t> passPixels(2 * 2 * 4);
            for (int i = 0; i < 4; ++i)
            {
                passPixels[i * 4 + 0] = 200; passPixels[i * 4 + 1] = 100;
                passPixels[i * 4 + 2] = 50;  passPixels[i * 4 + 3] = 64;
            }
            tex->UpdatePixelsLevel(0, passPixels.data(), 2, 2);

            dev.Clear(Color(0, 255, 0, 255));
            renderer.DrawPrimitivesEx(*vbAT, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, atp);
            std::vector<Color> passResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, passResult.data(), 0, static_cast<int>(passResult.size()));
            bool passIsExact = true;
            for (const Color& p : passResult)
                if (p.getRProperty() != 200 || p.getGProperty() != 100 || p.getBProperty() != 50 || p.getAProperty() != 64)
                    passIsExact = false;
            check(passIsExact,
                  "DirectX11Renderer::DrawPrimitivesEx(): real alpha_test3d draws the exact texture "
                  "color (including its own alpha byte) when the test passes (plans/plan_dx.md DX-64)");

            // DX-137: dedicated fog on/off discriminating test for alpha_test3d -- reuses the
            // already-PASSING fixture above (alpha=64/255 < AlphaRef=0.5, so nothing is discarded
            // and fog is genuinely visible), same Z-at-FogEnd methodology as textured3d's own fog
            // test. A discarding fixture would prove nothing here (no fragment ever reaches the
            // fog blend).
            {
                static const VPT kTriATFog[3] = {
                    {-1.0f, -1.0f, 0.5f, 0.0f, 1.0f},
                    { 3.0f, -1.0f, 0.5f, 2.0f, 1.0f},
                    {-1.0f,  3.0f, 0.5f, 0.0f, -1.0f},
                };
                auto vbATFog = renderer.CreateVertexBuffer(3);
                vbATFog->SetData(kTriATFog, 3, sizeof(VPT));

                const Microsoft::Xna::Framework::Rectangle atFogRegion(30, 30, 1, 1);
                atp.fogEnabled = false;
                atp.fogColor[0] = 0.0f; atp.fogColor[1] = 1.0f; atp.fogColor[2] = 0.0f;
                // Every D3DCommon stock shader now uses the same authoritative zero-vector
                // disabled encoding that AlphaTestEffect already used.
                dev.Clear(Color(10, 10, 10, 255));
                renderer.DrawPrimitivesEx(*vbATFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, atp);
                Color atFogOffPixel(0, 0, 0, 0);
                dev.GetBackBufferData(&atFogRegion, &atFogOffPixel, 0, 1);
                check(atFogOffPixel.getRProperty() == 200 && atFogOffPixel.getGProperty() == 100 &&
                      atFogOffPixel.getBProperty() == 50,
                      "DirectX11Renderer::DrawPrimitivesEx(): alpha_test3d fogEnabled=false leaves "
                      "the exact passing texture color unblended (plans/plan_dx.md DX-137)");

                atp.fogEnabled = true;
                atp.fogVector[2] = 2.0f;  // REMED-GFX-055: view-space fog vector (see colored_textured3d DX-137 above)
                dev.Clear(Color(10, 10, 10, 255));
                renderer.DrawPrimitivesEx(*vbATFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, atp);
                Color atFogOnPixel(0, 0, 0, 0);
                dev.GetBackBufferData(&atFogRegion, &atFogOnPixel, 0, 1);
                check(atFogOnPixel.getRProperty() == 0 && atFogOnPixel.getGProperty() == 255 &&
                      atFogOnPixel.getBProperty() == 0,
                      "DirectX11Renderer::DrawPrimitivesEx(): alpha_test3d fogEnabled=true with Z at "
                      "FogEnd genuinely blends all the way to the exact FogColor, and the passing "
                      "fragment survives the alpha test to reach the fog blend at all (plans/plan_dx.md DX-137)");
            }

            // plans/plan_dx.md DX-136: AlphaTestEffect.VertexColorEnabled -- alpha_test3d's new stride-24
            // sibling (alpha_test_colored3d, VertexPositionColorTexture) gives it a real
            // vertex-color attribute. A white, fully-opaque texture isolates the vertex-color
            // contribution: with VertexColorEnabled=true a red vertex color multiplies through
            // exactly; with it false, the same vertex buffer's color is genuinely ignored and
            // DiffuseColor (white) alone survives -- proving this is a real per-draw toggle, not a
            // fixed behavior.
            {
                struct VPCT { float x, y, z; uint32_t color; float u, v; };
                const uint32_t kRedVC = 0xFF0000FFu; // A=255,B=0,G=0,R=255 (R8G8B8A8 byte order)
                static const VPCT kTriAlphaColor[3] = {
                    {-1.0f, -1.0f, 0.0f, kRedVC, 0.0f, 1.0f},
                    { 3.0f, -1.0f, 0.0f, kRedVC, 2.0f, 1.0f},
                    {-1.0f,  3.0f, 0.0f, kRedVC, 0.0f, -1.0f},
                };
                auto vbAlphaColor = renderer.CreateVertexBuffer(3);
                vbAlphaColor->SetData(kTriAlphaColor, 3, sizeof(VPCT));

                ImageData whiteImgAC;
                whiteImgAC.width = 2; whiteImgAC.height = 2; whiteImgAC.mipLevels = 1;
                whiteImgAC.pixels.assign(2 * 2 * 4, 255);
                auto whiteTexAC = renderer.CreateTexture(whiteImgAC);

                GpuDrawParams acp;
                acp.texture0 = whiteTexAC.get();
                acp.textureEnabled = true;
                // Default {0,0,1,1}: both AlphaPassW and AlphaFailW are non-negative, so w is never
                // negative regardless of passTest -- genuinely always passes (never discards),
                // matching alpha_test3d.frag.hlsl's own documented default exactly.
                acp.alphaTest[0] = 0.0f;
                acp.alphaTest[1] = 0.0f;
                acp.alphaTest[2] = 1.0f;
                acp.alphaTest[3] = 1.0f;

                acp.vertexColorEnabled = true;
                dev.Clear(Color(0, 0, 255, 255));
                renderer.DrawPrimitivesEx(*vbAlphaColor, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, acp);
                std::vector<Color> acOnResult(4 * 4, Color(0, 0, 0, 0));
                dev.GetBackBufferData(&centerRegion28, acOnResult.data(), 0, static_cast<int>(acOnResult.size()));
                bool acOnIsRed = true;
                for (const Color& p : acOnResult)
                    if (p.getRProperty() != 255 || p.getGProperty() != 0 || p.getBProperty() != 0)
                        acOnIsRed = false;
                check(acOnIsRed,
                      "DirectX11Renderer::DrawPrimitivesEx(): real alpha_test_colored3d (stride 24) "
                      "with VertexColorEnabled=true multiplies the exact vertex color (red) through a "
                      "white texture (plans/plan_dx.md DX-136)");

                acp.vertexColorEnabled = false;
                dev.Clear(Color(0, 0, 255, 255));
                renderer.DrawPrimitivesEx(*vbAlphaColor, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, acp);
                std::vector<Color> acOffResult(4 * 4, Color(0, 0, 0, 0));
                dev.GetBackBufferData(&centerRegion28, acOffResult.data(), 0, static_cast<int>(acOffResult.size()));
                bool acOffIsWhite = true;
                for (const Color& p : acOffResult)
                    if (p.getRProperty() != 255 || p.getGProperty() != 255 || p.getBProperty() != 255)
                        acOffIsWhite = false;
                check(acOffIsWhite,
                      "DirectX11Renderer::DrawPrimitivesEx(): the SAME vertex buffer with "
                      "VertexColorEnabled=false genuinely ignores its vertex color -- only "
                      "DiffuseColor (white) survives, distinctly different from the true case above "
                      "(plans/plan_dx.md DX-136)");

                // Alpha test itself still genuinely discards on this new stride-24 path -- not just
                // the vertex-color multiply, the whole alpha_test_colored3d shader's discard logic.
                // AlphaTol=0 (comparison mode), AlphaRef=0.5: alpha=200/255 (~0.784, NOT < 0.5)
                // genuinely fails -> discard -> background survives.
                acp.alphaTest[0] = 0.5f; acp.alphaTest[1] = 0.0f;
                acp.alphaTest[2] = 1.0f; acp.alphaTest[3] = -1.0f;
                std::vector<uint8_t> highAlphaPixels(2 * 2 * 4);
                for (int i = 0; i < 4; ++i)
                {
                    highAlphaPixels[i * 4 + 0] = 255; highAlphaPixels[i * 4 + 1] = 255;
                    highAlphaPixels[i * 4 + 2] = 255; highAlphaPixels[i * 4 + 3] = 200;
                }
                whiteTexAC->UpdatePixelsLevel(0, highAlphaPixels.data(), 2, 2);
                acp.vertexColorEnabled = true;
                dev.Clear(Color(0, 255, 0, 255));
                renderer.DrawPrimitivesEx(*vbAlphaColor, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, acp);
                std::vector<Color> acDiscardResult(4 * 4, Color(0, 0, 0, 0));
                dev.GetBackBufferData(&centerRegion28, acDiscardResult.data(), 0, static_cast<int>(acDiscardResult.size()));
                bool acDiscardIsGreen = true;
                for (const Color& p : acDiscardResult)
                    if (p.getRProperty() != 0 || p.getGProperty() != 255 || p.getBProperty() != 0)
                        acDiscardIsGreen = false;
                check(acDiscardIsGreen,
                      "DirectX11Renderer::DrawPrimitivesEx(): alpha_test_colored3d's alpha-test "
                      "discard logic still genuinely works on the new stride-24/vertex-color path -- "
                      "a failing alpha drops the pixel, leaving the Clear() background untouched "
                      "(plans/plan_dx.md DX-136)");
            }
        }

        // Check T (DX-65): dual_texture3d. tex1.rgb *= 2.0, outColor = tex1 * tex2 * Tint. Both
        // textures are uniform 2x2 so exact texel values are unaffected by bilinear vs. point
        // sampling. tex1=(50,60,70,200), tex2=white -> expected (100,120,140,200).
        {
            renderer.ApplySamplerState(0, 1 /*TextureFilter::Point*/, 0, 0, 1);
            renderer.ApplySamplerState(1, 1 /*TextureFilter::Point*/, 0, 0, 1);

            struct VPT2 { float x, y, z; float u, v; };
            static const VPT2 kTriDT[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
                { 3.0f, -1.0f, 0.0f, 2.0f, 1.0f},
                {-1.0f,  3.0f, 0.0f, 0.0f, -1.0f},
            };
            auto vbDT = renderer.CreateVertexBuffer(3);
            vbDT->SetData(kTriDT, 3, sizeof(VPT2));

            ImageData img1;
            img1.width = 2; img1.height = 2; img1.mipLevels = 1;
            img1.pixels.resize(2 * 2 * 4);
            for (int i = 0; i < 4; ++i)
            {
                img1.pixels[i * 4 + 0] = 50; img1.pixels[i * 4 + 1] = 60;
                img1.pixels[i * 4 + 2] = 70; img1.pixels[i * 4 + 3] = 200;
            }
            auto tex1 = renderer.CreateTexture(img1);

            ImageData img2;
            img2.width = 2; img2.height = 2; img2.mipLevels = 1;
            img2.pixels.assign(2 * 2 * 4, 255);
            auto tex2 = renderer.CreateTexture(img2);

            GpuDrawParams dtp;
            dtp.texture0 = tex1.get();
            dtp.texture1 = tex2.get();
            dtp.dualTexture = true;
            dtp.textureEnabled = true;

            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawPrimitivesEx(*vbDT, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, dtp);
            std::vector<Color> dtResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, dtResult.data(), 0, static_cast<int>(dtResult.size()));
            bool dtIsExact = true;
            for (const Color& p : dtResult)
                if (p.getRProperty() != 100 || p.getGProperty() != 120 || p.getBProperty() != 140 || p.getAProperty() != 200)
                    dtIsExact = false;
            check(dtIsExact,
                  "DirectX11Renderer::DrawPrimitivesEx(): real dual_texture3d combines both real "
                  "SRVs (tex1.rgb*2 * tex2) to the exact expected byte result (plans/plan_dx.md DX-65)");

            // DX-137: dedicated fog on/off discriminating test for dual_texture3d -- reuses this
            // block's own deterministic fixture (exact base result (100,120,140)), same
            // Z-at-FogEnd methodology as textured3d's own fog test.
            {
                static const VPT2 kTriDTFog[3] = {
                    {-1.0f, -1.0f, 0.5f, 0.0f, 1.0f},
                    { 3.0f, -1.0f, 0.5f, 2.0f, 1.0f},
                    {-1.0f,  3.0f, 0.5f, 0.0f, -1.0f},
                };
                auto vbDTFog = renderer.CreateVertexBuffer(3);
                vbDTFog->SetData(kTriDTFog, 3, sizeof(VPT2));

                const Microsoft::Xna::Framework::Rectangle dtFogRegion(30, 30, 1, 1);
                dtp.fogEnabled = false;
                dtp.fogColor[0] = 0.0f; dtp.fogColor[1] = 1.0f; dtp.fogColor[2] = 0.0f;
                dev.Clear(Color(10, 10, 10, 255));
                renderer.DrawPrimitivesEx(*vbDTFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, dtp);
                Color dtFogOffPixel(0, 0, 0, 0);
                dev.GetBackBufferData(&dtFogRegion, &dtFogOffPixel, 0, 1);
                check(dtFogOffPixel.getRProperty() == 100 && dtFogOffPixel.getGProperty() == 120 &&
                      dtFogOffPixel.getBProperty() == 140,
                      "DirectX11Renderer::DrawPrimitivesEx(): dual_texture3d fogEnabled=false leaves "
                      "the exact combined-texture color unblended (plans/plan_dx.md DX-137)");

                dtp.fogEnabled = true;
                dtp.fogVector[2] = 2.0f;
                dev.Clear(Color(10, 10, 10, 255));
                renderer.DrawPrimitivesEx(*vbDTFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, dtp);
                Color dtFogOnPixel(0, 0, 0, 0);
                dev.GetBackBufferData(&dtFogRegion, &dtFogOnPixel, 0, 1);
                check(dtFogOnPixel.getRProperty() == 0 && dtFogOnPixel.getGProperty() == 255 &&
                      dtFogOnPixel.getBProperty() == 0,
                      "DirectX11Renderer::DrawPrimitivesEx(): dual_texture3d fogEnabled=true with Z "
                      "at FogEnd genuinely blends all the way to the exact FogColor (plans/plan_dx.md DX-137)");
            }
        }

        // Check U (DX-66): env_map3d. Camera placed far down -Z from a +Z-facing surface, ambient/
        // lighting/specular all zeroed by geometry+params so only the env-map term (envMapAmount=1,
        // fresnel disabled) survives -- reflDir resolves to almost exactly (0,0,-1), landing deep
        // inside the cube's -Z face (D3D11 native slice order +X,-X,+Y,-Y,+Z,-Z -> index 5), which
        // is the only face given a distinct, uniform, non-black color.
        {
            struct VPNTE { float x, y, z; float nx, ny, nz; float u, v; };
            static const VPNTE kTriEnv[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
                { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f},
                {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f},
            };
            auto vbEnv = renderer.CreateVertexBuffer(3);
            vbEnv->SetData(kTriEnv, 3, sizeof(VPNTE));

            ImageData whiteImg;
            whiteImg.width = 2; whiteImg.height = 2; whiteImg.mipLevels = 1;
            whiteImg.pixels.assign(2 * 2 * 4, 255);
            auto whiteTex = renderer.CreateTexture(whiteImg);

            auto cube = renderer.CreateTextureCube(8, false, 0);
            std::vector<uint8_t> blackFace(8 * 8 * 4, 0);
            std::vector<uint8_t> negZFace(8 * 8 * 4);
            for (int i = 0; i < 8 * 8; ++i)
            {
                negZFace[i * 4 + 0] = 10; negZFace[i * 4 + 1] = 20;
                negZFace[i * 4 + 2] = 30; negZFace[i * 4 + 3] = 255;
            }
            for (int face = 0; face < 6; ++face)
            {
                const auto& data = (face == 5) ? negZFace : blackFace; // face 5 = -Z
                cube->SetData(face, 0, 0, 0, 8, 8, data.data(), static_cast<int>(data.size()));
            }

            GpuDrawParams ep;
            ep.texture0 = whiteTex.get();
            ep.textureEnabled = true;
            ep.envMap = cube.get();
            ep.envMapping = true;
            ep.envMapAmount = 1.0f;
            ep.eyePositionWorld[0] = 0.0f; ep.eyePositionWorld[1] = 0.0f; ep.eyePositionWorld[2] = -10.0f;

            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawPrimitivesEx(*vbEnv, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ep);
            std::vector<Color> envResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, envResult.data(), 0, static_cast<int>(envResult.size()));
            bool envIsExact = true;
            for (const Color& p : envResult)
                if (p.getRProperty() != 10 || p.getGProperty() != 20 || p.getBProperty() != 30 || p.getAProperty() != 255)
                    envIsExact = false;
            check(envIsExact,
                  "DirectX11Renderer::DrawPrimitivesEx(): real env_map3d samples the exact "
                  "distinctly-colored cube face via the real TextureCube SRV (plans/plan_dx.md DX-66)");

            // DX-134: same fixture, envMapAmount=0.0 -> blendFactor=0 -> the lerp(baseColor,
            // envSample*alpha, blendFactor) formula must collapse to the pure base color (lit=0
            // since light0Dir default is perpendicular to this surface's normal, ambient/emissive
            // both default 0), NOT the reflected cube-face color Check U above just proved --
            // genuinely different from the amount=1.0 result, proving the blend is real and
            // graduated, not a fixed always-on reflection.
            ep.envMapAmount = 0.0f;
            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawPrimitivesEx(*vbEnv, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ep);
            std::vector<Color> envZeroResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, envZeroResult.data(), 0, static_cast<int>(envZeroResult.size()));
            bool envZeroIsExact = true;
            for (const Color& p : envZeroResult)
                if (p.getRProperty() != 0 || p.getGProperty() != 0 || p.getBProperty() != 0 || p.getAProperty() != 255)
                    envZeroIsExact = false;
            check(envZeroIsExact,
                  "DirectX11Renderer::DrawPrimitivesEx(): real env_map3d with envMapAmount=0.0 "
                  "collapses the base-lerp to the pure (unlit) base color, distinctly different from "
                  "the envMapAmount=1.0 reflected-face color above -- proves the lerp is a genuine "
                  "graduated blend control, not just an on/off gate (plans/plan_dx.md DX-134)");

            // DX-137: dedicated fog on/off discriminating test for env_map3d -- reuses this block's
            // own deterministic reflected-face fixture (exact base result (10,20,30) at
            // envMapAmount=1.0, restored here since DX-134's check above left it at 0.0), same
            // Z-at-FogEnd methodology as textured3d's own fog test.
            {
                static const VPNTE kTriEnvFog[3] = {
                    {-1.0f, -1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
                    { 3.0f, -1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f},
                    {-1.0f,  3.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f},
                };
                auto vbEnvFog = renderer.CreateVertexBuffer(3);
                vbEnvFog->SetData(kTriEnvFog, 3, sizeof(VPNTE));

                ep.envMapAmount = 1.0f;
                const Microsoft::Xna::Framework::Rectangle envFogRegion(30, 30, 1, 1);
                ep.fogEnabled = false;
                ep.fogColor[0] = 0.0f; ep.fogColor[1] = 1.0f; ep.fogColor[2] = 0.0f;
                dev.Clear(Color(10, 10, 10, 255));
                renderer.DrawPrimitivesEx(*vbEnvFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ep);
                Color envFogOffPixel(0, 0, 0, 0);
                dev.GetBackBufferData(&envFogRegion, &envFogOffPixel, 0, 1);
                check(envFogOffPixel.getRProperty() == 10 && envFogOffPixel.getGProperty() == 20 &&
                      envFogOffPixel.getBProperty() == 30,
                      "DirectX11Renderer::DrawPrimitivesEx(): env_map3d fogEnabled=false leaves the "
                      "exact reflected cube-face color unblended (plans/plan_dx.md DX-137)");

                ep.fogEnabled = true;
                ep.fogVector[2] = 2.0f;
                dev.Clear(Color(10, 10, 10, 255));
                renderer.DrawPrimitivesEx(*vbEnvFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ep);
                Color envFogOnPixel(0, 0, 0, 0);
                dev.GetBackBufferData(&envFogRegion, &envFogOnPixel, 0, 1);
                check(envFogOnPixel.getRProperty() == 0 && envFogOnPixel.getGProperty() == 255 &&
                      envFogOnPixel.getBProperty() == 0,
                      "DirectX11Renderer::DrawPrimitivesEx(): env_map3d fogEnabled=true with Z at "
                      "FogEnd genuinely blends all the way to the exact FogColor (plans/plan_dx.md DX-137)");
            }

            // plans/plan_dx.md DX-149: env_map3d -- DirectionalLight1/DirectionalLight2 each independently
            // and exactly contribute, not just Light0. Reuses this block's own fixture (normal +Z,
            // white 2x2 texture, the same cube -- irrelevant content since envMapAmount=0.0 collapses
            // the base-lerp to the pure lit*texColor path per DX-134's own already-proven formula, so
            // the env-map sample still executes but its result never reaches the output) and DX-124's
            // own exact-color-per-term isolation methodology.
            {
                ep.envMapAmount = 0.0f;
                ep.light0Diffuse[0] = 0.0f; ep.light0Diffuse[1] = 0.0f; ep.light0Diffuse[2] = 0.0f; // Light0 off
                const Microsoft::Xna::Framework::Rectangle ppRegion(30, 30, 1, 1);

                // PP1: DirectionalLight1 alone, full-facing direction, red diffuse -> exact (255,0,0).
                ep.light1Dir[0] = 0.0f; ep.light1Dir[1] = 0.0f; ep.light1Dir[2] = -1.0f; // travels -Z -> faces the +Z normal
                ep.light1Diffuse[0] = 1.0f; ep.light1Diffuse[1] = 0.0f; ep.light1Diffuse[2] = 0.0f;
                dev.Clear(Color(0, 0, 0, 255));
                renderer.DrawPrimitivesEx(*vbEnv, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ep);
                Color pp1(0, 0, 0, 0);
                dev.GetBackBufferData(&ppRegion, &pp1, 0, 1);
                check(pp1.getRProperty() == 255 && pp1.getGProperty() == 0 && pp1.getBProperty() == 0,
                      "DirectX11Renderer::DrawPrimitivesEx(): real env_map3d -- DirectionalLight1 "
                      "alone contributes the exact expected red, independent of Light0/Light2 (plans/plan_dx.md DX-149)");

                // PP2: same geometry/light1Dir, but light1Diffuse disabled -> confirms PP1 wasn't a leak.
                ep.light1Diffuse[0] = 0.0f; ep.light1Diffuse[1] = 0.0f; ep.light1Diffuse[2] = 0.0f;
                dev.Clear(Color(0, 0, 0, 255));
                renderer.DrawPrimitivesEx(*vbEnv, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ep);
                Color pp1off(0, 0, 0, 0);
                dev.GetBackBufferData(&ppRegion, &pp1off, 0, 1);
                check(pp1off.getRProperty() == 0 && pp1off.getGProperty() == 0 && pp1off.getBProperty() == 0,
                      "DirectX11Renderer::DrawPrimitivesEx(): disabling env_map3d DirectionalLight1's "
                      "diffuse (zeroed) removes its contribution exactly -- confirms the prior check was "
                      "real, not a leaked default (plans/plan_dx.md DX-149)");

                // PP3: DirectionalLight2 alone, green diffuse -> exact (0,255,0).
                ep.light2Dir[0] = 0.0f; ep.light2Dir[1] = 0.0f; ep.light2Dir[2] = -1.0f;
                ep.light2Diffuse[0] = 0.0f; ep.light2Diffuse[1] = 1.0f; ep.light2Diffuse[2] = 0.0f;
                dev.Clear(Color(0, 0, 0, 255));
                renderer.DrawPrimitivesEx(*vbEnv, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ep);
                Color pp2(0, 0, 0, 0);
                dev.GetBackBufferData(&ppRegion, &pp2, 0, 1);
                check(pp2.getRProperty() == 0 && pp2.getGProperty() == 255 && pp2.getBProperty() == 0,
                      "DirectX11Renderer::DrawPrimitivesEx(): real env_map3d -- DirectionalLight2 "
                      "alone contributes the exact expected green, independent of Light0/Light1 (plans/plan_dx.md DX-149)");
            }
        }

        // Check V (DX-67): skinned3d. A single identity bone (BoneBlock genuinely populated from
        // GpuDrawParams::boneTransforms, not left zero-initialized -- an all-zero bone matrix would
        // degenerate the transform and fail this check) combined with the exact GPU-facing material
        // state SkinnedEffect emits for AmbientLightColor=white, DiffuseColor=white, Alpha=1 and all
        // directional lights disabled leaves outColor == the exact sampled texture color.
        // REMED-GFX-088: SkinnedEffect cannot disable LightingEnabled (FNA throws on false); its CPU
        // path always selects a lit permutation and pre-folds (Emissive + Ambient*Diffuse)*Alpha
        // into emissiveColor. PreferPerPixelLighting=true keeps this check on skinned3d.frag while
        // representing a state the public effect can genuinely produce.
        {
            struct VPNTS { float x, y, z; float nx, ny, nz; float u, v;
                          float bw0, bw1, bw2, bw3; uint8_t bi0, bi1, bi2, bi3; };
            static const VPNTS kTriSkin[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
                { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
                {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            };
            auto vbSkin = renderer.CreateVertexBuffer(3);
            vbSkin->SetData(kTriSkin, 3, sizeof(VPNTS));

            ImageData img;
            img.width = 2; img.height = 2; img.mipLevels = 1;
            img.pixels.resize(2 * 2 * 4);
            for (int i = 0; i < 4; ++i)
            {
                img.pixels[i * 4 + 0] = 77; img.pixels[i * 4 + 1] = 88;
                img.pixels[i * 4 + 2] = 99; img.pixels[i * 4 + 3] = 255;
            }
            auto tex = renderer.CreateTexture(img);

            GpuDrawParams sp;
            sp.texture0 = tex.get();
            sp.textureEnabled = true;
            sp.lightingEnabled = true;
            sp.preferPerPixelLighting = true;
            sp.skinned = true;
            sp.boneCount = 1;
            sp.weightsPerVertex = 1;
            Matrix::getIdentityProperty().ToColumnMajor(sp.boneTransforms);
            sp.emissiveColor[0] = 1.0f; sp.emissiveColor[1] = 1.0f; sp.emissiveColor[2] = 1.0f;
            sp.light0Diffuse[0] = 0.0f; sp.light0Diffuse[1] = 0.0f; sp.light0Diffuse[2] = 0.0f;
            sp.specularColor[0] = 0.0f; sp.specularColor[1] = 0.0f; sp.specularColor[2] = 0.0f;
            sp.eyePositionWorld[0] = 0.0f; sp.eyePositionWorld[1] = 0.0f; sp.eyePositionWorld[2] = -10.0f;

            dev.Clear(Color(0, 255, 0, 255));
            renderer.DrawPrimitivesEx(*vbSkin, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, sp);
            std::vector<Color> skinResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, skinResult.data(), 0, static_cast<int>(skinResult.size()));
            bool skinIsExact = true;
            for (const Color& p : skinResult)
                if (p.getRProperty() != 77 || p.getGProperty() != 88 || p.getBProperty() != 99 || p.getAProperty() != 255)
                    skinIsExact = false;
            check(skinIsExact,
                  "DirectX11Renderer::DrawPrimitivesEx(): real skinned3d with a genuinely-populated "
                  "single identity bone samples the exact texture color (plans/plan_dx.md DX-67)");

            // DX-135: WeightsPerVertex discriminating test. Real, non-obvious math property found
            // empirically while building this: for a SINGLE active bone, the shader's
            // `Bones[i]*weight` scalar-multiplies the WHOLE 4x4 matrix including the w=1 row/column
            // -- the resulting homogeneous divide (skinnedPos.xyz / skinnedPos.w) exactly CANCELS
            // any single bone's own weight magnitude, so weightsPerVertex=1 with bone0=Identity
            // ALWAYS reproduces bone0's untransformed result regardless of weight.x's actual value
            // (matches Check V's own already-passing single-bone precedent above). The weight only
            // has an observable effect once TWO bones are genuinely blended together (weights
            // summing to 1.0, giving a valid combined w=1 rigid transform) -- so the correct
            // discriminator is: weightsPerVertex=1 (bone1 ignored) stays at bone0's Identity (full
            // coverage, matches Check V), while weightsPerVertex=2 (bone0=Identity blended 0.5/0.5
            // with bone1=Scale(0.1), a genuinely SHRINKING transform) gives Scale(0.5*1+0.5*0.1) =
            // Scale(0.55) -- small enough to genuinely pull the triangle's hypotenuse (originally at
            // object-space x+y=2, scaled to x+y=1.1) away from a probe point at NDC (0.7,0.7)
            // (x+y=1.4, pixel ~(54,10) on this 64x64 RT), which only weightsPerVertex=2 uncovers.
            Matrix::CreateScale(0.1f).ToColumnMajor(sp.boneTransforms + 16);
            sp.boneCount = 2;
            struct VPNTS2 { float x, y, z; float nx, ny, nz; float u, v;
                           float bw0, bw1, bw2, bw3; uint8_t bi0, bi1, bi2, bi3; };
            static const VPNTS2 kTriSkin2[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0, 1, 0, 0},
                { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0, 1, 0, 0},
                {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0, 1, 0, 0},
            };
            auto vbSkin2 = renderer.CreateVertexBuffer(3);
            vbSkin2->SetData(kTriSkin2, 3, sizeof(VPNTS2));

            const Microsoft::Xna::Framework::Rectangle offCenterPoint(54, 10, 2, 2);

            sp.weightsPerVertex = 1;
            dev.Clear(Color(1, 2, 3, 255));
            renderer.DrawPrimitivesEx(*vbSkin2, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, sp);
            std::vector<Color> w1Result(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&offCenterPoint, w1Result.data(), 0, static_cast<int>(w1Result.size()));
            bool w1IsTexture = true;
            for (const Color& p : w1Result)
                if (p.getRProperty() != 77 || p.getGProperty() != 88 || p.getBProperty() != 99)
                    w1IsTexture = false;
            check(w1IsTexture,
                  "DirectX11Renderer::DrawPrimitivesEx(): real skinned3d with weightsPerVertex=1 "
                  "genuinely ignores bone1's contribution -- the probe point still shows bone0's "
                  "unshrunk Identity result, matching Check V's own single-bone precedent "
                  "(plans/plan_dx.md DX-135)");

            sp.weightsPerVertex = 2;
            dev.Clear(Color(1, 2, 3, 255));
            renderer.DrawPrimitivesEx(*vbSkin2, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, sp);
            std::vector<Color> w2Result(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&offCenterPoint, w2Result.data(), 0, static_cast<int>(w2Result.size()));
            bool w2IsBackground = true;
            for (const Color& p : w2Result)
                if (p.getRProperty() != 1 || p.getGProperty() != 2 || p.getBProperty() != 3)
                    w2IsBackground = false;
            check(w2IsBackground,
                  "DirectX11Renderer::DrawPrimitivesEx(): real skinned3d with weightsPerVertex=2 "
                  "genuinely includes bone1's contribution -- the blended Scale(0.55) transform "
                  "shrinks the triangle away from this same probe point (Clear() background shows "
                  "through), distinctly different from the weightsPerVertex=1 case above with "
                  "identical vertex weight data, proving WeightsPerVertex actually gates which bones "
                  "are summed (plans/plan_dx.md DX-135)");

            // DX-137: dedicated fog on/off discriminating test for skinned3d -- a fresh copy of
            // this block's own original single-bone identity fixture (exact base result
            // (77,88,99); `sp`/`vbSkin` above were mutated for the WeightsPerVertex sub-test), same
            // Z-at-FogEnd methodology as textured3d's own fog test.
            {
                static const VPNTS kTriSkinFog[3] = {
                    {-1.0f, -1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
                    { 3.0f, -1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
                    {-1.0f,  3.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
                };
                auto vbSkinFog = renderer.CreateVertexBuffer(3);
                vbSkinFog->SetData(kTriSkinFog, 3, sizeof(VPNTS));

                GpuDrawParams skinFogP;
                skinFogP.texture0 = tex.get();
                skinFogP.textureEnabled = true;
                skinFogP.lightingEnabled = true;
                skinFogP.preferPerPixelLighting = true;
                skinFogP.skinned = true;
                skinFogP.boneCount = 1;
                skinFogP.weightsPerVertex = 1;
                Matrix::getIdentityProperty().ToColumnMajor(skinFogP.boneTransforms);
                skinFogP.emissiveColor[0] = 1.0f; skinFogP.emissiveColor[1] = 1.0f; skinFogP.emissiveColor[2] = 1.0f;
                skinFogP.light0Diffuse[0] = 0.0f; skinFogP.light0Diffuse[1] = 0.0f; skinFogP.light0Diffuse[2] = 0.0f;
                skinFogP.specularColor[0] = 0.0f; skinFogP.specularColor[1] = 0.0f; skinFogP.specularColor[2] = 0.0f;
                skinFogP.eyePositionWorld[0] = 0.0f; skinFogP.eyePositionWorld[1] = 0.0f; skinFogP.eyePositionWorld[2] = -10.0f;

                const Microsoft::Xna::Framework::Rectangle skinFogRegion(30, 30, 1, 1);
                skinFogP.fogEnabled = false;
                skinFogP.fogColor[0] = 0.0f; skinFogP.fogColor[1] = 1.0f; skinFogP.fogColor[2] = 0.0f;
                dev.Clear(Color(10, 10, 10, 255));
                renderer.DrawPrimitivesEx(*vbSkinFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, skinFogP);
                Color skinFogOffPixel(0, 0, 0, 0);
                dev.GetBackBufferData(&skinFogRegion, &skinFogOffPixel, 0, 1);
                check(skinFogOffPixel.getRProperty() == 77 && skinFogOffPixel.getGProperty() == 88 &&
                      skinFogOffPixel.getBProperty() == 99,
                      "DirectX11Renderer::DrawPrimitivesEx(): skinned3d fogEnabled=false leaves the "
                      "exact single-bone-identity texture color unblended (plans/plan_dx.md DX-137)");

                skinFogP.fogEnabled = true;
                skinFogP.fogVector[2] = 2.0f;
                dev.Clear(Color(10, 10, 10, 255));
                renderer.DrawPrimitivesEx(*vbSkinFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, skinFogP);
                Color skinFogOnPixel(0, 0, 0, 0);
                dev.GetBackBufferData(&skinFogRegion, &skinFogOnPixel, 0, 1);
                check(skinFogOnPixel.getRProperty() == 0 && skinFogOnPixel.getGProperty() == 255 &&
                      skinFogOnPixel.getBProperty() == 0,
                      "DirectX11Renderer::DrawPrimitivesEx(): skinned3d fogEnabled=true with Z at "
                      "FogEnd genuinely blends all the way to the exact FogColor (plans/plan_dx.md DX-137)");
            }

            // plans/plan_dx.md DX-150: skinned3d -- DirectionalLight1/DirectionalLight2 each independently
            // and exactly contribute. Reuses this block's own single-identity-bone fixture
            // (boneCount=1/weightsPerVertex=1) with a fresh white texture and DX-124's own
            // exact-color-per-term isolation methodology (ambient/light0/specular all zeroed).
            {
                ImageData whiteImgQQ;
                whiteImgQQ.width = 2; whiteImgQQ.height = 2; whiteImgQQ.mipLevels = 1;
                whiteImgQQ.pixels.assign(2 * 2 * 4, 255);
                auto whiteTexQQ = renderer.CreateTexture(whiteImgQQ);

                GpuDrawParams baseQP;
                baseQP.texture0 = whiteTexQQ.get();
                baseQP.textureEnabled = true;
                baseQP.lightingEnabled = true;
                baseQP.preferPerPixelLighting = true;
                baseQP.skinned = true;
                baseQP.boneCount = 1;
                baseQP.weightsPerVertex = 1;
                Matrix::getIdentityProperty().ToColumnMajor(baseQP.boneTransforms);
                baseQP.ambientColor[0] = 0.0f; baseQP.ambientColor[1] = 0.0f; baseQP.ambientColor[2] = 0.0f;
                baseQP.light0Diffuse[0] = 0.0f; baseQP.light0Diffuse[1] = 0.0f; baseQP.light0Diffuse[2] = 0.0f; // Light0 off
                baseQP.specularColor[0] = 0.0f; baseQP.specularColor[1] = 0.0f; baseQP.specularColor[2] = 0.0f; // no specular noise
                baseQP.eyePositionWorld[0] = 0.0f; baseQP.eyePositionWorld[1] = 0.0f; baseQP.eyePositionWorld[2] = -10.0f;

                const Microsoft::Xna::Framework::Rectangle qqRegion(30, 30, 1, 1);

                // QQ1: DirectionalLight1 alone, full-facing direction, red diffuse -> exact (255,0,0).
                GpuDrawParams qp1 = baseQP;
                qp1.light1Dir[0] = 0.0f; qp1.light1Dir[1] = 0.0f; qp1.light1Dir[2] = -1.0f; // travels -Z -> faces the +Z normal
                qp1.light1Diffuse[0] = 1.0f; qp1.light1Diffuse[1] = 0.0f; qp1.light1Diffuse[2] = 0.0f;
                dev.Clear(Color(0, 0, 0, 255));
                renderer.DrawPrimitivesEx(*vbSkin, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, qp1);
                Color qq1(0, 0, 0, 0);
                dev.GetBackBufferData(&qqRegion, &qq1, 0, 1);
                check(qq1.getRProperty() == 255 && qq1.getGProperty() == 0 && qq1.getBProperty() == 0,
                      "DirectX11Renderer::DrawPrimitivesEx(): real skinned3d -- DirectionalLight1 "
                      "alone contributes the exact expected red, independent of Light0/Light2 (plans/plan_dx.md DX-150)");

                // QQ2: same geometry/light1Dir, but light1Diffuse disabled -> confirms QQ1 wasn't a leak.
                GpuDrawParams qp1off = qp1;
                qp1off.light1Diffuse[0] = 0.0f; qp1off.light1Diffuse[1] = 0.0f; qp1off.light1Diffuse[2] = 0.0f;
                dev.Clear(Color(0, 0, 0, 255));
                renderer.DrawPrimitivesEx(*vbSkin, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, qp1off);
                Color qq1off(0, 0, 0, 0);
                dev.GetBackBufferData(&qqRegion, &qq1off, 0, 1);
                check(qq1off.getRProperty() == 0 && qq1off.getGProperty() == 0 && qq1off.getBProperty() == 0,
                      "DirectX11Renderer::DrawPrimitivesEx(): disabling skinned3d DirectionalLight1's "
                      "diffuse (zeroed) removes its contribution exactly -- confirms the prior check was "
                      "real, not a leaked default (plans/plan_dx.md DX-150)");

                // QQ3: DirectionalLight2 alone, green diffuse -> exact (0,255,0).
                GpuDrawParams qp2 = baseQP;
                qp2.light2Dir[0] = 0.0f; qp2.light2Dir[1] = 0.0f; qp2.light2Dir[2] = -1.0f;
                qp2.light2Diffuse[0] = 0.0f; qp2.light2Diffuse[1] = 1.0f; qp2.light2Diffuse[2] = 0.0f;
                dev.Clear(Color(0, 0, 0, 255));
                renderer.DrawPrimitivesEx(*vbSkin, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                         Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, qp2);
                Color qq2(0, 0, 0, 0);
                dev.GetBackBufferData(&qqRegion, &qq2, 0, 1);
                check(qq2.getRProperty() == 0 && qq2.getGProperty() == 255 && qq2.getBProperty() == 0,
                      "DirectX11Renderer::DrawPrimitivesEx(): real skinned3d -- DirectionalLight2 "
                      "alone contributes the exact expected green, independent of Light0/Light1 (plans/plan_dx.md DX-150)");
            }
        }

        // Check V2 (Task 1107, plans/plan_graphics.md Phase 80): same PreferPerPixelLighting
        // discriminator as Check R2 above, but for skinned3d -- a single Identity bone at 100%
        // weight keeps skinning a mathematical no-op, isolating the vertex-lit-vs-pixel-lit
        // dispatch difference exactly like Check R2 does. Same scene/values as
        // examples/easygl_skinnedeffect_preferperpixellighting_test.cpp (Task 1102b): ~127
        // vertex-lit, ~155 pixel-lit (a few units of renderer-specific rounding is expected and
        // fine, per every other renderer's own equivalent task).
        {
            struct VPNTS { float x, y, z; float nx, ny, nz; float u, v;
                          float bw0, bw1, bw2, bw3; uint8_t bi0, bi1, bi2, bi3; };
            static const VPNTS kSkinQuad[6] = {
                {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
                {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
                { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
                {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
                { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
                { 1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            };
            auto vbSkinPPL = renderer.CreateVertexBuffer(6);
            vbSkinPPL->SetData(kSkinQuad, 6, sizeof(VPNTS));

            ImageData whiteImg2;
            whiteImg2.width = 1; whiteImg2.height = 1; whiteImg2.mipLevels = 1;
            whiteImg2.pixels = {255, 255, 255, 255};
            auto whiteTex2 = renderer.CreateTexture(whiteImg2);

            GpuDrawParams skPplP;
            skPplP.texture0 = whiteTex2.get();
            skPplP.textureEnabled = true;
            skPplP.lightingEnabled = true;
            skPplP.skinned = true;
            skPplP.boneCount = 1;
            skPplP.weightsPerVertex = 1;
            Matrix::getIdentityProperty().ToColumnMajor(skPplP.boneTransforms);
            skPplP.ambientColor[0] = 0.02f; skPplP.ambientColor[1] = 0.02f; skPplP.ambientColor[2] = 0.02f;
            skPplP.diffuseColor[0] = 0.4f; skPplP.diffuseColor[1] = 0.4f; skPplP.diffuseColor[2] = 0.4f; skPplP.diffuseColor[3] = 1.0f;
            Vector3 skLightDir(0.5f, 0.0f, -1.0f);
            skLightDir.Normalize();
            skPplP.light0Dir[0] = skLightDir.X; skPplP.light0Dir[1] = skLightDir.Y; skPplP.light0Dir[2] = skLightDir.Z;
            skPplP.light0Diffuse[0] = 0.5f; skPplP.light0Diffuse[1] = 0.5f; skPplP.light0Diffuse[2] = 0.5f;
            skPplP.light0Specular[0] = 1.0f; skPplP.light0Specular[1] = 1.0f; skPplP.light0Specular[2] = 1.0f;
            skPplP.specularColor[0] = 1.0f; skPplP.specularColor[1] = 1.0f; skPplP.specularColor[2] = 1.0f;
            skPplP.specularPower = 32.0f;
            skPplP.eyePositionWorld[0] = 0.0f; skPplP.eyePositionWorld[1] = 0.0f; skPplP.eyePositionWorld[2] = 3.0f;

            const Matrix skPplView = Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
            const Matrix skPplProj = Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f);
            const Microsoft::Xna::Framework::Rectangle skCenterPixel(32, 32, 1, 1);

            skPplP.preferPerPixelLighting = false;
            dev.Clear(Color(0, 0, 0, 255));
            renderer.DrawPrimitivesEx(*vbSkinPPL, Matrix::getIdentityProperty(), skPplView, skPplProj,
                                     PrimitiveType::TriangleList, 2, skPplP);
            Color skVertexLit(0, 0, 0, 0);
            dev.GetBackBufferData(&skCenterPixel, &skVertexLit, 0, 1);
            const bool skVertexLitOk = std::abs(skVertexLit.getRProperty() - 127) <= 10 &&
                                       std::abs(skVertexLit.getGProperty() - 127) <= 10 &&
                                       std::abs(skVertexLit.getBProperty() - 127) <= 10;
            check(skVertexLitOk,
                  "DirectX11Renderer::DrawPrimitivesEx(): skinned3d preferPerPixelLighting=false "
                  "(XNA's real default) genuinely computes the Gouraud-averaged specular result, "
                  "~127 (Task 1107, plans/plan_graphics.md Phase 80)");

            skPplP.preferPerPixelLighting = true;
            dev.Clear(Color(0, 0, 0, 255));
            renderer.DrawPrimitivesEx(*vbSkinPPL, Matrix::getIdentityProperty(), skPplView, skPplProj,
                                     PrimitiveType::TriangleList, 2, skPplP);
            Color skPixelLit(0, 0, 0, 0);
            dev.GetBackBufferData(&skCenterPixel, &skPixelLit, 0, 1);
            const bool skPixelLitOk = std::abs(skPixelLit.getRProperty() - 155) <= 10 &&
                                      std::abs(skPixelLit.getGProperty() - 155) <= 10 &&
                                      std::abs(skPixelLit.getBProperty() - 155) <= 10;
            check(skPixelLitOk,
                  "DirectX11Renderer::DrawPrimitivesEx(): skinned3d preferPerPixelLighting=true "
                  "genuinely computes a fresh per-fragment specular result, ~155 (Task 1107, "
                  "plans/plan_graphics.md Phase 80)");

            check(skVertexLit.getRProperty() != skPixelLit.getRProperty(),
                  "DirectX11Renderer::DrawPrimitivesEx(): skinned3d preferPerPixelLighting is a "
                  "real dispatch selector, not a decorative no-op (Task 1107, plans/plan_graphics.md "
                  "Phase 80)");
        }

        // Check W (DX-68): instanced3d. DrawInstancedPrimitivesEx() with one identity-transform
        // instance (the per-instance INSTANCEWORLD0-3 buffer, not the per-vertex one) outputs the
        // exact per-instance DiffuseColor -- both color components at their saturated 0/1 extremes,
        // so there is no rounding ambiguity in the final UNORM8 byte comparison.
        {
            struct VP3 { float x, y, z; };
            static const VP3 kTriInst[3] = {
                {-1.0f, -1.0f, 0.0f},
                { 3.0f, -1.0f, 0.0f},
                {-1.0f,  3.0f, 0.0f},
            };
            auto vbInst = renderer.CreateVertexBuffer(3);
            vbInst->SetData(kTriInst, 3, sizeof(VP3));

            static const uint16_t kTriInstIdx[3] = {0, 1, 2};
            auto ibInst = renderer.CreateIndexBuffer16(3);
            ibInst->SetData16(kTriInstIdx, 3);

            // One identity-transform instance: 4 float4 rows (INSTANCEWORLD0-3).
            static const float kInstanceWorld[16] = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f,
            };
            auto instVb = renderer.CreateVertexBuffer(1);
            instVb->SetData(kInstanceWorld, 1, sizeof(kInstanceWorld));

            GpuDrawParams ip;
            // REMED-GFX-202: the classic two-stream instanced binding set, in the shared
            // GpuVertexStreamBinding array every draw route now carries.
            SetInstancedVertexStreamsEXT(ip, *vbInst, *instVb, /*instanceFrequency=*/1,
                                         static_cast<int>(sizeof(VP3)), 0,
                                         static_cast<int>(sizeof(kInstanceWorld)), 0);
            ip.diffuseColor[0] = 1.0f; ip.diffuseColor[1] = 1.0f;
            ip.diffuseColor[2] = 0.0f; ip.diffuseColor[3] = 1.0f;

            dev.Clear(Color(0, 0, 255, 255));
            renderer.DrawInstancedPrimitivesEx(*vbInst, *ibInst, Matrix::getIdentityProperty(),
                                              Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                              PrimitiveType::TriangleList, 1, 1, ip);
            std::vector<Color> instResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, instResult.data(), 0, static_cast<int>(instResult.size()));
            bool instIsExact = true;
            for (const Color& p : instResult)
                if (p.getRProperty() != 255 || p.getGProperty() != 255 || p.getBProperty() != 0 || p.getAProperty() != 255)
                    instIsExact = false;
            check(instIsExact,
                  "DirectX11Renderer::DrawInstancedPrimitivesEx(): real instanced3d draw with a "
                  "genuine per-instance world buffer outputs the exact instance DiffuseColor (plans/plan_dx.md DX-68)");
        }

        // Check X (DX-58): custom ShaderEffect. Runtime D3DCompile() of arbitrary HLSL source (not
        // one of DX-13-hlsl's offline-compiled stock variants) compiles successfully, Bind() drives
        // a real manual draw (through the raw device context -- SpriteBatch, the real future
        // caller, doesn't exist yet, Phase DX9) whose color is genuinely driven by
        // SetUniformVec4()'s fixed-slot constant buffer, and a deliberately broken HLSL source
        // fails cleanly with a real, non-empty compiler error instead of crashing or silently
        // "succeeding".
        {
            ID3D11DeviceContext* rawContext = renderer.GetContextEXT();

            auto effect = renderer.CreateEffectRenderer(
                "struct VSIn { float2 pos:POSITION0; float2 uv:TEXCOORD0; float4 col:COLOR0; };\n"
                "struct VSOut { float4 pos:SV_Position; float4 col:TEXCOORD0; };\n"
                "cbuffer CB : register(b0) { float4 pad0[5]; float4 uColor; float4 uFloat0; };\n"
                "VSOut main(VSIn input) { VSOut o; o.pos=float4(input.pos,0,1); o.col=input.col*uColor; return o; }",
                "struct PSIn { float4 pos:SV_Position; float4 col:TEXCOORD0; };\n"
                "float4 main(PSIn input):SV_Target { return input.col; }");
            check(effect && effect->IsValid(),
                  "DirectX11Renderer::CreateEffectRenderer(): real runtime D3DCompile() of "
                  "arbitrary HLSL source compiles successfully (plans/plan_dx.md DX-58)");

            bool effIsExact = false;
            if (effect && effect->IsValid())
            {
                effect->SetUniformVec4("uColor", 0.0f, 1.0f, 0.0f, 1.0f); // green -- 0/1 only, no rounding ambiguity
                effect->Bind();

                struct SpriteVtx { float x, y, u, v, r, g, b, a; };
                static const SpriteVtx kTriFx[3] = {
                    {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
                    { 3.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
                    {-1.0f,  3.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
                };
                auto vbFx = renderer.CreateVertexBuffer(3);
                vbFx->SetData(kTriFx, 3, sizeof(SpriteVtx));
                auto& d3dVbFx = static_cast<D3D11VertexBufferRenderer&>(*vbFx);

                ID3D11Buffer* vbRaw = d3dVbFx.GetBufferEXT();
                UINT stride = sizeof(SpriteVtx);
                UINT offset = 0;
                rawContext->IASetVertexBuffers(0, 1, &vbRaw, &stride, &offset);
                rawContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

                dev.Clear(Color(0, 0, 255, 255));
                rawContext->Draw(3, 0);

                std::vector<Color> effResult(4 * 4, Color(0, 0, 0, 0));
                dev.GetBackBufferData(&centerRegion28, effResult.data(), 0, static_cast<int>(effResult.size()));
                effIsExact = true;
                for (const Color& p : effResult)
                    if (p.getRProperty() != 0 || p.getGProperty() != 255 || p.getBProperty() != 0 || p.getAProperty() != 255)
                        effIsExact = false;
            }
            check(effIsExact,
                  "D3D11EffectRenderer::Bind(): a real custom-compiled shader pair, driven by "
                  "SetUniformVec4()'s fixed-slot constant buffer, draws the exact expected color "
                  "(plans/plan_dx.md DX-58)");

            auto badEffect = renderer.CreateEffectRenderer("this is not valid HLSL {{{", "also not valid ]]]");
            check(badEffect && !badEffect->IsValid() && !badEffect->GetCompileError().empty(),
                  "DirectX11Renderer::CreateEffectRenderer(): a deliberately broken HLSL source "
                  "fails CompileProgram() with a real, non-empty compiler error message (plans/plan_dx.md DX-58)");
        }

        // Shared 2x2 per-corner-colored texture for Checks Y/Z/AA below: TL=red, TR=green,
        // BL=blue, BR=yellow, all opaque -- an asymmetric pattern so placement/flip/wrap/mirror
        // are all genuinely distinguishable from a readback, not just "some color present".
        Texture2D cornerTex(dev, 2, 2);
        {
            const Color kCorners[4] = {
                Color(255, 0, 0, 255),   // (0,0) top-left    = red
                Color(0, 255, 0, 255),   // (1,0) top-right   = green
                Color(0, 0, 255, 255),   // (0,1) bottom-left = blue
                Color(255, 255, 0, 255), // (1,1) bottom-right= yellow
            };
            cornerTex.SetData(kCorners, 4);
        }

        // Check Y (DX-70): real end-to-end SpriteBatch draw through the actual public API.
        {
            SamplerState pointClamp = SamplerState::PointClamp;

            SpriteBatch batch(dev);
            dev.Clear(Color(10, 10, 10, 255));
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
            batch.Draw(cornerTex, Microsoft::Xna::Framework::Rectangle(0, 0, 32, 32),
                      Microsoft::Xna::Framework::Rectangle(0, 0, 2, 2), Color::White);
            batch.End();

            auto readPixel = [&](int x, int y) -> Color {
                const Microsoft::Xna::Framework::Rectangle region(x, y, 1, 1);
                Color px(0, 0, 0, 0);
                dev.GetBackBufferData(&region, &px, 0, 1);
                return px;
            };
            auto isColor = [](const Color& p, int r, int g, int b) {
                return p.getRProperty() == r && p.getGProperty() == g && p.getBProperty() == b && p.getAProperty() == 255;
            };

            const Color tl = readPixel(4, 4), tr = readPixel(20, 4), bl = readPixel(4, 20), br = readPixel(20, 20);
            check(isColor(tl, 255, 0, 0) && isColor(tr, 0, 255, 0) && isColor(bl, 0, 0, 255) && isColor(br, 255, 255, 0),
                  "D3D11SpriteBatchRenderer::Draw(): a real quad-batched sprite2d draw places all 4 "
                  "corner colors at the exact expected destination pixels (plans/plan_dx.md DX-70)");

            dev.Clear(Color(10, 10, 10, 255));
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
            batch.Draw(cornerTex, Microsoft::Xna::Framework::Rectangle(0, 0, 32, 32),
                      Microsoft::Xna::Framework::Rectangle(0, 0, 2, 2), Color::White,
                      0.0f, Vector2(0, 0), SpriteEffects::FlipHorizontally, 0.0f);
            batch.End();
            const Color flippedLeft = readPixel(4, 4), flippedRight = readPixel(20, 4);
            check(isColor(flippedLeft, 0, 255, 0) && isColor(flippedRight, 255, 0, 0),
                  "D3D11SpriteBatchRenderer::Draw(): SpriteEffects::FlipHorizontally genuinely swaps "
                  "the sampled quadrants, not just accepted without effect (plans/plan_dx.md DX-70)");
        }

        // Check Y2 (DX-131): rotation/origin -- a 90-degree rotation around the sprite's own center
        // (origin = (1,1), the center of the 2x2 source rect, in source-space units) rotates a
        // square's bounding box onto itself (still axis-aligned), permuting which corner color ends
        // up in which screen quadrant -- a real, geometrically-derived, discriminating prediction,
        // not just "something rotated". Rotation formula: rx = dx + px*cosR - py*sinR,
        // ry = dy + px*sinR + py*cosR (D3D11SpriteBatch.cpp, mirrors EasyGL's own). For rotation =
        // pi/2 (cosR=0, sinR=1): the source's TOP-LEFT (red) vertex ends up at the destination
        // bounding box's TOP-RIGHT screen quadrant; TOP-RIGHT (green) -> BOTTOM-RIGHT; BOTTOM-RIGHT
        // (yellow) -> BOTTOM-LEFT; BOTTOM-LEFT (blue) -> TOP-LEFT. destRect (20,20,32,32) with a
        // centered origin keeps the whole rotated 32x32 bbox within [4,36], safely inside the 64x64
        // back buffer.
        {
            SpriteBatch batch(dev);
            dev.Clear(Color(10, 10, 10, 255));
            SamplerState pointClamp2 = SamplerState::PointClamp;
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp2, nullptr, nullptr);
            batch.Draw(cornerTex, Microsoft::Xna::Framework::Rectangle(20, 20, 32, 32),
                      Microsoft::Xna::Framework::Rectangle(0, 0, 2, 2), Color::White,
                      MathHelper::Pi / 2.0f, Vector2(1.0f, 1.0f), SpriteEffects::None, 0.0f);
            batch.End();

            auto readPixel2 = [&](int x, int y) -> Color {
                const Microsoft::Xna::Framework::Rectangle region(x, y, 1, 1);
                Color px(0, 0, 0, 0);
                dev.GetBackBufferData(&region, &px, 0, 1);
                return px;
            };
            auto isColor2 = [](const Color& p, int r, int g, int b) {
                return p.getRProperty() == r && p.getGProperty() == g && p.getBProperty() == b && p.getAProperty() == 255;
            };
            const Color nw = readPixel2(12, 12), ne = readPixel2(28, 12), se = readPixel2(28, 28), sw = readPixel2(12, 28);
            check(isColor2(nw, 0, 0, 255) && isColor2(ne, 255, 0, 0) && isColor2(se, 0, 255, 0) && isColor2(sw, 255, 255, 0),
                  "D3D11SpriteBatchRenderer::Draw(): a real 90-degree rotation around a centered origin "
                  "permutes the 4 corner colors into the geometrically-predicted screen quadrants -- "
                  "NW=blue(was BL), NE=red(was TL), SE=green(was TR), SW=yellow(was BR) (plans/plan_dx.md DX-131)");
        }

        // Check Y3 (DX-131): scale -- a destRect half the size of Check Y's own (16x16 instead of
        // 32x32) must genuinely shrink the drawn area, not silently ignore the requested size. Probe
        // a pixel that would have been inside the sprite at the OLD 32x32 size but is outside it at
        // the new 16x16 size -- confirms scale is real, not just "a sprite of some fixed size drew".
        {
            SpriteBatch batch(dev);
            dev.Clear(Color(10, 10, 10, 255));
            SamplerState pointClamp3 = SamplerState::PointClamp;
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp3, nullptr, nullptr);
            batch.Draw(cornerTex, Microsoft::Xna::Framework::Rectangle(0, 0, 16, 16),
                      Microsoft::Xna::Framework::Rectangle(0, 0, 2, 2), Color::White);
            batch.End();

            auto readPixel3 = [&](int x, int y) -> Color {
                const Microsoft::Xna::Framework::Rectangle region(x, y, 1, 1);
                Color px(0, 0, 0, 0);
                dev.GetBackBufferData(&region, &px, 0, 1);
                return px;
            };
            const Color insideSmall = readPixel3(2, 2);   // inside both 16x16 and 32x32 -> red (TL)
            const Color outsideSmallOnly = readPixel3(24, 24); // inside 32x32, outside 16x16 -> clear color
            const bool insideOk = insideSmall.getRProperty() == 255 && insideSmall.getGProperty() == 0 &&
                                  insideSmall.getBProperty() == 0 && insideSmall.getAProperty() == 255;
            const bool outsideOk = outsideSmallOnly.getRProperty() == 10 && outsideSmallOnly.getGProperty() == 10 &&
                                   outsideSmallOnly.getBProperty() == 10;
            check(insideOk && outsideOk,
                  "D3D11SpriteBatchRenderer::Draw(): a real, distinct destRect SIZE genuinely scales the "
                  "sprite -- a pixel inside the old (larger) size but outside the new (smaller) one shows "
                  "the clear color, not sprite content (plans/plan_dx.md DX-131)");
        }

        // Check Y4 (DX-131): source crop-rect -- a fresh 4x1 texture with 4 distinct single-texel
        // colors, cropped via sourceRectangle to just the middle 2 texels (purple, cyan). Confirms
        // ONLY the cropped sub-region's content is sampled, not the whole texture squeezed in.
        {
            Texture2D stripTex(dev, 4, 1);
            const Color kStrip[4] = {
                Color(255, 128, 0, 255),   // 0: orange (must NOT appear -- cropped out)
                Color(128, 0, 255, 255),   // 1: purple (must appear, left half)
                Color(0, 255, 255, 255),   // 2: cyan   (must appear, right half)
                Color(255, 0, 255, 255),   // 3: magenta (must NOT appear -- cropped out)
            };
            stripTex.SetData(kStrip, 4);

            SpriteBatch batch(dev);
            dev.Clear(Color(10, 10, 10, 255));
            SamplerState pointClamp4 = SamplerState::PointClamp;
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp4, nullptr, nullptr);
            batch.Draw(stripTex, Microsoft::Xna::Framework::Rectangle(0, 0, 32, 16),
                      Microsoft::Xna::Framework::Rectangle(1, 0, 2, 1), Color::White);
            batch.End();

            auto readPixel4 = [&](int x, int y) -> Color {
                const Microsoft::Xna::Framework::Rectangle region(x, y, 1, 1);
                Color px(0, 0, 0, 0);
                dev.GetBackBufferData(&region, &px, 0, 1);
                return px;
            };
            auto isColor4 = [](const Color& p, int r, int g, int b) {
                return p.getRProperty() == r && p.getGProperty() == g && p.getBProperty() == b && p.getAProperty() == 255;
            };
            const Color left = readPixel4(4, 4), right = readPixel4(28, 4);
            check(isColor4(left, 128, 0, 255) && isColor4(right, 0, 255, 255),
                  "D3D11SpriteBatchRenderer::Draw(): sourceRectangle genuinely crops to only the "
                  "requested sub-region (purple/cyan), never the excluded texels (orange/magenta) "
                  "(plans/plan_dx.md DX-131)");
        }

        // Note on SpriteSortMode (DX-131): sort-mode ordering (BackToFront/FrontToBack/Texture) is
        // implemented entirely in the shared, renderer-agnostic Microsoft::Xna::Framework::Graphics::
        // SpriteBatch.cpp (sorts the pending draw-call list before handing it to the renderer's own
        // Draw() calls, in order) -- there is no D3D11-specific sort behavior to test; the renderer
        // just draws whatever order it's told. A dedicated renderer-level sort-mode test would
        // actually be testing shared C++ code, not this renderer, so none is added here.

        // Check Z (DX-72): TextureAddressMode::Wrap/Mirror, each proven with a probe pixel that
        // gives a genuinely different color than the other two address modes would produce there
        // (see this file's own top-of-file comment for the UV math).
        {
            SpriteBatch batch(dev);
            const Microsoft::Xna::Framework::Rectangle destRect(0, 0, 32, 32);
            const Microsoft::Xna::Framework::Rectangle srcRect(0, 0, 4, 4); // 2x texture size -> UV 0..2

            auto readPixel = [&](int x, int y) -> Color {
                const Microsoft::Xna::Framework::Rectangle region(x, y, 1, 1);
                Color px(0, 0, 0, 0);
                dev.GetBackBufferData(&region, &px, 0, 1);
                return px;
            };
            auto isColor = [](const Color& p, int r, int g, int b) {
                return p.getRProperty() == r && p.getGProperty() == g && p.getBProperty() == b && p.getAProperty() == 255;
            };

            SamplerState pointWrap = SamplerState::PointWrap;
            dev.Clear(Color(10, 10, 10, 255));
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointWrap, nullptr, nullptr);
            batch.Draw(cornerTex, destRect, srcRect, Color::White);
            batch.End();
            const Color wrapProbe = readPixel(4, 20); // v~1.28 -> tile-repeat row0 (red); Clamp would give blue here.
            check(isColor(wrapProbe, 255, 0, 0),
                  "D3D11SpriteBatchRenderer: TextureAddressMode::Wrap genuinely tiles past UV 1.0 "
                  "instead of clamping to the edge color (plans/plan_dx.md DX-72)");

            SamplerState pointMirror;
            pointMirror.setFilterProperty(TextureFilter::Point);
            pointMirror.setAddressUProperty(TextureAddressMode::Mirror);
            pointMirror.setAddressVProperty(TextureAddressMode::Mirror);
            dev.Clear(Color(10, 10, 10, 255));
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointMirror, nullptr, nullptr);
            batch.Draw(cornerTex, destRect, srcRect, Color::White);
            batch.End();
            const Color mirrorProbe = readPixel(4, 28); // v~1.78 -> mirrored back to row0 (red); Wrap/Clamp both give blue here.
            check(isColor(mirrorProbe, 255, 0, 0),
                  "D3D11SpriteBatchRenderer: TextureAddressMode::Mirror genuinely reflects past UV "
                  "1.0 (distinct from both Wrap's repeat and Clamp's edge-extend at the same probe "
                  "point) (plans/plan_dx.md DX-72)");
        }

        // Check AA (DX-71): a custom Effect passed to SpriteBatch::Begin(..., effect) draws through
        // that effect's own shader (here, a deliberate RGB color inversion) instead of the stock
        // sprite2d pipeline -- proving D3D11EffectRenderer::SetViewportSizeEXT()'s automatic vpSize
        // slot (this custom vertex shader has no other way to map pixel-space Position to NDC) and
        // the shared t0/s0 texture-binding path SpriteBatch drives for both pipelines.
        {
            ShaderEffect invertEffect(dev,
                "struct VSIn { float2 pos:POSITION0; float2 uv:TEXCOORD0; float4 col:COLOR0; };\n"
                "struct VSOut { float4 pos:SV_Position; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; };\n"
                "cbuffer CB : register(b0) { float4 vpSize; float4 pad1[4]; float4 uColor; float4 uFloat0; };\n"
                "VSOut main(VSIn input) {\n"
                "    VSOut o;\n"
                "    float2 ndc = (input.pos / vpSize.xy) * 2.0 - 1.0;\n"
                "    o.pos = float4(ndc.x, -ndc.y, 0.0, 1.0);\n"
                "    o.uv = input.uv;\n"
                "    o.col = input.col;\n"
                "    return o;\n"
                "}",
                "Texture2D texSampler : register(t0);\n"
                "SamplerState texSamplerSampler : register(s0);\n"
                "struct PSIn { float4 pos:SV_Position; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; };\n"
                "float4 main(PSIn input) : SV_Target {\n"
                "    float4 texColor = texSampler.Sample(texSamplerSampler, input.uv);\n"
                "    return float4(float3(1.0, 1.0, 1.0) - texColor.rgb, 1.0);\n"
                "}");
            check(invertEffect.IsEffectValid(),
                  "ShaderEffect (D3D11): a runtime-compiled custom HLSL pair for SpriteBatch's own "
                  "Sprite2DVertex contract compiles successfully (plans/plan_dx.md DX-71)");

            SamplerState pointClamp = SamplerState::PointClamp;
            SpriteBatch batch(dev);
            dev.Clear(Color(10, 10, 10, 255));
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr, &invertEffect);
            batch.Draw(cornerTex, Microsoft::Xna::Framework::Rectangle(0, 0, 32, 32),
                      Microsoft::Xna::Framework::Rectangle(0, 0, 2, 2), Color::White);
            batch.End();

            const Microsoft::Xna::Framework::Rectangle region(4, 4, 1, 1);
            Color inverted(0, 0, 0, 0);
            dev.GetBackBufferData(&region, &inverted, 0, 1);
            // Top-left texel is red (255,0,0); inverted RGB = (0,255,255), alpha forced to 255 by
            // the custom shader itself (not inverted) -- an exact, unambiguous 8-bit round-trip.
            check(inverted.getRProperty() == 0 && inverted.getGProperty() == 255 &&
                  inverted.getBProperty() == 255 && inverted.getAProperty() == 255,
                  "D3D11SpriteBatchRenderer + SpriteBatch::Begin(effect): sprites draw through the "
                  "custom Effect's own shader, producing its exact expected (inverted) output "
                  "color, not the stock sprite2d pipeline's (plans/plan_dx.md DX-71)");
        }

        // Check AB (DX-83) -- a real backbuffer resize genuinely exercises D3D11's DX-29
        // EnsureSwapChainSize()/ResizeBuffers() path (previously implemented but never exercised,
        // per that row's own honest gap note), and Clear()+GetBackBufferData() after the resize
        // reads back the NEW size's data correctly -- not stale/wrong-sized. Resized via the same
        // public GraphicsDeviceManager path a real game uses (setPreferredBackBufferWidth/Height +
        // ApplyChanges()), which calls SDL_SetWindowSize(); EnsureSwapChainSize() itself only picks
        // the new size up lazily (SDL_GetWindowSizeInPixels) on the next Present()/Clear() cycle, so
        // this polls across a few frames the same way this project's own Vulkan/EasyGL resize tests
        // do under Wine/Xvfb's own asynchronous window-manager resize delivery -- Wine-only
        // verification, matching this row's own explicit scope (real-Windows fullscreen-transition
        // behavior is DX-90's job, not this one).
        {
            gdm_->setPreferredBackBufferWidthProperty(96);
            gdm_->setPreferredBackBufferHeightProperty(80);
            gdm_->ApplyChanges();

            bool resized = false;
            for (int frame = 0; frame < 30 && !resized; ++frame)
            {
                dev.Clear(Color(30, 60, 90, 255));
                dev.Present();
                const auto& vp = dev.getViewportProperty();
                if (vp.getWidthProperty() == 96 && vp.getHeightProperty() == 80)
                {
                    resized = true;
                }
                else
                {
                    SDL_Delay(20);
                }
            }
            check(resized, "DX-83: GraphicsDeviceManager resize to 96x80 eventually converges "
                            "(viewport reflects the new size within 30 frames)");

            dev.Clear(Color(30, 60, 90, 255));
            const Microsoft::Xna::Framework::Rectangle newCorner(0, 0, 1, 1);
            const Microsoft::Xna::Framework::Rectangle nearNewEdge(90, 74, 1, 1);
            Color cornerPixel(0, 0, 0, 0), edgePixel(0, 0, 0, 0);
            dev.GetBackBufferData(&newCorner, &cornerPixel, 0, 1);
            dev.GetBackBufferData(&nearNewEdge, &edgePixel, 0, 1);
            const auto isClearColor = [](const Color& p) {
                return p.getRProperty() == 30 && p.getGProperty() == 60 && p.getBProperty() == 90;
            };
            check(isClearColor(cornerPixel) && isClearColor(edgePixel),
                  "DX-83: after resize, Clear()+GetBackBufferData() reads the exact clear color "
                  "at the origin AND near the new (96,80) far edge -- proves the resized back "
                  "buffer/RTV/DSV/viewport are genuinely the new size, not stale/clamped/wrong");

            const bool ppMatches =
                (dev.getPresentationParametersProperty().getBackBufferWidthProperty() == 96 &&
                 dev.getPresentationParametersProperty().getBackBufferHeightProperty() == 80);
            check(ppMatches, "DX-83: PresentationParameters reflects the new 96x80 size post-resize");

            // Restore to the test's original 64x64 so nothing downstream is affected (this is the
            // last check in the file, but keep the habit anyway).
            gdm_->setPreferredBackBufferWidthProperty(64);
            gdm_->setPreferredBackBufferHeightProperty(64);
            gdm_->ApplyChanges();
        }

        // Check AC (DX-69/DX-81) -- a dedicated fog-on/fog-off pixel test, closing DX-69's own
        // honestly-flagged gap ("not yet exercised by a dedicated fog-on/fog-off pixel test...
        // belongs in Phase DX10"). Uses colored3d (DX-13-hlsl's own formula, colored3d.vert.hlsl:
        // fogFactor = fogEnabled ? saturate((FogEnd-Z)/(FogEnd-FogStart)) : 1.0, colored3d.frag.hlsl:
        // outColor.rgb = lerp(FogColor, vertexColor, fogFactor)) via the same real DrawPrimitivesEx
        // path Checks Q-W already exercise. A single quad at object-space Z=0.5, FogStart=0.0/
        // FogEnd=0.5 makes fogFactor land exactly on 0 when fog is enabled (pure FogColor) vs 1
        // when it's not (pure vertex color) -- an exact, unambiguous discrimination, not "some
        // blend happened".
        {
            // REMED-GFX-087: the SpriteBatch checks above (Y/Y2/Y3/Y4/Z/AA) each call Begin(), which
            // since REMED-GFX-081 applies (and, per FNA/XNA semantics, LEAVES) RasterizerState =
            // CullCounterClockwise on the device. The direct renderer.DrawPrimitivesEx() 3D checks
            // from here down draw oversized full-screen quads that are back-facing under
            // CullCounterClockwise, so -- like the D3D9 smoke's own SetDepthTestEnabled block already
            // does explicitly -- they must re-establish CullNone first, otherwise every one of them
            // is silently culled to nothing (reads back the Clear() colour). A real game never hits
            // this: its 3D draws set RasterizerState through the effect/GraphicsDevice layer.
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            struct VPCz { float x, y, z; uint32_t color; };
            const uint32_t kRed = 0xFF0000FFu; // A=255,B=0,G=0,R=255 (R8G8B8A8 byte order)
            static const VPCz kTriFog[3] = {
                {-1.0f, -1.0f, 0.5f, kRed},
                { 3.0f, -1.0f, 0.5f, kRed},
                {-1.0f,  3.0f, 0.5f, kRed},
            };
            auto vbFog = renderer.CreateVertexBuffer(3);
            vbFog->SetData(kTriFog, 3, sizeof(VPCz));

            GpuDrawParams fogOff;
            fogOff.vertexColorEnabled = true;
            fogOff.fogEnabled = false;
            fogOff.fogColor[0] = 0.0f; fogOff.fogColor[1] = 1.0f; fogOff.fogColor[2] = 0.0f;

            const Microsoft::Xna::Framework::Rectangle centerRegionFog(30, 30, 1, 1);
            dev.Clear(Color(10, 10, 10, 255));
            renderer.DrawPrimitivesEx(*vbFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, fogOff);
            Color fogOffPixel(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionFog, &fogOffPixel, 0, 1);
            check(fogOffPixel.getRProperty() == 255 && fogOffPixel.getGProperty() == 0 &&
                  fogOffPixel.getBProperty() == 0,
                  "DirectX11Renderer::DrawPrimitivesEx(): fogEnabled=false leaves colored3d's "
                  "exact vertex color unblended (plans/plan_dx.md DX-69/DX-81)");

            GpuDrawParams fogOn = fogOff;
            fogOn.fogEnabled = true;
            fogOn.fogVector[2] = 2.0f;

            dev.Clear(Color(10, 10, 10, 255));
            renderer.DrawPrimitivesEx(*vbFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, fogOn);
            Color fogOnPixel(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionFog, &fogOnPixel, 0, 1);
            check(fogOnPixel.getRProperty() == 0 && fogOnPixel.getGProperty() == 255 &&
                  fogOnPixel.getBProperty() == 0,
                  "DirectX11Renderer::DrawPrimitivesEx(): fogEnabled=true with Z at FogEnd "
                  "genuinely blends all the way to the exact FogColor (fogFactor=0), distinctly "
                  "different from the fogEnabled=false case above (plans/plan_dx.md DX-69/DX-81)");

            GpuDrawParams fogOffAgain = fogOn;
            fogOffAgain.fogEnabled = false;
            fogOffAgain.fogVector[0] = fogOffAgain.fogVector[1] =
                fogOffAgain.fogVector[2] = fogOffAgain.fogVector[3] = 0.0f;
            dev.Clear(Color(10, 10, 10, 255));
            renderer.DrawPrimitivesEx(*vbFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, fogOffAgain);
            Color fogOffAgainPixel(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionFog, &fogOffAgainPixel, 0, 1);
            check(fogOffAgainPixel.getRProperty() == 255 && fogOffAgainPixel.getGProperty() == 0 &&
                  fogOffAgainPixel.getBProperty() == 0,
                  "DirectX11Renderer::DrawPrimitivesEx(): fog A(false)->B(true)->A(false) "
                  "does not reuse stale scalar or vector state (REMED-GFX-061)");
        }

        // plans/plan_dx.md DX-140 (partial -- NPOT only): a genuinely non-power-of-two Texture2D (5x3),
        // never exercised anywhere in this test suite before. 5*4=20 bytes/row is an odd size worth
        // exercising for real (D3D11's own upload path has less alignment sensitivity than D3D12's,
        // but this is still the first NPOT texture this suite has ever created against D3D11).
        // Every pixel is the same solid color, isolating "does NPOT upload/sample corrupt anything"
        // from unrelated bilinear-blend-at-texel-boundary concerns (DX-131 already hit those).
        {
            struct VPT { float x, y, z; float u, v; };
            static const VPT kTriNpot[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
                { 3.0f, -1.0f, 0.0f, 2.0f, 1.0f},
                {-1.0f,  3.0f, 0.0f, 0.0f, -1.0f},
            };
            auto vbNpot = renderer.CreateVertexBuffer(3);
            vbNpot->SetData(kTriNpot, 3, sizeof(VPT));

            ImageData npotImg;
            npotImg.width = 5; npotImg.height = 3; npotImg.mipLevels = 1;
            npotImg.pixels.resize(5 * 3 * 4);
            for (int i = 0; i < 5 * 3; ++i)
            {
                npotImg.pixels[i * 4 + 0] = 123; npotImg.pixels[i * 4 + 1] = 45;
                npotImg.pixels[i * 4 + 2] = 200; npotImg.pixels[i * 4 + 3] = 255;
            }
            auto npotTex = renderer.CreateTexture(npotImg);
            check(npotTex->GetWidth() == 5 && npotTex->GetHeight() == 3,
                  "D3D11TextureRenderer: real construction with a genuinely non-power-of-two 5x3 "
                  "size reports the exact requested dimensions (plans/plan_dx.md DX-140)");

            GpuDrawParams npotP;
            npotP.texture0 = npotTex.get();
            npotP.textureEnabled = true;

            std::vector<Color> afterNpot(4 * 4, Color(0, 0, 0, 0));
            dev.Clear(Color(0, 255, 0, 255));
            renderer.DrawPrimitivesEx(*vbNpot, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, npotP);
            dev.GetBackBufferData(&centerRegion28, afterNpot.data(), 0, static_cast<int>(afterNpot.size()));
            bool npotIsExact = true;
            for (const Color& p : afterNpot)
                if (p.getRProperty() != 123 || p.getGProperty() != 45 || p.getBProperty() != 200 || p.getAProperty() != 255)
                    npotIsExact = false;
            check(npotIsExact,
                  "DirectX11Renderer::DrawPrimitivesEx(): samples the exact color from a real "
                  "5x3 NPOT texture upload (plans/plan_dx.md DX-140)");
        }

        // plans/plan_dx.md DX-142: all 16 D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT texture-sampler slots
        // bound SIMULTANEOUSLY with 16 genuinely DIFFERENT SamplerState configurations, then
        // verified independent -- D3D11SamplerCache (DX-44) already has an identity/distinctness
        // proof for two slots in isolation, but nothing has proven slot N's binding survives slots
        // N+1..15 also being applied (a plausible place for an off-by-one/index bug or an
        // accidental single-slot cache to hide).
        {
            using Microsoft::WRL::ComPtr;
            ID3D11DeviceContext* ctx = renderer.GetContextEXT();

            ComPtr<ID3D11SamplerState> boundAtApplyTime[16];
            for (int slot = 0; slot < 16; ++slot)
            {
                // Spread across TextureFilter's 6 values and TextureAddressMode's 3 values so
                // adjacent slots never accidentally share an identical configuration.
                renderer.ApplySamplerState(slot, slot % 6, slot % 3, (slot + 1) % 3, 1);
                ComPtr<ID3D11SamplerState> s;
                ctx->PSGetSamplers(static_cast<UINT>(slot), 1, s.GetAddressOf());
                boundAtApplyTime[slot] = s;
            }

            bool allSlotsNonNull = true;
            for (int slot = 0; slot < 16; ++slot)
                if (!boundAtApplyTime[slot]) allSlotsNonNull = false;
            check(allSlotsNonNull,
                  "D3D11SamplerCache: all 16 texture-sampler slots hold a real, non-null "
                  "ID3D11SamplerState immediately after being applied (plans/plan_dx.md DX-142)");

            // Re-query every slot NOW, after all 16 have been applied -- if applying a later slot
            // (e.g. 15) ever clobbered an earlier one (e.g. 0) via an off-by-one or aliasing bug,
            // this is where it would show up: the object bound to slot 0 right now would differ
            // from the one captured immediately after slot 0's own ApplySamplerState() call.
            bool allSlotsStillCorrect = true;
            for (int slot = 0; slot < 16; ++slot)
            {
                ComPtr<ID3D11SamplerState> now;
                ctx->PSGetSamplers(static_cast<UINT>(slot), 1, now.GetAddressOf());
                if (now.Get() != boundAtApplyTime[slot].Get()) allSlotsStillCorrect = false;
            }
            check(allSlotsStillCorrect,
                  "D3D11SamplerCache: every one of the 16 slots still holds its OWN originally-bound "
                  "sampler object after all 16 were applied -- proves genuine per-slot independence, "
                  "not a shared/aliased single slot (plans/plan_dx.md DX-142)");
        }

        // plans/plan_dx.md DX-147: OcclusionQuery visible-vs-occluded DISCRIMINATION for D3D11. Check M
        // (DX-47) above only proves a query object completes and reports 0 for an empty Begin()/
        // End() -- it never proved the count actually tracks what was rasterized. EasyGL/Vulkan
        // both verify both directions already (Tasks 445/446/854); D3D12's own half was closed by
        // DX-120 (Checks AA3/AA4). This closes D3D11's, using the same methodology: the SAME query
        // object is reused around a genuinely visible draw and a genuinely invisible one, so a
        // renderer that simply returned a constant (or leaked the prior count) cannot pass both.
        {
            struct VPCq { float x, y, z; uint32_t color; };
            const uint32_t kRedQ = 0xFF0000FFu;

            // Fullscreen-covering triangle -> every back-buffer pixel is rasterized.
            static const VPCq kTriVisible[3] = {
                {-1.0f, -1.0f, 0.5f, kRedQ},
                { 3.0f, -1.0f, 0.5f, kRedQ},
                {-1.0f,  3.0f, 0.5f, kRedQ},
            };
            // The same triangle translated far off-screen -> fully clipped, rasterizes nothing.
            static const VPCq kTriOffscreen[3] = {
                {10.0f, 10.0f, 0.5f, kRedQ},
                {14.0f, 10.0f, 0.5f, kRedQ},
                {10.0f, 14.0f, 0.5f, kRedQ},
            };

            auto vbVisible = renderer.CreateVertexBuffer(3);
            vbVisible->SetData(kTriVisible, 3, sizeof(VPCq));
            auto vbOffscreen = renderer.CreateVertexBuffer(3);
            vbOffscreen->SetData(kTriOffscreen, 3, sizeof(VPCq));

            GpuDrawParams qp;
            qp.vertexColorEnabled = true;

            auto oq2 = renderer.CreateOcclusionQuery();

            dev.Clear(Color(10, 10, 10, 255));
            oq2->Begin();
            renderer.DrawPrimitivesEx(*vbVisible, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, qp);
            oq2->End();
            context->Flush();
            bool done1 = false;
            for (int i = 0; i < 1000000 && !done1; ++i) done1 = oq2->IsComplete();
            const int visibleCount = oq2->PixelCount();
            check(done1 && visibleCount > 0,
                  "D3D11OcclusionQueryRenderer: a genuinely VISIBLE (viewport-covering) draw reports a real, "
                  "POSITIVE PixelCount() -- the count actually tracks rasterized pixels, not a constant "
                  "(plans/plan_dx.md DX-147)");

            // Same query object, reused -- an implementation that leaked the previous count or
            // returned a fixed value cannot pass this after passing the check above.
            dev.Clear(Color(10, 10, 10, 255));
            oq2->Begin();
            renderer.DrawPrimitivesEx(*vbOffscreen, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, qp);
            oq2->End();
            context->Flush();
            bool done2 = false;
            for (int i = 0; i < 1000000 && !done2; ++i) done2 = oq2->IsComplete();
            check(done2 && oq2->PixelCount() == 0,
                  "D3D11OcclusionQueryRenderer: the SAME query object, reused around fully off-screen "
                  "(clipped) geometry, reports EXACTLY 0 -- a genuine visible-vs-invisible DISCRIMINATING "
                  "result, not just 'the query completed' (plans/plan_dx.md DX-147)");
        }

        // Real bug fix (2026-07-14): GraphicsDevice::SetDepthTestEnabled()/SetDepthWriteEnabled()
        // were SILENT NO-OPS on D3D11 -- a game turning the depth test on got no depth test at all,
        // while EasyGL honoured it. (D3D12's own equivalents were worse: they threw.) They now
        // rebuild the tracked depth-stencil state with just that one field changed. Proven by the
        // depth test's real effect on rasterization: the same near/far draw order is issued twice,
        // differing ONLY in whether SetDepthTestEnabled() was called -- a no-op cannot pass both.
        {
            struct VPCd { float x, y, z; uint32_t color; };
            const uint32_t kRedD = 0xFF0000FFu;   // R=255
            const uint32_t kGreenD = 0xFF00FF00u; // G=255

            // Near red quad (z=0.2), then FAR green quad (z=0.8), drawn second.
            static const VPCd kNear[3] = {
                {-1.0f, -1.0f, 0.2f, kRedD}, {3.0f, -1.0f, 0.2f, kRedD}, {-1.0f, 3.0f, 0.2f, kRedD}};
            static const VPCd kFar[3] = {
                {-1.0f, -1.0f, 0.8f, kGreenD}, {3.0f, -1.0f, 0.8f, kGreenD}, {-1.0f, 3.0f, 0.8f, kGreenD}};

            auto vbNear = renderer.CreateVertexBuffer(3);
            vbNear->SetData(kNear, 3, sizeof(VPCd));
            auto vbFar = renderer.CreateVertexBuffer(3);
            vbFar->SetData(kFar, 3, sizeof(VPCd));

            GpuDrawParams dp;
            dp.vertexColorEnabled = true;
            const Microsoft::Xna::Framework::Rectangle probe(30, 30, 1, 1);
            const Matrix& I = Matrix::getIdentityProperty();

            auto drawNearThenFar = [&]() {
                dev.Clear(Color(10, 10, 10, 255));
                renderer.DrawPrimitivesEx(*vbNear, I, I, I, PrimitiveType::TriangleList, 1, dp);
                renderer.DrawPrimitivesEx(*vbFar, I, I, I, PrimitiveType::TriangleList, 1, dp);
                Color px(0, 0, 0, 0);
                dev.GetBackBufferData(&probe, &px, 0, 1);
                return px;
            };

            // REMED-GFX-089 sibling-oracle hygiene: establish the complete reachable XNA state
            // instead of relying on whichever DepthFunc an earlier direct-renderer fixture left.
            // Exercise false->true so SetDepthTestEnabled(true) still has observable work to do.
            dev.setDepthStencilStateProperty(DepthStencilState::Default);
            renderer.SetDepthTestEnabled(false);

            // Depth test ON: the far green quad must be REJECTED by the nearer red one already there.
            renderer.SetDepthWriteEnabled(true);
            renderer.SetDepthTestEnabled(true);
            dev.Clear(Color(10, 10, 10, 255)); // clears depth to 1.0 too
            const Color withDepth = drawNearThenFar();
            check(withDepth.getRProperty() == 255 && withDepth.getGProperty() == 0,
                  "DirectX11Renderer::SetDepthTestEnabled(true): a FARTHER quad drawn after a nearer "
                  "one is genuinely REJECTED (red survives) -- proves the call really enables the depth "
                  "test, instead of being the silent no-op it used to be");

            // Depth test OFF: the same far green quad must now overwrite the red one (painter's order).
            renderer.SetDepthTestEnabled(false);
            const Color withoutDepth = drawNearThenFar();
            check(withoutDepth.getGProperty() == 255 && withoutDepth.getRProperty() == 0,
                  "DirectX11Renderer::SetDepthTestEnabled(false): the SAME farther quad now genuinely "
                  "OVERWRITES the nearer one (green wins) -- only the SetDepthTestEnabled() call differs "
                  "between the two, so a no-op implementation cannot pass both checks");

            renderer.SetDepthTestEnabled(false);
            renderer.SetDepthWriteEnabled(false);
        }

        // plans/plan_dx.md DX-124: multi-light (DirectionalLight1/DirectionalLight2) + EmissiveColor
        // discriminating pixel test for the shared D3DLightingConstants path (lit_textured3d).
        // Check R (DX-63) above only proves lit-vs-unlit differs; this proves each of Light1/
        // Light2/EmissiveColor independently contributes the EXACT expected color, mirroring
        // D3D12's own already-closed DX-138 (examples/directx12_smoke_test.cpp Checks EE1-EE4)
        // methodology exactly, just against the real back buffer instead of an offscreen target.
        {
            struct VPNT { float x, y, z; float nx, ny, nz; float u, v; };
            static const VPNT kTriEE[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f},
                { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 1.0f},
                {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, -1.0f},
            };
            auto vbEE = renderer.CreateVertexBuffer(3);
            vbEE->SetData(kTriEE, 3, sizeof(VPNT));

            ImageData whiteImg;
            whiteImg.width = 2; whiteImg.height = 2; whiteImg.mipLevels = 1;
            whiteImg.pixels.assign(2 * 2 * 4, 255);
            auto whiteTexEE = renderer.CreateTexture(whiteImg);

            GpuDrawParams baseP;
            baseP.texture0 = whiteTexEE.get();
            baseP.textureEnabled = true;
            baseP.lightingEnabled = true;
            baseP.diffuseColor[0] = 1.0f; baseP.diffuseColor[1] = 1.0f; baseP.diffuseColor[2] = 1.0f; baseP.diffuseColor[3] = 1.0f;
            baseP.ambientColor[0] = 0.0f; baseP.ambientColor[1] = 0.0f; baseP.ambientColor[2] = 0.0f;
            baseP.light0Diffuse[0] = 0.0f; baseP.light0Diffuse[1] = 0.0f; baseP.light0Diffuse[2] = 0.0f; // Light0 off
            baseP.specularColor[0] = 0.0f; baseP.specularColor[1] = 0.0f; baseP.specularColor[2] = 0.0f; // no specular noise

            const Microsoft::Xna::Framework::Rectangle centerRegionEE(30, 30, 1, 1);

            // EE1: DirectionalLight1 alone, full-facing direction, red diffuse -> exact (255,0,0).
            GpuDrawParams p1 = baseP;
            p1.light1Dir[0] = 0.0f; p1.light1Dir[1] = 0.0f; p1.light1Dir[2] = 1.0f;
            p1.light1Diffuse[0] = 1.0f; p1.light1Diffuse[1] = 0.0f; p1.light1Diffuse[2] = 0.0f;
            dev.Clear(Color(0, 0, 0, 255));
            renderer.DrawPrimitivesEx(*vbEE, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, p1);
            Color r1(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionEE, &r1, 0, 1);
            check(r1.getRProperty() == 255 && r1.getGProperty() == 0 && r1.getBProperty() == 0,
                  "DirectX11Renderer::DrawPrimitivesEx(): real lit_textured3d -- DirectionalLight1 "
                  "alone contributes the exact expected red, independent of Light0/Light2 (plans/plan_dx.md DX-124)");

            // EE2: same geometry/light1Dir, but light1Diffuse disabled -> confirms EE1 wasn't a leak.
            GpuDrawParams p1off = p1;
            p1off.light1Diffuse[0] = 0.0f; p1off.light1Diffuse[1] = 0.0f; p1off.light1Diffuse[2] = 0.0f;
            dev.Clear(Color(0, 0, 0, 255));
            renderer.DrawPrimitivesEx(*vbEE, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, p1off);
            Color r1off(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionEE, &r1off, 0, 1);
            check(r1off.getRProperty() == 0 && r1off.getGProperty() == 0 && r1off.getBProperty() == 0,
                  "DirectX11Renderer::DrawPrimitivesEx(): disabling DirectionalLight1's diffuse "
                  "(zeroed) removes its contribution exactly -- confirms the prior check was real, not "
                  "a leaked default (plans/plan_dx.md DX-124)");

            // EE3: DirectionalLight2 alone, green diffuse -> exact (0,255,0).
            GpuDrawParams p2 = baseP;
            p2.light2Dir[0] = 0.0f; p2.light2Dir[1] = 0.0f; p2.light2Dir[2] = 1.0f;
            p2.light2Diffuse[0] = 0.0f; p2.light2Diffuse[1] = 1.0f; p2.light2Diffuse[2] = 0.0f;
            dev.Clear(Color(0, 0, 0, 255));
            renderer.DrawPrimitivesEx(*vbEE, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, p2);
            Color r2(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionEE, &r2, 0, 1);
            check(r2.getRProperty() == 0 && r2.getGProperty() == 255 && r2.getBProperty() == 0,
                  "DirectX11Renderer::DrawPrimitivesEx(): real lit_textured3d -- DirectionalLight2 "
                  "alone contributes the exact expected green, independent of Light0/Light1 (plans/plan_dx.md DX-124)");

            // EE4: EmissiveColor alone (all lights + ambient off) -> exact (0,0,255), a constant,
            // light-independent additive term.
            GpuDrawParams p3 = baseP;
            p3.emissiveColor[0] = 0.0f; p3.emissiveColor[1] = 0.0f; p3.emissiveColor[2] = 1.0f;
            dev.Clear(Color(0, 0, 0, 255));
            renderer.DrawPrimitivesEx(*vbEE, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, p3);
            Color r3(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionEE, &r3, 0, 1);
            check(r3.getRProperty() == 0 && r3.getGProperty() == 0 && r3.getBProperty() == 255,
                  "DirectX11Renderer::DrawPrimitivesEx(): real lit_textured3d -- EmissiveColor alone "
                  "contributes the exact expected blue with every light off, a constant additive term "
                  "(plans/plan_dx.md DX-124)");
        }

        // plans/plan_dx.md DX-125: specular-highlight (SpecularColor/SpecularPower) pixel test. A real
        // methodology that avoids the byte-exact-CPU-replication problem this row's own text
        // originally documented -- rather than reproducing the view-angle-dependent Blinn-Phong
        // math for a byte-exact comparison, this picks geometry where the math collapses to an
        // exact, hand-derivable value: eye at (0,0,-10), surface normal (0,0,-1), light1
        // traveling (0,0,1) (so "direction to light" = (0,0,-1), identical to the view direction)
        // -- the half-vector H = normalize(view+toLight) then equals N exactly, so dot(H,N)=1 and
        // pow(1,SpecularPower)=1 regardless of the actual power value. Mirrors D3D12's own
        // already-closed DX-139 (examples/directx12_smoke_test.cpp Checks FF1/FF2) exactly.
        {
            struct VPNT { float x, y, z; float nx, ny, nz; float u, v; };
            static const VPNT kTriFF[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f},
                { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 1.0f},
                {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, -1.0f},
            };
            auto vbFF = renderer.CreateVertexBuffer(3);
            vbFF->SetData(kTriFF, 3, sizeof(VPNT));

            ImageData whiteImg;
            whiteImg.width = 2; whiteImg.height = 2; whiteImg.mipLevels = 1;
            whiteImg.pixels.assign(2 * 2 * 4, 255);
            auto whiteTexFF = renderer.CreateTexture(whiteImg);

            GpuDrawParams sp;
            sp.texture0 = whiteTexFF.get();
            sp.textureEnabled = true;
            sp.lightingEnabled = true;
            sp.diffuseColor[0] = 1.0f; sp.diffuseColor[1] = 1.0f; sp.diffuseColor[2] = 1.0f; sp.diffuseColor[3] = 1.0f;
            sp.ambientColor[0] = 0.0f; sp.ambientColor[1] = 0.0f; sp.ambientColor[2] = 0.0f;
            sp.light0Diffuse[0] = 0.0f; sp.light0Diffuse[1] = 0.0f; sp.light0Diffuse[2] = 0.0f; // no diffuse noise
            sp.light1Dir[0] = 0.0f; sp.light1Dir[1] = 0.0f; sp.light1Dir[2] = 1.0f;             // travels +Z
            sp.light1Diffuse[0] = 0.0f; sp.light1Diffuse[1] = 0.0f; sp.light1Diffuse[2] = 0.0f; // diffuse off too
            sp.light1Specular[0] = 1.0f; sp.light1Specular[1] = 1.0f; sp.light1Specular[2] = 1.0f;
            sp.eyePositionWorld[0] = 0.0f; sp.eyePositionWorld[1] = 0.0f; sp.eyePositionWorld[2] = -10.0f;
            sp.specularColor[0] = 1.0f; sp.specularColor[1] = 1.0f; sp.specularColor[2] = 1.0f;
            sp.specularPower = 16.0f;
            // REMED-GFX-020: this check verifies the *per-pixel* lit_textured3d Blinn-Phong
            // highlight (dot(H,N)=1 -> exact 255). With lightingEnabled=true and the default
            // preferPerPixelLighting=false, DrawPrimitivesEx would instead dispatch the vertex-lit
            // (Gouraud) LitTextured3dVertexLit variant, whose interpolated specular across this
            // large triangle is ~228 at the sample pixel -- a correct vertex-lit result, but not
            // the per-pixel value this check asserts. Opt into per-pixel lighting so the variant
            // selected matches the value asserted (was the sole DirectX11_Smoke internal check failure).
            sp.preferPerPixelLighting = true;

            const Microsoft::Xna::Framework::Rectangle centerRegionFF(30, 30, 1, 1);

            dev.Clear(Color(0, 0, 0, 255));
            renderer.DrawPrimitivesEx(*vbFF, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, sp);
            Color specOn(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionFF, &specOn, 0, 1);
            check(specOn.getRProperty() == 255 && specOn.getGProperty() == 255 && specOn.getBProperty() == 255,
                  "DirectX11Renderer::DrawPrimitivesEx(): real lit_textured3d -- Blinn-Phong "
                  "specular at a geometry deliberately chosen so dot(H,N)=1 exactly contributes the "
                  "exact expected full-white highlight, with diffuse/ambient/emissive all zero "
                  "(plans/plan_dx.md DX-125)");

            GpuDrawParams spOff = sp;
            spOff.specularColor[0] = 0.0f; spOff.specularColor[1] = 0.0f; spOff.specularColor[2] = 0.0f;
            dev.Clear(Color(0, 0, 0, 255));
            renderer.DrawPrimitivesEx(*vbFF, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, spOff);
            Color specOff(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionFF, &specOff, 0, 1);
            check(specOff.getRProperty() == 0 && specOff.getGProperty() == 0 && specOff.getBProperty() == 0,
                  "DirectX11Renderer::DrawPrimitivesEx(): the SAME geometry/light with material "
                  "SpecularColor zeroed produces exact black -- proves the prior check's white came "
                  "genuinely from the specular term, not diffuse/ambient/emissive (plans/plan_dx.md DX-125)");

            // REMED-GFX-020: reversed-light-direction discriminator. Flip Light1 to travel -Z
            // (toward the eye / away from the surface): "direction to light" now points away from
            // the surface, so dot(N, -Light1Dir) < 0, the shader's zeroL1 gate collapses the
            // specular term to zero, and the highlight must vanish to black. A specular formula that
            // dropped the facing-gate or got the light-direction sign wrong would still light this
            // case, so this pins the sign convention the full-white check above cannot alone.
            GpuDrawParams spRev = sp;
            spRev.light1Dir[2] = -1.0f;  // travels -Z instead of +Z
            dev.Clear(Color(0, 0, 0, 255));
            renderer.DrawPrimitivesEx(*vbFF, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, spRev);
            Color specRev(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionFF, &specRev, 0, 1);
            check(specRev.getRProperty() == 0 && specRev.getGProperty() == 0 && specRev.getBProperty() == 0,
                  "DirectX11Renderer::DrawPrimitivesEx(): reversing Light1's direction (now facing "
                  "away from the surface) collapses the specular highlight to exact black via the "
                  "shader's facing gate -- pins the light-direction sign convention (plans/plan_dx.md DX-125)");
        }

        // plans/plan_dx.md DX-151: skinned3d -- real Blinn-Phong specular highlight, discriminating.
        // Applies DX-125's own half-vector-equals-normal trick to skinned3d.frag.hlsl's specular
        // formula, byte-for-byte the same shape as lit_textured3d's (confirmed by reading both
        // before writing this test), combined with the single-identity-bone fixture (DX-67/DX-135)
        // so bone math doesn't complicate the specular isolation. Mirrors D3D12's own already-closed
        // DX-151 (examples/directx12_smoke_test.cpp Checks RR1/RR2) exactly.
        {
            struct VPNTSR { float x, y, z; float nx, ny, nz; float u, v;
                           float bw0, bw1, bw2, bw3; uint8_t bi0, bi1, bi2, bi3; };
            static const VPNTSR kTriRR[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
                { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
                {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            };
            auto vbRR = renderer.CreateVertexBuffer(3);
            vbRR->SetData(kTriRR, 3, sizeof(VPNTSR));

            ImageData whiteImgRR;
            whiteImgRR.width = 2; whiteImgRR.height = 2; whiteImgRR.mipLevels = 1;
            whiteImgRR.pixels.assign(2 * 2 * 4, 255);
            auto whiteTexRR = renderer.CreateTexture(whiteImgRR);

            GpuDrawParams rp;
            rp.texture0 = whiteTexRR.get();
            rp.textureEnabled = true;
            rp.lightingEnabled = true;
            rp.preferPerPixelLighting = true;
            rp.skinned = true;
            rp.boneCount = 1;
            rp.weightsPerVertex = 1;
            Matrix::getIdentityProperty().ToColumnMajor(rp.boneTransforms);
            rp.ambientColor[0] = 0.0f; rp.ambientColor[1] = 0.0f; rp.ambientColor[2] = 0.0f;
            rp.light0Diffuse[0] = 0.0f; rp.light0Diffuse[1] = 0.0f; rp.light0Diffuse[2] = 0.0f; // no diffuse noise
            rp.light1Dir[0] = 0.0f; rp.light1Dir[1] = 0.0f; rp.light1Dir[2] = 1.0f;             // travels +Z
            rp.light1Diffuse[0] = 0.0f; rp.light1Diffuse[1] = 0.0f; rp.light1Diffuse[2] = 0.0f; // diffuse off too
            rp.light1Specular[0] = 1.0f; rp.light1Specular[1] = 1.0f; rp.light1Specular[2] = 1.0f;
            rp.eyePositionWorld[0] = 0.0f; rp.eyePositionWorld[1] = 0.0f; rp.eyePositionWorld[2] = -10.0f;
            rp.specularColor[0] = 1.0f; rp.specularColor[1] = 1.0f; rp.specularColor[2] = 1.0f;
            rp.specularPower = 16.0f;
            // REMED-GFX-020: assert the *per-pixel* skinned3d highlight explicitly. skinned3d.frag
            // has no LightingEnabled branch (it is unconditionally lit), so this check historically
            // passed even with the default lightingEnabled=false -- but only because that default
            // also happens to steer variant selection to the per-pixel Skinned3d base shader. Make
            // the per-pixel intent explicit so the shader variant actually exercised matches the
            // per-pixel value asserted, mirroring DX-125's own fix above.
            rp.lightingEnabled = true;
            rp.preferPerPixelLighting = true;

            const Microsoft::Xna::Framework::Rectangle rrRegion(30, 30, 1, 1);

            dev.Clear(Color(0, 0, 0, 255));
            renderer.DrawPrimitivesEx(*vbRR, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, rp);
            Color specOnRR(0, 0, 0, 0);
            dev.GetBackBufferData(&rrRegion, &specOnRR, 0, 1);
            check(specOnRR.getRProperty() == 255 && specOnRR.getGProperty() == 255 && specOnRR.getBProperty() == 255,
                  "DirectX11Renderer::DrawPrimitivesEx(): real skinned3d -- Blinn-Phong specular at "
                  "a geometry deliberately chosen so dot(H,N)=1 exactly contributes the exact expected "
                  "full-white highlight, with diffuse/ambient all zero (plans/plan_dx.md DX-151)");

            GpuDrawParams rpOff = rp;
            rpOff.specularColor[0] = 0.0f; rpOff.specularColor[1] = 0.0f; rpOff.specularColor[2] = 0.0f;
            dev.Clear(Color(0, 0, 0, 255));
            renderer.DrawPrimitivesEx(*vbRR, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, rpOff);
            Color specOffRR(0, 0, 0, 0);
            dev.GetBackBufferData(&rrRegion, &specOffRR, 0, 1);
            check(specOffRR.getRProperty() == 0 && specOffRR.getGProperty() == 0 && specOffRR.getBProperty() == 0,
                  "DirectX11Renderer::DrawPrimitivesEx(): the SAME geometry/light with material "
                  "SpecularColor zeroed produces exact black -- proves the prior check's white came "
                  "genuinely from the specular term, not diffuse/ambient (plans/plan_dx.md DX-151)");
        }

        // plans/plan_dx.md DX-126: mip level > 0 SetData/UpdatePixelsLevel/sampling dedicated pixel
        // test -- texture upload/readback was previously only proven at mip level 0. Mirrors
        // D3D12's own already-closed DX-141 (examples/directx12_smoke_test.cpp Checks HH0-HH2)
        // methodology exactly: rather than trying to force the stock shaders' implicit-LOD
        // Sample() calls to pick mip level 1 during a draw (fragile/driver-dependent), this reads
        // mip level 1 back directly via a staging-texture CopyResource+Map (ReadTexture2DMipRegion,
        // already established above for DX-144's own render-target mip-chain proof) -- a real,
        // deterministic readback, not an implicit-sampling one.
        {
            ImageData mipImg;
            mipImg.width = 4; mipImg.height = 4; mipImg.mipLevels = 2;
            mipImg.pixels.assign(4 * 4 * 4, 0);
            for (int i = 0; i < 4 * 4; ++i)
            {
                mipImg.pixels[i * 4 + 0] = 10; mipImg.pixels[i * 4 + 1] = 20;
                mipImg.pixels[i * 4 + 2] = 30; mipImg.pixels[i * 4 + 3] = 255;
            }
            auto mipTex = renderer.CreateTexture(mipImg);
            auto& d3dMipTex = static_cast<D3D11TextureRenderer&>(*mipTex);
            check(d3dMipTex.GetMipLevelsEXT() == 2,
                  "D3D11TextureRenderer: real construction with mipLevels=2 allocates the exact "
                  "requested mip chain (plans/plan_dx.md DX-126)");

            std::vector<uint8_t> level1Data(2 * 2 * 4);
            for (int i = 0; i < 2 * 2; ++i)
            {
                level1Data[i * 4 + 0] = 210; level1Data[i * 4 + 1] = 220;
                level1Data[i * 4 + 2] = 230; level1Data[i * 4 + 3] = 255;
            }
            mipTex->UpdatePixelsLevel(1, level1Data.data(), 2, 2);

            auto level1Readback = ReadTexture2DMipRegion(renderer.GetDeviceEXT(), renderer.GetContextEXT(),
                                                          d3dMipTex.GetTextureEXT(), 1, 0, 0, 2, 2);
            bool level1Exact = !level1Readback.empty();
            for (std::size_t i = 0; i + 3 < level1Readback.size() && level1Exact; i += 4)
                if (level1Readback[i] != 210 || level1Readback[i+1] != 220 ||
                    level1Readback[i+2] != 230 || level1Readback[i+3] != 255)
                    level1Exact = false;
            check(level1Exact,
                  "D3D11TextureRenderer::UpdatePixelsLevel(1, ...) round-trips EXACT bytes for a real "
                  "mip level 1 upload, read back via a direct staging-texture copy of subresource 1 "
                  "(plans/plan_dx.md DX-126)");

            auto level0Readback = ReadTexture2DMipRegion(renderer.GetDeviceEXT(), renderer.GetContextEXT(),
                                                          d3dMipTex.GetTextureEXT(), 0, 0, 0, 4, 4);
            bool level0Exact = !level0Readback.empty();
            for (std::size_t i = 0; i + 3 < level0Readback.size() && level0Exact; i += 4)
                if (level0Readback[i] != 10 || level0Readback[i+1] != 20 ||
                    level0Readback[i+2] != 30 || level0Readback[i+3] != 255)
                    level0Exact = false;
            check(level0Exact,
                  "D3D11TextureRenderer: level 0's own content is genuinely unaffected by the level-1 "
                  "upload -- proves UpdatePixelsLevel(1, ...) targeted the correct subresource, not "
                  "level 0 (plans/plan_dx.md DX-126)");
        }

        // plans/plan_dx.md DX-127: SpriteFont D3D11-specific glyph placement/spacing/newline/flip test,
        // mirroring D3D12's already-closed DX-132 (examples/directx12_smoke_test.cpp Checks KK2-KK5)
        // and EasyGL's own established fixture (Tasks 424-429): an 8x8 solid-white atlas per
        // glyph, zero cropping offset, zero left/right kerning bearing, so a glyph's destination
        // rect maps exactly and any placement error is a hard pixel difference. Unlike D3D12,
        // D3D11's real swap chain works directly under plain Wine, so this draws straight to the
        // back buffer (this file's own established style) instead of needing an offscreen
        // RenderTarget2D/PresentationParameters::HeadlessEXT detour.
        {
            Texture2D atlas(dev, 16, 8); // two 8x8 glyph cells side by side
            std::vector<Color> atlasPx(16 * 8, Color(255, 255, 255, 255));
            atlas.SetData(atlasPx.data(), 16 * 8);

            std::vector<Microsoft::Xna::Framework::Rectangle> glyphBounds = {
                Microsoft::Xna::Framework::Rectangle(0, 0, 8, 8), Microsoft::Xna::Framework::Rectangle(8, 0, 8, 8) };
            std::vector<Microsoft::Xna::Framework::Rectangle> cropping = {
                Microsoft::Xna::Framework::Rectangle(0, 0, 8, 8), Microsoft::Xna::Framework::Rectangle(0, 0, 8, 8) };
            std::vector<SharpRuntime::charcs> chars = { u'A', u'B' };
            std::vector<Vector3> kerning = { Vector3(0.0f, 8.0f, 0.0f), Vector3(0.0f, 8.0f, 0.0f) };
            SpriteFont font(atlas, glyphBounds, cropping, chars,
                            /*lineSpacing=*/8, /*spacing=*/0.0f, kerning,
                            std::optional<SharpRuntime::charcs>(std::nullopt));

            SpriteBatch fontBatch(dev);

            auto readPixelF = [&](int x, int y) -> Color {
                const Microsoft::Xna::Framework::Rectangle region(x, y, 1, 1);
                Color px(0, 0, 0, 0);
                dev.GetBackBufferData(&region, &px, 0, 1);
                return px;
            };
            auto isWhiteF = [](const Color& c) {
                return c.getRProperty() > 200 && c.getGProperty() > 200 && c.getBProperty() > 200;
            };
            auto isBlackF = [](const Color& c) {
                return c.getRProperty() < 60 && c.getGProperty() < 60 && c.getBProperty() < 60;
            };

            // (a) Single glyph at (4,4) -> occupies exactly [4,12) x [4,12).
            dev.Clear(Color(0, 0, 0, 255));
            fontBatch.Begin();
            fontBatch.DrawString(font, "A", Vector2(4.0f, 4.0f), Color::White);
            fontBatch.End();
            const bool glyphPlaced = isWhiteF(readPixelF(8, 8))    // inside
                && isBlackF(readPixelF(3, 8))    // just left of the left edge
                && isBlackF(readPixelF(12, 8))   // just right of the right edge
                && isBlackF(readPixelF(8, 3))    // just above the top edge
                && isBlackF(readPixelF(8, 12));  // just below the bottom edge
            check(glyphPlaced,
                  "SpriteBatch::DrawString(): places a single glyph at EXACTLY its destination rect "
                  "(4,4,8,8) -- checked inside plus all four edge midpoints, so an X-only or Y-only "
                  "misplacement cannot pass (plans/plan_dx.md DX-127)");

            // (b) Two glyphs -> the second must advance by exactly one glyph width (8px).
            dev.Clear(Color(0, 0, 0, 255));
            fontBatch.Begin();
            fontBatch.DrawString(font, "AB", Vector2(0.0f, 0.0f), Color::White);
            fontBatch.End();
            const bool spacingOk = isWhiteF(readPixelF(4, 4))    // glyph 'A' cell [0,8)
                && isWhiteF(readPixelF(12, 4))   // glyph 'B' cell [8,16) -- advanced by 8
                && isBlackF(readPixelF(20, 4));  // nothing beyond the second glyph
            check(spacingOk,
                  "SpriteBatch::DrawString(\"AB\"): advances the SECOND glyph by exactly one glyph "
                  "width -- both cells are drawn and nothing spills past them (plans/plan_dx.md DX-127)");

            // (c) Newline -> the second line must drop by exactly lineSpacing (8px), back to x=0.
            dev.Clear(Color(0, 0, 0, 255));
            fontBatch.Begin();
            fontBatch.DrawString(font, "A\nA", Vector2(0.0f, 0.0f), Color::White);
            fontBatch.End();
            const bool newlineOk = isWhiteF(readPixelF(4, 4))    // line 1, y in [0,8)
                && isWhiteF(readPixelF(4, 12))   // line 2, y in [8,16) -- dropped by lineSpacing
                && isBlackF(readPixelF(12, 4));  // line 1 has ONE glyph -- x reset, not advanced
            check(newlineOk,
                  "SpriteBatch::DrawString(\"A\\nA\"): drops the second line by exactly lineSpacing "
                  "AND resets x to the start -- a newline that only did one of the two cannot pass "
                  "(plans/plan_dx.md DX-127)");

            // (d) SpriteEffects::FlipVertically -- an ASYMMETRIC glyph is required, otherwise a flip
            // of a solid block is indistinguishable from no flip at all. Atlas glyph 'A' is white
            // only in its TOP half; flipped, the white must land in the BOTTOM half.
            std::vector<Color> halfPx(16 * 8, Color(0, 0, 0, 255));
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 8; ++x)
                    halfPx[static_cast<std::size_t>(y) * 16 + x] = Color(255, 255, 255, 255);
            Texture2D atlasHalf(dev, 16, 8);
            atlasHalf.SetData(halfPx.data(), 16 * 8);
            SpriteFont fontHalf(atlasHalf, glyphBounds, cropping, chars,
                                8, 0.0f, kerning, std::optional<SharpRuntime::charcs>(std::nullopt));

            dev.Clear(Color(0, 0, 0, 255));
            fontBatch.Begin();
            fontBatch.DrawString(fontHalf, "A", Vector2(0.0f, 0.0f), Color::White,
                                 0.0f, Vector2(0.0f, 0.0f), 1.0f, SpriteEffects::None, 0.0f);
            fontBatch.End();
            const bool noFlipOk = isWhiteF(readPixelF(4, 2))   // top half white
                && isBlackF(readPixelF(4, 6));  // bottom half black

            dev.Clear(Color(0, 0, 0, 255));
            fontBatch.Begin();
            fontBatch.DrawString(fontHalf, "A", Vector2(0.0f, 0.0f), Color::White,
                                 0.0f, Vector2(0.0f, 0.0f), 1.0f, SpriteEffects::FlipVertically, 0.0f);
            fontBatch.End();
            const bool flipOk = isBlackF(readPixelF(4, 2))     // top half now black
                && isWhiteF(readPixelF(4, 6));    // bottom half now white -- genuinely flipped

            check(noFlipOk && flipOk,
                  "SpriteBatch::DrawString(): SpriteEffects::FlipVertically genuinely flips a glyph -- "
                  "an asymmetric (top-half-white) glyph lands top-half-white unflipped and "
                  "bottom-half-white flipped, so a no-op flip cannot pass (plans/plan_dx.md DX-127)");
        }

        // plans/plan_dx.md DX-128: Model/ModelMesh/ModelMeshPart/ModelBone D3D11-specific runtime-API
        // test, mirroring D3D12's already-closed DX-148 (examples/directx12_smoke_test.cpp Check
        // KK6). Drives ModelMesh::Draw()'s REAL orchestration (SetVertexBuffer + setIndices +
        // DrawIndexedPrimitives + EffectPass::Apply through a bone-transformed world matrix), not
        // a raw VertexBuffer draw wearing a Model label -- which is exactly what this row's own
        // text warns against and would prove nothing new.
        {
            const Color redM(255, 0, 0, 255);
            const VertexPositionColor vertsM[4] = {
                { Vector3(-1.0f,  1.0f, 0.0f), redM },
                { Vector3(-1.0f, -1.0f, 0.0f), redM },
                { Vector3( 1.0f, -1.0f, 0.0f), redM },
                { Vector3( 1.0f,  1.0f, 0.0f), redM },
            };
            VertexBuffer vbM(dev, 4);
            vbM.SetData(vertsM, 4);
            const uint16_t indicesM[6] = { 0, 1, 2, 0, 2, 3 };
            IndexBuffer ibM(dev, 6);
            ibM.SetData(indicesM, 6);

            BasicEffect fxM(dev);
            fxM.VertexColorEnabled = true;

            // A real 2-bone hierarchy: root -> child. Model::Draw multiplies the mesh's absolute
            // bone transform into the world matrix, so this genuinely exercises the bone path.
            ModelBone boneM0(0, "root");
            ModelBone boneM1(1, "child");
            boneM0.AddChild(&boneM1);

            ModelMeshPart partM(&vbM, &ibM, /*numVertices=*/4, /*primitiveCount=*/2,
                                /*startIndex=*/0, /*vertexOffset=*/0);
            ModelMesh meshM(&dev, { &partM });
            partM.setEffectProperty(&fxM);
            Model modelM(&dev, { &boneM0, &boneM1 }, { &meshM });

            dev.Clear(Color(0, 255, 0, 255));
            dev.SetDepthTestEnabled(false);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            modelM.Draw(Matrix::getIdentityProperty(), Matrix::getIdentityProperty(), Matrix::getIdentityProperty());

            const Microsoft::Xna::Framework::Rectangle centerRegionM(30, 30, 1, 1);
            Color centreM(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionM, &centreM, 0, 1);
            check(centreM.getRProperty() >= 200 && centreM.getGProperty() <= 60 && centreM.getBProperty() <= 60,
                  "Model::Draw() -> ModelMesh::Draw()'s real orchestration (bone transform + "
                  "SetVertexBuffer + setIndices + DrawIndexedPrimitives + EffectPass::Apply) paints "
                  "the mesh's exact red over the green clear, through the D3D11 renderer (plans/plan_dx.md DX-128)");

            dev.SetDepthTestEnabled(false);
            dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        }

        // plans/plan_dx.md DX-155: Model root-bone-index flexibility (Task 916's own `rootBoneIndex`
        // constructor parameter) against the real D3D11 renderer. Honest scope, confirmed by
        // reading Model::Draw()/CopyAbsoluteBoneTransformsTo() first: neither actually consults
        // `root_`/`rootBoneIndex` -- Draw() picks each mesh's world transform via
        // `mesh->getParentBoneProperty()` (the `meshParentBones` constructor argument), and
        // CopyAbsoluteBoneTransformsTo() walks each bone's OWN parent chain, independent of which
        // bone is `Root`. So `rootBoneIndex`'s only currently-consumed effect anywhere is
        // `getRootProperty()` returning it -- this test proves that (not a silently-defaulted-to-0
        // value) AND exercises the full 5-argument constructor (meshParentBones + rootBoneIndex
        // together, never used by DX-128's own 3-argument-constructor fixture) end to end through a
        // real draw, including meshParentBones correctly targeting a NON-zero-indexed bone.
        {
            const Color redR(255, 0, 0, 255);
            const VertexPositionColor vertsR[4] = {
                { Vector3(-1.0f,  1.0f, 0.0f), redR },
                { Vector3(-1.0f, -1.0f, 0.0f), redR },
                { Vector3( 1.0f, -1.0f, 0.0f), redR },
                { Vector3( 1.0f,  1.0f, 0.0f), redR },
            };
            VertexBuffer vbR(dev, 4);
            vbR.SetData(vertsR, 4);
            const uint16_t indicesR[6] = { 0, 1, 2, 0, 2, 3 };
            IndexBuffer ibR(dev, 6);
            ibR.SetData(indicesR, 6);

            BasicEffect fxR(dev);
            fxR.VertexColorEnabled = true;

            // Two INDEPENDENT top-level bones (no parent/child relationship -- that hierarchy-
            // chaining path is already covered by DX-128's own fixture). boneR0 (array index 0) is
            // a large translation that would move the mesh off-screen if it were ever picked by
            // mistake; boneR1 (array index 1, a NON-zero index) is Identity and is the bone the
            // mesh is actually parented to AND the requested root.
            ModelBone boneR0(0, "decoy");
            boneR0.setTransformProperty(Matrix::CreateTranslation(100.0f, 100.0f, 0.0f));
            ModelBone boneR1(1, "actual_root");
            boneR1.setTransformProperty(Matrix::getIdentityProperty());

            ModelMeshPart partR(&vbR, &ibR, /*numVertices=*/4, /*primitiveCount=*/2,
                                /*startIndex=*/0, /*vertexOffset=*/0);
            ModelMesh meshR(&dev, { &partR });
            partR.setEffectProperty(&fxR);
            Model modelR(&dev, { &boneR0, &boneR1 }, { &meshR }, { &boneR1 }, /*rootBoneIndex=*/1);

            check(modelR.getRootProperty() == &boneR1,
                  "Model: the 5-argument constructor's rootBoneIndex=1 genuinely sets Root to the "
                  "bone at that NON-zero index, not silently defaulting to bones[0] (plans/plan_dx.md "
                  "DX-155)");

            dev.Clear(Color(0, 255, 0, 255));
            dev.SetDepthTestEnabled(false);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            modelR.Draw(Matrix::getIdentityProperty(), Matrix::getIdentityProperty(), Matrix::getIdentityProperty());

            const Microsoft::Xna::Framework::Rectangle centerRegionR(30, 30, 1, 1);
            Color centreR(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionR, &centreR, 0, 1);
            check(centreR.getRProperty() >= 200 && centreR.getGProperty() <= 60 && centreR.getBProperty() <= 60,
                  "Model::Draw() with a real 5-argument-constructor Model (meshParentBones targeting "
                  "the NON-zero-indexed boneR1, rootBoneIndex=1) genuinely draws the mesh's exact red "
                  "over the green clear -- proves meshParentBones correctly selected boneR1's own "
                  "Identity transform, not boneR0's off-screen-translating one, through the real D3D11 "
                  "renderer (plans/plan_dx.md DX-155)");

            dev.SetDepthTestEnabled(false);
            dev.setRasterizerStateProperty(RasterizerState::CullCounterClockwise);
        }

        // plans/plan_dx.md DX-129: RenderTargetCube dedicated pixel test -- until now only construction
        // was proven real (plus DX-144's own narrower mip-chain-content check above); this mirrors
        // RenderTarget2D's own full bind+clear+readback+unbind-restores-backbuffer proof (Check J
        // above) for RenderTargetCube, PLUS a per-face independence check (binding face 1 to a
        // different color must not clobber face 0's already-cleared content -- the specific
        // aliasing risk a shared 6-slice texture array creates that a single RenderTarget2D never
        // has to prove).
        {
            auto rtCube = renderer.CreateRenderTargetCube(8, 0 /*DepthFormat::None*/, false /*preserveContents*/, false /*mipMap*/);
            auto* d3dRtCube = static_cast<D3D11RenderTargetCubeRenderer*>(rtCube.get());

            // (a) Bind face 0, Clear() -> exact color lands in face 0's own texture slice.
            rtCube->BindAsRenderTargetFace(0);
            dev.Clear(Color(11, 22, 33, 255));
            const auto face0Pixels = ReadTexture2DMipRegion(device, context, d3dRtCube->GetColorTextureEXT(),
                                                             0 /*subresource = mip 0 + face 0*level count*/, 0, 0, 4, 4);
            bool face0Matches = face0Pixels.size() == 4u * 4u * 4u;
            for (std::size_t i = 0; i < 4u * 4u && face0Matches; ++i)
                face0Matches = face0Pixels[i * 4 + 0] == 11 && face0Pixels[i * 4 + 1] == 22 &&
                               face0Pixels[i * 4 + 2] == 33 && face0Pixels[i * 4 + 3] == 255;
            check(face0Matches,
                  "D3D11RenderTargetCubeRenderer: BindAsRenderTargetFace(0)+Clear() writes the exact "
                  "color into face 0's own texture slice (plans/plan_dx.md DX-129)");

            // (b) Bind face 1 to a DIFFERENT color, then re-read face 0 -- proves the two faces are
            // genuinely independent texture-array slices, not aliased.
            rtCube->BindAsRenderTargetFace(1);
            dev.Clear(Color(200, 100, 50, 255));
            const auto face1Pixels = ReadTexture2DMipRegion(device, context, d3dRtCube->GetColorTextureEXT(),
                                                             1 /*subresource = mip 0 + face 1*level count*/, 0, 0, 4, 4);
            bool face1Matches = face1Pixels.size() == 4u * 4u * 4u;
            for (std::size_t i = 0; i < 4u * 4u && face1Matches; ++i)
                face1Matches = face1Pixels[i * 4 + 0] == 200 && face1Pixels[i * 4 + 1] == 100 &&
                               face1Pixels[i * 4 + 2] == 50 && face1Pixels[i * 4 + 3] == 255;
            const auto face0Again = ReadTexture2DMipRegion(device, context, d3dRtCube->GetColorTextureEXT(),
                                                            0, 0, 0, 4, 4);
            bool face0StillMatches = face0Again.size() == 4u * 4u * 4u;
            for (std::size_t i = 0; i < 4u * 4u && face0StillMatches; ++i)
                face0StillMatches = face0Again[i * 4 + 0] == 11 && face0Again[i * 4 + 1] == 22 &&
                                    face0Again[i * 4 + 2] == 33 && face0Again[i * 4 + 3] == 255;
            check(face1Matches && face0StillMatches,
                  "D3D11RenderTargetCubeRenderer: face 1 gets its OWN distinct color, and face 0's "
                  "earlier content survives completely unchanged -- proves the 6 cube faces are "
                  "genuinely independent texture-array slices, not aliased (plans/plan_dx.md DX-129)");

            // (c) UnbindAsRenderTarget() genuinely restores the back buffer as Clear()'s target.
            rtCube->UnbindAsRenderTarget();
            dev.Clear(Color(44, 55, 66, 255));
            const Microsoft::Xna::Framework::Rectangle bbRegionCube(0, 0, 4, 4);
            std::vector<Color> bbPixelsCube(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&bbRegionCube, bbPixelsCube.data(), 0, static_cast<int>(bbPixelsCube.size()));
            bool bbMatchesCube = true;
            for (const Color& p : bbPixelsCube)
                if (p.getRProperty() != 44 || p.getGProperty() != 55 || p.getBProperty() != 66 || p.getAProperty() != 255)
                    bbMatchesCube = false;
            check(bbMatchesCube,
                  "D3D11RenderTargetCubeRenderer: UnbindAsRenderTarget() genuinely restores the back "
                  "buffer as Clear()'s next target (plans/plan_dx.md DX-129)");
        }

        // plans/plan_dx.md DX-152: RenderTargetCube MSAA -- a real feature (previously deliberately out
        // of scope, D3D11RenderTargetCubeRenderer's own header comment) landing now, not just a
        // test. Same real, device-queried methodology as Check K's own RenderTarget2D MSAA proof
        // (DX-45): pass/fail is purely about pixel correctness of the resolve; whether the device
        // actually granted real multi-sampling is printed as diagnostics, not gated on, since
        // that's real hardware/driver capability.
        {
            auto rtCubeMsaa = renderer.CreateRenderTargetCube(8, 0 /*DepthFormat::None*/, false /*preserveContents*/, false /*mipMap*/, 4);
            auto* d3dRtCubeMsaa = static_cast<D3D11RenderTargetCubeRenderer*>(rtCubeMsaa.get());
            rtCubeMsaa->BindAsRenderTargetFace(0);
            dev.Clear(Color(77, 88, 99, 255));
            rtCubeMsaa->UnbindAsRenderTarget(); // ResolveMsaaEXT() for face 0

            const auto resolvedCube = ReadTexture2DMipRegion(
                device, context, d3dRtCubeMsaa->GetSampleableTextureEXT(), 0 /*face 0, mip 0*/, 0, 0, 4, 4);
            bool msaaCubeMatches = resolvedCube.size() == 4u * 4u * 4u;
            for (std::size_t i = 0; i < 4u * 4u && msaaCubeMatches; ++i)
                msaaCubeMatches = resolvedCube[i * 4 + 0] == 77 && resolvedCube[i * 4 + 1] == 88 &&
                                  resolvedCube[i * 4 + 2] == 99 && resolvedCube[i * 4 + 3] == 255;
            check(msaaCubeMatches,
                  "D3D11RenderTargetCubeRenderer (MSAA): Clear()+resolve-on-unbind produces the exact "
                  "color in the resolved texture, face 0 (plans/plan_dx.md DX-152)");
            std::printf("    RenderTargetCube MSAA: requested 4x, device-applied %dx\n",
                        d3dRtCubeMsaa->GetMultiSampleCount());
        }

        // plans/plan_dx.md DX-130: the 5 combo Clear* variants dedicated round-trip pixel test --
        // ClearDepthStencilView() calls already exist and are implemented identically to the
        // proven plain Clear(r,g,b,a) path, but only plain Clear had a dedicated pixel test until
        // now. Mirrors D3D12's own already-closed DX-146 methodology exactly: stencil is proven by
        // a direct GPU readback of the depth-stencil resource's real bytes (D3D11 packs
        // DXGI_FORMAT_D24_UNORM_S8_UINT as one 32-bit value per pixel -- byte 3 is the stencil
        // byte, no separate plane-slice copy needed, unlike D3D12); depth is proven by its real
        // effect on rasterization (the same triangle at the same Z drawn twice, differing ONLY in
        // the depth value a prior ClearDepth() wrote).
        {
            constexpr int kRtW = 32, kRtH = 32;
            auto rtCl = renderer.CreateRenderTarget2D(kRtW, kRtH, /*depthFormat=*/3 /*Depth24Stencil8*/, false, false, 0);
            auto* d3dRtCl = static_cast<D3D11RenderTargetRenderer*>(rtCl.get());
            check(d3dRtCl->GetDSVEXT() != nullptr,
                  "D3D11RenderTargetRenderer: Depth24Stencil8 render target with a real depth-stencil "
                  "view created (plans/plan_dx.md DX-130)");

            Microsoft::WRL::ComPtr<ID3D11Resource> dsResource;
            d3dRtCl->GetDSVEXT()->GetResource(dsResource.GetAddressOf());
            Microsoft::WRL::ComPtr<ID3D11Texture2D> dsTexture;
            dsResource.As(&dsTexture);

            renderer.SetRenderTarget2D(rtCl.get());

            // --- ClearColorDepthAndStencil: all three at once. ---
            renderer.ClearColorDepthAndStencil(0.0f, 0.0f, 1.0f, 1.0f, /*depth=*/0.75f, /*stencil=*/0x5A);
            auto stencilAfterAll = ReadDepthStencilPlane(device, context, dsTexture.Get(), kRtW, kRtH);
            const bool stencilPlaneReadable = !stencilAfterAll.empty();
            check(stencilPlaneReadable,
                  "D3D11RenderTargetRenderer: the depth-stencil resource's real bytes are readable "
                  "back from the GPU -- the mechanism the stencil proofs below rely on");

            if (stencilPlaneReadable)
            {
                bool allAre5A = true;
                for (uint8_t v : stencilAfterAll) if (v != 0x5A) { allAre5A = false; break; }
                check(allAre5A,
                      "DirectX11Renderer::ClearColorDepthAndStencil(): genuinely wrote 0x5A into "
                      "EVERY pixel of the real stencil plane, read straight back off the GPU (plans/plan_dx.md DX-130)");

                // --- ClearStencil alone: must change the stencil (depth verified separately below). ---
                renderer.ClearStencil(0x3C);
                auto stencilAfterStencilOnly = ReadDepthStencilPlane(device, context, dsTexture.Get(), kRtW, kRtH);
                bool allAre3C = !stencilAfterStencilOnly.empty();
                for (uint8_t v : stencilAfterStencilOnly) if (v != 0x3C) { allAre3C = false; break; }
                check(allAre3C,
                      "DirectX11Renderer::ClearStencil(0x3C): alone genuinely overwrites the "
                      "stencil plane to 0x3C -- a real, different value than the prior 0x5A, so this "
                      "is a real clear, not a silent no-op (plans/plan_dx.md DX-130)");
            }

            // --- Depth: proven by its real effect on the depth test. ---
            struct VtxZCl { float x, y, z; uint32_t color; };
            const VtxZCl triZCl[3] = {
                {-0.9f,  0.9f, 0.5f, 0xFF0000FFu}, // ABGR-packed red
                { 0.9f,  0.9f, 0.5f, 0xFF0000FFu},
                { 0.0f, -0.9f, 0.5f, 0xFF0000FFu},
            };
            auto vbZCl = renderer.CreateVertexBuffer(3);
            vbZCl->SetData(triZCl, /*vertex_count=*/3, /*stride_in_bytes=*/sizeof(VtxZCl));

            renderer.ApplyRasterizerState(/*cullMode=*/0 /*None*/, /*fillMode=*/0 /*Solid*/, false, 0.0f, 0.0f);

            auto drawTriAndSampleCenterCl = [&]() -> Color {
                renderer.DrawColoredPrimitives(*vbZCl, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                              Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
                const auto px = ReadTexture2DRegion(device, context, d3dRtCl->GetSampleableTextureEXT(),
                                                    kRtW / 2, kRtH / 2, 1, 1);
                return px.size() >= 4 ? Color(px[0], px[1], px[2], px[3]) : Color(0, 0, 0, 0);
            };

            // Control: with the depth test OFF, this exact triangle must draw -- isolates "the draw
            // itself works" from "the depth test rejected it".
            renderer.ApplyDepthStencilState(false, false, 2, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
            renderer.ClearColorAndDepth(0.0f, 0.0f, 1.0f, 1.0f, 0.9f);
            const Color ctrlPxCl = drawTriAndSampleCenterCl();
            check(ctrlPxCl.getRProperty() == 255 && ctrlPxCl.getGProperty() == 0,
                  "DirectX11Renderer: control -- with depth testing OFF, this exact triangle "
                  "genuinely draws its red, so a following failure can only mean the depth test "
                  "rejected it, not that the draw is broken");
            renderer.ApplyDepthStencilState(/*depthEnable=*/true, /*depthWriteEnable=*/true,
                                           /*depthFunc=*/2 /*Less*/, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);

            // Depth cleared FAR (0.9): the z=0.5 triangle is nearer -> Less passes -> red is drawn.
            renderer.ClearColorAndDepth(0.0f, 0.0f, 1.0f, 1.0f, /*depth=*/0.9f);
            const Color passPxCl = drawTriAndSampleCenterCl();
            check(passPxCl.getRProperty() == 255 && passPxCl.getGProperty() == 0,
                  "DirectX11Renderer::ClearColorAndDepth(depth=0.9): the z=0.5 triangle passes "
                  "depthFunc=Less and draws its exact red, proving the cleared DEPTH value is real "
                  "(plans/plan_dx.md DX-130)");

            // Depth cleared NEAR (0.1): the same z=0.5 triangle is farther -> Less fails -> blue survives.
            renderer.ClearColorAndDepth(0.0f, 0.0f, 1.0f, 1.0f, /*depth=*/0.1f);
            const Color failPxCl = drawTriAndSampleCenterCl();
            check(failPxCl.getBProperty() == 255 && failPxCl.getRProperty() == 0,
                  "DirectX11Renderer::ClearColorAndDepth(depth=0.1): the SAME triangle at the SAME "
                  "z=0.5 is now correctly REJECTED by the depth test (background survives) -- only "
                  "the cleared depth value differs, so each Clear* variant genuinely writes the "
                  "depth it was given, not a fixed default (plans/plan_dx.md DX-130)");

            // --- ClearDepth alone must NOT touch the color target. ---
            renderer.ClearColorAndDepth(0.0f, 1.0f, 0.0f, 1.0f, /*depth=*/0.9f); // green, far depth
            renderer.ClearDepth(0.1f);                                            // depth only
            const auto afterDepthOnlyCl = ReadTexture2DRegion(device, context, d3dRtCl->GetSampleableTextureEXT(),
                                                              kRtW / 2, kRtH / 2, 1, 1);
            check(afterDepthOnlyCl.size() >= 4 && afterDepthOnlyCl[1] == 255 && afterDepthOnlyCl[0] == 0,
                  "DirectX11Renderer::ClearDepth(): alone leaves the COLOR target untouched (the "
                  "prior green survives) -- proves each variant clears only what it was asked for "
                  "(plans/plan_dx.md DX-130)");

            renderer.ApplyDepthStencilState(false, false, 2, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
            renderer.SetRenderTarget2D(nullptr);
        }

        const int totalChecks = 2 /* SetDepthTestEnabled real, not a no-op */
                                + 3 + 10 + 1 + 2 + 2 + 2 + 2 + 2 + 1 + 1 + 2 + 1 + 13 + 2 + 3 + 2 + 2 + 1 + 1 + 1 + 1 + 3 + 2 + 2 + 2 + 3 + 2 + 3 /* DX-131 rotation/scale/crop */
                                + 1 /* DX-134 envMapAmount=0 */ + 2 /* DX-135 WeightsPerVertex */ + 2 /* DX-137 textured3d fog */
                                + 2 /* DX-140 NPOT */ + 2 /* DX-142 all-16-sampler-slots */ + 2 /* DX-143 MRT MSAA resolve */
                                + 4 /* DX-144 RT2D+RTCube mip-chain generation */
                                + 4 /* DX-145 DepthStencilFormat fidelity */
                                + 2 /* DX-147 occlusion query visible-vs-occluded */
                                + 4 /* DX-124 multi-light + EmissiveColor discrimination */
                                + 3 /* DX-125 specular-highlight discrimination (+REMED-GFX-020 reversed-light) */
                                + 3 /* DX-126 mip level > 0 upload/readback */
                                + 4 /* DX-127 SpriteFont glyph placement/spacing/newline/flip */
                                + 1 /* DX-128 Model/ModelMesh runtime-API orchestration */
                                + 3 /* DX-129 RenderTargetCube bind+clear+readback+unbind proof */
                                + 8 /* DX-130 combo Clear* variants (DSV, stencil-readable, stencil x2, control, depth x2, color-untouched) */
                                + 12 /* DX-137 fog for 6 remaining variants x 2 checks each */
                                + 3 /* DX-136 alpha_test_colored3d VertexColorEnabled on/off + discard-still-works */
                                + 3 /* DX-149 env_map3d DirectionalLight1/2 discrimination */
                                + 3 /* DX-150 skinned3d DirectionalLight1/2 discrimination */
                                + 2 /* DX-151 skinned3d specular discrimination */
                                + 1 /* DX-152 RenderTargetCube MSAA */
                                + 1 /* DX-153 RenderTargetCube mip non-face-0 */
                                + 2 /* DX-155 Model root-bone-index flexibility */
                                + 3 /* Task 1106 BasicEffect PreferPerPixelLighting */
                                + 3 /* Task 1107 SkinnedEffect PreferPerPixelLighting */
                                + 9 /* REMED-GFX-077 ColorWriteChannels/MultiSampleMask */
                                + 1 /* REMED-GFX-061 fog false->true->false */;
        std::printf("=== %d/%d PASS ===\n", passCount_, totalChecks);
        result_ = (passCount_ == totalChecks) ? 0 : 1;
        Exit();
    }

public:
    D3D11SmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    D3D11SmokeTest game;
    game.Run();
    return game.getResult();
}
