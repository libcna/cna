// SPDX-License-Identifier: MS-PL
// SKIA-122: generated blending is identical across SpriteBatch modes and explicit SkSL effects.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr const char* kSkiaSkslMarker = "CNA_SKIA_SKSL_V1";
    constexpr const char* kIdentitySksl = R"(
        uniform shader cnaTexture0;
        uniform float4 cnaTint;
        half4 main(float2 sourcePixel) {
            half4 sampled = cnaTexture0.eval(sourcePixel);
            return half4(sampled.rgb * cnaTint.rgb, sampled.a * cnaTint.a);
        }
    )";

    const Color kDestination(192, 160, 128, 192);
    const Color kExpected(128, 128, 112, 128);

    [[nodiscard]] BlendState GeneratedState()
    {
        BlendState state;
        state.setColorSourceBlendProperty(Blend::One);
        state.setColorDestinationBlendProperty(Blend::One);
        state.setColorBlendFunctionProperty(BlendFunction::ReverseSubtract);
        state.setAlphaSourceBlendProperty(Blend::One);
        state.setAlphaDestinationBlendProperty(Blend::Zero);
        state.setAlphaBlendFunctionProperty(BlendFunction::Add);
        return state;
    }

    [[nodiscard]] const char* ModeName(SpriteSortMode mode)
    {
        switch (mode)
        {
            case SpriteSortMode::Deferred: return "Deferred";
            case SpriteSortMode::Immediate: return "Immediate";
            case SpriteSortMode::Texture: return "Texture";
            case SpriteSortMode::BackToFront: return "BackToFront";
            case SpriteSortMode::FrontToBack: return "FrontToBack";
        }
        return "invalid";
    }

    [[nodiscard]] bool Near(Color actual, Color expected, int tolerance = 3)
    {
        const auto channel = [tolerance](int left, int right)
        {
            return std::abs(left - right) <= tolerance;
        };
        return channel(actual.getRProperty(), expected.getRProperty())
            && channel(actual.getGProperty(), expected.getGProperty())
            && channel(actual.getBProperty(), expected.getBProperty())
            && channel(actual.getAProperty(), expected.getAProperty());
    }
}

class SkiaGeneratedBlendBatchModesTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> translucentTexture_;
    std::unique_ptr<Texture2D> secondaryTexture_;
    bool finished_ = false;
    int checks_ = 0;
    int failures_ = 0;

    void Check(bool condition, const std::string& label, Color actual = Color::Transparent)
    {
        ++checks_;
        std::printf("[%s] %s: got=(%d,%d,%d,%d)\n", condition ? "PASS" : "FAIL",
                    label.c_str(), actual.getRProperty(), actual.getGProperty(),
                    actual.getBProperty(), actual.getAProperty());
        if (!condition)
            ++failures_;
    }

    [[nodiscard]] Color Read(int x = 0)
    {
        Color result(0, 0, 0, 0);
        const Rectangle region(x, 0, 1, 1);
        getGraphicsDeviceProperty().GetBackBufferData(&region, &result, 0, 1);
        return result;
    }

    [[nodiscard]] Color DrawGenerated(Texture2D& source, SpriteSortMode mode, Effect* effect)
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(kDestination);
        spriteBatch_->Begin(mode, GeneratedState(),
                            const_cast<SamplerState*>(&SamplerState::PointClamp),
                            nullptr, nullptr, effect);
        // Two distinct textures force Texture mode through its actual stable-sort path. They are
        // non-overlapping, so every mode must still produce the same primary pixel.
        spriteBatch_->Draw(*secondaryTexture_, Rectangle(1, 0, 1, 1), Rectangle(0, 0, 1, 1),
                           Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, 0.75f);
        spriteBatch_->Draw(source, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1),
                           Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, 0.25f);
        spriteBatch_->End();
        return Read();
    }

    void DrawStock(Color tint)
    {
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr);
        spriteBatch_->Draw(*secondaryTexture_, Rectangle(0, 0, 1, 1),
                           Rectangle(0, 0, 1, 1), tint);
        spriteBatch_->End();
    }

protected:
    void Initialize() override
    {
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        // These are already-premultiplied working components for logical (128,64,32,128).
        translucentTexture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{64, 32, 16, 128}));
        secondaryTexture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
    }

    void Draw(const GameTime&) override
    {
        if (finished_)
            return;
        finished_ = true;
        auto& device = getGraphicsDeviceProperty();

        RenderTarget2D targetSource(device, 1, 1, false, SurfaceFormat::Color, DepthFormat::None,
                                    0, RenderTargetUsage::PreserveContents);
        device.SetRenderTarget(&targetSource);
        device.Clear(Color(128, 64, 32, 128));
        device.SetRenderTarget(nullptr);

        ShaderEffect identityEffect(device, kSkiaSkslMarker, kIdentitySksl);
        Check(identityEffect.IsEffectValid(), "identity SkSL effect compiles");

        constexpr SpriteSortMode modes[] = {
            SpriteSortMode::Deferred,
            SpriteSortMode::Immediate,
            SpriteSortMode::Texture,
            SpriteSortMode::BackToFront,
            SpriteSortMode::FrontToBack,
        };
        for (SpriteSortMode mode : modes)
        {
            const Color texturePixel = DrawGenerated(*translucentTexture_, mode, nullptr);
            Check(Near(texturePixel, kExpected),
                  std::string(ModeName(mode)) + " translucent Texture2D", texturePixel);

            const Color targetPixel = DrawGenerated(targetSource, mode, nullptr);
            Check(Near(targetPixel, kExpected),
                  std::string(ModeName(mode)) + " translucent RenderTarget2D", targetPixel);

            const Color effectPixel = DrawGenerated(*translucentTexture_, mode, &identityEffect);
            Check(Near(effectPixel, kExpected),
                  std::string(ModeName(mode)) + " translucent explicit SkSL effect", effectPixel);
        }

        device.Clear(Color::Black);
        DrawStock(Color(211, 37, 83, 255));
        const Color afterSuccess = Read();
        Check(Near(afterSuccess, Color(211, 37, 83, 255), 1),
              "stock Opaque state is restored after successful generated/effect batches", afterSuccess);

        ShaderEffect malformed(device, kSkiaSkslMarker,
            "uniform shader cnaTexture0; uniform float4 cnaTint; half4 main(");
        std::string failure;
        try
        {
            spriteBatch_->Begin(SpriteSortMode::Deferred, GeneratedState(),
                                const_cast<SamplerState*>(&SamplerState::PointClamp),
                                nullptr, nullptr, &malformed);
        }
        catch (const std::exception& exception)
        {
            failure = exception.what();
        }
        Check(failure.find("SkSL compilation failed") != std::string::npos,
              "malformed effect rejects the generated batch before Begin commits");

        device.Clear(Color::Black);
        DrawStock(Color(29, 197, 71, 255));
        const Color afterFailure = Read();
        Check(Near(afterFailure, Color(29, 197, 71, 255), 1),
              "stock Opaque state is reusable after generated/effect Begin failure", afterFailure);

        std::printf("=== %d/%d PASS ===\n", checks_ - failures_, checks_);
        Exit();
    }

public:
    SkiaGeneratedBlendBatchModesTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(2);
        graphics_->setPreferredBackBufferHeightProperty(1);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }
};

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    SkiaGeneratedBlendBatchModesTest game;
    game.Run();
    return game.Result();
}
