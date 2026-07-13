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

        const int totalChecks = 3 + 10 + 1 + 2 + 2 + 2 + 2 + 2 + 1 + 1 + 2 + 1;
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
