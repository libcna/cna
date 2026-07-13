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
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include "CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11Buffers.hpp"
#include "CNA/Internal/Backends/D3DCommon/D3DShaderCache.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::D3D11;
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

        const int totalChecks = 3 + 10 + 1 + 2 + 2;
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
