// SPDX-License-Identifier: MS-PL
// plans/plan_directx12_parity.md DX12-0007: the foundational DirectX 12 presentation stress on a real
// CNA Win32 window.
//
// A real Game -- Win32Platform window, GraphicsDeviceManager, DirectX12Renderer, DXGI flip-model swap
// chain -- runs for thousands of frames while this program does to its window what a user does:
// SetWindowPos to a new client size (WM_SIZE -> platform -> renderer -> ResizeBuffers), minimize and
// restore. Every frame clears and draws a sprite; on a schedule it also creates, renders into, reads back
// and disposes render targets, textures and dynamic vertex buffers. It checks:
//
//   * the swap chain's back buffer follows the window's client size after every resize;
//   * a clear and a sprite read back as drawn, from the back buffer and from a render target;
//   * resource churn holds no process handles and no private memory beyond a stated tolerance;
//   * the D3D12 debug layer, when enabled, recorded nothing.
//
// Run it with CNA_D3D12_ADAPTER=warp on a machine without a D3D12 GPU. WARP is a software rasteriser:
// what this proves is CNA's command recording, barriers, fences and swap-chain lifetime against
// Microsoft's D3D12 runtime, not a GPU driver.
//
// Usage: cna_stress_directx12_win32_present [--frames N] [--max-handle-growth N] [--max-private-mb N]

#if defined(_WIN32)

#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include <windows.h>
#include <psapi.h>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::DirectX12::DirectX12Renderer;

namespace
{
    struct ProcessSample
    {
        DWORD handles = 0;
        std::uint64_t privateBytes = 0;
    };

