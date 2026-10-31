// SPDX-License-Identifier: MS-PL

// DX9 audit regression: GraphicsDeviceManager::ApplyChanges must reach the live D3D9 swap chain.

#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <cstdio>
#include <memory>

#include <d3d9.h>
#include <wrl/client.h>

using CNA::Internal::Renderers::DirectX9::DirectX9Renderer;
using Microsoft::WRL::ComPtr;
using namespace Microsoft::Xna::Framework;

namespace
{
    class DirectX9SwapIntervalTest final : public Game
    {
        std::unique_ptr<GraphicsDeviceManager> manager_;
        int failures_ = 0;
        int result_ = 1;
        bool done_ = false;

        void Check(bool condition, const char* label)
        {
            std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
            if (!condition) ++failures_;
        }

        D3DPRESENT_PARAMETERS ReadPresentationParameters(DirectX9Renderer& renderer)
        {
            D3DPRESENT_PARAMETERS parameters{};
            ComPtr<IDirect3DSwapChain9> swapChain;
            const HRESULT getChain = renderer.GetDeviceEXT()->GetSwapChain(
                0, swapChain.ReleaseAndGetAddressOf());
            Check(SUCCEEDED(getChain) && swapChain != nullptr,
                  "IDirect3DDevice9::GetSwapChain returns the live primary chain");
            if (FAILED(getChain) || !swapChain) return parameters;

            const HRESULT getParameters = swapChain->GetPresentParameters(&parameters);
            Check(SUCCEEDED(getParameters),
                  "IDirect3DSwapChain9::GetPresentParameters returns the applied mode");
            return parameters;
        }

    protected:
        void Draw(const GameTime&) override
        {
            if (done_) return;
            done_ = true;

            auto& renderer = static_cast<DirectX9Renderer&>(
                getGraphicsDeviceProperty().GetRenderer());

            manager_->setSynchronizeWithVerticalRetraceProperty(false);
            manager_->ApplyChanges();
            Check(renderer.GetSwapIntervalEXT() == 0,
                  "ApplyChanges forwards SynchronizeWithVerticalRetrace=false");
            const D3DPRESENT_PARAMETERS immediate = ReadPresentationParameters(renderer);
            Check(immediate.PresentationInterval == D3DPRESENT_INTERVAL_IMMEDIATE,
                  "the live D3D9 swap chain applies D3DPRESENT_INTERVAL_IMMEDIATE");
            Check(!renderer.IsDeviceLostEXT(), "the immediate reset leaves the device usable");

            manager_->setSynchronizeWithVerticalRetraceProperty(true);
            manager_->ApplyChanges();
            Check(renderer.GetSwapIntervalEXT() == 1,
                  "ApplyChanges forwards SynchronizeWithVerticalRetrace=true");
            const D3DPRESENT_PARAMETERS synchronized = ReadPresentationParameters(renderer);
            Check(synchronized.PresentationInterval == D3DPRESENT_INTERVAL_ONE,
                  "the live D3D9 swap chain applies D3DPRESENT_INTERVAL_ONE");
            Check(!renderer.IsDeviceLostEXT(), "the synchronized reset leaves the device usable");

            getGraphicsDeviceProperty().Clear(Color(20, 40, 60, 255));
            result_ = failures_ == 0 ? 0 : 1;
            Exit();
        }

    public:
        DirectX9SwapIntervalTest()
        {
            manager_ = std::make_unique<GraphicsDeviceManager>(this);
            manager_->setPreferredBackBufferWidthProperty(64);
            manager_->setPreferredBackBufferHeightProperty(64);
        }

        [[nodiscard]] int Result() const { return result_; }
    };
}

int main()
{
    DirectX9SwapIntervalTest game;
    game.Run();
    return game.Result();
}
