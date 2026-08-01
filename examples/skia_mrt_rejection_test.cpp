// SPDX-License-Identifier: MS-PL
// SKIA-87--SKIA-88: prove that the raster backend rejects MRT atomically.
//
// SkCanvas produces one destination colour per draw. CNA's public MRT contract additionally
// permits ShaderEffect fragment programs with distinct outputs at locations 0--3 and independent
// ColorWriteChannels0--3. Replaying one raster command into several surfaces therefore cannot
// preserve the general contract: it would duplicate output 0 instead of producing each slot's
// shader result. The honest raster policy is to reject every otherwise-valid 2--4 target set
// before changing the active target, clearing DiscardContents resources, or drawing any pixels.

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 4;

    bool Near(const Color& actual, const Color& expected, int tolerance = 0)
    {
        return std::abs(static_cast<int>(actual.getRProperty())
                        - static_cast<int>(expected.getRProperty())) <= tolerance
            && std::abs(static_cast<int>(actual.getGProperty())
                        - static_cast<int>(expected.getGProperty())) <= tolerance
            && std::abs(static_cast<int>(actual.getBProperty())
                        - static_cast<int>(expected.getBProperty())) <= tolerance
            && std::abs(static_cast<int>(actual.getAProperty())
                        - static_cast<int>(expected.getAProperty())) <= tolerance;
    }

    Color ReadCenter(RenderTarget2D& target)
    {
        std::vector<Color> pixels(kSize * kSize, Color::Transparent);
        target.GetData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels[(kSize / 2) * kSize + kSize / 2];
    }

    Color ReadCenter(RenderTargetCube& target, CubeMapFace face)
    {
        std::vector<Color> pixels(kSize * kSize, Color::Transparent);
        target.GetData(face, pixels.data(), static_cast<int>(pixels.size()));
        return pixels[(kSize / 2) * kSize + kSize / 2];
    }
}

