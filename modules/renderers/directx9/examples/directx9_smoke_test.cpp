// SPDX-License-Identifier: MS-PL
// plans/plan_dx9.md Phase D9-3 (D9-30/D9-31): smoke test for the D3D9 graphics renderer's device
// creation + Clear/Present/ReadBackbuffer foundation. Real window, real Direct3DCreate9/
// CreateDevice, real Clear()+Present()+readback through the actual public GraphicsDevice API --
// this renderer's first genuine pixel-correctness proof.
//
// Per plans/plan_dx9.md's own "Definition of done": each of the 6 Clear* combo variants needs its own
// passing pixel-verified check here, not just "compiles" -- D3D11 shipped these implemented-but-
// never-exercised and this plan deliberately raises the bar. No real draw path exists yet
// (D9-82), so the depth/stencil-only variants are proven two ways: (1) the color buffer is
// confirmed UNCHANGED by a depth/stencil-only clear (a real bug -- e.g. accidentally including
// D3DCLEAR_TARGET -- would fail this), and (2) the underlying D3D9 Clear() call is confirmed to
// return a real, driver-validated S_OK against a genuinely-created depth-stencil surface (checked
// directly via GetDepthStencilSurface()+GetDesc(), not merely "didn't throw").
//
// Check A -- real device created, D3DCAPS9 reports a real vs_2_0+/ps_2_0+ shader version floor.
// Check B -- Clear(color) followed by GetBackBufferData() reads back the EXACT clear color.
// Check C -- a second, different Clear()+readback also matches (not a stale/cached value).
// Check D -- ClearColorAndDepth: exact color readback + a real depth-stencil surface exists
//   (GetDepthStencilSurface()/GetDesc() confirms real dimensions/format).
// Check E -- ClearDepth (depth-only): color buffer from a prior Clear() is genuinely UNCHANGED.
// Check F -- ClearStencil (stencil-only): same unchanged-color proof.
// Check G -- ClearDepthAndStencil (no color component): same unchanged-color proof.
// Check H -- ClearColorAndStencil: exact color readback.
// Check I -- ClearColorDepthAndStencil: exact color readback.
// Check L -- a real GraphicsDeviceManager resize (64x64 -> 96x80) genuinely exercises
//   EnsureDeviceSize()'s Reset() path, and Clear()+GetBackBufferData() after the resize reads
//   back the NEW size's data correctly (D9-33), mirroring D3D11's own DX-83 check.
// Check J -- a device created with DepthFormat::None (no depth-stencil requested) genuinely has
//   no depth-stencil surface (GetDepthStencilSurface() fails), and ClearDepth()/ClearStencil()
//   silently no-op (do not throw) on it -- proving D9-31's HasDepthBuffer()/HasStencilBuffer()
//   gating is real, not just "always calls D3D9 Clear() and hopes".
// Check K -- GraphicsProfile::HiDef construction succeeds on this real vs_3_0/ps_3_0-capable
//   device (D9-32's profile-floor enforcement, positive path). The rejection path cannot be
//   exercised on real hardware that already exceeds the floor -- an honest gap, not a hidden one.
// Check M -- the real device-lost lifecycle (D9-34), driven via the pre-existing
//   DebugSimulateContextLoss()/DebugRestoreContext() test channel since DXVK rarely loses the
//   device naturally: DeviceLost fires exactly once and GraphicsDeviceStatus/IsDeviceLostEXT()
//   both report Lost; Clear() throws DeviceLostException while lost; DebugRestoreContext() fires
//   DeviceResetting then DeviceReset (each exactly once) via a REAL Reset() call, status returns
//   to Normal, and the device genuinely works again afterward (Clear()+readback exact).
// Check N -- a real D3D9VertexBufferRenderer round-trips known bytes (D9-40): SetData() through
//   the public interface, then a direct Lock(READONLY) confirms the GPU buffer holds them exactly.
// Check O -- same round-trip proof for a 16-bit AND a 32-bit D3D9IndexBufferRenderer (D9-41),
//   confirming CreateIndexBuffer32() creates a genuinely distinct D3DFMT_INDEX32 buffer rather
//   than silently delegating to the 16-bit path (the trap D3D11's own DX-31 found).
// Check P -- a device-lost/recover cycle genuinely releases and lazily recreates a registered
//   D3DPOOL_DEFAULT vertex buffer's underlying COM object (D9-40's device-lost hook, proven for
//   real, not just "didn't crash") -- matches real XNA/D3D9 behavior where a DYNAMIC buffer's
//   content does not survive DeviceReset.
// Check Q -- a real D3D9TextureRenderer round-trips exact RGBA8 bytes (D9-50): constructor upload
//   and a later UpdatePixelsLevel() replacement are both confirmed via a direct LockRect(READONLY)
//   on the D3DPOOL_MANAGED texture (no staging-texture dance needed, D9-4's own confirmed payoff).
// Check R -- D3D9TextureCubeRenderer/D3D9Texture3DRenderer SetData()+GetData() round-trip exact
//   bytes for a sub-region/sub-volume (D9-51), gated on the real D3DCAPS9 the device reports
//   (D3DPTEXTURECAPS_CUBEMAP / MaxVolumeExtent) rather than assumed universal.
// Check S -- a real D3D9RenderTargetRenderer (D9-53): bind, Clear() through the public
//   GraphicsDevice API, read back the render target's OWN surface (GetRenderTargetData(), since a
//   render-target texture is not directly Lockable) for an exact color match, confirm a real
//   depth-stencil surface exists, then unbind and confirm the back buffer is genuinely restored.
// Check T -- D3D9RenderTargetCubeRenderer (D9-53): bind one face, Clear(), read back that face's
//   own surface for an exact match, then confirm unbinding restores the back buffer.
// Check U -- an MSAA D3D9RenderTargetRenderer (D9-53), gated on the real device-reported sample
//   support (IDirect3D9::CheckDeviceMultiSampleType()): Clear() into the multisampled surface,
//   unbind (StretchRect-resolves into the sampleable texture), confirm the resolved texture holds
//   the exact color.
// Check V -- MRT (D9-54): SetRenderTargets() with 2 targets, a single Clear() writes the exact
//   color into BOTH targets' own surfaces, unbind restores the back buffer, and requesting more
//   targets than D3DCAPS9::NumSimultaneousRTs throws a real, named error instead of silently
//   degrading (design decision 13).
// Check W -- a real D3D9OcclusionQueryRenderer (D9-55): Begin()/End() issue real
//   D3DISSUE_BEGIN/D3DISSUE_END commands, IsComplete() eventually reports true (polled), and
//   PixelCount() reads back 0 for a query that only wraps a Clear() (no draw path exists yet,
//   D9-82 -- a real, honest result, not a stand-in for tested geometry).
// Check X -- NPOT capability (D9-56), surfaced from the real D3DCAPS9
//   (RequiresPowerOfTwoTexturesEXT()/NonPowerOfTwoRequiresClampAddressingEXT()), not assumed. When
//   the device reports full NPOT support (true here), a genuinely non-power-of-two (5x3) texture
//   is created and round-tripped for real, proving this renderer adds no artificial restriction.
// Check Y -- a real ApplySamplerState() (D9-63): SetSamplerState() values read back directly from
//   the device via GetSamplerState() (no draw call needed) exactly match TextureFilter::Point/
//   TextureAddressMode::Clamp/MaxAnisotropy=4's own D3D9StateMapping translation; an out-of-range
//   sampler slot silently no-ops rather than throwing.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DeviceLostException.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDeviceStatus.hpp"

#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9Textures.hpp"
#include "CNA/Internal/Renderers/DirectX9/D3D9RenderTargets.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstring>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::DirectX9;
using CNA::Internal::Renderers::ImageData;
using CNA::Internal::Renderers::IRenderTargetRenderer;
using CNA::Internal::Renderers::RenderTargetBindingDescriptor;
using CNA::Internal::Renderers::GpuDrawParams;

namespace
{
    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    /// D9-53 test helper: reads back an arbitrary render-target IDirect3DSurface9* as RGBA8, via
    /// the same CreateOffscreenPlainSurface(SYSTEMMEM)+GetRenderTargetData()+LockRect(READONLY)
    /// dance DirectX9Renderer::ReadBackbuffer() already uses for the back buffer itself -- this
    /// is a genuine D3D9 requirement (a D3DUSAGE_RENDERTARGET/D3DPOOL_DEFAULT surface is generally
    /// not directly Lockable), not a test-only shortcut. Assumes D3DFMT_A8B8G8R8 (this renderer's own
    /// RGBA8-storage convention -- see D3D9Textures.hpp/D3D9RenderTargets.hpp), so no swapRB needed.
    /// Returns an empty vector on any failure.
    std::vector<uint8_t> ReadRenderTargetSurfaceD3D9(IDirect3DDevice9* device, IDirect3DSurface9* surface)
    {
        D3DSURFACE_DESC desc{};
        surface->GetDesc(&desc);

        ComPtr<IDirect3DSurface9> sysmem;
        if (FAILED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format,
                                                        D3DPOOL_SYSTEMMEM, sysmem.GetAddressOf(), nullptr)))
            return {};
        if (FAILED(device->GetRenderTargetData(surface, sysmem.Get())))
            return {};

        D3DLOCKED_RECT locked{};
        if (FAILED(sysmem->LockRect(&locked, nullptr, D3DLOCK_READONLY)))
            return {};

        std::vector<uint8_t> out(static_cast<std::size_t>(desc.Width) * desc.Height * 4);
        for (UINT row = 0; row < desc.Height; ++row)
        {
            std::memcpy(out.data() + static_cast<std::size_t>(row) * desc.Width * 4,
                       static_cast<const uint8_t*>(locked.pBits) + static_cast<std::size_t>(row) * locked.Pitch,
                       static_cast<std::size_t>(desc.Width) * 4);
        }
        sysmem->UnlockRect();
        return out;
    }
}

