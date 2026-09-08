// SPDX-License-Identifier: MS-PL
// plans/plan_software.md SOFTWARE-122: deterministic public CPU occlusion-query contract.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <array>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;

    BlendState SampleMask(unsigned int mask)
    {
        BlendState state;
        state.setMultiSampleMaskProperty(static_cast<int>(mask));
        return state;
    }

    BlendState NoColorWrites()
    {
        BlendState state;
        state.setColorWriteChannelsProperty(ColorWriteChannels::None);
        return state;
    }
}

class SoftwareOcclusionQueryContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<RenderTarget2D> singleSampleTarget_;
    std::unique_ptr<RenderTarget2D> multiSampleTarget_;
    std::unique_ptr<BasicEffect> basicEffect_;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool condition, const std::string& label)
    {
        ++total_;
        if (condition)
            ++passed_;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
    }

    void BeginTarget(GraphicsDevice& device, RenderTarget2D* target,
                     int stencil = 0)
    {
        device.SetRenderTarget(target);
        device.setViewportProperty(Viewport(0, 0, kSize, kSize));
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     Color::Black, 1.0f, stencil);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
    }

    void DrawQuad(GraphicsDevice& device, float depth = 0.5f,
                  Color color = Color::Red)
    {
        const std::array<VertexPositionColor, 6> vertices{{
            {Vector3(-1.0f,  1.0f, depth), color},
            {Vector3( 1.0f,  1.0f, depth), color},
            {Vector3( 1.0f, -1.0f, depth), color},
            {Vector3(-1.0f,  1.0f, depth), color},
            {Vector3( 1.0f, -1.0f, depth), color},
            {Vector3(-1.0f, -1.0f, depth), color},
        }};
        basicEffect_->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 2);
    }

    template<class Draw>
    int Measure(GraphicsDevice& device, Draw&& draw)
    {
        OcclusionQuery query(device);
        Check(query.HasRenderer() && query.isPixelCountPreciseEXT(),
              "query owns an exact renderer implementation");
        Check(!query.getIsCompleteProperty() && query.getPixelCountProperty() == 0,
              "a fresh query has no completed result");
        query.Begin();
        draw();
        Check(!query.getIsCompleteProperty(),
              "an open synchronous query is not complete before End");
        query.End();
        Check(query.getIsCompleteProperty(),
              "End makes the CPU result immediately complete");
        return query.getPixelCountProperty();
    }

    void CheckFullCoverageAndReset(GraphicsDevice& device)
    {
        BeginTarget(device, singleSampleTarget_.get());
        OcclusionQuery query(device);
        query.Begin();
        DrawQuad(device);
        query.End();
        Check(query.getPixelCountProperty() == kSize * kSize,
              "a full single-sample quad counts every pixel exactly once");

        DrawQuad(device, 0.6f, Color::Green);
        query.Begin();
        query.Begin();
        query.End();
        query.End();
        Check(query.getIsCompleteProperty() && query.getPixelCountProperty() == 0,
              "Begin resets the prior result; repeated Begin/End is harmless and outside draws do not count");
    }

    void CheckRasterGates(GraphicsDevice& device)
    {
        BeginTarget(device, singleSampleTarget_.get());
        RasterizerState scissored;
        scissored.setCullModeProperty(CullMode::None);
        scissored.setScissorTestEnableProperty(true);
        device.setScissorRectangleProperty(Rectangle(2, 1, 3, 4));
        device.setRasterizerStateProperty(scissored);
        Check(Measure(device, [&] { DrawQuad(device); }) == 12,
              "scissor leaves exactly its 3x4 surviving pixels");

        BeginTarget(device, singleSampleTarget_.get());
        AlphaTestEffect rejected(device);
        rejected.setVertexColorEnabledProperty(true);
        rejected.setAlphaFunctionProperty(CompareFunction::Never);
        rejected.Apply();
        const std::array<VertexPositionColor, 6> vertices{{
            {Vector3(-1,  1, 0.2f), Color::White},
            {Vector3( 1,  1, 0.2f), Color::White},
            {Vector3( 1, -1, 0.2f), Color::White},
            {Vector3(-1,  1, 0.2f), Color::White},
            {Vector3( 1, -1, 0.2f), Color::White},
            {Vector3(-1, -1, 0.2f), Color::White},
        }};
        Check(Measure(device, [&] {
                  rejected.Apply();
                  device.DrawUserPrimitives(
                      PrimitiveType::TriangleList, vertices.data(), 0, 2);
              }) == 0,
              "AlphaTestEffect discard contributes no samples");

        BeginTarget(device, singleSampleTarget_.get());
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        DrawQuad(device, 0.2f, Color::Blue);
        Check(Measure(device, [&] { DrawQuad(device, 0.8f); }) == 0,
              "depth-rejected fragments contribute no samples");

        BeginTarget(device, singleSampleTarget_.get(), 0);
        DepthStencilState stencilReject;
        stencilReject.setDepthBufferEnableProperty(false);
        stencilReject.setDepthBufferWriteEnableProperty(false);
        stencilReject.setStencilEnableProperty(true);
        stencilReject.setStencilFunctionProperty(CompareFunction::Equal);
        stencilReject.setReferenceStencilProperty(1);
        device.setDepthStencilStateProperty(stencilReject);
        Check(Measure(device, [&] { DrawQuad(device); }) == 0,
              "stencil-rejected fragments contribute no samples");

        BeginTarget(device, singleSampleTarget_.get());
        device.setBlendStateProperty(NoColorWrites());
        Check(Measure(device, [&] { DrawQuad(device); }) == kSize * kSize,
              "ColorWriteChannels.None does not suppress an occlusion result");
        std::array<Color, kSize * kSize> pixels{};
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        singleSampleTarget_->GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        bool stayedBlack = true;
        for (const Color& pixel : pixels)
            stayedBlack = stayedBlack && pixel == Color::Black;
        Check(stayedBlack, "the color-write-disabled control really left every pixel black");
    }

    void CheckMultiSampleCounts(GraphicsDevice& device)
    {
        BeginTarget(device, multiSampleTarget_.get());
        RasterizerState interior;
        interior.setCullModeProperty(CullMode::None);
        interior.setScissorTestEnableProperty(true);
        device.setScissorRectangleProperty(Rectangle(1, 1, kSize - 2, kSize - 2));
        device.setRasterizerStateProperty(interior);
        device.setBlendStateProperty(SampleMask(0x5u));
        Check(Measure(device, [&] { DrawQuad(device); }) == (kSize - 2) * (kSize - 2) * 2,
              "4x MSAA query counts only the two samples selected by MultiSampleMask");

        BeginTarget(device, multiSampleTarget_.get());
        device.setScissorRectangleProperty(Rectangle(1, 1, kSize - 2, kSize - 2));
        device.setRasterizerStateProperty(interior);
        device.setBlendStateProperty(SampleMask(0xFu));
        Check(Measure(device, [&] { DrawQuad(device); }) == (kSize - 2) * (kSize - 2) * 4,
              "4x MSAA interior coverage counts four samples per pixel");
    }

    void CheckSpriteBatch(GraphicsDevice& device)
    {
        BeginTarget(device, singleSampleTarget_.get());
        Texture2D white(device, 1, 1);
        const Color texel = Color::White;
        white.SetData(&texel, 1);
        SpriteBatch batch(device);
        const int count = Measure(device, [&] {
            batch.Begin();
            batch.Draw(white, Rectangle(0, 0, kSize, kSize), Color::White);
            batch.End();
        });
        Check(count == kSize * kSize,
              "deferred SpriteBatch geometry participates in the same query interval");
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        singleSampleTarget_ = std::make_unique<RenderTarget2D>(
            device, kSize, kSize, false, SurfaceFormat::Color,
            DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::PreserveContents);
        multiSampleTarget_ = std::make_unique<RenderTarget2D>(
            device, kSize, kSize, false, SurfaceFormat::Color,
            DepthFormat::Depth24Stencil8, 4, RenderTargetUsage::PreserveContents);
        basicEffect_ = std::make_unique<BasicEffect>(device);
        basicEffect_->VertexColorEnabled = true;
        basicEffect_->setLightingEnabledProperty(false);
        basicEffect_->setWorldProperty(Matrix::getIdentityProperty());
        basicEffect_->setViewProperty(Matrix::getIdentityProperty());
        basicEffect_->setProjectionProperty(Matrix::getIdentityProperty());

        CheckFullCoverageAndReset(device);
        CheckRasterGates(device);
        CheckMultiSampleCounts(device);
        CheckSpriteBatch(device);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    SoftwareOcclusionQueryContractTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(32);
        graphics_->setPreferredBackBufferHeightProperty(32);
    }

    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    SoftwareOcclusionQueryContractTest game;
    game.Run();
    return game.Result();
}
