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
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11Buffers.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11Textures.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11RenderTargets.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11SamplerCache.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11OcclusionQuery.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11StateObjectCache.hpp"
#include "CNA/Internal/Backends/D3DCommon/D3DShaderCache.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"

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

        const int totalChecks = 3 + 10 + 1 + 2 + 2 + 2 + 2 + 2 + 1 + 1 + 2 + 1 + 13 + 2 + 3 + 2 + 2;
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
