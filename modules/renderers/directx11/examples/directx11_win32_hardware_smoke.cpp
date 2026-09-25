// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/D3DCommon/D3DDebugLayerLog.hpp"
#include "CNA/Internal/Renderers/DirectX11/DirectX11Renderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <vector>
#include <windows.h>
#include <wrl/client.h>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::DirectX11::DirectX11Renderer;
using CNA::Internal::Renderers::D3DCommon::D3DDebugLayerLog;
using Microsoft::WRL::ComPtr;

namespace
{
    class HardwareSmoke final : public Game
    {
    public:
        HardwareSmoke()
        {
            graphics_ = std::make_unique<GraphicsDeviceManager>(this);
            graphics_->setPreferredBackBufferWidthProperty(320);
            graphics_->setPreferredBackBufferHeightProperty(240);
            graphics_->setSynchronizeWithVerticalRetraceProperty(false);
            setIsFixedTimeStepProperty(false);
            getWindowProperty().setAllowUserResizingProperty(true);
        }

        int Result()
        {
            if (renderer_)
                renderer_->DrainDebugMessagesEXT();
            const auto totals = D3DDebugLayerLog::Totals();
            std::printf("frames=%d draw_checks=%d resize_observed=%d debug_layer=%d "
                        "messages corruption=%llu error=%llu warning=%llu\n",
                        frames_, drawChecks_, resizeObserved_ ? 1 : 0,
                        renderer_ && renderer_->IsDebugLayerEnabledEXT() ? 1 : 0,
                        static_cast<unsigned long long>(totals.corruption),
                        static_cast<unsigned long long>(totals.error),
                        static_cast<unsigned long long>(totals.warning));
            for (const auto& message : D3DDebugLayerLog::Recent())
                std::printf("debug_message severity=%d id=%d %s\n", message.severity,
                            message.id, message.description.c_str());
            Check(frames_ >= 10 && drawChecks_ >= 2 && resizeObserved_,
                  "draw, presentation loop, and swap-chain resize completed");
            Check(totals.corruption == 0 && totals.error == 0,
                  "debug layer recorded no corruption or error");
            std::printf("result=%s failures=%d\n", failures_ == 0 ? "PASS" : "FAIL", failures_);
            return failures_ == 0 ? 0 : 1;
        }

    protected:
        void LoadContent() override
        {
            renderer_ = dynamic_cast<DirectX11Renderer*>(&getGraphicsDeviceProperty().GetRenderer());
            if (!Check(renderer_ != nullptr && renderer_->GetDeviceEXT() != nullptr,
                       "native D3D11 renderer and device exist"))
            {
                Exit();
                return;
            }

            ComPtr<IDXGIDevice> dxgiDevice;
            ComPtr<IDXGIAdapter> baseAdapter;
            ComPtr<IDXGIAdapter1> adapter;
            const HRESULT hr = renderer_->GetDeviceEXT()->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
            if (!Check(SUCCEEDED(hr) && SUCCEEDED(dxgiDevice->GetAdapter(&baseAdapter)) &&
                       SUCCEEDED(baseAdapter.As(&adapter)), "device exposes its DXGI adapter"))
            {
                Exit();
                return;
            }
            DXGI_ADAPTER_DESC1 desc{};
            if (!Check(SUCCEEDED(adapter->GetDesc1(&desc)), "DXGI adapter description is readable"))
            {
                Exit();
                return;
            }
            const bool software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
            std::printf("adapter=%ls vendor=0x%04X device=0x%04X software=%d "
                        "feature_level=0x%X debug_layer=%d\n",
                        desc.Description, desc.VendorId, desc.DeviceId, software ? 1 : 0,
                        static_cast<unsigned>(renderer_->GetFeatureLevelEXT()),
                        renderer_->IsDebugLayerEnabledEXT() ? 1 : 0);
            Check(!software && desc.VendorId != 0 && desc.VendorId != 0x1414,
                  "device uses a hardware adapter");

            texture_ = std::make_unique<Texture2D>(getGraphicsDeviceProperty(), 4, 4);
            std::array<Color, 16> red;
            red.fill(Color(255, 0, 0, 255));
            texture_->SetData(red.data(), 0, static_cast<int>(red.size()));
            batch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        }

