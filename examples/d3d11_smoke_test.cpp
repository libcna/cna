// SPDX-License-Identifier: MS-PL
// plan_dx.md Phase DX4/DX80: smoke test for the D3D11 graphics backend's device/swap-chain/
// back-buffer foundation. Real window, real DXGI swap chain, real Clear()+Present()+readback --
// this is the backend's first genuine pixel-correctness proof (DX-28's own bar).
//
// Check A -- real device created with feature level >= 11_0 (design decision 12's floor).
// Check B -- Clear(r,g,b,a) followed by GetBackBufferData() reads back the EXACT clear color.
// Check C -- a second, different Clear()+readback also matches (not a stale/cached value).
// Check D (DX-15-embed) -- D3DShaderCache creates a real ID3D11VertexShader+ID3D11PixelShader,
//   through this same real device, for each of DX-13-hlsl's 10 stock shader variants.
// Check E (DX-30) -- a real D3D11VertexBufferBackend round-trips known VertexPositionColor data
//   through Map(WRITE_DISCARD)/CopyResource-to-staging/Map(READ) -- a genuine GPU write+readback,
//   not just "SetData() didn't throw".
// Check F (DX-31) -- same round-trip proof for both a 16-bit and a 32-bit D3D11IndexBufferBackend.
// Check G (DX-32) -- D3D11InputLayoutCache actually calls CreateInputLayout() against a real
//   vertex shader's DXBC input signature for a couple of DX-16-vtx's established strides, and
//   caching returns the identical object on a second request for the same (variant, stride).
// Check H (DX-40) -- a real D3D11TextureBackend round-trips known RGBA8 pixel data: constructed
//   from an ImageData, then read back via a staging-texture copy and compared byte-for-byte.
// Check I (DX-41/DX-42) -- D3D11TextureCubeBackend/D3D11Texture3DBackend SetData()+GetData() also
//   round-trip exact bytes for a sub-region of one cube face / one 3D slice.
// Check J (DX-43) -- a real offscreen D3D11RenderTargetBackend: BindAsRenderTarget(), Clear() (now
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
//   (Phase DX8) is proven exactly this far, honestly, and no further.
// Check O (DX-50/DX-51/DX-52/DX-53) -- D3D11BlendStateCache/D3D11DepthStencilStateCache/
//   D3D11RasterizerStateCache each create a real state object, cache it (identical XNA params ->
//   identical object, different params -> different object), and ApplyBlendState()/
//   ApplyDepthStencilState()/ApplyRasterizerState() genuinely bind it (confirmed via
//   OMGetBlendState()/OMGetDepthStencilState()/RSGetState()) -- plus SetBlendFactor()/
//   SetReferenceStencil()'s standalone re-bind (Task 870/319 parity) and SetViewport()/
//   SetScissorRect()'s direct RSSetViewports()/RSSetScissorRects() round-trip. Behavioral pixel
//   correctness of blend/stencil *output* needs an actual draw call (Phase DX8), proven exactly
//   this far, honestly, and no further -- same bar Check N already set for MRT.
// Check P (DX-60/DX-60a/DX-61) -- the first real 3D triangle this backend has ever drawn: a real
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
// Check Y (DX-70) -- real, end-to-end D3D11SpriteBatchBackend via the actual public
//   Microsoft::Xna::Framework::Graphics::SpriteBatch/Texture2D classes (not the raw backend
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
//   sprite2d pipeline, proving D3D11EffectBackend::SetViewportSizeEXT()'s automatic vpSize slot
//   and the shared texture-binding path both work for the custom-effect draw path for real.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11Buffers.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11Textures.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11RenderTargets.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11SamplerCache.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11OcclusionQuery.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11StateObjectCache.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11EffectBackend.hpp"
#include "CNA/Internal/Backends/D3DCommon/D3DShaderCache.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::D3D11;
using CNA::Internal::Backends::ImageData;
using CNA::Internal::Backends::IRenderTargetBackend;
using CNA::Internal::Backends::GpuDrawParams;
using CNA::Internal::Backends::D3DCommon::D3DShaderVariant;
using CNA::Internal::Backends::D3DCommon::CreateVertexShaderForVariant;
using CNA::Internal::Backends::D3DCommon::CreatePixelShaderForVariant;

namespace
{
    /// Copies an ID3D11Buffer to a CPU-readable staging buffer and returns its bytes -- the same
    /// CopyResource+Map(READ) technique D3D11GraphicsBackend::ReadBackbuffer() already uses for
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
    /// D3D11GraphicsBackend::ReadBackbuffer() uses for the swap chain's own back buffer --
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

    /// Reads a w*h RGBA8 region starting at (x,y) out of an arbitrary subresource index (mip level,
    /// or mip+face for a texture array) of an arbitrary ID3D11Texture2D -- same staging-texture
    /// technique as ReadTexture2DRegion() above, but Map()'d at `subresource` instead of always 0
    /// (NOXNA, test-only -- DX-144's real mip-chain-content proof needs to read back mip levels > 0
    /// directly, since there is no public sampling path in this raw-backend-level smoke test).
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
        auto& backend = static_cast<D3D11GraphicsBackend&>(dev.GetBackend());

        // Check A: real feature level negotiated, meeting design decision 12's 11_0 floor.
        check(backend.GetFeatureLevelEXT() >= D3D_FEATURE_LEVEL_11_0,
              "device negotiated feature level 11_0 or higher");
        std::printf("    feature level = 0x%04x, debug layer = %s, tearing = %s\n",
                    static_cast<unsigned>(backend.GetFeatureLevelEXT()),
                    backend.IsDebugLayerEnabledEXT() ? "enabled" : "disabled",
                    backend.IsTearingCapableEXT() ? "capable" : "not capable");

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

            ID3D11Device* device = backend.GetDeviceEXT();
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