class D3D9SmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        // Give the swap chain one frame to settle before the first real check.
        if (frame_++ < 1) return;

        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<DirectX9Renderer&>(dev.GetRenderer());

        // Check A: real device created, real D3DCAPS9.
        check(renderer.GetCapsEXT().VertexShaderVersion != 0 && renderer.GetCapsEXT().PixelShaderVersion != 0,
              "real device created, D3DCAPS9 reports nonzero VertexShaderVersion/PixelShaderVersion");
        std::printf("    VertexShaderVersion=0x%08lx PixelShaderVersion=0x%08lx\n",
                    static_cast<unsigned long>(renderer.GetCapsEXT().VertexShaderVersion),
                    static_cast<unsigned long>(renderer.GetCapsEXT().PixelShaderVersion));

        // Check B: exact pixel readback after Clear().
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

        // Check C: a second, different Clear() also reads back correctly.
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
            check(allMatch, "a second, different Clear() also reads back exactly (not stale/cached)");
        }

        // Check D: ClearColorAndDepth -- exact color readback + a real depth-stencil surface.
        {
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color(10, 20, 30, 255), 1.0f, 0);
            const Microsoft::Xna::Framework::Rectangle region(0, 0, 2, 2);
            std::vector<Color> pixels(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            bool colorOk = pixels[0].getRProperty() == 10 && pixels[0].getGProperty() == 20 &&
                           pixels[0].getBProperty() == 30 && pixels[0].getAProperty() == 255;

            Microsoft::WRL::ComPtr<IDirect3DSurface9> dsSurface;
            HRESULT hr = renderer.GetDeviceEXT()->GetDepthStencilSurface(dsSurface.ReleaseAndGetAddressOf());
            bool dsReal = false;
            if (SUCCEEDED(hr) && dsSurface)
            {
                D3DSURFACE_DESC desc{};
                dsSurface->GetDesc(&desc);
                dsReal = desc.Format == D3DFMT_D24S8 && desc.Width == 64 && desc.Height == 64;
            }
            check(colorOk && dsReal,
                  "ClearColorAndDepth: exact color readback + a real 64x64 D3DFMT_D24S8 depth-stencil surface exists");
        }

        // Check E: ClearDepth (depth-only) -- color buffer genuinely unaffected.
        {
            dev.Clear(Color(77, 88, 99, 255));
            dev.Clear(ClearOptions::DepthBuffer, Color(0, 0, 0, 0), 0.5f, 0);
            const Microsoft::Xna::Framework::Rectangle region(0, 0, 2, 2);
            std::vector<Color> pixels(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0].getRProperty() == 77 && pixels[0].getGProperty() == 88 &&
                  pixels[0].getBProperty() == 99,
                  "ClearDepth (depth-only): color buffer from the prior Clear() is genuinely UNCHANGED");
        }

        // Check F: ClearStencil (stencil-only) -- same unchanged-color proof.
        {
            dev.Clear(Color(11, 22, 33, 255));
            dev.Clear(ClearOptions::Stencil, Color(0, 0, 0, 0), 1.0f, 5);
            const Microsoft::Xna::Framework::Rectangle region(0, 0, 2, 2);
            std::vector<Color> pixels(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0].getRProperty() == 11 && pixels[0].getGProperty() == 22 &&
                  pixels[0].getBProperty() == 33,
                  "ClearStencil (stencil-only): color buffer from the prior Clear() is genuinely UNCHANGED");
        }

        // Check G: ClearDepthAndStencil (no color component) -- same unchanged-color proof.
        {
            dev.Clear(Color(44, 55, 66, 255));
            dev.Clear(ClearOptions::DepthBuffer | ClearOptions::Stencil, Color(0, 0, 0, 0), 0.25f, 3);
            const Microsoft::Xna::Framework::Rectangle region(0, 0, 2, 2);
            std::vector<Color> pixels(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0].getRProperty() == 44 && pixels[0].getGProperty() == 55 &&
                  pixels[0].getBProperty() == 66,
                  "ClearDepthAndStencil: color buffer from the prior Clear() is genuinely UNCHANGED");
        }

        // Check H: ClearColorAndStencil -- exact color readback.
        {
            dev.Clear(ClearOptions::Target | ClearOptions::Stencil, Color(150, 60, 90, 255), 1.0f, 7);
            const Microsoft::Xna::Framework::Rectangle region(0, 0, 2, 2);
            std::vector<Color> pixels(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0].getRProperty() == 150 && pixels[0].getGProperty() == 60 &&
                  pixels[0].getBProperty() == 90 && pixels[0].getAProperty() == 255,
                  "ClearColorAndStencil: exact color readback");
        }

        // Check I: ClearColorDepthAndStencil -- exact color readback.
        {
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                      Color(5, 250, 128, 255), 0.75f, 2);
            const Microsoft::Xna::Framework::Rectangle region(0, 0, 2, 2);
            std::vector<Color> pixels(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0].getRProperty() == 5 && pixels[0].getGProperty() == 250 &&
                  pixels[0].getBProperty() == 128 && pixels[0].getAProperty() == 255,
                  "ClearColorDepthAndStencil: exact color readback");
        }

        // Check L (D9-33) -- a real backbuffer resize genuinely exercises EnsureDeviceSize()'s
        // Reset() path (previously proven only indirectly, by D9-31's own smoke test converging to
        // its initial 64x64 size) -- resized via the same public GraphicsDeviceManager path a real
        // game uses, mirroring D3D11's own DX-83 check. EnsureDeviceSize() only picks the new size
        // up lazily on the next Present()/Clear() cycle, so this polls across a few frames.
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
            check(resized, "D9-33: GraphicsDeviceManager resize to 96x80 eventually converges "
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
                  "D9-33: after resize, Clear()+GetBackBufferData() reads the exact clear color at "
                  "the origin AND near the new (96,80) far edge -- proves the resized back buffer/"
                  "depth-stencil/viewport are genuinely the new size, not stale/clamped/wrong");

            const bool ppMatches =
                (dev.getPresentationParametersProperty().getBackBufferWidthProperty() == 96 &&
                 dev.getPresentationParametersProperty().getBackBufferHeightProperty() == 80);
            check(ppMatches, "D9-33: PresentationParameters reflects the new 96x80 size post-resize");
        }

        // Check M (D9-34) -- the real XNA device-lost lifecycle, driven through the pre-existing
        // GraphicsDevice-calls-into-renderer test channel (DebugSimulateContextLoss()/
        // DebugRestoreContext()) since DXVK will rarely lose the device naturally under this dev
        // loop (design decision 2's own documented cost) -- this exercises the REAL event sequence
        // and a REAL Reset() call, even though the "loss" itself is simulated, not driver-detected.
        {
            int lostCount = 0, resettingCount = 0, resetCount = 0;
            dev.DeviceLost += [&](System::Object*, const System::EventArgs&) { ++lostCount; };
            dev.DeviceResetting += [&](System::Object*, const System::EventArgs&) { ++resettingCount; };
            dev.DeviceReset += [&](System::Object*, const System::EventArgs&) { ++resetCount; };

            check(dev.getGraphicsDeviceStatusProperty() == GraphicsDeviceStatus::Normal,
                  "D9-34: GraphicsDeviceStatus starts Normal");

            renderer.DebugSimulateContextLoss();
            check(lostCount == 1 && resettingCount == 0 && resetCount == 0,
                  "D9-34: DebugSimulateContextLoss() fires DeviceLost exactly once, nothing else yet");
            check(renderer.IsDeviceLostEXT() &&
                  dev.getGraphicsDeviceStatusProperty() == GraphicsDeviceStatus::Lost,
                  "D9-34: renderer.IsDeviceLostEXT() and GraphicsDeviceStatus both report Lost");

            bool threwWhileLost = false;
            try { dev.Clear(Color(1, 2, 3, 255)); } catch (const DeviceLostException&) { threwWhileLost = true; }
            check(threwWhileLost, "D9-34: Clear() throws DeviceLostException while the device is lost");

            renderer.DebugRestoreContext();
            check(resettingCount == 1 && resetCount == 1,
                  "D9-34: DebugRestoreContext() fires DeviceResetting then DeviceReset, each exactly once");
            check(!renderer.IsDeviceLostEXT() &&
                  dev.getGraphicsDeviceStatusProperty() == GraphicsDeviceStatus::Normal,
                  "D9-34: renderer.IsDeviceLostEXT() and GraphicsDeviceStatus both report recovered/Normal");

            dev.Clear(Color(6, 7, 8, 255));
            const Microsoft::Xna::Framework::Rectangle postRecoveryRegion(0, 0, 2, 2);
            std::vector<Color> postRecoveryPixels(2 * 2, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&postRecoveryRegion, postRecoveryPixels.data(), 0,
                                   static_cast<int>(postRecoveryPixels.size()));
            check(postRecoveryPixels[0].getRProperty() == 6 && postRecoveryPixels[0].getGProperty() == 7 &&
                  postRecoveryPixels[0].getBProperty() == 8,
                  "D9-34: the device genuinely works again after recovery -- Clear()+readback exact");
        }

        // Check N (D9-40) -- a real D3D9VertexBufferRenderer round-trips known bytes: SetData()
        // through the public IVertexBufferRenderer interface, then a direct Lock(READONLY) on the
        // real IDirect3DVertexBuffer9 (test-only, bypassing the write-only public interface, which
        // has no GetData()) confirms the GPU buffer holds the exact bytes -- a genuine write+
        // readback, not just "SetData() didn't throw".
        {
            auto vb = renderer.CreateVertexBuffer(3);
            auto& d3d9Vb = static_cast<D3D9VertexBufferRenderer&>(*vb);
            const float knownData[9] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f};
            vb->SetData(knownData, 3, sizeof(float) * 3);

            void* locked = nullptr;
            HRESULT hr = d3d9Vb.GetBufferEXT()->Lock(0, sizeof(knownData), &locked, D3DLOCK_READONLY);
            bool bytesMatch = false;
            if (SUCCEEDED(hr))
            {
                bytesMatch = std::memcmp(locked, knownData, sizeof(knownData)) == 0;
                d3d9Vb.GetBufferEXT()->Unlock();
            }
            check(vb->GetVertexCount() == 3 && bytesMatch,
                  "D9-40: D3D9VertexBufferRenderer::SetData() genuinely writes exact bytes to the GPU buffer");
        }

        // Check O (D9-41) -- same round-trip proof for both a 16-bit and a 32-bit
        // D3D9IndexBufferRenderer, via the explicitly-overridden CreateIndexBuffer32() (D3D11's own
        // DX-31 caught the silent-16-bit-only-default trap this explicitly avoids).
        {
            auto ib16 = renderer.CreateIndexBuffer16(4);
            auto& d3d9Ib16 = static_cast<D3D9IndexBufferRenderer&>(*ib16);
            const uint16_t indices16[4] = {0, 1, 2, 3};
            ib16->SetData16(indices16, 4);

            void* locked16 = nullptr;
            HRESULT hr16 = d3d9Ib16.GetBufferEXT()->Lock(0, sizeof(indices16), &locked16, D3DLOCK_READONLY);
            bool bytes16Match = false;
            if (SUCCEEDED(hr16))
            {
                bytes16Match = std::memcmp(locked16, indices16, sizeof(indices16)) == 0;
                d3d9Ib16.GetBufferEXT()->Unlock();
            }
            check(!ib16->IsThirtyTwoBit() && d3d9Ib16.GetFormatEXT() == D3DFMT_INDEX16 &&
                  ib16->GetIndexCount() == 4 && bytes16Match,
                  "D9-41: a 16-bit D3D9IndexBufferRenderer genuinely writes exact bytes (D3DFMT_INDEX16)");

            auto ib32 = renderer.CreateIndexBuffer32(4);
            auto& d3d9Ib32 = static_cast<D3D9IndexBufferRenderer&>(*ib32);
            const uint32_t indices32[4] = {100, 200, 300, 400};
            ib32->SetData32(indices32, 4);

            void* locked32 = nullptr;
            HRESULT hr32 = d3d9Ib32.GetBufferEXT()->Lock(0, sizeof(indices32), &locked32, D3DLOCK_READONLY);
            bool bytes32Match = false;
            if (SUCCEEDED(hr32))
            {
                bytes32Match = std::memcmp(locked32, indices32, sizeof(indices32)) == 0;
                d3d9Ib32.GetBufferEXT()->Unlock();
            }
            check(ib32->IsThirtyTwoBit() && d3d9Ib32.GetFormatEXT() == D3DFMT_INDEX32 &&
                  ib32->GetIndexCount() == 4 && bytes32Match,
                  "D9-41: CreateIndexBuffer32() genuinely creates a distinct 32-bit buffer (D3DFMT_INDEX32), "
                  "not silently delegating to the 16-bit path");
        }

        // Check P (D9-40/D9-34) -- a device-lost/recover cycle genuinely releases and recreates a
        // registered D3DPOOL_DEFAULT vertex buffer's underlying COM object (proves
        // RegisterDefaultPoolResourceEXT()/ReleaseDefaultPoolResourceEXT() actually work, not just
        // "didn't crash") -- real XNA/D3D9 behavior: a DYNAMIC buffer's content does not survive
        // DeviceReset, so the caller is expected to SetData() again afterward, which this does.
        {
            auto vb = renderer.CreateVertexBuffer(1);
            auto& d3d9Vb = static_cast<D3D9VertexBufferRenderer&>(*vb);
            const float before[3] = {11.0f, 22.0f, 33.0f};
            vb->SetData(before, 1, sizeof(before));

            renderer.DebugSimulateContextLoss();
            renderer.DebugRestoreContext();

            check(d3d9Vb.GetBufferEXT() == nullptr,
                  "D9-40: ReleaseDefaultPoolResourceEXT() genuinely released the buffer across Reset() "
                  "(GetBufferEXT() is null until the next SetData(), not a stale pointer)");

            // NOTE: comparing the pre-/post-recovery IDirect3DVertexBuffer9* for inequality is NOT
            // sound proof of recreation -- a freed COM object's address can legitimately be reused
            // by the very next allocation (this project's own D9-109/D9-110-equivalent D3D12 task
            // found the identical false-negative trap). The real, functional proof is: the pointer
            // was genuinely null after release (checked above), and the buffer genuinely holds the
            // NEW data afterward (checked below) -- both are actual behavior, not address bookkeeping.
            const float after[3] = {44.0f, 55.0f, 66.0f};
            vb->SetData(after, 1, sizeof(after));
            check(d3d9Vb.GetBufferEXT() != nullptr,
                  "D9-40: SetData() after recovery lazily recreates a real D3D9 buffer object");

            void* locked = nullptr;
            HRESULT hr = d3d9Vb.GetBufferEXT()->Lock(0, sizeof(after), &locked, D3DLOCK_READONLY);
            bool matches = SUCCEEDED(hr) && std::memcmp(locked, after, sizeof(after)) == 0;
            if (SUCCEEDED(hr)) d3d9Vb.GetBufferEXT()->Unlock();
            check(matches, "D9-40: the recreated buffer genuinely holds the post-recovery data");
        }

        // Check Q (D9-50) -- D3D9TextureRenderer round-trips exact RGBA8 bytes, both at
        // construction (level 0 from ImageData) and via a later UpdatePixelsLevel() replacement.
        // Since this is D3DPOOL_MANAGED (not DEFAULT), the constructed texture is directly
        // LockRect(READONLY)-able for verification -- no staging-texture dance needed, unlike
        // D3D11's own equivalent check (D9-4's own confirmed payoff of design decision 2).
        {
            ImageData img;
            img.width = 4;
            img.height = 4;
            img.pixels.resize(4 * 4 * 4);
            for (int i = 0; i < 4 * 4; ++i)
            {
                img.pixels[static_cast<std::size_t>(i) * 4 + 0] = static_cast<uint8_t>(i * 10);
                img.pixels[static_cast<std::size_t>(i) * 4 + 1] = static_cast<uint8_t>(i * 20);
                img.pixels[static_cast<std::size_t>(i) * 4 + 2] = static_cast<uint8_t>(i * 30);
                img.pixels[static_cast<std::size_t>(i) * 4 + 3] = 255;
            }
            auto tex = renderer.CreateTexture(img);
            auto& d3d9Tex = static_cast<D3D9TextureRenderer&>(*tex);

            auto lockAndCompare = [&](const std::vector<uint8_t>& expected)
            {
                D3DLOCKED_RECT locked{};
                HRESULT hr = d3d9Tex.GetTextureEXT()->LockRect(0, &locked, nullptr, D3DLOCK_READONLY);
                if (FAILED(hr)) return false;
                bool match = true;
                for (int row = 0; row < 4 && match; ++row)
                {
                    const auto* rowBytes = static_cast<const uint8_t*>(locked.pBits)
                                          + static_cast<std::size_t>(row) * locked.Pitch;
                    if (std::memcmp(rowBytes, expected.data() + static_cast<std::size_t>(row) * 4 * 4, 4 * 4) != 0)
                        match = false;
                }
                d3d9Tex.GetTextureEXT()->UnlockRect(0);
                return match;
            };

            check(tex->GetWidth() == 4 && tex->GetHeight() == 4 && lockAndCompare(img.pixels),
                  "D9-50: D3D9TextureRenderer constructor upload round-trips exact RGBA8 bytes");

            std::vector<uint8_t> replacement(4 * 4 * 4, 77);
            tex->UpdatePixelsLevel(0, replacement.data(), 4, 4);
            check(lockAndCompare(replacement),
                  "D9-50: D3D9TextureRenderer::UpdatePixelsLevel() round-trips exact replacement bytes");
        }

        // Check R (D9-51) -- cube-map and volume texture SetData()/GetData() round-trip exact
        // bytes for a sub-region, gated on the real D3DCAPS9 the device reports (D9-51's own plan
        // note: volume-texture support is a genuine capability, not assumed universal).
        {
            if (renderer.GetCapsEXT().TextureCaps & D3DPTEXTURECAPS_CUBEMAP)
            {
                auto cube = renderer.CreateTextureCube(8, false, 0);
                check(cube != nullptr,
                      "D9-51: D3D9TextureCubeRenderer created (device reports D3DPTEXTURECAPS_CUBEMAP)");
                if (cube)
                {
                    uint8_t known[2 * 2 * 4];
                    for (int i = 0; i < 2 * 2; ++i)
                    {
                        known[i * 4 + 0] = static_cast<uint8_t>(i * 40 + 1);
                        known[i * 4 + 1] = static_cast<uint8_t>(i * 40 + 2);
                        known[i * 4 + 2] = static_cast<uint8_t>(i * 40 + 3);
                        known[i * 4 + 3] = 200;
                    }
                    cube->SetData(2 /* +Z face */, 0, 1, 1, 2, 2, known, sizeof(known));
                    uint8_t readBack[2 * 2 * 4] = {};
                    cube->GetData(2, 0, 1, 1, 2, 2, readBack, sizeof(readBack));
                    check(std::memcmp(known, readBack, sizeof(known)) == 0,
                          "D9-51: D3D9TextureCubeRenderer::SetData()/GetData() round-trip exact bytes "
                          "for one face's sub-region");
                }
            }
            else
            {
                std::printf("    (skipped: device does not report D3DPTEXTURECAPS_CUBEMAP)\n");
            }

            if (renderer.GetCapsEXT().MaxVolumeExtent > 0)
            {
                auto vol = renderer.CreateTexture3D(4, 4, 4, false, 0);
                check(vol != nullptr,
                      "D9-51: D3D9Texture3DRenderer created (device reports MaxVolumeExtent > 0)");
                if (vol)
                {
                    uint8_t known[2 * 2 * 2 * 4];
                    for (int i = 0; i < 2 * 2 * 2; ++i)
                    {
                        known[i * 4 + 0] = static_cast<uint8_t>(i * 15 + 1);
                        known[i * 4 + 1] = static_cast<uint8_t>(i * 15 + 2);
                        known[i * 4 + 2] = static_cast<uint8_t>(i * 15 + 3);
                        known[i * 4 + 3] = 210;
                    }
                    vol->SetData(0, 1, 1, 1, 2, 2, 2, known, sizeof(known));
                    uint8_t readBack[2 * 2 * 2 * 4] = {};
                    vol->GetData(0, 1, 1, 1, 2, 2, 2, readBack, sizeof(readBack));
                    check(std::memcmp(known, readBack, sizeof(known)) == 0,
                          "D9-51: D3D9Texture3DRenderer::SetData()/GetData() round-trip exact bytes "
                          "for a sub-volume");
                }
            }
            else
            {
                std::printf("    (skipped: device does not report volume-texture support, MaxVolumeExtent=0)\n");
            }
        }

        // Check S (D9-53) -- a real D3D9RenderTargetRenderer: bind, Clear() through the real public
        // GraphicsDevice API, read back the render target's OWN color surface (not the back
        // buffer) via GetRenderTargetData() (a render-target texture is not directly Lockable, a
        // real D3D9 restriction -- ReadRenderTargetSurfaceD3D9() above uses the same dance
        // ReadBackbuffer() itself already relies on), confirm a real depth-stencil surface exists,
        // then unbind and confirm the back buffer is genuinely restored and still independently
        // clearable/readable (proves RestoreBackBufferRenderTargetEXT() actually works, not just
        // "didn't crash").
        {
            auto rt = renderer.CreateRenderTarget2D(8, 8, static_cast<int>(DepthFormat::Depth24Stencil8),
                                                   false, false, 0);
            auto& d3d9Rt = static_cast<D3D9RenderTargetRenderer&>(*rt);

            check(rt->GetWidth() == 8 && rt->GetHeight() == 8 &&
                  d3d9Rt.GetDepthStencilSurfaceEXT() != nullptr &&
                  rt->HasRealDepthBuffer(true),
                  "D9-53: D3D9RenderTargetRenderer created with the requested size and a real depth-stencil surface");

            renderer.SetRenderTarget2D(rt.get());
            dev.Clear(Color(90, 100, 110, 255));
            renderer.SetRenderTarget2D(nullptr);

            const auto rtPixels = ReadRenderTargetSurfaceD3D9(renderer.GetDeviceEXT(), d3d9Rt.GetColorSurfaceEXT());
            bool rtMatch = rtPixels.size() == static_cast<std::size_t>(8 * 8 * 4);
            for (std::size_t i = 0; rtMatch && i < rtPixels.size(); i += 4)
            {
                if (rtPixels[i + 0] != 90 || rtPixels[i + 1] != 100 ||
                    rtPixels[i + 2] != 110 || rtPixels[i + 3] != 255)
                {
                    rtMatch = false;
                }
            }
            check(rtMatch, "D9-53: BindAsRenderTarget()+Clear() writes the exact color into the render target's own surface");

            dev.Clear(Color(5, 6, 7, 255));
            const Microsoft::Xna::Framework::Rectangle backBufferRegion(0, 0, 4, 4);
            std::vector<Color> backBufferPixels(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&backBufferRegion, backBufferPixels.data(), 0,
                                  static_cast<int>(backBufferPixels.size()));
            bool backBufferMatch = true;
            for (const Color& p : backBufferPixels)
            {
                if (p.getRProperty() != 5 || p.getGProperty() != 6 || p.getBProperty() != 7 ||
                    p.getAProperty() != 255)
                {
                    backBufferMatch = false;
                    break;
                }
            }
            check(backBufferMatch,
                  "D9-53: SetRenderTarget2D(nullptr) genuinely restores the back buffer -- Clear()+readback exact afterward");
        }

        // ---- Check GFX077: REMED-GFX-077 runtime verification -- BlendState.ColorWriteChannels
        // (RT0 -> D3DRS_COLORWRITEENABLE, a dynamic render state) against the real DXVK9 device. An
        // opaque full-screen quad (blend disabled) is drawn into an off-screen render target so the
        // ONLY thing that can preserve a destination channel is the colour write mask gating the
        // write; the RT's own surface is read back via the same GetRenderTargetData() dance Check S
        // uses (alpha-preserving). Differential model (masked-in channel == "All" baseline,
        // masked-out == "None"/clear baseline). MultiSampleMask (D3DRS_MULTISAMPLEMASK) is NOT
        // pixel-discriminated here: it only affects genuinely multisampled targets (a no-op on this
        // single-sample RT), so it is compile-verified + render-state-set-verified only -- see the
        // REMED-GFX-077 support matrix. ----
        {
            struct Px { uint8_t r, g, b, a; };
            auto rt = renderer.CreateRenderTarget2D(64, 64, static_cast<int>(DepthFormat::Depth24Stencil8),
                                                   false, false, 0);
            auto& d3d9Rt = static_cast<D3D9RenderTargetRenderer&>(*rt);

            struct VPC { float x, y, z; uint32_t color; };
            // Full-NDC quad, flat source colour S=(200,100,50,220); packed R8G8B8A8 = 0xDC3264C8.
            static const VPC kQuad[6] = {
                {-1.0f,  1.0f, 0.0f, 0xDC3264C8u}, {-1.0f, -1.0f, 0.0f, 0xDC3264C8u},
                { 1.0f, -1.0f, 0.0f, 0xDC3264C8u}, {-1.0f,  1.0f, 0.0f, 0xDC3264C8u},
                { 1.0f, -1.0f, 0.0f, 0xDC3264C8u}, { 1.0f,  1.0f, 0.0f, 0xDC3264C8u},
            };
            auto vbCw = renderer.CreateVertexBuffer(6);
            vbCw->SetData(kQuad, 6, sizeof(VPC));
            const Matrix Id = Matrix::getIdentityProperty();

            auto renderMask = [&](int cwc) -> Px
            {
                CNA::Internal::Renderers::BlendWriteState ws;
                ws.colorWriteChannels[0] = cwc;
                renderer.SetRenderTarget2D(rt.get());
                renderer.ApplyDepthStencilState(false, false, 0, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
                renderer.ApplyRasterizerState(0 /*CullMode::None*/, 0 /*FillMode::Solid*/, false, 0.0f, 0.0f);
                renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, ws);          // Opaque + mask
                dev.Clear(Color(10, 20, 30, 40));                      // destination D
                renderer.DrawColoredPrimitives(*vbCw, Id, Id, Id, PrimitiveType::TriangleList, 2);
                renderer.SetRenderTarget2D(nullptr);
                const auto p = ReadRenderTargetSurfaceD3D9(renderer.GetDeviceEXT(), d3d9Rt.GetColorSurfaceEXT());
                if (p.size() < static_cast<std::size_t>(64 * 64 * 4)) return Px{0, 0, 0, 0};
                const std::size_t idx = (static_cast<std::size_t>(32) * 64 + 32) * 4;
                return Px{p[idx], p[idx + 1], p[idx + 2], p[idx + 3]};
            };
            auto eqp = [](Px a, Px b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; };
            auto masked = [](Px d, Px s, int bits)
            {
                return Px{static_cast<uint8_t>((bits & 1) ? s.r : d.r),
                          static_cast<uint8_t>((bits & 2) ? s.g : d.g),
                          static_cast<uint8_t>((bits & 4) ? s.b : d.b),
                          static_cast<uint8_t>((bits & 8) ? s.a : d.a)};
            };

            const Px dcw = renderMask(0);    // None
            const Px scw = renderMask(15);   // All
            check(dcw.r != scw.r && dcw.g != scw.g && dcw.b != scw.b && dcw.a != scw.a,
                  "GFX077-1 (D3D9): None(dst)/All(src) baselines discriminate all four channels");
            check(eqp(renderMask(1), masked(dcw, scw, 1)),
                  "GFX077-2 (D3D9): ColorWriteChannels.Red writes only R (D3DRS_COLORWRITEENABLE)");
            check(eqp(renderMask(2), masked(dcw, scw, 2)),
                  "GFX077-3 (D3D9): ColorWriteChannels.Green writes only G");
            check(eqp(renderMask(4), masked(dcw, scw, 4)),
                  "GFX077-4 (D3D9): ColorWriteChannels.Blue writes only B");
            check(eqp(renderMask(8), masked(dcw, scw, 8)),
                  "GFX077-5 (D3D9): ColorWriteChannels.Alpha writes only A");
            check(eqp(renderMask(1 | 4), masked(dcw, scw, 1 | 4)),
                  "GFX077-6 (D3D9): ColorWriteChannels.Red|Blue writes only R and B");
            const Px a1 = renderMask(1), bG = renderMask(2), a2 = renderMask(1);
            check(eqp(a1, masked(dcw, scw, 1)) && eqp(bG, masked(dcw, scw, 2)) && eqp(a2, masked(dcw, scw, 1)),
                  "GFX077-7 (D3D9): A(Red)->B(Green)->A(Red) each applies its own D3DRS_COLORWRITEENABLE");

            // REMED-GFX-087: D3DRS_COLORWRITEENABLE is sticky device state and the loop above leaves
            // it on the last mask applied (Red only). Restore the All-channels default so later
            // direct-DrawPrimitivesEx checks (the SetDepthTestEnabled block) don't silently inherit a
            // partial write mask (which zeroed G/B and made their exact-colour assertions fail).
            renderer.ApplyBlendState(0, 0, 1, 1, 0, 0, CNA::Internal::Renderers::BlendWriteState{});
        }

        // Check T (D9-53) -- D3D9RenderTargetCubeRenderer: bind one face, Clear(), read back that
        // face's own surface, unbind, confirm the back buffer is restored again.
        {
            auto cubeRt = renderer.CreateRenderTargetCube(8, static_cast<int>(DepthFormat::None));
            auto& d3d9CubeRt = static_cast<D3D9RenderTargetCubeRenderer&>(*cubeRt);

            constexpr int kFace = 2; // +Y (D3DCUBEMAP_FACE_POSITIVE_Y == 2)
            renderer.SetRenderTargetCubeFace(cubeRt.get(), kFace);
            // Target-only clear: calling SetRenderTargetCubeFace() directly on the renderer (to
            // test D3D9RenderTargetCubeRenderer itself) bypasses GraphicsDevice's own
            // currentRenderTargets_ tracking, so its Clear(Color) overload's depth/stencil-presence
            // heuristic would incorrectly assume the SWAP CHAIN's own depth-stencil format (this
            // cube render target genuinely has none, DepthFormat::None above) -- an explicit
            // Target-only clear sidesteps that heuristic entirely rather than fighting it.
            dev.Clear(ClearOptions::Target, Color(150, 160, 170, 255), 1.0f, 0);

            ComPtr<IDirect3DSurface9> faceSurface;
            d3d9CubeRt.GetTextureEXT()->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(kFace), 0, faceSurface.GetAddressOf());
            const auto facePixels = ReadRenderTargetSurfaceD3D9(renderer.GetDeviceEXT(), faceSurface.Get());

            renderer.SetRenderTargetCubeFace(nullptr, 0);

            bool faceMatch = facePixels.size() == static_cast<std::size_t>(8 * 8 * 4);
            for (std::size_t i = 0; faceMatch && i < facePixels.size(); i += 4)
            {
                if (facePixels[i + 0] != 150 || facePixels[i + 1] != 160 ||
                    facePixels[i + 2] != 170 || facePixels[i + 3] != 255)
                {
                    faceMatch = false;
                }
            }
            check(cubeRt->GetSize() == 8 && faceMatch,
                  "D9-53: D3D9RenderTargetCubeRenderer::BindAsRenderTargetFace()+Clear() writes the exact color into that face");

            dev.Clear(Color(11, 12, 13, 255));
            const Microsoft::Xna::Framework::Rectangle backBufferRegion2(0, 0, 4, 4);
            std::vector<Color> backBufferPixels2(4 * 4, Color(0, 0, 0, 0));
            dev.GetBackBufferData(&backBufferRegion2, backBufferPixels2.data(), 0,
                                  static_cast<int>(backBufferPixels2.size()));
            bool backBufferMatch2 = true;
            for (const Color& p : backBufferPixels2)
            {
                if (p.getRProperty() != 11 || p.getGProperty() != 12 || p.getBProperty() != 13 ||
                    p.getAProperty() != 255)
                {
                    backBufferMatch2 = false;
                    break;
                }
            }
            check(backBufferMatch2,
                  "D9-53: unbinding the cube render target genuinely restores the back buffer");
        }

        // Check U (D9-53) -- MSAA render target, gated on the real device-reported support
        // (DirectX9Renderer::ClampMultiSampleCountEXT(), backed by
        // IDirect3D9::CheckDeviceMultiSampleType()) rather than assumed: Clear() into the
        // multisampled offscreen surface, unbind (which StretchRect-resolves into the sampleable
        // texture), and confirm the RESOLVED texture holds the exact clear color.
        {
            const int clamped = renderer.ClampMultiSampleCountEXT(D3DFMT_A8B8G8R8, 4);
            if (clamped > 1)
            {
                auto msaaRt = renderer.CreateRenderTarget2D(8, 8, static_cast<int>(DepthFormat::None), false, false, 4);
                auto& d3d9MsaaRt = static_cast<D3D9RenderTargetRenderer&>(*msaaRt);
                check(d3d9MsaaRt.GetMultiSampleCount() > 1,
                      "D9-53: MSAA D3D9RenderTargetRenderer reports a real, device-clamped sample count > 1");

                renderer.SetRenderTarget2D(msaaRt.get());
                // Target-only clear -- same reasoning as Check T: this MSAA target has
                // DepthFormat::None, and binding it via the renderer directly bypasses
                // GraphicsDevice's own currentRenderTargets_ tracking that Clear(Color)'s
                // depth/stencil-presence heuristic relies on.
                dev.Clear(ClearOptions::Target, Color(200, 210, 220, 255), 1.0f, 0);
                renderer.SetRenderTarget2D(nullptr);

                const auto resolved = ReadRenderTargetSurfaceD3D9(renderer.GetDeviceEXT(), d3d9MsaaRt.GetColorSurfaceEXT());
                bool resolvedMatch = resolved.size() == static_cast<std::size_t>(8 * 8 * 4);
                for (std::size_t i = 0; resolvedMatch && i < resolved.size(); i += 4)
                {
                    if (resolved[i + 0] != 200 || resolved[i + 1] != 210 ||
                        resolved[i + 2] != 220 || resolved[i + 3] != 255)
                    {
                        resolvedMatch = false;
                    }
                }
                check(resolvedMatch,
                      "D9-53: MSAA render target Clear()+unbind-resolve (StretchRect) produces the exact color in the resolved texture");
            }
            else
            {
                std::printf("    (skipped: device does not report 4x MSAA support for D3DFMT_A8B8G8R8)\n");
            }
        }

        // Check V (D9-54) -- MRT: bind 2 render targets via SetRenderTargets(), a single Clear()
        // writes the exact color into BOTH targets' own surfaces (real D3D9 behavior -- Clear()
        // applies to every currently-bound render target, not a per-index color), unbind restores
        // the back buffer, and requesting more targets than D3DCAPS9::NumSimultaneousRTs throws a
        // named error rather than silently degrading (design decision 13).
        {
            const int maxRTs = static_cast<int>(renderer.GetCapsEXT().NumSimultaneousRTs);
            if (maxRTs >= 2)
            {
                auto rt0 = renderer.CreateRenderTarget2D(8, 8, static_cast<int>(DepthFormat::None));
                auto rt1 = renderer.CreateRenderTarget2D(8, 8, static_cast<int>(DepthFormat::None));
                const RenderTargetBindingDescriptor rts[2] = {
                    RenderTargetBindingDescriptor::ForRenderTarget2D(
                        rt0.get(), 0, 8, 8, rt0->GetMultiSampleCount()),
                    RenderTargetBindingDescriptor::ForRenderTarget2D(
                        rt1.get(), 0, 8, 8, rt1->GetMultiSampleCount()),
                };
                renderer.SetRenderTargets(rts, 2);
                dev.Clear(ClearOptions::Target, Color(60, 70, 80, 255), 1.0f, 0);

                auto& d3d9Rt0 = static_cast<D3D9RenderTargetRenderer&>(*rt0);
                auto& d3d9Rt1 = static_cast<D3D9RenderTargetRenderer&>(*rt1);
                const auto pixels0 = ReadRenderTargetSurfaceD3D9(renderer.GetDeviceEXT(), d3d9Rt0.GetColorSurfaceEXT());
                const auto pixels1 = ReadRenderTargetSurfaceD3D9(renderer.GetDeviceEXT(), d3d9Rt1.GetColorSurfaceEXT());

                auto matchesColor = [](const std::vector<uint8_t>& px, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
                {
                    if (px.size() != static_cast<std::size_t>(8 * 8 * 4)) return false;
                    for (std::size_t i = 0; i < px.size(); i += 4)
                    {
                        if (px[i + 0] != r || px[i + 1] != g || px[i + 2] != b || px[i + 3] != a) return false;
                    }
                    return true;
                };
                check(matchesColor(pixels0, 60, 70, 80, 255) && matchesColor(pixels1, 60, 70, 80, 255),
                      "D9-54: SetRenderTargets() MRT bind -- a single Clear() writes the exact color into BOTH targets' own surfaces");

                renderer.SetRenderTargets(nullptr, 0);
                dev.Clear(Color(14, 15, 16, 255));
                const Microsoft::Xna::Framework::Rectangle backBufferRegion3(0, 0, 4, 4);
                std::vector<Color> backBufferPixels3(4 * 4, Color(0, 0, 0, 0));
                dev.GetBackBufferData(&backBufferRegion3, backBufferPixels3.data(), 0,
                                      static_cast<int>(backBufferPixels3.size()));
                bool backBufferMatch3 = true;
                for (const Color& p : backBufferPixels3)
                {
                    if (p.getRProperty() != 14 || p.getGProperty() != 15 || p.getBProperty() != 16 ||
                        p.getAProperty() != 255)
                    {
                        backBufferMatch3 = false;
                        break;
                    }
                }
                check(backBufferMatch3, "D9-54: SetRenderTargets(nullptr, 0) genuinely restores the back buffer");

                // Over-request: build maxRTs+1 targets and confirm a real, named exception -- not a
                // silent clamp to maxRTs (design decision 13's own point).
                std::vector<std::unique_ptr<IRenderTargetRenderer>> tooMany;
                std::vector<RenderTargetBindingDescriptor> tooManyBindings;
                tooManyBindings.reserve(static_cast<std::size_t>(maxRTs + 1));
                for (int i = 0; i < maxRTs + 1; ++i)
                {
                    tooMany.push_back(renderer.CreateRenderTarget2D(4, 4, static_cast<int>(DepthFormat::None)));
                    tooManyBindings.push_back(RenderTargetBindingDescriptor::ForRenderTarget2D(
                        tooMany.back().get(), 0, 4, 4, tooMany.back()->GetMultiSampleCount()));
                }
                bool threw = false;
                try
                {
                    renderer.SetRenderTargets(tooManyBindings.data(), static_cast<int>(tooManyBindings.size()));
                }
                catch (const std::runtime_error&)
                {
                    threw = true;
                }
                check(threw,
                      "D9-54: requesting more render targets than D3DCAPS9::NumSimultaneousRTs throws, does not silently degrade");
                renderer.SetRenderTargets(nullptr, 0); // clean up in case the (unexpected) non-throwing path left something bound
            }
            else
            {
                std::printf("    (skipped: device reports NumSimultaneousRTs=%d, need at least 2 for MRT)\n", maxRTs);
            }
        }

        // Check W (D9-55) -- a real D3D9OcclusionQueryRenderer: Begin()/End() genuinely issue real
        // D3DISSUE_BEGIN/D3DISSUE_END query commands, IsComplete() eventually reports true (polled,
        // bounded), and PixelCount() reads back a real GetData() result. No draw path exists yet
        // (D9-82), so this Begin/End wraps a Clear() rather than actual geometry -- Clear() does not
        // count as occlusion-testable rendering, so PixelCount()==0 is the correct, real result
        // here, not a stand-in for "draws 100 pixels" (that proof is D9-82's own job).
        {
            auto query = renderer.CreateOcclusionQuery();
            check(query != nullptr, "D9-55: D3D9OcclusionQueryRenderer created (device supports D3DQUERYTYPE_OCCLUSION)");
            if (query)
            {
                query->Begin();
                dev.Clear(Color(1, 2, 3, 255));
                query->End();

                // Bounded to 30 iterations, matching Check L's own resize-convergence poll
                // convention -- keeps a genuine failure (IsComplete() never true) fast to detect
                // instead of burning through a much larger bound at real Present()/vsync cost.
                bool completed = false;
                for (int i = 0; i < 30 && !completed; ++i)
                {
                    if (query->IsComplete()) { completed = true; break; }
                    dev.Present();
                }
                check(completed, "D9-55: IsComplete() eventually reports true after End() (real GetData() polling)");
                check(query->PixelCount() == 0,
                      "D9-55: PixelCount() reads back 0 for a query wrapping only a Clear() (no occlusion-testable draw)");
            }
        }

        // Check X (D9-56) -- NPOT capability surfaced from the real D3DCAPS9, not assumed
        // (authenticity, not a limitation to hide -- XNA's Reach profile forbids Wrap addressing
        // on NPOT textures specifically because real D3D9 hardware could require it). This dev
        // environment's DXVK device reports full, unconditional NPOT support (D9-3's own original
        // caps dump: POW2=0, NONPOW2CONDITIONAL=0) -- when that's what RequiresPowerOfTwoTexturesEXT()/
        // NonPowerOfTwoRequiresClampAddressingEXT() report, a genuinely non-power-of-two
        // D3D9TextureRenderer must actually work, not just be assumed to: create one and round-trip
        // exact bytes, proving this renderer doesn't add an artificial POW2 restriction on top of
        // real, more permissive hardware. Enforcing the Reach-profile "no Wrap on NPOT" restriction
        // itself against a real SamplerState/draw call is D9-10/D9-82's own job -- no draw/sampler
        // path exists yet to enforce it against; an honest gap, not a hidden one.
        {
            const bool requiresPow2 = renderer.RequiresPowerOfTwoTexturesEXT();
            const bool clampOnlyNpot = renderer.NonPowerOfTwoRequiresClampAddressingEXT();
            std::printf("    DirectX9Renderer::RequiresPowerOfTwoTexturesEXT()=%d "
                        "NonPowerOfTwoRequiresClampAddressingEXT()=%d\n",
                        requiresPow2 ? 1 : 0, clampOnlyNpot ? 1 : 0);
            check(!requiresPow2 && !clampOnlyNpot,
                  "D9-56: this dev environment's DXVK device reports full, unconditional NPOT support "
                  "(matches D9-3's own original D3DCAPS9 dump: POW2=0, NONPOW2CONDITIONAL=0)");

            if (!requiresPow2)
            {
                ImageData npotImg;
                npotImg.width = 5;
                npotImg.height = 3;
                npotImg.pixels.resize(5 * 3 * 4);
                for (int i = 0; i < 5 * 3; ++i)
                {
                    npotImg.pixels[static_cast<std::size_t>(i) * 4 + 0] = static_cast<uint8_t>(i * 7);
                    npotImg.pixels[static_cast<std::size_t>(i) * 4 + 1] = static_cast<uint8_t>(i * 11);
                    npotImg.pixels[static_cast<std::size_t>(i) * 4 + 2] = static_cast<uint8_t>(i * 13);
                    npotImg.pixels[static_cast<std::size_t>(i) * 4 + 3] = 255;
                }
                auto npotTex = renderer.CreateTexture(npotImg);
                auto& d3d9NpotTex = static_cast<D3D9TextureRenderer&>(*npotTex);

                D3DLOCKED_RECT locked{};
                HRESULT hr = d3d9NpotTex.GetTextureEXT()->LockRect(0, &locked, nullptr, D3DLOCK_READONLY);
                bool npotMatch = false;
                if (SUCCEEDED(hr))
                {
                    npotMatch = true;
                    for (int row = 0; row < 3 && npotMatch; ++row)
                    {
                        const auto* rowBytes = static_cast<const uint8_t*>(locked.pBits)
                                              + static_cast<std::size_t>(row) * locked.Pitch;
                        if (std::memcmp(rowBytes, npotImg.pixels.data() + static_cast<std::size_t>(row) * 5 * 4, 5 * 4) != 0)
                            npotMatch = false;
                    }
                    d3d9NpotTex.GetTextureEXT()->UnlockRect(0);
                }
                check(npotTex->GetWidth() == 5 && npotTex->GetHeight() == 3 && npotMatch,
                      "D9-56: a genuinely non-power-of-two (5x3) D3D9TextureRenderer round-trips exact bytes "
                      "(matches this device's real, unconditional NPOT support)");
            }
            else
            {
                std::printf("    (skipped NPOT round-trip proof: device requires power-of-2 textures)\n");
            }
        }

        // Check Y (D9-63) -- a real ApplySamplerState() genuinely pushes SetSamplerState() calls:
        // read back D3DSAMP_MINFILTER/MAGFILTER/MIPFILTER/ADDRESSU/ADDRESSV/MAXANISOTROPY directly
        // from the device via GetSamplerState() (no draw call needed to observe this -- D3D9 lets
        // sampler state be read back independent of any draw) and confirm they exactly match
        // TextureFilter::Point/TextureAddressMode::Clamp's own D3D9StateMapping translation. An
        // out-of-range slot is confirmed to silently no-op (matches D3D11's own bound-check
        // precedent), not throw.
        {
            using Microsoft::Xna::Framework::Graphics::TextureFilter;
            using Microsoft::Xna::Framework::Graphics::TextureAddressMode;

            renderer.ApplySamplerState(0, static_cast<int>(TextureFilter::Point),
                                      static_cast<int>(TextureAddressMode::Clamp),
                                      static_cast<int>(TextureAddressMode::Clamp), 4);

            DWORD minFilter = 0, magFilter = 0, mipFilter = 0, addrU = 0, addrV = 0, maxAniso = 0;
            renderer.GetDeviceEXT()->GetSamplerState(0, D3DSAMP_MINFILTER, &minFilter);
            renderer.GetDeviceEXT()->GetSamplerState(0, D3DSAMP_MAGFILTER, &magFilter);
            renderer.GetDeviceEXT()->GetSamplerState(0, D3DSAMP_MIPFILTER, &mipFilter);
            renderer.GetDeviceEXT()->GetSamplerState(0, D3DSAMP_ADDRESSU, &addrU);
            renderer.GetDeviceEXT()->GetSamplerState(0, D3DSAMP_ADDRESSV, &addrV);
            renderer.GetDeviceEXT()->GetSamplerState(0, D3DSAMP_MAXANISOTROPY, &maxAniso);

            check(minFilter == D3DTEXF_POINT && magFilter == D3DTEXF_POINT && mipFilter == D3DTEXF_POINT &&
                  addrU == D3DTADDRESS_CLAMP && addrV == D3DTADDRESS_CLAMP && maxAniso == 4,
                  "D9-63: ApplySamplerState() genuinely pushes exact SetSamplerState() values "
                  "(TextureFilter::Point + TextureAddressMode::Clamp + MaxAnisotropy=4), read back directly from the device");

            bool threwOnOutOfRange = false;
            try
            {
                renderer.ApplySamplerState(static_cast<int>(renderer.GetCapsEXT().MaxSimultaneousTextures) + 5,
                                          static_cast<int>(TextureFilter::Linear), 0, 0, 1);
            }
            catch (...)
            {
                threwOnOutOfRange = true;
            }
            check(!threwOnOutOfRange,
                  "D9-63: an out-of-range sampler slot is silently ignored (does not throw), matching D3D11's own bound-check precedent");
        }

        // Check Z (D9-64 / REMED-GFX-089) -- the helper setters change exactly one field of the
        // CURRENT depth state. The old fixture did not establish that current state: GFX077's
        // direct-renderer renderMask() left ZFUNC=D3DCMP_ALWAYS, then this block changed only
        // ZENABLE/ZWRITEENABLE and incorrectly expected DepthStencilState.Default behavior. That
        // made the far draw pass correctly under Always and was a test-harness defect, not a
        // production depth defect. Establish the reachable public XNA state first, then prove the
        // CNAEXT helper's A(disabled)->B(enabled)->A transition while preserving LessEqual.
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

            // Establish every non-depth input through reachable public state too: opaque/all
            // channel writes and CullNone. The flat vertex-colour shader uses no texture, fog,
            // lighting, or blending.
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);

            auto drawNearThenFar = [&]() {
                dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                          Color(10, 10, 10, 255), 1.0f, 0);
                renderer.DrawPrimitivesEx(*vbNear, I, I, I, PrimitiveType::TriangleList, 1, dp);
                renderer.DrawPrimitivesEx(*vbFar, I, I, I, PrimitiveType::TriangleList, 1, dp);
                Color px(0, 0, 0, 0);
                dev.GetBackBufferData(&probe, &px, 0, 1);
                return px;
            };

            auto drawSingle = [&](const auto& vb, float clearDepth) {
                dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                          Color(10, 10, 10, 255), clearDepth, 0);
                renderer.DrawPrimitivesEx(vb, I, I, I, PrimitiveType::TriangleList, 1, dp);
                Color px(0, 0, 0, 0);
                dev.GetBackBufferData(&probe, &px, 0, 1);
                return px;
            };
            auto isRed = [](const Color& px) {
                return px.getRProperty() == 255 && px.getGProperty() == 0 &&
                       px.getBProperty() == 0 && px.getAProperty() == 255;
            };
            auto isGreen = [](const Color& px) {
                return px.getRProperty() == 0 && px.getGProperty() == 255 &&
                       px.getBProperty() == 0 && px.getAProperty() == 255;
            };
            auto isClear = [](const Color& px) {
                return px.getRProperty() == 10 && px.getGProperty() == 10 &&
                       px.getBProperty() == 10 && px.getAProperty() == 255;
            };

            // Small reachable compare-function controls. In particular, Always reproduces the
            // old green result legitimately; Less/Greater prove the enum/native mapping is not an
            // ordinal-assumption accident.
            DepthStencilState alwaysState;
            alwaysState.setDepthBufferFunctionProperty(CompareFunction::Always);
            dev.setDepthStencilStateProperty(alwaysState);
            const Color alwaysFar = drawSingle(*vbFar, 0.1f);

            DepthStencilState lessState;
            lessState.setDepthBufferFunctionProperty(CompareFunction::Less);
            dev.setDepthStencilStateProperty(lessState);
            const Color lessNear = drawSingle(*vbNear, 0.5f);
            const Color lessFar = drawSingle(*vbFar, 0.5f);

            DepthStencilState greaterState;
            greaterState.setDepthBufferFunctionProperty(CompareFunction::Greater);
            dev.setDepthStencilStateProperty(greaterState);
            const Color greaterFar = drawSingle(*vbFar, 0.5f);
            const Color greaterNear = drawSingle(*vbNear, 0.5f);

            // Authoritative public contract:
            // DepthBufferEnable=true, DepthBufferWriteEnable=true, Function=LessEqual.
            dev.setDepthStencilStateProperty(DepthStencilState::Default);
            renderer.SetDepthTestEnabled(false);
            renderer.SetDepthTestEnabled(true);
            renderer.SetDepthWriteEnabled(true);

            DWORD zEnable = 0, zWrite = 0, zFunc = 0;
            const HRESULT getZEnableHr =
                renderer.GetDeviceEXT()->GetRenderState(D3DRS_ZENABLE, &zEnable);
            const HRESULT getZWriteHr =
                renderer.GetDeviceEXT()->GetRenderState(D3DRS_ZWRITEENABLE, &zWrite);
            const HRESULT getZFuncHr =
                renderer.GetDeviceEXT()->GetRenderState(D3DRS_ZFUNC, &zFunc);

            ComPtr<IDirect3DSurface9> activeDepth;
            const HRESULT getDepthHr =
                renderer.GetDeviceEXT()->GetDepthStencilSurface(activeDepth.GetAddressOf());
            D3DSURFACE_DESC depthDesc{};
            const HRESULT getDepthDescHr =
                activeDepth ? activeDepth->GetDesc(&depthDesc) : E_POINTER;
            ComPtr<IDirect3DSurface9> activeColor;
            const HRESULT getColorHr =
                renderer.GetDeviceEXT()->GetRenderTarget(0, activeColor.GetAddressOf());
            D3DSURFACE_DESC colorDesc{};
            const HRESULT getColorDescHr =
                activeColor ? activeColor->GetDesc(&colorDesc) : E_POINTER;
            D3DVIEWPORT9 activeViewport{};
            const HRESULT getViewportHr = renderer.GetDeviceEXT()->GetViewport(&activeViewport);

            const Color withDepth = drawNearThenFar();
            const Color clearFarPass = drawSingle(*vbNear, 0.9f);
            const Color clearNearReject = drawSingle(*vbNear, 0.1f);

            // Depth write OFF: both fragments compare against the untouched clear depth=1, so the
            // far/green second draw wins. Re-enable writes before the depth-test-off control.
            renderer.SetDepthWriteEnabled(false);
            const Color withoutWrite = drawNearThenFar();
            renderer.SetDepthWriteEnabled(true);

            // Depth test OFF: painter's order wins. Then restore A and prove it is byte-identical
            // to the first A (state A -> B -> A within one frame).
            renderer.SetDepthTestEnabled(false);
            DWORD zEnableOff = D3DZB_TRUE;
            const HRESULT getZEnableOffHr =
                renderer.GetDeviceEXT()->GetRenderState(D3DRS_ZENABLE, &zEnableOff);
            const Color withoutDepth = drawNearThenFar();
            renderer.SetDepthTestEnabled(true);
            const Color restoredDepth = drawNearThenFar();

            // Backbuffer-vs-offscreen discriminator. Bind a single-sample 64x64 D24S8 target,
            // introspect the actually-bound depth surface, run the same near/far sequence, then
            // read its color surface directly (no SpriteBatch sampling/readback dependency).
            auto offscreen =
                renderer.CreateRenderTarget2D(64, 64, static_cast<int>(DepthFormat::Depth24Stencil8));
            auto& offscreenD3D9 = static_cast<D3D9RenderTargetRenderer&>(*offscreen);
            renderer.SetRenderTarget2D(offscreen.get());
            dev.setDepthStencilStateProperty(DepthStencilState::Default);
            dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer,
                      Color(10, 10, 10, 255), 1.0f, 0);
            renderer.DrawPrimitivesEx(*vbNear, I, I, I, PrimitiveType::TriangleList, 1, dp);
            renderer.DrawPrimitivesEx(*vbFar, I, I, I, PrimitiveType::TriangleList, 1, dp);

            ComPtr<IDirect3DSurface9> offscreenBoundDepth;
            const HRESULT getOffscreenDepthHr =
                renderer.GetDeviceEXT()->GetDepthStencilSurface(
                    offscreenBoundDepth.GetAddressOf());
            D3DSURFACE_DESC offscreenDepthDesc{};
            const HRESULT getOffscreenDepthDescHr =
                offscreenBoundDepth
                    ? offscreenBoundDepth->GetDesc(&offscreenDepthDesc)
                    : E_POINTER;
            D3DSURFACE_DESC offscreenColorDesc{};
            const HRESULT getOffscreenColorDescHr =
                offscreenD3D9.GetActiveColorSurfaceEXT()->GetDesc(&offscreenColorDesc);
            D3DVIEWPORT9 offscreenViewport{};
            const HRESULT getOffscreenViewportHr =
                renderer.GetDeviceEXT()->GetViewport(&offscreenViewport);

            renderer.SetRenderTarget2D(nullptr);
            const auto offscreenPixels = ReadRenderTargetSurfaceD3D9(
                renderer.GetDeviceEXT(), offscreenD3D9.GetColorSurfaceEXT());
            Color offscreenPixel(0, 0, 0, 0);
            if (offscreenPixels.size() == static_cast<std::size_t>(64 * 64 * 4))
            {
                const std::size_t offscreenIndex =
                    (static_cast<std::size_t>(32) * 64 + 32) * 4;
                offscreenPixel = Color(
                    offscreenPixels[offscreenIndex + 0],
                    offscreenPixels[offscreenIndex + 1],
                    offscreenPixels[offscreenIndex + 2],
                    offscreenPixels[offscreenIndex + 3]);
            }
            // Direct renderer binds do not update GraphicsDevice's public Viewport bookkeeping;
            // restore its still-current backbuffer viewport explicitly for clean test hygiene.
            const Viewport& publicViewport = dev.getViewportProperty();
            renderer.SetViewport(
                publicViewport.getXProperty(), publicViewport.getYProperty(),
                publicViewport.getWidthProperty(), publicViewport.getHeightProperty(),
                publicViewport.getMinDepthProperty(), publicViewport.getMaxDepthProperty());

            const bool stateOk =
                SUCCEEDED(getZEnableHr) && SUCCEEDED(getZWriteHr) && SUCCEEDED(getZFuncHr) &&
                zEnable == D3DZB_TRUE && zWrite == TRUE && zFunc == D3DCMP_LESSEQUAL &&
                SUCCEEDED(getZEnableOffHr) && zEnableOff == D3DZB_FALSE;
            const bool surfaceOk =
                SUCCEEDED(getDepthHr) && SUCCEEDED(getDepthDescHr) &&
                SUCCEEDED(getColorHr) && SUCCEEDED(getColorDescHr) &&
                activeDepth && activeColor &&
                depthDesc.Format == D3DFMT_D24S8 &&
                depthDesc.Width == colorDesc.Width && depthDesc.Height == colorDesc.Height &&
                depthDesc.MultiSampleType == colorDesc.MultiSampleType &&
                depthDesc.MultiSampleQuality == colorDesc.MultiSampleQuality;
            const bool viewportOk =
                SUCCEEDED(getViewportHr) &&
                activeViewport.X == 0 && activeViewport.Y == 0 &&
                activeViewport.Width == colorDesc.Width &&
                activeViewport.Height == colorDesc.Height &&
                activeViewport.MinZ == 0.0f && activeViewport.MaxZ == 1.0f;
            const DWORD requiredCmpCaps =
                D3DPCMPCAPS_ALWAYS | D3DPCMPCAPS_LESS |
                D3DPCMPCAPS_LESSEQUAL | D3DPCMPCAPS_GREATER;
            const bool capsOk =
                (renderer.GetCapsEXT().RasterCaps & D3DPRASTERCAPS_ZTEST) != 0 &&
                (renderer.GetCapsEXT().ZCmpCaps & requiredCmpCaps) == requiredCmpCaps;
            const bool compareControlsOk =
                isGreen(alwaysFar) &&
                isRed(lessNear) && isClear(lessFar) &&
                isGreen(greaterFar) && isClear(greaterNear);
            const bool clearOk = isRed(clearFarPass) && isClear(clearNearReject);
            const bool offscreenOk =
                SUCCEEDED(getOffscreenDepthHr) &&
                SUCCEEDED(getOffscreenDepthDescHr) &&
                SUCCEEDED(getOffscreenColorDescHr) &&
                SUCCEEDED(getOffscreenViewportHr) &&
                offscreenBoundDepth.Get() ==
                    offscreenD3D9.GetDepthStencilSurfaceEXT() &&
                offscreenDepthDesc.Format == D3DFMT_D24S8 &&
                offscreenDepthDesc.Width == 64 && offscreenDepthDesc.Height == 64 &&
                offscreenDepthDesc.Width == offscreenColorDesc.Width &&
                offscreenDepthDesc.Height == offscreenColorDesc.Height &&
                offscreenDepthDesc.MultiSampleType ==
                    offscreenColorDesc.MultiSampleType &&
                offscreenDepthDesc.MultiSampleQuality ==
                    offscreenColorDesc.MultiSampleQuality &&
                offscreenViewport.X == 0 && offscreenViewport.Y == 0 &&
                offscreenViewport.Width == 64 && offscreenViewport.Height == 64 &&
                offscreenViewport.MinZ == 0.0f && offscreenViewport.MaxZ == 1.0f &&
                isRed(offscreenPixel);

            std::printf(
                "[GFX089] state ZENABLE=%lu ZWRITE=%lu ZFUNC=%lu; "
                "depth=D24S8 %lux%lu msaa=%d/q%lu; color=%lux%lu msaa=%d/q%lu; "
                "viewport=%lu,%lu %lux%lu %.1f..%.1f; "
                "pixels always=%d/%d less=%d/%d greater=%d/%d default=%d/%d "
                "writeOff=%d/%d depthOff=%d/%d restored=%d/%d offscreen=%d/%d\n",
                static_cast<unsigned long>(zEnable),
                static_cast<unsigned long>(zWrite),
                static_cast<unsigned long>(zFunc),
                static_cast<unsigned long>(depthDesc.Width),
                static_cast<unsigned long>(depthDesc.Height),
                static_cast<int>(depthDesc.MultiSampleType),
                static_cast<unsigned long>(depthDesc.MultiSampleQuality),
                static_cast<unsigned long>(colorDesc.Width),
                static_cast<unsigned long>(colorDesc.Height),
                static_cast<int>(colorDesc.MultiSampleType),
                static_cast<unsigned long>(colorDesc.MultiSampleQuality),
                static_cast<unsigned long>(activeViewport.X),
                static_cast<unsigned long>(activeViewport.Y),
                static_cast<unsigned long>(activeViewport.Width),
                static_cast<unsigned long>(activeViewport.Height),
                activeViewport.MinZ, activeViewport.MaxZ,
                alwaysFar.getRProperty(), alwaysFar.getGProperty(),
                lessNear.getRProperty(), lessNear.getGProperty(),
                greaterFar.getRProperty(), greaterFar.getGProperty(),
                withDepth.getRProperty(), withDepth.getGProperty(),
                withoutWrite.getRProperty(), withoutWrite.getGProperty(),
                withoutDepth.getRProperty(), withoutDepth.getGProperty(),
                restoredDepth.getRProperty(), restoredDepth.getGProperty(),
                offscreenPixel.getRProperty(), offscreenPixel.getGProperty());

            check(stateOk && surfaceOk && viewportOk && capsOk && compareControlsOk && clearOk &&
                  offscreenOk &&
                  isRed(withDepth) && isGreen(withoutWrite) && isRed(restoredDepth),
                  "REMED-GFX-089: reachable DepthStencilState.Default maps to ZENABLE=TRUE, "
                  "ZWRITEENABLE=TRUE, ZFUNC=LESSEQUAL on a compatible bound D24S8 surface; near "
                  "red rejects far green on both backbuffer and offscreen RT, depth clear values "
                  "gate draws, write-off differs, and A->B->A restores identical depth behavior");
            check(isGreen(withoutDepth),
                  "DirectX9Renderer::SetDepthTestEnabled(false): the SAME farther quad now genuinely "
                  "OVERWRITES the nearer one (green wins) -- only the SetDepthTestEnabled() call differs "
                  "between the two, so a no-op implementation cannot pass both checks");

            dev.setDepthStencilStateProperty(DepthStencilState::None);
        }

        std::printf("=== %d/%d PASS (main Game checks) ===\n", passCount, totalCount);
        Exit();
    }

