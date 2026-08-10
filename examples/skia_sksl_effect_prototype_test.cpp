// SPDX-License-Identifier: MS-PL
// SKIA-91: explicit, bounded SkSL SpriteBatch shader proof. Untagged GLSL remains invalid.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const std::string kSkiaSkslMarker = "CNA_SKIA_SKSL_V1";
    const std::string kSwapRedGreenSksl = R"(
        uniform shader cnaTexture0;
        uniform float4 cnaTint;
        half4 main(float2 sourcePixel) {
            half4 sampled = cnaTexture0.eval(sourcePixel);
            return half4(sampled.g * cnaTint.g,
                         sampled.r * cnaTint.r,
                         0.0,
                         sampled.a * cnaTint.a);
        }
    )";
}

class SkiaSkslEffectPrototypeTest final : public Game
{
public:
    SkiaSkslEffectPrototypeTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(8);
        graphics_->setPreferredBackBufferHeightProperty(4);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        texture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 2, 1,
            std::vector<std::uint8_t>{255, 0, 0, 255, 0, 255, 0, 255}));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        ShaderEffect effect(device, kSkiaSkslMarker, kSwapRedGreenSksl);
        Check(effect.IsEffectValid() && effect.GetEffectRendererPtr() != nullptr,
              "explicit SkSL marker compiles a real Skia effect renderer");

        device.Clear(Color(17, 29, 43, 255));
        spriteBatch_->Begin(
            SpriteSortMode::Deferred, BlendState::Opaque,
            const_cast<SamplerState*>(&SamplerState::PointClamp),
            nullptr, nullptr, &effect);
        spriteBatch_->Draw(
            *texture_, Rectangle(0, 0, 8, 4), Rectangle(0, 0, 2, 1), Color::White);
        spriteBatch_->End();

        Color left = Color::Transparent;
        Color right = Color::Transparent;
        const Rectangle leftRegion(1, 1, 1, 1);
        const Rectangle rightRegion(6, 1, 1, 1);
        device.GetBackBufferData(&leftRegion, &left, 0, 1);
        device.GetBackBufferData(&rightRegion, &right, 0, 1);
        Check(left == Color(0, 255, 0, 255) && right == Color(255, 0, 0, 255),
              "runtime shader samples cnaTexture0 and changes both sprite texels");

        ShaderEffect malformed(device, kSkiaSkslMarker,
            "uniform shader cnaTexture0; uniform float4 cnaTint; half4 main(");
        Check(!malformed.IsEffectValid() && malformed.GetEffectRendererPtr() != nullptr,
              "malformed tagged SkSL retains an invalid renderer with compiler diagnostics");
        std::string malformedError;
        try
        {
            spriteBatch_->Begin(
                SpriteSortMode::Deferred, BlendState::Opaque,
                nullptr, nullptr, nullptr, &malformed);
        }
        catch (const std::exception& exception)
        {
            malformedError = exception.what();
        }
        Check(malformedError.find("SkSL compilation failed") != std::string::npos,
              "SpriteBatch reports the tagged SkSL compilation failure exactly");

        ShaderEffect wrongAbi(device, kSkiaSkslMarker,
            "uniform float4 cnaTint; half4 main(float2 p) { return half4(cnaTint); }");
        Check(!wrongAbi.IsEffectValid()
              && wrongAbi.GetEffectRendererPtr()->GetCompileError().find("cnaTexture0")
                  != std::string::npos,
              "compiled SkSL with the wrong child ABI is rejected clearly");

        ShaderEffect oversized(device, kSkiaSkslMarker, std::string(65537u, ' '));
        Check(!oversized.IsEffectValid()
              && oversized.GetEffectRendererPtr()->GetCompileError().find("65536-byte")
                  != std::string::npos,
              "source-size limit rejects before invoking the SkSL compiler");

        // A failed Begin clears the renderer's pending effect pointer. Prove ordinary rendering is
        // immediately safe and does not accidentally retain the last successful runtime shader.
        device.Clear(Color::Black);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        spriteBatch_->Draw(
            *texture_, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 1, 1), Color::White);
        spriteBatch_->End();
        Color stock = Color::Transparent;
        const Rectangle stockRegion(1, 1, 1, 1);
        device.GetBackBufferData(&stockRegion, &stock, 0, 1);
        Check(stock == Color(255, 0, 0, 255),
              "stock SpriteBatch is reusable after tagged effect rejection");

        std::printf("=== %d checks, %d failures ===\n", checks_, failures_);
        Exit();
    }

private:
    void Check(bool condition, const char* label)
    {
        ++checks_;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaSkslEffectPrototypeTest game;
    game.Run();
    return game.Result();
}