        ID3D11Device* device = backend.GetDeviceEXT();
        ID3D11DeviceContext* context = backend.GetContextEXT();

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
            auto vb = backend.CreateVertexBuffer(4);
            vb->SetData(kVerts, 4, sizeof(VPC));
            auto* d3dVb = static_cast<D3D11VertexBufferBackend*>(vb.get());
            const auto readBack = ReadBufferBytes(device, context, d3dVb->GetBufferEXT(),
                                                   static_cast<UINT>(4 * sizeof(VPC)));
            const bool matches = readBack.size() == sizeof(kVerts) &&
                                 std::memcmp(readBack.data(), kVerts, sizeof(kVerts)) == 0;
            check(vb->GetVertexCount() == 4 && matches,
                  "D3D11VertexBufferBackend: SetData() round-trips exact VertexPositionColor bytes");
        }

        // Check F (DX-31): both 16-bit and 32-bit index buffers round-trip the same way.
        {
            static const uint16_t kIdx16[6] = {0, 1, 2, 2, 3, 0};
            auto ib16 = backend.CreateIndexBuffer16(6);
            ib16->SetData16(kIdx16, 6);
            auto* d3dIb16 = static_cast<D3D11IndexBufferBackend*>(ib16.get());
            const auto readBack16 = ReadBufferBytes(device, context, d3dIb16->GetBufferEXT(),
                                                     static_cast<UINT>(sizeof(kIdx16)));
            const bool matches16 = readBack16.size() == sizeof(kIdx16) &&
                                   std::memcmp(readBack16.data(), kIdx16, sizeof(kIdx16)) == 0;
            check(!ib16->IsThirtyTwoBit() && d3dIb16->GetFormatEXT() == DXGI_FORMAT_R16_UINT &&
                      ib16->GetIndexCount() == 6 && matches16,
                  "D3D11IndexBufferBackend (16-bit): SetData16() round-trips exact index bytes");

            static const uint32_t kIdx32[6] = {0, 1, 2, 2, 3, 0};
            auto ib32 = backend.CreateIndexBuffer32(6);
            ib32->SetData32(kIdx32, 6);
            auto* d3dIb32 = static_cast<D3D11IndexBufferBackend*>(ib32.get());
            const auto readBack32 = ReadBufferBytes(device, context, d3dIb32->GetBufferEXT(),
                                                     static_cast<UINT>(sizeof(kIdx32)));
            const bool matches32 = readBack32.size() == sizeof(kIdx32) &&
                                   std::memcmp(readBack32.data(), kIdx32, sizeof(kIdx32)) == 0;
            check(ib32->IsThirtyTwoBit() && d3dIb32->GetFormatEXT() == DXGI_FORMAT_R32_UINT &&
                      ib32->GetIndexCount() == 6 && matches32,
                  "D3D11IndexBufferBackend (32-bit): SetData32() round-trips exact index bytes");
        }

        // Check G (DX-32): CreateInputLayout() actually succeeds against a real vertex shader's
        // DXBC input signature for two of DX-16-vtx's established strides, and the cache returns
        // the identical object (not just an equal-looking one) on a repeat request.
        {
            auto& cache = backend.GetInputLayoutCacheEXT();
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
            auto tex = backend.CreateTexture(img);
            auto* d3dTex = static_cast<D3D11TextureBackend*>(tex.get());
            const auto readBack = ReadTexture2DRegion(device, context, d3dTex->GetTextureEXT(), 0, 0, 4, 4);
            check(tex->GetWidth() == 4 && tex->GetHeight() == 4 && readBack == img.pixels,
                  "D3D11TextureBackend: constructor upload round-trips exact RGBA8 pixel bytes");

            std::vector<uint8_t> newPixels(4 * 4 * 4);
            for (int i = 0; i < 4 * 4; ++i)
            {
                newPixels[i * 4 + 0] = 99; newPixels[i * 4 + 1] = 88;
                newPixels[i * 4 + 2] = 77; newPixels[i * 4 + 3] = 255;
            }
            tex->UpdatePixelsLevel(0, newPixels.data(), 4, 4);
            const auto readBack2 = ReadTexture2DRegion(device, context, d3dTex->GetTextureEXT(), 0, 0, 4, 4);
            check(readBack2 == newPixels,
                  "D3D11TextureBackend: UpdatePixelsLevel() round-trips exact replacement bytes");
        }

        // Check I (DX-41/DX-42): cube-map and 3D texture SetData()/GetData() round-trip exact
        // bytes for a sub-region -- one face + a sub-volume, not just level 0's full extent.
        {
            auto cube = backend.CreateTextureCube(8, false, 0);
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
                  "D3D11TextureCubeBackend: SetData()/GetData() round-trip exact bytes for one face's sub-region");

            auto vol = backend.CreateTexture3D(4, 4, 4, false, 0);
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
                  "D3D11Texture3DBackend: SetData()/GetData() round-trip exact bytes for a sub-volume");
        }

        // Check J (DX-43): BindAsRenderTarget()+Clear() (now routed to whatever's bound, not the
        // hardcoded back buffer) writes the exact color into the render target's own texture, and
        // UnbindAsRenderTarget() genuinely restores the back buffer as Clear()'s next target.
        {
            auto rt = backend.CreateRenderTarget2D(8, 8, 0 /*DepthFormat::None*/, false, false, 0);
            auto* d3dRt = static_cast<D3D11RenderTargetBackend*>(rt.get());
            backend.SetRenderTarget2D(rt.get());
            dev.Clear(Color(11, 22, 33, 255));
            backend.SetRenderTarget2D(nullptr);

            const auto rtPixels = ReadTexture2DRegion(device, context, d3dRt->GetSampleableTextureEXT(), 0, 0, 4, 4);
            bool rtMatches = true;
            for (int i = 0; i < 4 * 4 && rtMatches; ++i)
            {
                rtMatches = rtPixels[i * 4 + 0] == 11 && rtPixels[i * 4 + 1] == 22 &&
                           rtPixels[i * 4 + 2] == 33 && rtPixels[i * 4 + 3] == 255;
            }
            check(rtMatches,
                  "D3D11RenderTargetBackend: BindAsRenderTarget()+Clear() writes the exact color into the RT's own texture");

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
                  "D3D11RenderTargetBackend: UnbindAsRenderTarget() genuinely restores the back buffer as Clear()'s target");
        }

        // Check K (DX-45): an MSAA render target's ResolveSubresource()-on-unbind produces the
        // exact clear color in the resolved, sampleable texture. Pass/fail is purely about pixel
        // correctness -- whether the device actually granted real multi-sampling (vs. this
        // backend's own real, device-queried fallback to single-sample) is printed as
        // diagnostics, not gated on, since that's real hardware/driver capability, not a bug here.
        {
            auto rtMsaa = backend.CreateRenderTarget2D(8, 8, 0, false, false, 4);
            auto* d3dRtMsaa = static_cast<D3D11RenderTargetBackend*>(rtMsaa.get());
            backend.SetRenderTarget2D(rtMsaa.get());
            dev.Clear(Color(77, 88, 99, 255));
            backend.SetRenderTarget2D(nullptr);

            const auto resolved = ReadTexture2DRegion(
                device, context, d3dRtMsaa->GetSampleableTextureEXT(), 0, 0, 4, 4);
            bool msaaMatches = true;
            for (int i = 0; i < 4 * 4 && msaaMatches; ++i)
            {
                msaaMatches = resolved[i * 4 + 0] == 77 && resolved[i * 4 + 1] == 88 &&
                             resolved[i * 4 + 2] == 99 && resolved[i * 4 + 3] == 255;
            }
            check(msaaMatches,
                  "D3D11RenderTargetBackend (MSAA): Clear()+resolve-on-unbind produces the exact color in the resolved texture");
            std::printf("    MSAA: requested 4x, device-applied %dx\n", d3dRtMsaa->GetMultiSampleCount());
        }

        // Check L (DX-44): the sampler cache creates a real ID3D11SamplerState, caches it for
        // identical XNA-level state, and creates a distinct object for different state; then
        // exercise the backend's own ApplySamplerState() (PSSetSamplers wiring) end-to-end.
        {
            D3D11SamplerCache cache;
            auto s1 = cache.GetOrCreate(device, 0, 0, 0, 1);
            auto s2 = cache.GetOrCreate(device, 0, 0, 0, 1);
            auto s3 = cache.GetOrCreate(device, 2, 1, 1, 4);
            check(s1 != nullptr && s1.Get() == s2.Get() && s3 != nullptr && s3.Get() != s1.Get() &&
                      cache.GetCacheSizeEXT() == 2,
                  "D3D11SamplerCache: caches identical XNA-level state, creates a distinct object for different state");
            backend.ApplySamplerState(0, 0, 0, 0, 1);
        }

        // Check M (DX-47): a real ID3D11Query(D3D11_QUERY_OCCLUSION) completes and reports data.
        // PixelCount() == 0 is expected here (no draws occur between Begin()/End()) -- the point
        // of this check is that the query object is real and GetData() succeeds, not a nonzero
        // count (this backend has no draw path to exercise until Phase DX8).
        {
            auto oq = backend.CreateOcclusionQuery();
            oq->Begin();
            oq->End();
            context->Flush();
            bool completed = false;
            for (int i = 0; i < 100000 && !completed; ++i) completed = oq->IsComplete();
            check(oq != nullptr && completed,
                  "D3D11OcclusionQueryBackend: a real ID3D11Query completes and reports data");
            check(oq->PixelCount() == 0,
                  "D3D11OcclusionQueryBackend: PixelCount() is 0 with no draws between Begin()/End()");
        }

        // Check N (DX-46): SetRenderTargets() with 2 targets performs one real OMSetRenderTargets
        // call binding both RTVs, and Clear() writes the exact color into both -- real MRT
        // binding + clear, honestly not a claim about multi-target shader output (no draw path
        // exists yet, Phase DX8).
        {
            auto rtA = backend.CreateRenderTarget2D(4, 4, 0, false, false, 0);
            auto rtB = backend.CreateRenderTarget2D(4, 4, 0, false, false, 0);
            auto* d3dRtA = static_cast<D3D11RenderTargetBackend*>(rtA.get());
            auto* d3dRtB = static_cast<D3D11RenderTargetBackend*>(rtB.get());
            IRenderTargetBackend* rts[2] = { rtA.get(), rtB.get() };
            backend.SetRenderTargets(rts, 2);
            dev.Clear(Color(123, 45, 67, 255));
            backend.SetRenderTargets(nullptr, 0);

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
                  "D3D11GraphicsBackend::SetRenderTargets() (MRT): one OMSetRenderTargets binds both targets, Clear() writes both");
        }

        // plan_dx.md DX-143: multi-target (N>1) MRT per-target MSAA-resolve-on-unbind -- the real
        // gap this task closes. Before this fix, D3D11GraphicsBackend::SetRenderTargets() never
        // called any bound target's own resolve/mip-regen logic when the MRT set was replaced or
        // unbound (only the single-target SetRenderTarget2D() path did, via
        // D3D11RenderTargetBackend::UnbindAsRenderTarget()) -- so an MSAA MRT target's resolved,
        // sampleable texture would have stayed whatever it was before this Clear(), not the real
        // cleared-and-resolved content. Two DIFFERENT MSAA targets bound as MRT, cleared to two
        // DIFFERENT colors, proves each target's OWN resolve ran correctly (not, say, one target's
        // resolve accidentally running twice while the other's never runs).
        {
            auto rtMsaaA = backend.CreateRenderTarget2D(8, 8, 0, false, false, 4);
            auto rtMsaaB = backend.CreateRenderTarget2D(8, 8, 0, false, false, 4);
            auto* d3dRtMsaaA = static_cast<D3D11RenderTargetBackend*>(rtMsaaA.get());
            auto* d3dRtMsaaB = static_cast<D3D11RenderTargetBackend*>(rtMsaaB.get());
            IRenderTargetBackend* msaaRts[2] = { rtMsaaA.get(), rtMsaaB.get() };
            backend.SetRenderTargets(msaaRts, 2);
            dev.Clear(Color(200, 30, 40, 255));
            backend.SetRenderTargets(nullptr, 0);

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
                  "D3D11GraphicsBackend::SetRenderTargets() (MRT, N=2 MSAA targets): unbinding the "
                  "MRT set now genuinely resolves BOTH targets independently, not silently skipped "
                  "(plan_dx.md DX-143 -- same ResolveAndGenerateMipsEXT() code path also covers "
                  "mip-regeneration for a mipMap=true MRT target, not independently pixel-tested here)");

            // A subsequent single-target bind must also trigger the pending MRT flush -- proves
            // FlushPendingMRTResolveEXT() is genuinely called from SetRenderTarget2D() too, not
            // only from SetRenderTargets()'s own unbind-to-back-buffer path.
            auto rtMsaaC = backend.CreateRenderTarget2D(8, 8, 0, false, false, 4);
            auto rtMsaaD = backend.CreateRenderTarget2D(8, 8, 0, false, false, 4);
            auto* d3dRtMsaaC = static_cast<D3D11RenderTargetBackend*>(rtMsaaC.get());
            IRenderTargetBackend* msaaRts2[2] = { rtMsaaC.get(), rtMsaaD.get() };
            backend.SetRenderTargets(msaaRts2, 2);
            dev.Clear(Color(5, 6, 7, 255));
            auto rtPlain = backend.CreateRenderTarget2D(4, 4, 0, false, false, 0);
            backend.SetRenderTarget2D(rtPlain.get()); // switch straight to a single target, not null
            backend.SetRenderTarget2D(nullptr);

            const auto resolvedC = ReadTexture2DRegion(device, context, d3dRtMsaaC->GetSampleableTextureEXT(), 0, 0, 4, 4);
            bool mrtToSingleFlushed = true;
            for (int i = 0; i < 4 * 4 && mrtToSingleFlushed; ++i)
                mrtToSingleFlushed = resolvedC[i * 4 + 0] == 5 && resolvedC[i * 4 + 1] == 6 && resolvedC[i * 4 + 2] == 7;
            check(mrtToSingleFlushed,
                  "D3D11GraphicsBackend::SetRenderTarget2D(): switching directly from an MRT set to "
                  "a different single target also flushes the prior MRT set's resolve (plan_dx.md DX-143)");
        }

        // plan_dx.md DX-144: RenderTarget2D/RenderTargetCube mip-chain generation/sampling --
        // proves ResolveAndGenerateMipsEXT()'s GenerateMips() call (DX-45/DX-143) actually writes
        // correct content into mip levels > 0, not zero/garbage. Uses a solid single color across
        // the whole base mip: box-filtering a solid color always produces the exact same solid
        // color at every downstream mip level regardless of the filter kernel's specifics, so this
        // sidesteps needing to replicate exact box-filter math while still being a real,
        // discriminating proof (a zeroed/garbage mip would fail this immediately).
        {
            auto rtMip = backend.CreateRenderTarget2D(8, 8, 0 /*DepthFormat::None*/, false, true /*mipMap*/, 0);
            auto* d3dRtMip = static_cast<D3D11RenderTargetBackend*>(rtMip.get());
            backend.SetRenderTarget2D(rtMip.get());
            dev.Clear(Color(200, 90, 10, 255));
            backend.SetRenderTarget2D(nullptr); // UnbindAsRenderTarget() -> GenerateMips()

            check(d3dRtMip->GetLevelCountEXT() == 4,
                  "D3D11RenderTargetBackend: an 8x8 mipMap=true render target reports the expected "
                  "4-level mip chain (8x8/4x4/2x2/1x1, plan_dx.md DX-144)");

            const auto mip1 = ReadTexture2DMipRegion(device, context, d3dRtMip->GetSampleableTextureEXT(), 1, 0, 0, 4, 4);
            bool mip1Exact = mip1.size() == 4u * 4u * 4u;
            for (std::size_t i = 0; i < 4u * 4u && mip1Exact; ++i)
                mip1Exact = mip1[i * 4 + 0] == 200 && mip1[i * 4 + 1] == 90 && mip1[i * 4 + 2] == 10 && mip1[i * 4 + 3] == 255;
            check(mip1Exact,
                  "D3D11RenderTargetBackend: GenerateMips()-on-unbind writes the exact box-filtered "
                  "(here: solid-color-preserving) content into mip level 1, read back directly from "
                  "the real GPU resource, not zero/garbage (plan_dx.md DX-144)");

            const auto mip2 = ReadTexture2DMipRegion(device, context, d3dRtMip->GetSampleableTextureEXT(), 2, 0, 0, 2, 2);
            bool mip2Exact = mip2.size() == 2u * 2u * 4u;
            for (std::size_t i = 0; i < 2u * 2u && mip2Exact; ++i)
                mip2Exact = mip2[i * 4 + 0] == 200 && mip2[i * 4 + 1] == 90 && mip2[i * 4 + 2] == 10 && mip2[i * 4 + 3] == 255;
            check(mip2Exact,
                  "D3D11RenderTargetBackend: mip level 2 (2x2) is also exact, confirming the full "
                  "mip chain regenerates correctly, not just level 1 (plan_dx.md DX-144)");
        }
        {
            auto rtCubeMip = backend.CreateRenderTargetCube(8, 0 /*DepthFormat::None*/, true /*mipMap*/);
            auto* d3dRtCubeMip = static_cast<D3D11RenderTargetCubeBackend*>(rtCubeMip.get());
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
                  "D3D11RenderTargetCubeBackend: GenerateMips()-on-unbind also regenerates face 0's "
                  "own mip chain correctly, read back directly from the real GPU resource "
                  "(plan_dx.md DX-144; D3D12's own render-target mip-chain generation does not exist "
                  "at all yet -- D3D12RenderTargets.hpp's own DX-117 scope note -- so DX-144's D3D12 "
                  "leg stays open, a real follow-up feature, not a test gap)");
        }

        // plan_dx.md DX-145: RenderTarget2D DepthStencilFormat fidelity -- confirms a render target
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

            auto rtNone = backend.CreateRenderTarget2D(4, 4, 0 /*DepthFormat::None*/, false, false, 0);
            auto* d3dRtNone = static_cast<D3D11RenderTargetBackend*>(rtNone.get());
            check(d3dRtNone->GetDSVEXT() == nullptr,
                  "D3D11RenderTargetBackend: DepthFormat::None creates no depth-stencil view at all "
                  "(plan_dx.md DX-145)");

            auto rtD16 = backend.CreateRenderTarget2D(4, 4, 1 /*DepthFormat::Depth16*/, false, false, 0);
            auto* d3dRtD16 = static_cast<D3D11RenderTargetBackend*>(rtD16.get());
            check(GetDsvFormat(d3dRtD16->GetDSVEXT()) == DXGI_FORMAT_D16_UNORM,
                  "D3D11RenderTargetBackend: DepthFormat::Depth16 genuinely creates a "
                  "DXGI_FORMAT_D16_UNORM depth resource, not silently upgraded to a combined "
                  "depth+stencil format (plan_dx.md DX-145)");

            auto rtD24 = backend.CreateRenderTarget2D(4, 4, 2 /*DepthFormat::Depth24*/, false, false, 0);
            auto* d3dRtD24 = static_cast<D3D11RenderTargetBackend*>(rtD24.get());
            check(GetDsvFormat(d3dRtD24->GetDSVEXT()) == DXGI_FORMAT_D24_UNORM_S8_UINT,
                  "D3D11RenderTargetBackend: DepthFormat::Depth24 lands on the documented "
                  "DXGI_FORMAT_D24_UNORM_S8_UINT fallback (D3D11 has no pure 24-bit depth-only "
                  "format, plan_dx.md DX-145)");

            auto rtD24S8 = backend.CreateRenderTarget2D(4, 4, 3 /*DepthFormat::Depth24Stencil8*/, false, false, 0);
            auto* d3dRtD24S8 = static_cast<D3D11RenderTargetBackend*>(rtD24S8.get());
            check(GetDsvFormat(d3dRtD24S8->GetDSVEXT()) == DXGI_FORMAT_D24_UNORM_S8_UINT,
                  "D3D11RenderTargetBackend: DepthFormat::Depth24Stencil8 also creates "
                  "DXGI_FORMAT_D24_UNORM_S8_UINT -- both Depth24 and Depth24Stencil8 genuinely share "
                  "the SAME real DXGI resource format, not just coincidentally 'both work' "
                  "(plan_dx.md DX-145)");
        }

        // Check O (DX-50/DX-51/DX-52/DX-53): real ID3D11BlendState/DepthStencilState/
        // RasterizerState creation+caching+binding, plus SetViewport()/SetScissorRect()'s direct
        // RSSetViewports()/RSSetScissorRects() round-trip. Behavioral pixel-correctness of
        // blend/stencil *output* needs an actual draw call (Phase DX8, not yet available) -- this
        // check honestly proves creation, caching identity/distinctness, and real device binding,
        // same honest bound Check J/K/N already established for render targets/MRT.
        {
            // BlendState: NonPremultiplied-style (SourceAlpha/InvSourceAlpha/Add on both channels)
            // -- deliberately not the Opaque combo, so BlendEnable ends up TRUE.
            auto& blendCache = backend.GetBlendStateCacheEXT();
            auto blendA1 = blendCache.GetOrCreate(device, 4, 4, 5, 5, 0, 0);   // SourceAlpha/InvSourceAlpha/Add
            auto blendA2 = blendCache.GetOrCreate(device, 4, 4, 5, 5, 0, 0);
            auto blendB = blendCache.GetOrCreate(device, 0, 0, 1, 1, 0, 0);    // Opaque
            check(blendA1.Get() != nullptr && blendA1.Get() == blendA2.Get(),
                  "D3D11BlendStateCache: identical XNA blend params return the identical object");
            check(blendA1.Get() != blendB.Get(),
                  "D3D11BlendStateCache: different XNA blend params return a different object");

            backend.ApplyBlendState(4, 4, 5, 5, 0, 0);
            ID3D11BlendState* boundBlend = nullptr;
            float boundBlendFactor[4] = {};
            UINT boundSampleMask = 0;
            context->OMGetBlendState(&boundBlend, boundBlendFactor, &boundSampleMask);
            check(boundBlend == blendA1.Get(),
                  "D3D11GraphicsBackend::ApplyBlendState(): OMSetBlendState actually bound the cached object");
            if (boundBlend) boundBlend->Release();

            backend.SetBlendFactor(0.25f, 0.5f, 0.75f, 1.0f);
            ID3D11BlendState* boundBlend2 = nullptr;
            context->OMGetBlendState(&boundBlend2, boundBlendFactor, &boundSampleMask);
            check(boundBlend2 == blendA1.Get() &&
                  boundBlendFactor[0] == 0.25f && boundBlendFactor[1] == 0.5f &&
                  boundBlendFactor[2] == 0.75f && boundBlendFactor[3] == 1.0f,
                  "D3D11GraphicsBackend::SetBlendFactor(): re-binds the current blend state with the new factor, standalone");
            if (boundBlend2) boundBlend2->Release();

            // DepthStencilState: depth+stencil enabled, distinct front-face ops.
            auto& dsCache = backend.GetDepthStencilStateCacheEXT();
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

            backend.ApplyDepthStencilState(true, true, 2, true, 0, 2, 0, 0, 0xFF, 0xFF, 77,
                                           false, 0, 0, 0, 0);
            ID3D11DepthStencilState* boundDS = nullptr;
            UINT boundRef = 0;
            context->OMGetDepthStencilState(&boundDS, &boundRef);
            check(boundDS == dsA1.Get() && boundRef == 77,
                  "D3D11GraphicsBackend::ApplyDepthStencilState(): OMSetDepthStencilState bound the cached object with the reference value");
            if (boundDS) boundDS->Release();

            backend.SetReferenceStencil(123);
            ID3D11DepthStencilState* boundDS2 = nullptr;
            context->OMGetDepthStencilState(&boundDS2, &boundRef);
            check(boundDS2 == dsA1.Get() && boundRef == 123,
                  "D3D11GraphicsBackend::SetReferenceStencil(): re-binds the current depth-stencil state with the new reference, standalone");
            if (boundDS2) boundDS2->Release();

            // RasterizerState: cull-back/solid vs. cull-none/wireframe.
            auto& rsCache = backend.GetRasterizerStateCacheEXT();
            auto rsA1 = rsCache.GetOrCreate(device, 2 /*CullCounterClockwiseFace*/, 0 /*Solid*/, false, 0.0f, 0.0f);
            auto rsA2 = rsCache.GetOrCreate(device, 2, 0, false, 0.0f, 0.0f);
            auto rsB = rsCache.GetOrCreate(device, 0 /*None*/, 1 /*WireFrame*/, true, 1.0f, 2.0f);
            check(rsA1.Get() != nullptr && rsA1.Get() == rsA2.Get(),
                  "D3D11RasterizerStateCache: identical XNA rasterizer params return the identical object");
            check(rsA1.Get() != rsB.Get(),
                  "D3D11RasterizerStateCache: different XNA rasterizer params return a different object");

            backend.ApplyRasterizerState(2, 0, false, 0.0f, 0.0f);
            ID3D11RasterizerState* boundRS = nullptr;
            context->RSGetState(&boundRS);
            check(boundRS == rsA1.Get(),
                  "D3D11GraphicsBackend::ApplyRasterizerState(): RSSetState actually bound the cached object");
            if (boundRS) boundRS->Release();

            // Viewport / scissor rect round-trip.
            backend.SetViewport(2, 3, 40, 30, 0.1f, 0.9f);
            UINT vpCount = 1;
            D3D11_VIEWPORT boundVp{};
            context->RSGetViewports(&vpCount, &boundVp);
            check(vpCount == 1 && boundVp.TopLeftX == 2.0f && boundVp.TopLeftY == 3.0f &&
                  boundVp.Width == 40.0f && boundVp.Height == 30.0f &&
                  boundVp.MinDepth == 0.1f && boundVp.MaxDepth == 0.9f,
                  "D3D11GraphicsBackend::SetViewport(): RSSetViewports round-trips the exact rectangle and depth range");

            backend.SetScissorRect(5, 6, 20, 10);
            UINT rectCount = 1;
            D3D11_RECT boundRect{};
            context->RSGetScissorRects(&rectCount, &boundRect);
            check(rectCount == 1 && boundRect.left == 5 && boundRect.top == 6 &&
                  boundRect.right == 25 && boundRect.bottom == 16,
                  "D3D11GraphicsBackend::SetScissorRect(): RSSetScissorRects round-trips as (x,y,x+w,y+h)");

            // Restore the window-size viewport so nothing downstream (there is nothing after this
            // check today, but keep the invariant honest) is left with a stale small viewport.
            int vw = 0, vh = 0;
            backend.GetViewportSize(vw, vh);
            backend.SetViewport(0, 0, vw, vh, 0.0f, 1.0f);
        }

        // Check P (DX-60/DX-60a/DX-61) -- the first real 3D triangle this backend has ever drawn.
        // A real colored3d draw (input layout + VS/PS + PerDraw/FogParams constant buffers, all
        // wired for real) paints a known solid-red vertex color over a known-blue-cleared
        // background; reading back the SAME pixel before and after the draw call (blue -> red)
        // proves the fragment genuinely came from the draw, not a stale/cached readback. Exercised
        // for both the non-indexed (DrawColoredPrimitives) and indexed (DrawIndexedColoredPrimitives)
        // paths. State is reset to a known-safe baseline first (opaque blend, depth/stencil off,
        // no culling) so this check doesn't depend on whatever Check O happened to leave bound.
        {
            backend.ApplyBlendState(0, 0, 1, 1, 0, 0);                        // Opaque
            backend.ApplyDepthStencilState(false, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
            backend.ApplyRasterizerState(0 /*CullMode::None*/, 0 /*FillMode::Solid*/, false, 0.0f, 0.0f);

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
            auto vb = backend.CreateVertexBuffer(3);
            vb->SetData(kTri, 3, sizeof(VPC));

            const Microsoft::Xna::Framework::Rectangle centerRegion(28, 28, 4, 4);
            std::vector<Color> before(4 * 4, Color(0, 0, 0, 0));
            std::vector<Color> after(4 * 4, Color(0, 0, 0, 0));

            // Non-indexed path.
            dev.Clear(Color(0, 0, 255, 255));
            dev.GetBackBufferData(&centerRegion, before.data(), 0, static_cast<int>(before.size()));
            backend.DrawColoredPrimitives(*vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
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
                  "D3D11GraphicsBackend::DrawColoredPrimitives(): real colored3d draw paints exact "
                  "vertex color over the Clear() background at the same readback location");

            // Indexed path: same triangle, via DrawIndexedColoredPrimitives.
            static const uint16_t kTriIdx[3] = {0, 1, 2};
            auto ib = backend.CreateIndexBuffer16(3);
            ib->SetData16(kTriIdx, 3);

            dev.Clear(Color(0, 0, 255, 255));
            std::vector<Color> beforeIdx(4 * 4, Color(0, 0, 0, 0));
            std::vector<Color> afterIdx(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion, beforeIdx.data(), 0, static_cast<int>(beforeIdx.size()));
            backend.DrawIndexedColoredPrimitives(*vb, *ib, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
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
                  "D3D11GraphicsBackend::DrawIndexedColoredPrimitives(): real colored3d indexed draw "
                  "paints exact vertex color over the Clear() background at the same readback location");
        }

        const Microsoft::Xna::Framework::Rectangle centerRegion28(28, 28, 4, 4);

        // Check Q (DX-62): textured3d (stride 20) + colored_textured3d (stride 24) via real
        // GpuDrawParams, using the same before/after-Clear() discipline Check P established.
        {
            backend.ApplySamplerState(0, 1 /*TextureFilter::Point*/, 0, 0, 1);

            struct VPT { float x, y, z; float u, v; };
            static const VPT kTriTex[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
                { 3.0f, -1.0f, 0.0f, 2.0f, 1.0f},
                {-1.0f,  3.0f, 0.0f, 0.0f, -1.0f},
            };
            auto vbTex = backend.CreateVertexBuffer(3);
            vbTex->SetData(kTriTex, 3, sizeof(VPT));

            ImageData img;
            img.width = 2; img.height = 2; img.mipLevels = 1;
            img.pixels.resize(2 * 2 * 4);
            for (int i = 0; i < 4; ++i)
            {
                img.pixels[i * 4 + 0] = 11; img.pixels[i * 4 + 1] = 22;
                img.pixels[i * 4 + 2] = 33; img.pixels[i * 4 + 3] = 255;
            }
            auto tex = backend.CreateTexture(img);

            GpuDrawParams tp;
            tp.texture0 = tex.get();
            tp.textureEnabled = true;
            // diffuseColor left at its default (1,1,1,1) so outColor == the raw sampled texel exactly.

            std::vector<Color> beforeQ(4 * 4, Color(0, 0, 0, 0));
            std::vector<Color> afterQ(4 * 4, Color(0, 0, 0, 0));
            dev.Clear(Color(0, 255, 0, 255));
            dev.GetBackBufferData(&centerRegion28, beforeQ.data(), 0, static_cast<int>(beforeQ.size()));
            backend.DrawPrimitivesEx(*vbTex, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
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
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): real textured3d draw samples the exact "
                  "texture color (diffuseColor=white) over the Clear() background (plan_dx.md DX-62)");

            // Indexed path, same textured3d draw -- proves DrawIndexedPrimitivesEx shares the same
            // real pipeline (DrawPrimitivesExImpl), not just the non-indexed entry point.
            static const uint16_t kTriTexIdx[3] = {0, 1, 2};
            auto ibTex = backend.CreateIndexBuffer16(3);
            ibTex->SetData16(kTriTexIdx, 3);
            dev.Clear(Color(0, 255, 0, 255));
            backend.DrawIndexedPrimitivesEx(*vbTex, *ibTex, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                            Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, tp);
            std::vector<Color> afterQIdx(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, afterQIdx.data(), 0, static_cast<int>(afterQIdx.size()));
            bool afterQIdxIsTexColor = true;
            for (const Color& p : afterQIdx)
                if (p.getRProperty() != 11 || p.getGProperty() != 22 || p.getBProperty() != 33 || p.getAProperty() != 255)
                    afterQIdxIsTexColor = false;
            check(afterQIdxIsTexColor,
                  "D3D11GraphicsBackend::DrawIndexedPrimitivesEx(): indexed textured3d draw shares "
                  "the same real pipeline and samples the exact texture color");

            // colored_textured3d (stride 24): a white texture tinted by an exact vertex color,
            // proving VertexColorEnabled's real multiply, not just the texture sample alone.
            struct VPCT { float x, y, z; uint32_t color; float u, v; };
            const uint32_t kVertColor = 0xFFF0A050u; // A=255,B=240,G=160,R=80 (R8G8B8A8 byte order)
            static VPCT kTriColTex[3];
            kTriColTex[0] = { -1.0f, -1.0f, 0.0f, kVertColor, 0.0f, 1.0f };
            kTriColTex[1] = {  3.0f, -1.0f, 0.0f, kVertColor, 2.0f, 1.0f };
            kTriColTex[2] = { -1.0f,  3.0f, 0.0f, kVertColor, 0.0f, -1.0f };
            auto vbColTex = backend.CreateVertexBuffer(3);
            vbColTex->SetData(kTriColTex, 3, sizeof(VPCT));

            ImageData whiteImg;
            whiteImg.width = 2; whiteImg.height = 2; whiteImg.mipLevels = 1;
            whiteImg.pixels.assign(2 * 2 * 4, 255);
            auto whiteTex = backend.CreateTexture(whiteImg);

            GpuDrawParams ctp;
            ctp.texture0 = whiteTex.get();
            ctp.textureEnabled = true;
            ctp.vertexColorEnabled = true;

            dev.Clear(Color(0, 255, 0, 255));
            backend.DrawPrimitivesEx(*vbColTex, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ctp);
            std::vector<Color> afterCT(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, afterCT.data(), 0, static_cast<int>(afterCT.size()));
            bool afterCTIsVertColor = true;
            for (const Color& p : afterCT)
                if (p.getRProperty() != 80 || p.getGProperty() != 160 || p.getBProperty() != 240 || p.getAProperty() != 255)
                    afterCTIsVertColor = false;
            check(afterCTIsVertColor,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): real colored_textured3d draw multiplies "
                  "the exact vertex color through a white texture (plan_dx.md DX-62)");

            // DX-137: dedicated fog on/off discriminating test for textured3d -- a representative
            // variant of the 7 fog-capable non-colored3d variants (chosen for its simple, already-
            // proven fixture above; a full 7-variant x 2-backend sweep is out of this task's own
            // scope, documented honestly rather than silently partial). Same Z-at-FogEnd
            // methodology DX-69/DX-81's own colored3d fog test already established -- needs its OWN
            // vertex buffer at Z=0.5 (fogEnd), not the shared vbTex (Z=0, which gives fogFactor=1,
            // i.e. no blending at all -- a real fixture bug found and fixed while writing this test).
            static const VPT kTriTexFog[3] = {
                {-1.0f, -1.0f, 0.5f, 0.0f, 1.0f},
                { 3.0f, -1.0f, 0.5f, 2.0f, 1.0f},
                {-1.0f,  3.0f, 0.5f, 0.0f, -1.0f},
            };
            auto vbTexFog = backend.CreateVertexBuffer(3);
            vbTexFog->SetData(kTriTexFog, 3, sizeof(VPT));

            const Microsoft::Xna::Framework::Rectangle texFogRegion(30, 30, 1, 1);
            tp.fogEnabled = false;
            tp.fogColor[0] = 0.0f; tp.fogColor[1] = 1.0f; tp.fogColor[2] = 0.0f;
            tp.fogStart = 0.0f; tp.fogEnd = 0.5f;
            dev.Clear(Color(10, 10, 10, 255));
            backend.DrawPrimitivesEx(*vbTexFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, tp);
            Color texFogOffPixel(0, 0, 0, 0);
            dev.GetBackBufferData(&texFogRegion, &texFogOffPixel, 0, 1);
            check(texFogOffPixel.getRProperty() == 11 && texFogOffPixel.getGProperty() == 22 &&
                  texFogOffPixel.getBProperty() == 33,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): textured3d fogEnabled=false leaves the "
                  "exact sampled texture color unblended (plan_dx.md DX-137)");

            tp.fogEnabled = true;
            dev.Clear(Color(10, 10, 10, 255));
            backend.DrawPrimitivesEx(*vbTexFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, tp);
            Color texFogOnPixel(0, 0, 0, 0);
            dev.GetBackBufferData(&texFogRegion, &texFogOnPixel, 0, 1);
            check(texFogOnPixel.getRProperty() == 0 && texFogOnPixel.getGProperty() == 255 &&
                  texFogOnPixel.getBProperty() == 0,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): textured3d fogEnabled=true with Z at "
                  "FogEnd genuinely blends all the way to the exact FogColor, distinctly different "
                  "from the fogEnabled=false case above (plan_dx.md DX-137)");
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
            auto vbLit = backend.CreateVertexBuffer(3);
            vbLit->SetData(kTriLit, 3, sizeof(VPNT));

            ImageData img;
            img.width = 2; img.height = 2; img.mipLevels = 1;
            img.pixels.resize(2 * 2 * 4);
            for (int i = 0; i < 4; ++i)
            {
                img.pixels[i * 4 + 0] = 44; img.pixels[i * 4 + 1] = 55;
                img.pixels[i * 4 + 2] = 66; img.pixels[i * 4 + 3] = 255;
            }
            auto tex = backend.CreateTexture(img);

            GpuDrawParams unlitP;
            unlitP.texture0 = tex.get();
            unlitP.textureEnabled = true;
            unlitP.lightingEnabled = false;

            dev.Clear(Color(0, 0, 255, 255));
            backend.DrawPrimitivesEx(*vbLit, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, unlitP);
            std::vector<Color> unlitResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, unlitResult.data(), 0, static_cast<int>(unlitResult.size()));
            bool unlitIsTexColor = true;
            for (const Color& p : unlitResult)
                if (p.getRProperty() != 44 || p.getGProperty() != 55 || p.getBProperty() != 66 || p.getAProperty() != 255)
                    unlitIsTexColor = false;
            check(unlitIsTexColor,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): real lit_textured3d unlit branch samples "
                  "diffuseColor*texture exactly (plan_dx.md DX-63)");

            GpuDrawParams litP = unlitP;
            litP.lightingEnabled = true;
            litP.ambientColor[0] = 0.5f; litP.ambientColor[1] = 0.5f; litP.ambientColor[2] = 0.5f;
            litP.specularColor[0] = 0.0f; litP.specularColor[1] = 0.0f; litP.specularColor[2] = 0.0f;

            dev.Clear(Color(0, 0, 255, 255));
            backend.DrawPrimitivesEx(*vbLit, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
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
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): real lit_textured3d lit branch genuinely "
                  "computes a different color than both the unlit result and the Clear() background "
                  "(plan_dx.md DX-63)");
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
            auto vbAT = backend.CreateVertexBuffer(3);
            vbAT->SetData(kTriAT, 3, sizeof(VPT));

            ImageData img;
            img.width = 2; img.height = 2; img.mipLevels = 1;
            img.pixels.resize(2 * 2 * 4);
            for (int i = 0; i < 4; ++i)
            {
                img.pixels[i * 4 + 0] = 200; img.pixels[i * 4 + 1] = 100;
                img.pixels[i * 4 + 2] = 50;  img.pixels[i * 4 + 3] = 128; // alpha=128/255 ~ 0.502
            }
            auto tex = backend.CreateTexture(img);

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
            backend.DrawPrimitivesEx(*vbAT, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, atp);
            std::vector<Color> discardResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, discardResult.data(), 0, static_cast<int>(discardResult.size()));
            bool stillGreen = true;
            for (const Color& p : discardResult)
                if (p.getRProperty() != 0 || p.getGProperty() != 255 || p.getBProperty() != 0 || p.getAProperty() != 255)
                    stillGreen = false;
            check(stillGreen,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): real alpha_test3d discard() genuinely "
                  "drops a failing pixel, leaving the Clear() background untouched (plan_dx.md DX-64)");

            // Sub-check 2: replace the texture's alpha with 64/255 (< 0.5) -> passes -> drawn exactly.
            std::vector<uint8_t> passPixels(2 * 2 * 4);
            for (int i = 0; i < 4; ++i)
            {
                passPixels[i * 4 + 0] = 200; passPixels[i * 4 + 1] = 100;
                passPixels[i * 4 + 2] = 50;  passPixels[i * 4 + 3] = 64;
            }
            tex->UpdatePixelsLevel(0, passPixels.data(), 2, 2);

            dev.Clear(Color(0, 255, 0, 255));
            backend.DrawPrimitivesEx(*vbAT, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, atp);
            std::vector<Color> passResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, passResult.data(), 0, static_cast<int>(passResult.size()));
            bool passIsExact = true;
            for (const Color& p : passResult)
                if (p.getRProperty() != 200 || p.getGProperty() != 100 || p.getBProperty() != 50 || p.getAProperty() != 64)
                    passIsExact = false;
            check(passIsExact,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): real alpha_test3d draws the exact texture "
                  "color (including its own alpha byte) when the test passes (plan_dx.md DX-64)");
        }

        // Check T (DX-65): dual_texture3d. tex1.rgb *= 2.0, outColor = tex1 * tex2 * Tint. Both
        // textures are uniform 2x2 so exact texel values are unaffected by bilinear vs. point
        // sampling. tex1=(50,60,70,200), tex2=white -> expected (100,120,140,200).
        {
            backend.ApplySamplerState(0, 1 /*TextureFilter::Point*/, 0, 0, 1);
            backend.ApplySamplerState(1, 1 /*TextureFilter::Point*/, 0, 0, 1);

            struct VPT2 { float x, y, z; float u, v; };
            static const VPT2 kTriDT[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
                { 3.0f, -1.0f, 0.0f, 2.0f, 1.0f},
                {-1.0f,  3.0f, 0.0f, 0.0f, -1.0f},
            };
            auto vbDT = backend.CreateVertexBuffer(3);
            vbDT->SetData(kTriDT, 3, sizeof(VPT2));

            ImageData img1;
            img1.width = 2; img1.height = 2; img1.mipLevels = 1;
            img1.pixels.resize(2 * 2 * 4);
            for (int i = 0; i < 4; ++i)
            {
                img1.pixels[i * 4 + 0] = 50; img1.pixels[i * 4 + 1] = 60;
                img1.pixels[i * 4 + 2] = 70; img1.pixels[i * 4 + 3] = 200;
            }
            auto tex1 = backend.CreateTexture(img1);

            ImageData img2;
            img2.width = 2; img2.height = 2; img2.mipLevels = 1;
            img2.pixels.assign(2 * 2 * 4, 255);
            auto tex2 = backend.CreateTexture(img2);

            GpuDrawParams dtp;
            dtp.texture0 = tex1.get();
            dtp.texture1 = tex2.get();
            dtp.dualTexture = true;
            dtp.textureEnabled = true;

            dev.Clear(Color(0, 0, 255, 255));
            backend.DrawPrimitivesEx(*vbDT, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, dtp);
            std::vector<Color> dtResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, dtResult.data(), 0, static_cast<int>(dtResult.size()));
            bool dtIsExact = true;
            for (const Color& p : dtResult)
                if (p.getRProperty() != 100 || p.getGProperty() != 120 || p.getBProperty() != 140 || p.getAProperty() != 200)
                    dtIsExact = false;
            check(dtIsExact,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): real dual_texture3d combines both real "
                  "SRVs (tex1.rgb*2 * tex2) to the exact expected byte result (plan_dx.md DX-65)");
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
            auto vbEnv = backend.CreateVertexBuffer(3);
            vbEnv->SetData(kTriEnv, 3, sizeof(VPNTE));

            ImageData whiteImg;
            whiteImg.width = 2; whiteImg.height = 2; whiteImg.mipLevels = 1;
            whiteImg.pixels.assign(2 * 2 * 4, 255);
            auto whiteTex = backend.CreateTexture(whiteImg);

            auto cube = backend.CreateTextureCube(8, false, 0);
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
            backend.DrawPrimitivesEx(*vbEnv, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ep);
            std::vector<Color> envResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, envResult.data(), 0, static_cast<int>(envResult.size()));
            bool envIsExact = true;
            for (const Color& p : envResult)
                if (p.getRProperty() != 10 || p.getGProperty() != 20 || p.getBProperty() != 30 || p.getAProperty() != 255)
                    envIsExact = false;
            check(envIsExact,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): real env_map3d samples the exact "
                  "distinctly-colored cube face via the real TextureCube SRV (plan_dx.md DX-66)");

            // DX-134: same fixture, envMapAmount=0.0 -> blendFactor=0 -> the lerp(baseColor,
            // envSample*alpha, blendFactor) formula must collapse to the pure base color (lit=0
            // since light0Dir default is perpendicular to this surface's normal, ambient/emissive
            // both default 0), NOT the reflected cube-face color Check U above just proved --
            // genuinely different from the amount=1.0 result, proving the blend is real and
            // graduated, not a fixed always-on reflection.
            ep.envMapAmount = 0.0f;
            dev.Clear(Color(0, 0, 255, 255));
            backend.DrawPrimitivesEx(*vbEnv, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ep);
            std::vector<Color> envZeroResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, envZeroResult.data(), 0, static_cast<int>(envZeroResult.size()));
            bool envZeroIsExact = true;
            for (const Color& p : envZeroResult)
                if (p.getRProperty() != 0 || p.getGProperty() != 0 || p.getBProperty() != 0 || p.getAProperty() != 255)
                    envZeroIsExact = false;
            check(envZeroIsExact,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): real env_map3d with envMapAmount=0.0 "
                  "collapses the base-lerp to the pure (unlit) base color, distinctly different from "
                  "the envMapAmount=1.0 reflected-face color above -- proves the lerp is a genuine "
                  "graduated blend control, not just an on/off gate (plan_dx.md DX-134)");
        }

        // Check V (DX-67): skinned3d. A single identity bone (BoneBlock genuinely populated from
        // GpuDrawParams::boneTransforms, not left zero-initialized -- an all-zero bone matrix would
        // degenerate the transform and fail this check) combined with ambient=white and
        // specular=zeroed (light0's own diffuse contribution is already zero by construction: the
        // vertex normal (0,0,1) is perpendicular to the default light0Dir (0,-1,0)) leaves
        // outColor == the exact sampled texture color.
        {
            struct VPNTS { float x, y, z; float nx, ny, nz; float u, v;
                          float bw0, bw1, bw2, bw3; uint8_t bi0, bi1, bi2, bi3; };
            static const VPNTS kTriSkin[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
                { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
                {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            };
            auto vbSkin = backend.CreateVertexBuffer(3);
            vbSkin->SetData(kTriSkin, 3, sizeof(VPNTS));

            ImageData img;
            img.width = 2; img.height = 2; img.mipLevels = 1;
            img.pixels.resize(2 * 2 * 4);
            for (int i = 0; i < 4; ++i)
            {
                img.pixels[i * 4 + 0] = 77; img.pixels[i * 4 + 1] = 88;
                img.pixels[i * 4 + 2] = 99; img.pixels[i * 4 + 3] = 255;
            }
            auto tex = backend.CreateTexture(img);

            GpuDrawParams sp;
            sp.texture0 = tex.get();
            sp.textureEnabled = true;
            sp.skinned = true;
            sp.boneCount = 1;
            sp.weightsPerVertex = 1;
            Matrix::getIdentityProperty().ToColumnMajor(sp.boneTransforms);
            sp.ambientColor[0] = 1.0f; sp.ambientColor[1] = 1.0f; sp.ambientColor[2] = 1.0f;
            sp.specularColor[0] = 0.0f; sp.specularColor[1] = 0.0f; sp.specularColor[2] = 0.0f;
            sp.eyePositionWorld[0] = 0.0f; sp.eyePositionWorld[1] = 0.0f; sp.eyePositionWorld[2] = -10.0f;

            dev.Clear(Color(0, 255, 0, 255));
            backend.DrawPrimitivesEx(*vbSkin, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, sp);
            std::vector<Color> skinResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, skinResult.data(), 0, static_cast<int>(skinResult.size()));
            bool skinIsExact = true;
            for (const Color& p : skinResult)
                if (p.getRProperty() != 77 || p.getGProperty() != 88 || p.getBProperty() != 99 || p.getAProperty() != 255)
                    skinIsExact = false;
            check(skinIsExact,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): real skinned3d with a genuinely-populated "
                  "single identity bone samples the exact texture color (plan_dx.md DX-67)");

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
            auto vbSkin2 = backend.CreateVertexBuffer(3);
            vbSkin2->SetData(kTriSkin2, 3, sizeof(VPNTS2));

            const Microsoft::Xna::Framework::Rectangle offCenterPoint(54, 10, 2, 2);

            sp.weightsPerVertex = 1;
            dev.Clear(Color(1, 2, 3, 255));
            backend.DrawPrimitivesEx(*vbSkin2, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, sp);
            std::vector<Color> w1Result(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&offCenterPoint, w1Result.data(), 0, static_cast<int>(w1Result.size()));
            bool w1IsTexture = true;
            for (const Color& p : w1Result)
                if (p.getRProperty() != 77 || p.getGProperty() != 88 || p.getBProperty() != 99)
                    w1IsTexture = false;
            check(w1IsTexture,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): real skinned3d with weightsPerVertex=1 "
                  "genuinely ignores bone1's contribution -- the probe point still shows bone0's "
                  "unshrunk Identity result, matching Check V's own single-bone precedent "
                  "(plan_dx.md DX-135)");

            sp.weightsPerVertex = 2;
            dev.Clear(Color(1, 2, 3, 255));
            backend.DrawPrimitivesEx(*vbSkin2, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, sp);
            std::vector<Color> w2Result(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&offCenterPoint, w2Result.data(), 0, static_cast<int>(w2Result.size()));
            bool w2IsBackground = true;
            for (const Color& p : w2Result)
                if (p.getRProperty() != 1 || p.getGProperty() != 2 || p.getBProperty() != 3)
                    w2IsBackground = false;
            check(w2IsBackground,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): real skinned3d with weightsPerVertex=2 "
                  "genuinely includes bone1's contribution -- the blended Scale(0.55) transform "
                  "shrinks the triangle away from this same probe point (Clear() background shows "
                  "through), distinctly different from the weightsPerVertex=1 case above with "
                  "identical vertex weight data, proving WeightsPerVertex actually gates which bones "
                  "are summed (plan_dx.md DX-135)");
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
            auto vbInst = backend.CreateVertexBuffer(3);
            vbInst->SetData(kTriInst, 3, sizeof(VP3));

            static const uint16_t kTriInstIdx[3] = {0, 1, 2};
            auto ibInst = backend.CreateIndexBuffer16(3);
            ibInst->SetData16(kTriInstIdx, 3);

            // One identity-transform instance: 4 float4 rows (INSTANCEWORLD0-3).
            static const float kInstanceWorld[16] = {
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f,
            };
            auto instVb = backend.CreateVertexBuffer(1);
            instVb->SetData(kInstanceWorld, 1, sizeof(kInstanceWorld));

            GpuDrawParams ip;
            ip.instanceVb = instVb.get();
            ip.diffuseColor[0] = 1.0f; ip.diffuseColor[1] = 1.0f;
            ip.diffuseColor[2] = 0.0f; ip.diffuseColor[3] = 1.0f;

            dev.Clear(Color(0, 0, 255, 255));
            backend.DrawInstancedPrimitivesEx(*vbInst, *ibInst, Matrix::getIdentityProperty(),
                                              Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                              PrimitiveType::TriangleList, 1, 1, ip);
            std::vector<Color> instResult(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&centerRegion28, instResult.data(), 0, static_cast<int>(instResult.size()));
            bool instIsExact = true;
            for (const Color& p : instResult)
                if (p.getRProperty() != 255 || p.getGProperty() != 255 || p.getBProperty() != 0 || p.getAProperty() != 255)
                    instIsExact = false;
            check(instIsExact,
                  "D3D11GraphicsBackend::DrawInstancedPrimitivesEx(): real instanced3d draw with a "
                  "genuine per-instance world buffer outputs the exact instance DiffuseColor (plan_dx.md DX-68)");
        }

        // Check X (DX-58): custom ShaderEffect. Runtime D3DCompile() of arbitrary HLSL source (not
        // one of DX-13-hlsl's offline-compiled stock variants) compiles successfully, Bind() drives
        // a real manual draw (through the raw device context -- SpriteBatch, the real future
        // caller, doesn't exist yet, Phase DX9) whose color is genuinely driven by
        // SetUniformVec4()'s fixed-slot constant buffer, and a deliberately broken HLSL source
        // fails cleanly with a real, non-empty compiler error instead of crashing or silently
        // "succeeding".
        {
            ID3D11DeviceContext* rawContext = backend.GetContextEXT();

            auto effect = backend.CreateEffectBackend(
                "struct VSIn { float2 pos:POSITION0; float2 uv:TEXCOORD0; float4 col:COLOR0; };\n"
                "struct VSOut { float4 pos:SV_Position; float4 col:TEXCOORD0; };\n"
                "cbuffer CB : register(b0) { float4 pad0[5]; float4 uColor; float4 uFloat0; };\n"
                "VSOut main(VSIn input) { VSOut o; o.pos=float4(input.pos,0,1); o.col=input.col*uColor; return o; }",
                "struct PSIn { float4 pos:SV_Position; float4 col:TEXCOORD0; };\n"
                "float4 main(PSIn input):SV_Target { return input.col; }");
            check(effect && effect->IsValid(),
                  "D3D11GraphicsBackend::CreateEffectBackend(): real runtime D3DCompile() of "
                  "arbitrary HLSL source compiles successfully (plan_dx.md DX-58)");

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
                auto vbFx = backend.CreateVertexBuffer(3);
                vbFx->SetData(kTriFx, 3, sizeof(SpriteVtx));
                auto& d3dVbFx = static_cast<D3D11VertexBufferBackend&>(*vbFx);

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
                  "D3D11EffectBackend::Bind(): a real custom-compiled shader pair, driven by "
                  "SetUniformVec4()'s fixed-slot constant buffer, draws the exact expected color "
                  "(plan_dx.md DX-58)");

            auto badEffect = backend.CreateEffectBackend("this is not valid HLSL {{{", "also not valid ]]]");
            check(badEffect && !badEffect->IsValid() && !badEffect->GetCompileError().empty(),
                  "D3D11GraphicsBackend::CreateEffectBackend(): a deliberately broken HLSL source "
                  "fails CompileProgram() with a real, non-empty compiler error message (plan_dx.md DX-58)");
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
                  "D3D11SpriteBatchBackend::Draw(): a real quad-batched sprite2d draw places all 4 "
                  "corner colors at the exact expected destination pixels (plan_dx.md DX-70)");

            dev.Clear(Color(10, 10, 10, 255));
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &pointClamp, nullptr, nullptr);
            batch.Draw(cornerTex, Microsoft::Xna::Framework::Rectangle(0, 0, 32, 32),
                      Microsoft::Xna::Framework::Rectangle(0, 0, 2, 2), Color::White,
                      0.0f, Vector2(0, 0), SpriteEffects::FlipHorizontally, 0.0f);
            batch.End();
            const Color flippedLeft = readPixel(4, 4), flippedRight = readPixel(20, 4);
            check(isColor(flippedLeft, 0, 255, 0) && isColor(flippedRight, 255, 0, 0),
                  "D3D11SpriteBatchBackend::Draw(): SpriteEffects::FlipHorizontally genuinely swaps "
                  "the sampled quadrants, not just accepted without effect (plan_dx.md DX-70)");
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
                  "D3D11SpriteBatchBackend::Draw(): a real 90-degree rotation around a centered origin "
                  "permutes the 4 corner colors into the geometrically-predicted screen quadrants -- "
                  "NW=blue(was BL), NE=red(was TL), SE=green(was TR), SW=yellow(was BR) (plan_dx.md DX-131)");
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
                  "D3D11SpriteBatchBackend::Draw(): a real, distinct destRect SIZE genuinely scales the "
                  "sprite -- a pixel inside the old (larger) size but outside the new (smaller) one shows "
                  "the clear color, not sprite content (plan_dx.md DX-131)");
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
                  "D3D11SpriteBatchBackend::Draw(): sourceRectangle genuinely crops to only the "
                  "requested sub-region (purple/cyan), never the excluded texels (orange/magenta) "
                  "(plan_dx.md DX-131)");
        }

        // Note on SpriteSortMode (DX-131): sort-mode ordering (BackToFront/FrontToBack/Texture) is
        // implemented entirely in the shared, backend-agnostic Microsoft::Xna::Framework::Graphics::
        // SpriteBatch.cpp (sorts the pending draw-call list before handing it to the backend's own
        // Draw() calls, in order) -- there is no D3D11-specific sort behavior to test; the backend
        // just draws whatever order it's told. A dedicated backend-level sort-mode test would
        // actually be testing shared C++ code, not this backend, so none is added here.

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
                  "D3D11SpriteBatchBackend: TextureAddressMode::Wrap genuinely tiles past UV 1.0 "
                  "instead of clamping to the edge color (plan_dx.md DX-72)");

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
                  "D3D11SpriteBatchBackend: TextureAddressMode::Mirror genuinely reflects past UV "
                  "1.0 (distinct from both Wrap's repeat and Clamp's edge-extend at the same probe "
                  "point) (plan_dx.md DX-72)");
        }

        // Check AA (DX-71): a custom Effect passed to SpriteBatch::Begin(..., effect) draws through
        // that effect's own shader (here, a deliberate RGB color inversion) instead of the stock
        // sprite2d pipeline -- proving D3D11EffectBackend::SetViewportSizeEXT()'s automatic vpSize
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
                  "Sprite2DVertex contract compiles successfully (plan_dx.md DX-71)");

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
                  "D3D11SpriteBatchBackend + SpriteBatch::Begin(effect): sprites draw through the "
                  "custom Effect's own shader, producing its exact expected (inverted) output "
                  "color, not the stock sprite2d pipeline's (plan_dx.md DX-71)");
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
            struct VPCz { float x, y, z; uint32_t color; };
            const uint32_t kRed = 0xFF0000FFu; // A=255,B=0,G=0,R=255 (R8G8B8A8 byte order)
            static const VPCz kTriFog[3] = {
                {-1.0f, -1.0f, 0.5f, kRed},
                { 3.0f, -1.0f, 0.5f, kRed},
                {-1.0f,  3.0f, 0.5f, kRed},
            };
            auto vbFog = backend.CreateVertexBuffer(3);
            vbFog->SetData(kTriFog, 3, sizeof(VPCz));

            GpuDrawParams fogOff;
            fogOff.vertexColorEnabled = true;
            fogOff.fogEnabled = false;
            fogOff.fogColor[0] = 0.0f; fogOff.fogColor[1] = 1.0f; fogOff.fogColor[2] = 0.0f;
            fogOff.fogStart = 0.0f;
            fogOff.fogEnd = 0.5f;

            const Microsoft::Xna::Framework::Rectangle centerRegionFog(30, 30, 1, 1);
            dev.Clear(Color(10, 10, 10, 255));
            backend.DrawPrimitivesEx(*vbFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, fogOff);
            Color fogOffPixel(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionFog, &fogOffPixel, 0, 1);
            check(fogOffPixel.getRProperty() == 255 && fogOffPixel.getGProperty() == 0 &&
                  fogOffPixel.getBProperty() == 0,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): fogEnabled=false leaves colored3d's "
                  "exact vertex color unblended (plan_dx.md DX-69/DX-81)");

            GpuDrawParams fogOn = fogOff;
            fogOn.fogEnabled = true;

            dev.Clear(Color(10, 10, 10, 255));
            backend.DrawPrimitivesEx(*vbFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, fogOn);
            Color fogOnPixel(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionFog, &fogOnPixel, 0, 1);
            check(fogOnPixel.getRProperty() == 0 && fogOnPixel.getGProperty() == 255 &&
                  fogOnPixel.getBProperty() == 0,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): fogEnabled=true with Z at FogEnd "
                  "genuinely blends all the way to the exact FogColor (fogFactor=0), distinctly "
                  "different from the fogEnabled=false case above (plan_dx.md DX-69/DX-81)");
        }

        // plan_dx.md DX-140 (partial -- NPOT only): a genuinely non-power-of-two Texture2D (5x3),
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
            auto vbNpot = backend.CreateVertexBuffer(3);
            vbNpot->SetData(kTriNpot, 3, sizeof(VPT));

            ImageData npotImg;
            npotImg.width = 5; npotImg.height = 3; npotImg.mipLevels = 1;
            npotImg.pixels.resize(5 * 3 * 4);
            for (int i = 0; i < 5 * 3; ++i)
            {
                npotImg.pixels[i * 4 + 0] = 123; npotImg.pixels[i * 4 + 1] = 45;
                npotImg.pixels[i * 4 + 2] = 200; npotImg.pixels[i * 4 + 3] = 255;
            }
            auto npotTex = backend.CreateTexture(npotImg);
            check(npotTex->GetWidth() == 5 && npotTex->GetHeight() == 3,
                  "D3D11TextureBackend: real construction with a genuinely non-power-of-two 5x3 "
                  "size reports the exact requested dimensions (plan_dx.md DX-140)");

            GpuDrawParams npotP;
            npotP.texture0 = npotTex.get();
            npotP.textureEnabled = true;

            std::vector<Color> afterNpot(4 * 4, Color(0, 0, 0, 0));
            dev.Clear(Color(0, 255, 0, 255));
            backend.DrawPrimitivesEx(*vbNpot, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, npotP);
            dev.GetBackBufferData(&centerRegion28, afterNpot.data(), 0, static_cast<int>(afterNpot.size()));
            bool npotIsExact = true;
            for (const Color& p : afterNpot)
                if (p.getRProperty() != 123 || p.getGProperty() != 45 || p.getBProperty() != 200 || p.getAProperty() != 255)
                    npotIsExact = false;
            check(npotIsExact,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): samples the exact color from a real "
                  "5x3 NPOT texture upload (plan_dx.md DX-140)");
        }

        // plan_dx.md DX-142: all 16 D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT texture-sampler slots
        // bound SIMULTANEOUSLY with 16 genuinely DIFFERENT SamplerState configurations, then
        // verified independent -- D3D11SamplerCache (DX-44) already has an identity/distinctness
        // proof for two slots in isolation, but nothing has proven slot N's binding survives slots
        // N+1..15 also being applied (a plausible place for an off-by-one/index bug or an
        // accidental single-slot cache to hide).
        {
            using Microsoft::WRL::ComPtr;
            ID3D11DeviceContext* ctx = backend.GetContextEXT();

            ComPtr<ID3D11SamplerState> boundAtApplyTime[16];
            for (int slot = 0; slot < 16; ++slot)
            {
                // Spread across TextureFilter's 6 values and TextureAddressMode's 3 values so
                // adjacent slots never accidentally share an identical configuration.
                backend.ApplySamplerState(slot, slot % 6, slot % 3, (slot + 1) % 3, 1);
                ComPtr<ID3D11SamplerState> s;
                ctx->PSGetSamplers(static_cast<UINT>(slot), 1, s.GetAddressOf());
                boundAtApplyTime[slot] = s;
            }

            bool allSlotsNonNull = true;
            for (int slot = 0; slot < 16; ++slot)
                if (!boundAtApplyTime[slot]) allSlotsNonNull = false;
            check(allSlotsNonNull,
                  "D3D11SamplerCache: all 16 texture-sampler slots hold a real, non-null "
                  "ID3D11SamplerState immediately after being applied (plan_dx.md DX-142)");

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
                  "not a shared/aliased single slot (plan_dx.md DX-142)");
        }

        // plan_dx.md DX-147: OcclusionQuery visible-vs-occluded DISCRIMINATION for D3D11. Check M
        // (DX-47) above only proves a query object completes and reports 0 for an empty Begin()/
        // End() -- it never proved the count actually tracks what was rasterized. EasyGL/Vulkan
        // both verify both directions already (Tasks 445/446/854); D3D12's own half was closed by
        // DX-120 (Checks AA3/AA4). This closes D3D11's, using the same methodology: the SAME query
        // object is reused around a genuinely visible draw and a genuinely invisible one, so a
        // backend that simply returned a constant (or leaked the prior count) cannot pass both.
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

            auto vbVisible = backend.CreateVertexBuffer(3);
            vbVisible->SetData(kTriVisible, 3, sizeof(VPCq));
            auto vbOffscreen = backend.CreateVertexBuffer(3);
            vbOffscreen->SetData(kTriOffscreen, 3, sizeof(VPCq));

            GpuDrawParams qp;
            qp.vertexColorEnabled = true;

            auto oq2 = backend.CreateOcclusionQuery();

            dev.Clear(Color(10, 10, 10, 255));
            oq2->Begin();
            backend.DrawPrimitivesEx(*vbVisible, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, qp);
            oq2->End();
            context->Flush();
            bool done1 = false;
            for (int i = 0; i < 1000000 && !done1; ++i) done1 = oq2->IsComplete();
            const int visibleCount = oq2->PixelCount();
            check(done1 && visibleCount > 0,
                  "D3D11OcclusionQueryBackend: a genuinely VISIBLE (viewport-covering) draw reports a real, "
                  "POSITIVE PixelCount() -- the count actually tracks rasterized pixels, not a constant "
                  "(plan_dx.md DX-147)");

            // Same query object, reused -- an implementation that leaked the previous count or
            // returned a fixed value cannot pass this after passing the check above.
            dev.Clear(Color(10, 10, 10, 255));
            oq2->Begin();
            backend.DrawPrimitivesEx(*vbOffscreen, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, qp);
            oq2->End();
            context->Flush();
            bool done2 = false;
            for (int i = 0; i < 1000000 && !done2; ++i) done2 = oq2->IsComplete();
            check(done2 && oq2->PixelCount() == 0,
                  "D3D11OcclusionQueryBackend: the SAME query object, reused around fully off-screen "
                  "(clipped) geometry, reports EXACTLY 0 -- a genuine visible-vs-invisible DISCRIMINATING "
                  "result, not just 'the query completed' (plan_dx.md DX-147)");
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

            auto vbNear = backend.CreateVertexBuffer(3);
            vbNear->SetData(kNear, 3, sizeof(VPCd));
            auto vbFar = backend.CreateVertexBuffer(3);
            vbFar->SetData(kFar, 3, sizeof(VPCd));

            GpuDrawParams dp;
            dp.vertexColorEnabled = true;
            const Microsoft::Xna::Framework::Rectangle probe(30, 30, 1, 1);
            const Matrix& I = Matrix::getIdentityProperty();

            auto drawNearThenFar = [&]() {
                dev.Clear(Color(10, 10, 10, 255));
                backend.DrawPrimitivesEx(*vbNear, I, I, I, PrimitiveType::TriangleList, 1, dp);
                backend.DrawPrimitivesEx(*vbFar, I, I, I, PrimitiveType::TriangleList, 1, dp);
                Color px(0, 0, 0, 0);
                dev.GetBackBufferData(&probe, &px, 0, 1);
                return px;
            };

            // Depth test ON: the far green quad must be REJECTED by the nearer red one already there.
            backend.SetDepthWriteEnabled(true);
            backend.SetDepthTestEnabled(true);
            dev.Clear(Color(10, 10, 10, 255)); // clears depth to 1.0 too
            const Color withDepth = drawNearThenFar();
            check(withDepth.getRProperty() == 255 && withDepth.getGProperty() == 0,
                  "D3D11GraphicsBackend::SetDepthTestEnabled(true): a FARTHER quad drawn after a nearer "
                  "one is genuinely REJECTED (red survives) -- proves the call really enables the depth "
                  "test, instead of being the silent no-op it used to be");

            // Depth test OFF: the same far green quad must now overwrite the red one (painter's order).
            backend.SetDepthTestEnabled(false);
            const Color withoutDepth = drawNearThenFar();
            check(withoutDepth.getGProperty() == 255 && withoutDepth.getRProperty() == 0,
                  "D3D11GraphicsBackend::SetDepthTestEnabled(false): the SAME farther quad now genuinely "
                  "OVERWRITES the nearer one (green wins) -- only the SetDepthTestEnabled() call differs "
                  "between the two, so a no-op implementation cannot pass both checks");

            backend.SetDepthTestEnabled(false);
            backend.SetDepthWriteEnabled(false);
        }

        // plan_dx.md DX-124: multi-light (DirectionalLight1/DirectionalLight2) + EmissiveColor
        // discriminating pixel test for the shared D3DLightingConstants path (lit_textured3d).
        // Check R (DX-63) above only proves lit-vs-unlit differs; this proves each of Light1/
        // Light2/EmissiveColor independently contributes the EXACT expected color, mirroring
        // D3D12's own already-closed DX-138 (examples/d3d12_smoke_test.cpp Checks EE1-EE4)
        // methodology exactly, just against the real back buffer instead of an offscreen target.
        {
            struct VPNT { float x, y, z; float nx, ny, nz; float u, v; };
            static const VPNT kTriEE[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f},
                { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 1.0f},
                {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, -1.0f},
            };
            auto vbEE = backend.CreateVertexBuffer(3);
            vbEE->SetData(kTriEE, 3, sizeof(VPNT));

            ImageData whiteImg;
            whiteImg.width = 2; whiteImg.height = 2; whiteImg.mipLevels = 1;
            whiteImg.pixels.assign(2 * 2 * 4, 255);
            auto whiteTexEE = backend.CreateTexture(whiteImg);

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
            backend.DrawPrimitivesEx(*vbEE, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, p1);
            Color r1(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionEE, &r1, 0, 1);
            check(r1.getRProperty() == 255 && r1.getGProperty() == 0 && r1.getBProperty() == 0,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): real lit_textured3d -- DirectionalLight1 "
                  "alone contributes the exact expected red, independent of Light0/Light2 (plan_dx.md DX-124)");

            // EE2: same geometry/light1Dir, but light1Diffuse disabled -> confirms EE1 wasn't a leak.
            GpuDrawParams p1off = p1;
            p1off.light1Diffuse[0] = 0.0f; p1off.light1Diffuse[1] = 0.0f; p1off.light1Diffuse[2] = 0.0f;
            dev.Clear(Color(0, 0, 0, 255));
            backend.DrawPrimitivesEx(*vbEE, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, p1off);
            Color r1off(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionEE, &r1off, 0, 1);
            check(r1off.getRProperty() == 0 && r1off.getGProperty() == 0 && r1off.getBProperty() == 0,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): disabling DirectionalLight1's diffuse "
                  "(zeroed) removes its contribution exactly -- confirms the prior check was real, not "
                  "a leaked default (plan_dx.md DX-124)");

            // EE3: DirectionalLight2 alone, green diffuse -> exact (0,255,0).
            GpuDrawParams p2 = baseP;
            p2.light2Dir[0] = 0.0f; p2.light2Dir[1] = 0.0f; p2.light2Dir[2] = 1.0f;
            p2.light2Diffuse[0] = 0.0f; p2.light2Diffuse[1] = 1.0f; p2.light2Diffuse[2] = 0.0f;
            dev.Clear(Color(0, 0, 0, 255));
            backend.DrawPrimitivesEx(*vbEE, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, p2);
            Color r2(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionEE, &r2, 0, 1);
            check(r2.getRProperty() == 0 && r2.getGProperty() == 255 && r2.getBProperty() == 0,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): real lit_textured3d -- DirectionalLight2 "
                  "alone contributes the exact expected green, independent of Light0/Light1 (plan_dx.md DX-124)");

            // EE4: EmissiveColor alone (all lights + ambient off) -> exact (0,0,255), a constant,
            // light-independent additive term.
            GpuDrawParams p3 = baseP;
            p3.emissiveColor[0] = 0.0f; p3.emissiveColor[1] = 0.0f; p3.emissiveColor[2] = 1.0f;
            dev.Clear(Color(0, 0, 0, 255));
            backend.DrawPrimitivesEx(*vbEE, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, p3);
            Color r3(0, 0, 0, 0);
            dev.GetBackBufferData(&centerRegionEE, &r3, 0, 1);
            check(r3.getRProperty() == 0 && r3.getGProperty() == 0 && r3.getBProperty() == 255,
                  "D3D11GraphicsBackend::DrawPrimitivesEx(): real lit_textured3d -- EmissiveColor alone "
                  "contributes the exact expected blue with every light off, a constant additive term "
                  "(plan_dx.md DX-124)");
        }

        const int totalChecks = 2 /* SetDepthTestEnabled real, not a no-op */
                                + 3 + 10 + 1 + 2 + 2 + 2 + 2 + 2 + 1 + 1 + 2 + 1 + 13 + 2 + 3 + 2 + 2 + 1 + 1 + 1 + 1 + 3 + 2 + 2 + 2 + 3 + 2 + 3 /* DX-131 rotation/scale/crop */
                                + 1 /* DX-134 envMapAmount=0 */ + 2 /* DX-135 WeightsPerVertex */ + 2 /* DX-137 textured3d fog */
                                + 2 /* DX-140 NPOT */ + 2 /* DX-142 all-16-sampler-slots */ + 2 /* DX-143 MRT MSAA resolve */
                                + 4 /* DX-144 RT2D+RTCube mip-chain generation */
                                + 4 /* DX-145 DepthStencilFormat fidelity */
                                + 2 /* DX-147 occlusion query visible-vs-occluded */
                                + 4 /* DX-124 multi-light + EmissiveColor discrimination */;
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
