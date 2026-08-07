// SPDX-License-Identifier: MS-PL
// SKIA-89: public proof of the current custom-effect boundary before any SkSL opt-in exists.

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
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
    const std::string kVertexGlsl = R"(#version 300 es
layout(location = 0) in vec2 aPos;
void main() { gl_Position = vec4(aPos, 0.0, 1.0); }
)";
    const std::string kFragmentGlsl = R"(#version 300 es
precision mediump float;
out vec4 color;
void main() { color = vec4(1.0, 0.0, 0.0, 1.0); }
)";
}

class SkiaEffectBoundaryTest final : public Game
{
public:
    SkiaEffectBoundaryTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(8);
        graphics_->setPreferredBackBufferHeightProperty(8);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        white_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(
                device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255}));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        ShaderEffect effect(device, kVertexGlsl, kFragmentGlsl);
        Check(!effect.IsEffectValid() && effect.GetEffectBackendPtr() == nullptr,
              "untagged GLSL does not fabricate a Skia effect backend");
        Check(effect.getVertexSourceProperty() == kVertexGlsl
              && effect.getFragmentSourceProperty() == kFragmentGlsl,
              "invalid effect retains both opaque source strings exactly");
        Check(!device.SupportsCapability(CNA::GraphicsCapability::CustomEffects),
              "CustomEffects remains unadvertised");

        bool settersSafe = true;
        try
        {
            effect.Apply();
            const float matrix[16] = {
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                0, 0, 0, 1,
            };
            const float scalars[2] = {0.25f, 0.75f};
            const float vectors[4] = {0.0f, 1.0f, 2.0f, 3.0f};
            effect.SetUniformFloat("uFloat", 0.5f);
            effect.SetUniformInt("uInt", 2);
            effect.SetUniformVec2("uVec2", 1.0f, 2.0f);
            effect.SetUniformVec3("uVec3", 1.0f, 2.0f, 3.0f);
            effect.SetUniformVec4("uVec4", 1.0f, 2.0f, 3.0f, 4.0f);
            effect.SetUniformMat4("uMatrix", matrix);
            effect.SetUniformFloatArray("uFloats", scalars, 2);
            effect.SetUniformVec2Array("uVectors", vectors, 2);
            effect.SetTexture(1, *white_);
        }
        catch (const std::exception&)
        {
            settersSafe = false;
        }
        Check(settersSafe,
              "invalid effect Apply/uniform/Texture2D calls remain harmless without drawing");

        std::unique_ptr<Effect> clone(effect.Clone());
        auto* shaderClone = dynamic_cast<ShaderEffect*>(clone.get());
        Check(shaderClone != nullptr
              && !shaderClone->IsEffectValid()
              && shaderClone->getVertexSourceProperty() == kVertexGlsl
              && shaderClone->getFragmentSourceProperty() == kFragmentGlsl,
              "clone retains sources and remains honestly invalid");

        bool customBeginRejected = false;
        std::string customError;
        try
        {
            spriteBatch_->Begin(
                SpriteSortMode::Deferred, BlendState::Opaque,
                nullptr, nullptr, nullptr, &effect);
        }
        catch (const std::exception& exception)
        {
            customBeginRejected = true;
            customError = exception.what();
        }
        Check(customBeginRejected
              && customError.find("custom Effects") != std::string::npos,
              "custom SpriteBatch Begin rejects before accepting an invalid program");

        device.Clear(Color::Black);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        spriteBatch_->Draw(
            *white_, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 1, 1),
            Color(0, 255, 0, 255));
        spriteBatch_->End();
        Color center = Color::Transparent;
        const Rectangle region(4, 4, 1, 1);
        device.GetBackBufferData(&region, &center, 0, 1);
        Check(center.getRProperty() == 0
              && center.getGProperty() == 255
              && center.getBProperty() == 0
              && center.getAProperty() == 255,
              "SpriteBatch remains reusable on its stock path after custom-effect refusal");

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
    std::unique_ptr<Texture2D> white_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaEffectBoundaryTest game;
    game.Run();
    return game.Result();
}