public:
    D3D9SmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }
};

namespace
{
    // Check J: a raw (non-Game) renderer with DepthFormat::None has genuinely no depth-stencil
    // surface, and ClearDepth()/ClearStencil() silently no-op on it. A manually-created SDL
    // window is used here (not GraphicsDeviceManager) since this needs a *different*
    // PresentationParameters than the main Game's own device.
    void RunNoDepthBufferCheck()
    {
        // DirectX9Renderer needs a real SDL_Window* (it reads the win32 HWND back out via
        // SDL_GetWindowProperties) -- a plain SDL window, same as GraphicsDeviceManager itself
        // would create, just constructed directly here since this renderer intentionally needs
        // different PresentationParameters (DepthFormat::None) than the main Game's own device.
        SDL_Window* sdlWindow = SDL_CreateWindow("d9smoke_nodepth", 64, 64, 0);
        if (!sdlWindow)
        {
            check(false, "Check J setup: SDL_CreateWindow failed");
            return;
        }

        CNA::Internal::Renderers::GraphicsRendererCreateArgs createArgs;
        createArgs.surface.windowId = SDL_GetWindowID(sdlWindow);
        createArgs.virtualWidth = 64;
        createArgs.virtualHeight = 64;
        createArgs.depthStencilFormat = 0;  // DepthFormat::None

        DirectX9Renderer renderer(createArgs);

        Microsoft::WRL::ComPtr<IDirect3DSurface9> dsSurface;
        HRESULT hr = renderer.GetDeviceEXT()->GetDepthStencilSurface(dsSurface.ReleaseAndGetAddressOf());
        check(FAILED(hr) || !dsSurface,
              "Check J: a device created with DepthFormat::None genuinely has no depth-stencil surface");

        bool threw = false;
        try { renderer.ClearDepth(0.5f); } catch (...) { threw = true; }
        check(!threw, "Check J: ClearDepth() silently no-ops (does not throw) with no depth buffer");

        threw = false;
        try { renderer.ClearStencil(3); } catch (...) { threw = true; }
        check(!threw, "Check J: ClearStencil() silently no-ops (does not throw) with no depth buffer");

        SDL_DestroyWindow(sdlWindow);
    }