class SkiaMrtRejectionTest final : public Game
{
public:
    SkiaMrtRejectionTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(16);
        graphics_->setPreferredBackBufferHeightProperty(16);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        premultipliedRed_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color pixel(128, 0, 0, 128);
        premultipliedRed_->SetData(&pixel, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        RenderTarget2D anchor(
            device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        RenderTarget2D a(
            device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        RenderTarget2D discard(
            device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::DiscardContents);
        RenderTarget2D small(
            device, kSize - 1, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        RenderTargetCube cube(
            device, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);

        const Color anchorBase(0, 0, 255, 255);
        const Color aSeed(20, 40, 60, 255);
        const Color discardSeed(70, 90, 110, 255);
        const Color cubePositiveSeed(120, 140, 160, 255);
        const Color cubeNegativeSeed(170, 190, 210, 255);
        Seed(device, anchor, anchorBase);
        Seed(device, a, aSeed);
        Seed(device, discard, discardSeed);
        Seed(device, small, Color(11, 22, 33, 255));
        Seed(device, cube, CubeMapFace::PositiveX, cubePositiveSeed);
        Seed(device, cube, CubeMapFace::NegativeX, cubeNegativeSeed);

        device.SetRenderTarget(&anchor);
        device.setViewportProperty(Viewport(1, 1, 2, 2));
        device.setScissorRectangleProperty(Rectangle(1, 0, 2, 3));

        const std::vector<RenderTargetBinding> two{
            RenderTargetBinding(&a), RenderTargetBinding(&discard)};
        const std::vector<RenderTargetBinding> three{
            RenderTargetBinding(&a), RenderTargetBinding(&discard),
            RenderTargetBinding(static_cast<Texture*>(&cube), CubeMapFace::PositiveX)};
        const std::vector<RenderTargetBinding> four{
            RenderTargetBinding(&a), RenderTargetBinding(&discard),
            RenderTargetBinding(static_cast<Texture*>(&cube), CubeMapFace::PositiveX),
            RenderTargetBinding(static_cast<Texture*>(&cube), CubeMapFace::NegativeX)};

        ExpectFailure(device, anchor, two, "multiple render targets", "valid two-target set");
        ExpectFailure(device, anchor, three, "multiple render targets", "valid three-target set");
        ExpectFailure(device, anchor, four, "multiple render targets", "valid four-target set");

        ExpectFailure(
            device, anchor,
            {RenderTargetBinding(&a), RenderTargetBinding(&small)},
            "matching dimensions", "dimension validation precedes backend MRT refusal");
        ExpectFailure(
            device, anchor,
            {RenderTargetBinding(&a), RenderTargetBinding(&a)},
            "same render-target subresource", "duplicate validation precedes backend MRT refusal");
        ExpectFailure(
            device, anchor,
            {RenderTargetBinding(&a), RenderTargetBinding()},
            "null render target", "null validation precedes backend MRT refusal");
        ExpectFailure(
            device, anchor,
            {RenderTargetBinding(&a), RenderTargetBinding(&discard),
             RenderTargetBinding(static_cast<Texture*>(&cube), CubeMapFace::PositiveX),
             RenderTargetBinding(static_cast<Texture*>(&cube), CubeMapFace::NegativeX),
             RenderTargetBinding(&small)},
            "at most 4", "public four-target ceiling precedes descriptor processing");

        Check(!device.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets),
              "MultipleRenderTargets remains unadvertised");

        // The anchor is still current. This blended draw proves the failed submissions neither
        // changed the destination nor left deferred work that could replay into a candidate.
        spriteBatch_->Begin(
            SpriteSortMode::Deferred, BlendState::AlphaBlend,
            const_cast<SamplerState*>(&SamplerState::PointClamp),
            nullptr, nullptr, nullptr, Matrix::getIdentityProperty());
        spriteBatch_->Draw(
            *premultipliedRed_, Rectangle(0, 0, kSize, kSize), Rectangle(0, 0, 1, 1),
            Color::White);
        spriteBatch_->End();
        device.SetRenderTargets({});

        Check(Near(ReadCenter(anchor), Color(128, 0, 127, 255), 2),
              "post-refusal AlphaBlend draw reaches only the previously active target");
        Check(Near(ReadCenter(a), aSeed),
              "preserved candidate receives no clear or replayed draw");
        Check(Near(ReadCenter(discard), discardSeed),
              "failed MRT bind does not apply DiscardContents clearing");
        Check(Near(ReadCenter(cube, CubeMapFace::PositiveX), cubePositiveSeed),
              "positive cube face receives no partial MRT write");
        Check(Near(ReadCenter(cube, CubeMapFace::NegativeX), cubeNegativeSeed),
              "negative cube face receives no partial MRT write");

        std::printf("=== %d checks, %d failures ===\n", checks_, failures_);
        Exit();
    }

private:
    void Check(bool condition, const std::string& label)
    {
        ++checks_;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition) ++failures_;
    }

    void CheckAnchorState(GraphicsDevice& device, RenderTarget2D& anchor,
                          const std::string& label)
    {
        const auto current = device.GetRenderTargets();
        const auto& viewport = device.getViewportProperty();
        Check(current.size() == 1
              && current[0].getRenderTargetProperty() == &anchor
              && viewport.getXProperty() == 1
              && viewport.getYProperty() == 1
              && viewport.getWidthProperty() == 2
              && viewport.getHeightProperty() == 2
              && device.getScissorRectangleProperty() == Rectangle(1, 0, 2, 3),
              label + " leaves binding, viewport, and scissor unchanged");
    }

    void ExpectFailure(GraphicsDevice& device, RenderTarget2D& anchor,
                       const std::vector<RenderTargetBinding>& bindings,
                       const std::string& expectedMessage,
                       const std::string& label)
    {
        bool threw = false;
        std::string message;
        try
        {
            device.SetRenderTargets(bindings);
        }
        catch (const std::exception& exception)
        {
            threw = true;
            message = exception.what();
        }
        Check(threw && message.find(expectedMessage) != std::string::npos,
              label + " throws the expected diagnostic");
        CheckAnchorState(device, anchor, label);
    }

    static void Seed(GraphicsDevice& device, RenderTarget2D& target, const Color& color)
    {
        device.SetRenderTarget(&target);
        device.Clear(color);
        device.SetRenderTargets({});
    }

    static void Seed(GraphicsDevice& device, RenderTargetCube& target,
                     CubeMapFace face, const Color& color)
    {
        device.SetRenderTarget(&target, face);
        device.Clear(color);
        device.SetRenderTargets({});
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> premultipliedRed_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaMrtRejectionTest game;
    game.Run();
    return game.Result();
}