        void Draw(const GameTime&) override
        {
            if (!renderer_ || !batch_ || !texture_)
            {
                Exit();
                return;
            }
            auto& device = getGraphicsDeviceProperty();
            device.Clear(Color(0, 0, 255, 255));
            batch_->Begin();
            batch_->Draw(*texture_, Microsoft::Xna::Framework::Rectangle(8, 8, 32, 32),
                         Color::White);
            batch_->End();

            if (frames_ == 2 || frames_ == 8)
            {
                std::uint8_t inside[4]{};
                std::uint8_t outside[4]{};
                renderer_->ReadBackbuffer(16, 16, 1, 1, inside);
                renderer_->ReadBackbuffer(80, 80, 1, 1, outside);
                std::printf("frame=%d inside=%u,%u,%u outside=%u,%u,%u\n", frames_,
                            inside[0], inside[1], inside[2],
                            outside[0], outside[1], outside[2]);
                int redPixels = 0;
                if (frames_ == 8)
                {
                    int width = 0;
                    int height = 0;
                    renderer_->GetViewportSize(width, height);
                    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width * height * 4));
                    renderer_->ReadBackbuffer(0, 0, width, height, pixels.data());
                    int minX = width, minY = height, maxX = -1, maxY = -1;
                    for (std::size_t p = 0; p < pixels.size(); p += 4)
                    {
                        if (pixels[p] != 255 || pixels[p + 1] != 0 || pixels[p + 2] != 0)
                            continue;
                        ++redPixels;
                        const int x = static_cast<int>((p / 4) % static_cast<std::size_t>(width));
                        const int y = static_cast<int>((p / 4) / static_cast<std::size_t>(width));
                        minX = std::min(minX, x);
                        minY = std::min(minY, y);
                        maxX = std::max(maxX, x);
                        maxY = std::max(maxY, y);
                    }
                    std::printf("post_resize_viewport=%dx%d red_pixels=%d bounds=%d,%d..%d,%d\n",
                                width, height, redPixels, minX, minY, maxX, maxY);
                }
                const bool blueClear = outside[0] == 0 && outside[1] == 0 && outside[2] == 255;
                if (frames_ == 2)
                    Check(inside[0] == 255 && inside[1] == 0 && inside[2] == 0 && blueClear,
                          "sprite draw and clear reach the swap-chain back buffer");
                else
                    Check(redPixels > 0 && blueClear,
                          "sprite draw and clear still execute after swap-chain resize");
                ++drawChecks_;
            }

            HWND window = reinterpret_cast<HWND>(getWindowProperty().getHandleProperty());
            if (frames_ == 3)
            {
                RECT outer{0, 0, 497, 301};
                const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
                const DWORD exStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
                AdjustWindowRectEx(&outer, style, FALSE, exStyle);
                SetWindowPos(window, nullptr, 0, 0, outer.right - outer.left,
                             outer.bottom - outer.top,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
            if (frames_ >= 5)
            {
                RECT client{};
                GetClientRect(window, &client);
                ComPtr<ID3D11RenderTargetView> rtv;
                renderer_->GetContextEXT()->OMGetRenderTargets(1, rtv.GetAddressOf(), nullptr);
                if (rtv)
                {
                    ComPtr<ID3D11Resource> resource;
                    ComPtr<ID3D11Texture2D> backBuffer;
                    rtv->GetResource(resource.GetAddressOf());
                    if (SUCCEEDED(resource.As(&backBuffer)))
                    {
                        D3D11_TEXTURE2D_DESC desc{};
                        backBuffer->GetDesc(&desc);
                        resizeObserved_ |= client.right == 497 && client.bottom == 301 &&
                                           desc.Width == 497 && desc.Height == 301;
                    }
                }
            }
            if (++frames_ >= 10)
                Exit();
        }

    private:
        bool Check(bool condition, const char* label)
        {
            std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
            if (!condition) ++failures_;
            return condition;
        }

        std::unique_ptr<GraphicsDeviceManager> graphics_;
        std::unique_ptr<Texture2D> texture_;
        std::unique_ptr<SpriteBatch> batch_;
        DirectX11Renderer* renderer_ = nullptr;
        int frames_ = 0;
        int drawChecks_ = 0;
        int failures_ = 0;
        bool resizeObserved_ = false;
    };
}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try
    {
        HardwareSmoke game;
        game.Run();
        return game.Result();
    }
    catch (const std::exception& error)
    {
        std::printf("[FAIL] %s\nresult=FAIL\n", error.what());
        return 1;
    }
}