    ProcessSample SampleProcess()
    {
        ProcessSample sample;
        GetProcessHandleCount(GetCurrentProcess(), &sample.handles);
        PROCESS_MEMORY_COUNTERS_EX counters{};
        counters.cb = sizeof(counters);
        if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                                 sizeof(counters)))
            sample.privateBytes = counters.PrivateUsage;
        return sample;
    }

    // Client sizes the window is driven through. Deliberately includes odd sizes and a size narrower
    // than the back buffer the game asked for.
    constexpr int kClientSizes[][2] = {{320, 240}, {497, 301}, {160, 120}, {640, 360}, {243, 199}, {400, 400}};

    class PresentStressGame final : public Game
    {
    public:
        PresentStressGame(int frames, int maxHandleGrowth, int maxPrivateMb)
            : frames_(frames), maxHandleGrowth_(maxHandleGrowth), maxPrivateMb_(maxPrivateMb)
        {
            graphics_ = std::make_unique<GraphicsDeviceManager>(this);
            graphics_->setPreferredBackBufferWidthProperty(320);
            graphics_->setPreferredBackBufferHeightProperty(240);
            graphics_->setSynchronizeWithVerticalRetraceProperty(false);
            // HiDef: the churn creates mipmapped non-power-of-two render targets, which Reach forbids.
            graphics_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
            setIsFixedTimeStepProperty(false);
            getWindowProperty().setAllowUserResizingProperty(true);
        }

        int Result() const { return failures_ == 0 ? 0 : 1; }

    protected:
        void LoadContent() override
        {
            auto& device = getGraphicsDeviceProperty();
            spriteBatch_ = std::make_unique<SpriteBatch>(device);
            texture_ = std::make_unique<Texture2D>(device, 4, 4);
            std::vector<Color> red(16, Color(255, 0, 0, 255));
            texture_->SetData(red.data(), 0, static_cast<int>(red.size()));
            renderer_ = dynamic_cast<DirectX12Renderer*>(&device.GetRenderer());
            if (renderer_ == nullptr)
                Fail("the active renderer is not DirectX 12");
            else
                std::printf("adapter=%s software=%d selected_by=%s feature_level=0x%X debug_layer=%d\n",
                            renderer_->GetAdapterInfoEXT().description.c_str(),
                            renderer_->GetAdapterInfoEXT().software ? 1 : 0,
                            CNA::Internal::Renderers::DirectX12::D3D12AdapterPreferenceName(
                                renderer_->GetConfigurationEXT().adapter),
                            static_cast<unsigned>(renderer_->GetFeatureLevelEXT()),
                            renderer_->IsDebugLayerEnabledEXT() ? 1 : 0);
        }

        void Draw(const GameTime&) override
        {
            try
            {
                DrawFrame();
            }
            catch (const std::exception& error)
            {
                Fail(std::string("exception at frame ") + std::to_string(frame_) + ": " + error.what());
                Exit();
            }
        }

    private:
        void Fail(const std::string& message)
        {
            ++failures_;
            if (failures_ <= 40)
                std::printf("[FAIL] %s\n", message.c_str());
        }

        HWND Window() { return reinterpret_cast<HWND>(getWindowProperty().getHandleProperty()); }

        void DrawFrame()
        {
            auto& device = getGraphicsDeviceProperty();
            if (frame_ == 50)
                warm_ = SampleProcess();

            const int shade = frame_ % 200;
            const Color clear(0, shade, 64, 255);
            device.Clear(clear);
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Microsoft::Xna::Framework::Rectangle(8, 8, 32, 32), Color::White);
            spriteBatch_->End();

            if (frame_ % 10 == 5 && !IsIconic(Window()))
                VerifyBackBuffer(clear);
            if (frame_ % 25 == 12)
                ChurnResources();
            DriveWindow();

            ++frame_;
            if (frame_ >= frames_)
            {
                Finish();
                Exit();
            }
        }

        void VerifyBackBuffer(const Color& clear)
        {
            if (renderer_ == nullptr || !renderer_->IsSwapChainAvailableEXT())
                return;
            RECT client{};
            GetClientRect(Window(), &client);
            ID3D12Resource* const resource = renderer_->GetCurrentBackBufferResourceEXT();
            const D3D12_RESOURCE_DESC desc = resource->GetDesc();
            ++backBufferChecks_;
            // The size check is made on frames where the last resize had a whole frame to arrive.
            if (framesSinceResize_ >= 2 &&
                (desc.Width != static_cast<UINT64>(client.right) || desc.Height != static_cast<UINT>(client.bottom)))
                Fail("frame " + std::to_string(frame_) + ": back buffer " + std::to_string(desc.Width) + "x" +
                     std::to_string(desc.Height) + " but client " + std::to_string(client.right) + "x" +
                     std::to_string(client.bottom));

            // The logical back buffer's far corner: the clear colour, whatever scaling or letterboxing
            // the presentation applies, because the whole target is cleared.
            int logicalW = 0;
            int logicalH = 0;
            renderer_->GetViewportSize(logicalW, logicalH);
            std::uint8_t pixel[4]{};
            renderer_->ReadBackbuffer(logicalW - 1, logicalH - 1, 1, 1, pixel);
            if (pixel[0] != clear.getRProperty() || pixel[1] != clear.getGProperty() ||
                pixel[2] != clear.getBProperty())
                Fail("frame " + std::to_string(frame_) + ": back-buffer corner read " + std::to_string(pixel[0]) +
                     "," + std::to_string(pixel[1]) + "," + std::to_string(pixel[2]) + ", expected the clear colour");
        }

        void ChurnResources()
        {
            auto& device = getGraphicsDeviceProperty();
            {
                RenderTarget2D target(device, 37, 29, (churns_ % 2) == 0, SurfaceFormat::Color,
                                      DepthFormat::Depth24Stencil8);
                device.SetRenderTarget(&target);
                device.Clear(Color(0, 0, 255, 255));
                spriteBatch_->Begin();
                spriteBatch_->Draw(*texture_, Microsoft::Xna::Framework::Rectangle(2, 3, 10, 10), Color::White);
                spriteBatch_->End();
                device.SetRenderTarget(nullptr);
                std::vector<Color> pixels(37 * 29);
                target.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
                const Color inside = pixels[static_cast<std::size_t>(7 * 37 + 6)];
                const Color outside = pixels[static_cast<std::size_t>(20 * 37 + 30)];
                ++targetChecks_;
                if (inside != Color(255, 0, 0, 255) || outside != Color(0, 0, 255, 255))
                    Fail("frame " + std::to_string(frame_) + ": render target read inside " +
                         std::to_string(inside.getRProperty()) + "," + std::to_string(inside.getGProperty()) + "," +
                         std::to_string(inside.getBProperty()) + " outside " + std::to_string(outside.getRProperty()) +
                         "," + std::to_string(outside.getGProperty()) + "," + std::to_string(outside.getBProperty()));
            }
            {
                Texture2D texture(device, 19, 7, true, SurfaceFormat::Color);
                std::vector<Color> data(19 * 7);
                for (std::size_t i = 0; i < data.size(); ++i)
                    data[i] = Color(static_cast<int>(i % 251), static_cast<int>((i * 7) % 253), churns_ % 255, 255);
                texture.SetData(data.data(), 0, static_cast<int>(data.size()));
                std::vector<Color> back(data.size());
                texture.GetData(back.data(), 0, static_cast<int>(back.size()));
                if (std::memcmp(back.data(), data.data(), data.size() * sizeof(Color)) != 0)
                    Fail("frame " + std::to_string(frame_) + ": Texture2D SetData/GetData round trip differs");
            }
            {
                DynamicVertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(), 64, BufferUsage::WriteOnly);
                std::vector<VertexPositionColor> vertices(64);
                buffer.SetData(vertices.data(), 0, static_cast<int>(vertices.size()));
            }
            ++churns_;
        }

        void DriveWindow()
        {
            ++framesSinceResize_;
            if (minimizedUntil_ > 0)
            {
                if (frame_ >= minimizedUntil_)
                {
                    ShowWindow(Window(), SW_RESTORE);
                    minimizedUntil_ = 0;
                    framesSinceResize_ = 0;
                }
                return;
            }
            if (frame_ % 150 == 149)
            {
                ShowWindow(Window(), SW_MINIMIZE);
                minimizedUntil_ = frame_ + 10;
                ++minimizes_;
                return;
            }
            if (frame_ % 20 == 19)
            {
                const auto& size = kClientSizes[static_cast<std::size_t>(resizes_) % std::size(kClientSizes)];
                RECT rect{0, 0, size[0], size[1]};
                const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(Window(), GWL_STYLE));
                const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(Window(), GWL_EXSTYLE));
                AdjustWindowRectEx(&rect, style, FALSE, exStyle);
                SetWindowPos(Window(), nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                ++resizes_;
                framesSinceResize_ = 0;
            }
        }

        void Finish()
        {
            const ProcessSample end = SampleProcess();
            const long handleGrowth = static_cast<long>(end.handles) - static_cast<long>(warm_.handles);
            const long long privateGrowthMb =
                (static_cast<long long>(end.privateBytes) - static_cast<long long>(warm_.privateBytes)) / (1024 * 1024);
            const auto totals = DirectX12Renderer::GetProcessDebugMessageTotalsEXT();
            std::printf("frames=%d resizes=%d minimizes=%d churns=%d backbuffer_checks=%d target_checks=%d\n", frame_ + 1,
                        resizes_, minimizes_, churns_, backBufferChecks_, targetChecks_);
            std::printf("handles warm=%lu end=%lu growth=%ld (max %d)\n", warm_.handles, end.handles, handleGrowth,
                        maxHandleGrowth_);
            std::printf("private_mb warm=%llu end=%llu growth=%lld (max %d)\n",
                        static_cast<unsigned long long>(warm_.privateBytes / (1024 * 1024)),
                        static_cast<unsigned long long>(end.privateBytes / (1024 * 1024)), privateGrowthMb,
                        maxPrivateMb_);
            if (renderer_ != nullptr)
            {
                renderer_->DrainDebugMessagesEXT();
                const auto drained = DirectX12Renderer::GetProcessDebugMessageTotalsEXT();
                std::printf("debug_layer corruption=%llu error=%llu warning=%llu\n",
                            static_cast<unsigned long long>(drained.corruption),
                            static_cast<unsigned long long>(drained.error),
                            static_cast<unsigned long long>(drained.warning));
                for (const auto& message : DirectX12Renderer::GetRecentProcessDebugMessagesEXT())
                    std::printf("debug_layer message severity=%d id=%d %s\n", message.severity, message.id,
                                message.description.c_str());
                if (drained.corruption + drained.error + drained.warning != 0)
                    Fail("the D3D12 debug layer recorded messages");
                const auto rtv = renderer_->GetDescriptorHeapsEXT()->rtv.GetStatsEXT();
                const auto srv = renderer_->GetDescriptorHeapsEXT()->cbvSrvUav.GetStatsEXT();
                std::printf("descriptors rtv live=%u peak=%u cap=%u srv live=%u peak=%u cap=%u\n", rtv.live,
                            rtv.peakLive, rtv.capacity, srv.live, srv.peakLive, srv.capacity);
            }
            (void)totals;
            if (handleGrowth > maxHandleGrowth_)
                Fail("process handles grew by " + std::to_string(handleGrowth));
            if (privateGrowthMb > maxPrivateMb_)
                Fail("private memory grew by " + std::to_string(privateGrowthMb) + " MB");
            if (resizes_ == 0 || minimizes_ == 0 || backBufferChecks_ == 0 || targetChecks_ == 0)
                Fail("the run was too short to exercise resize, minimize and readback");
            std::printf("result=%s failures=%d\n", failures_ == 0 ? "PASS" : "FAIL", failures_);
            std::fflush(stdout);
        }

        std::unique_ptr<GraphicsDeviceManager> graphics_;
        std::unique_ptr<SpriteBatch> spriteBatch_;
        std::unique_ptr<Texture2D> texture_;
        DirectX12Renderer* renderer_ = nullptr;
        const int frames_;
        const int maxHandleGrowth_;
        const int maxPrivateMb_;
        int frame_ = 0;
        int failures_ = 0;
        int resizes_ = 0;
        int minimizes_ = 0;
        int churns_ = 0;
        int backBufferChecks_ = 0;
        int targetChecks_ = 0;
        int framesSinceResize_ = 0;
        int minimizedUntil_ = 0;
        ProcessSample warm_{};
    };
}

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    int frames = 3000;
    int maxHandleGrowth = 64;
    int maxPrivateMb = 32;
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--frames")) frames = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--max-handle-growth")) maxHandleGrowth = std::atoi(argv[++i]);
        else if (!std::strcmp(argv[i], "--max-private-mb")) maxPrivateMb = std::atoi(argv[++i]);
    }
    try
    {
        PresentStressGame game(frames, maxHandleGrowth, maxPrivateMb);
        game.Run();
        return game.Result();
    }
    catch (const std::exception& error)
    {
        std::printf("[FAIL] %s\nresult=FAIL\n", error.what());
        return 1;
    }
}

#else
int main() { return 77; }
#endif