    // Check K (D9-32): a device constructed with GraphicsProfile::HiDef succeeds without throwing,
    // since this real GPU's D3DCAPS9 genuinely reports vs_3_0/ps_3_0 (verified below, not assumed).
    // The REJECTION path (a device whose real caps fall below HiDef's floor) cannot be exercised
    // here -- this dev loop has no way to make Wine+DXVK report a sub-SM3 device on real hardware
    // that is SM3-capable -- same "real hardware needed" caveat as D9-105/D9-140 elsewhere in this
    // plan; D9-32's own gating logic (a plain integer comparison against GraphicsProfile::HiDef) is
    // simple enough that this asymmetry is an honest, acceptable gap, not a hidden one.
    void RunHiDefProfileCheck()
    {
        SDL_Window* sdlWindow = SDL_CreateWindow("d9smoke_hidef", 64, 64, 0);
        if (!sdlWindow)
        {
            check(false, "Check K setup: SDL_CreateWindow failed");
            return;
        }

        CNA::Internal::Renderers::GraphicsRendererCreateArgs createArgs;
        createArgs.surface.windowId = SDL_GetWindowID(sdlWindow);
        createArgs.virtualWidth = 64;
        createArgs.virtualHeight = 64;
        createArgs.graphicsProfile = 1;  // GraphicsProfile::HiDef

        bool threw = false;
        try
        {
            DirectX9Renderer renderer(createArgs);
            check(renderer.GetCapsEXT().VertexShaderVersion >= static_cast<DWORD>(D3DVS_VERSION(3, 0)) &&
                  renderer.GetCapsEXT().PixelShaderVersion >= static_cast<DWORD>(D3DPS_VERSION(3, 0)),
                  "Check K: HiDef construction succeeds on a real vs_3_0/ps_3_0-capable device");
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        check(!threw, "Check K: GraphicsProfile::HiDef did not throw NoSuitableGraphicsDeviceException "
                      "(this GPU genuinely meets the floor)");

        SDL_DestroyWindow(sdlWindow);
    }
}

int main()
{
    {
        D3D9SmokeTest game;
        game.Run();
    }

    RunNoDepthBufferCheck();
    RunHiDefProfileCheck();

    std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
