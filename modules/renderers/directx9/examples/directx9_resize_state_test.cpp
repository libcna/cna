// SPDX-License-Identifier: MS-PL
// FULLSCREEN-002: native presentation resets must retain logical geometry and invalidate cached state.
#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "common/PixelTestGame.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::DirectX9::DirectX9Renderer;
using Rect = Microsoft::Xna::Framework::Rectangle;

class ResizeStateTest final : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> manager_;
    void ExpectPhysical(const char* label, int x, int y, const Color& expected)
    {
        std::uint8_t pixel[4]{};
        getGraphicsDeviceProperty().GetRenderer().ReadBackbuffer(x, y, 1, 1, pixel);
        ExpectTrue(label, pixel[0] == expected.getRProperty() &&
                          pixel[1] == expected.getGProperty() &&
                          pixel[2] == expected.getBProperty());
    }
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<DirectX9Renderer&>(dev.GetRenderer());
        dev.setBlendStateProperty(BlendState::AlphaBlend);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        getWindowProperty().EndScreenDeviceChange("", 160, 96);
        ExpectTrue("successful resize is ready to draw", renderer.TryBeginDrawEXT());
        dev.setBlendStateProperty(BlendState::AlphaBlend);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        DWORD blend = 0, depth = 1;
        renderer.GetDeviceEXT()->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend);
        renderer.GetDeviceEXT()->GetRenderState(D3DRS_ZENABLE, &depth);
        std::printf("native blend=%lu depth=%lu\n", blend, depth);
        ExpectTrue("successful resize invalidates cached AlphaBlend", blend == TRUE);
        ExpectTrue("successful resize invalidates cached DepthStencil.None", depth == D3DZB_FALSE);
        DWORD cull = 0;
        renderer.GetDeviceEXT()->GetRenderState(D3DRS_CULLMODE, &cull);
        ExpectTrue("successful resize invalidates cached CullNone", cull == D3DCULL_NONE);
        dev.Present();
        ExpectTrue("logical viewport remains 64 by 64 after resize",
                   dev.getViewportProperty().getWidthProperty() == 64 &&
                   dev.getViewportProperty().getHeightProperty() == 64);
        float lx, ly, wx, wy;
        ExpectTrue("letterbox input center maps to logical center",
                   renderer.TransformWindowToLogical(80, 48, lx, ly) && lx == 32 && ly == 32);
        ExpectTrue("letterbox input inverse preserves window coordinates",
                   renderer.TransformLogicalToWindow(lx, ly, wx, wy) && wx == 80 && wy == 48);

        Texture2D white(dev, 1, 1);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 1);
        SpriteBatch batch(dev);
        auto draw = [&](const Rect& rect, const Color& color)
        {
            batch.Begin(SpriteSortMode::BackToFront, BlendState::AlphaBlend,
                        &SamplerState::PointClamp, nullptr, nullptr, nullptr,
                        Matrix::getIdentityProperty());
            batch.Draw(white, rect, color);
            batch.End();
        };
        for (int frame = 0; frame < 5; ++frame)
        {
            if (frame == 3)
            {
                getWindowProperty().EndScreenDeviceChange("", 208, 112);
                ExpectTrue("second resize remains ready", renderer.TryBeginDrawEXT());
                dev.Present();
            }
            const int pos = 4 + frame * 8;
            draw(Rect(0, 0, 64, 64), Color::Red);
            draw(Rect(pos, 24, 4, 4), Color::Green);
            int vx, vy, vw, vh;
            renderer.GetDefaultViewportRect(vx, vy, vw, vh);
            ExpectPhysical("moving sprite is visible after successful resize",
                           vx + (pos + 2) * vw / 64, vy + 26 * vh / 64, Color::Green);
            if (frame > 0)
                ExpectPhysical("previous sprite position is repainted without Clear",
                               vx + (pos - 6) * vw / 64, vy + 26 * vh / 64, Color::Red);
            ExpectPhysical("background covers the enlarged far edge",
                           vx + 60 * vw / 64, vy + 60 * vh / 64, Color::Red);
            dev.Present();
        }

        using CNA::Internal::Renderers::CnaPresentationMode;
        renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Overscan));
        dev.Present();
        draw(Rect(0, 0, 64, 64), Color::Red);
        ExpectPhysical("Overscan fills the whole physical target", 200, 100, Color::Red);
        draw(Rect(0, 0, 64, 16), Color::Green);
        ExpectPhysical("Overscan crops logical coordinates at the top", 100, 2, Color::Green);
        ExpectPhysical("Overscan keeps the middle outside the top stripe", 100, 56, Color::Red);
    }
public:
    ResizeStateTest() : manager_(std::make_unique<GraphicsDeviceManager>(this))
    {
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }
};
int main() { return CNA::Examples::RunPixelTest<ResizeStateTest>(); }
