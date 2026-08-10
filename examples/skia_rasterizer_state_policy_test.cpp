// SPDX-License-Identifier: MS-PL
// SKIA-58: the CPU SpriteBatch path uses only RasterizerState.ScissorTestEnable.
// Cull/depth-bias/MSAA fields remain constructible 3D state, but do not alter a 2D sprite;
// WireFrame is rejected explicitly because the raster renderer neither advertises nor emulates it.

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;

    [[nodiscard]] bool isColor(const Color& actual, const Color& expected)
    {
        return actual.getRProperty() == expected.getRProperty()
            && actual.getGProperty() == expected.getGProperty()
            && actual.getBProperty() == expected.getBProperty()
            && actual.getAProperty() == expected.getAProperty();
    }
}

class SkiaRasterizerStatePolicyTest final : public Game
{
public:
    SkiaRasterizerStatePolicyTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kSize);
        graphics_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        batch_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;

        auto& device = getGraphicsDeviceProperty();
        Check(!device.SupportsCapability(CNA::GraphicsCapability::WireFrame),
              "WireFrame is not advertised by the raster renderer");
        Check(!device.SupportsCapability(CNA::GraphicsCapability::ThreeD),
              "3D is not advertised by the raster renderer");
        Check(!device.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing),
              "MSAA is not advertised by the raster renderer");

        RasterizerState solid;
        solid.setCullModeProperty(CullMode::CullClockwiseFace);
        solid.setDepthBiasProperty(-1000.0f);
        solid.setSlopeScaleDepthBiasProperty(1000.0f);
        solid.setMultiSampleAntiAliasProperty(false);
        Check(solid.getCullModeProperty() == CullMode::CullClockwiseFace
                  && solid.getDepthBiasProperty() == -1000.0f
                  && solid.getSlopeScaleDepthBiasProperty() == 1000.0f
                  && !solid.getMultiSampleAntiAliasProperty()
                  && solid.getFillModeProperty() == FillMode::Solid,
              "3D-only RasterizerState fields construct and retain their values");

        device.Clear(Color::Black);
        DrawSprite(solid, Color(255, 0, 0, 255));
        Check(isColor(SampleCentre(), Color(255, 0, 0, 255)),
              "solid SpriteBatch draw ignores cull/depth-bias/MSAA fields");

        RasterizerState wireFrame;
        wireFrame.setFillModeProperty(FillMode::WireFrame);
        Check(wireFrame.getFillModeProperty() == FillMode::WireFrame,
              "WireFrame RasterizerState construction succeeds");

        bool wireFrameRejected = false;
        try
        {
            DrawSprite(wireFrame, Color(0, 255, 0, 255));
        }
        catch (const std::runtime_error& error)
        {
            wireFrameRejected = std::string(error.what()).find("FillMode::WireFrame") != std::string::npos;
        }
        Check(wireFrameRejected, "WireFrame SpriteBatch use fails with an actionable Skia error");

        // Begin must remain reusable after the rejected state.  A default solid state also proves
        // the rejected request did not mutate the previous 2D raster state.
        device.Clear(Color::Black);
        RasterizerState noCull = RasterizerState::CullNone;
        DrawSprite(noCull, Color(0, 255, 0, 255));
        Check(isColor(SampleCentre(), Color(0, 255, 0, 255)),
              "solid SpriteBatch remains usable after a rejected WireFrame request");
        Exit();
    }

private:
    void DrawSprite(RasterizerState& state, Color color)
    {
        SamplerState point = SamplerState::PointClamp;
        batch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &state);
        batch_->Draw(*white_, Rectangle(0, 0, kSize, kSize), color);
        batch_->End();
    }

    [[nodiscard]] Color SampleCentre()
    {
        Color result(0, 0, 0, 0);
        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&centre, &result, 0, 1);
        return result;
    }

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> batch_;
    std::unique_ptr<Texture2D> white_;
    bool finished_ = false;
    int failures_ = 0;
};

int main()
{
    SkiaRasterizerStatePolicyTest game;
    game.Run();
    return game.Result();
}
