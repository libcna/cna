// SPDX-License-Identifier: MS-PL
// SKIA-92: exact reflected uniform setters and bounded additional 2D SkSL child snapshots.

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const std::string kMarker = "CNA_SKIA_SKSL_V1";
    const std::string kUniformTextureSksl = R"(
        uniform shader cnaTexture0;
        uniform shader cnaTexture1;
        uniform float4 cnaTint;
        uniform float uFloat;
        uniform int uInt;
        uniform float2 uVec2;
        uniform float3 uVec3;
        uniform float4 uVec4;
        uniform float4x4 uMatrix;
        uniform float uFloats[2];
        uniform float2 uVectors[2];

        half4 main(float2 sourcePixel) {
            half4 primary = cnaTexture0.eval(sourcePixel);
            half4 secondary = cnaTexture1.eval(float2(0.5, 0.5));
            float signal = (uFloat + float(uInt) + uVec2.y + uVec3.z + uVec4.w
                          + uMatrix[2][1] + uFloats[1] + uVectors[1].x) / 8.0;
            return half4(primary.r * signal * cnaTint.r,
                         signal * cnaTint.g,
                         secondary.b * signal * cnaTint.b,
                         cnaTint.a);
        }
    )";
}

class SkiaSkslUniformTextureTest final : public Game
{
public:
    SkiaSkslUniformTextureTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(12);
        graphics_->setPreferredBackBufferHeightProperty(6);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        primary_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 3, 1,
            std::vector<std::uint8_t>{
                0, 255, 0, 255,
                255, 0, 0, 255,
                255, 255, 0, 255,
            }));
        secondary_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{0, 255, 0, 255}));
        cloneSecondary_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{0, 0, 255, 255}));
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        ShaderEffect effect(device, kMarker, kUniformTextureSksl);
        Check(effect.IsEffectValid(), "supported reflected uniform/child set compiles");

        effect.SetUniformFloat("uFloat", 1.0f);
        effect.SetUniformInt("uInt", 1);
        effect.SetUniformVec2("uVec2", 0.0f, 1.0f);
        effect.SetUniformVec3("uVec3", 0.0f, 0.0f, 1.0f);
        effect.SetUniformVec4("uVec4", 0.0f, 0.0f, 0.0f, 1.0f);
        float matrix[16] = {};
        matrix[9] = 1.0f; // column 2, row 1; SkSL reads uMatrix[2][1].
        effect.SetUniformMat4("uMatrix", matrix);
        const float floats[2] = {0.0f, 1.0f};
        const float vectors[4] = {0.0f, 0.0f, 1.0f, 0.0f};
        effect.SetUniformFloatArray("uFloats", floats, 2);
        effect.SetUniformVec2Array("uVectors", vectors, 2);
        effect.SetTexture(1, *secondary_);
        const Color updatedSecondary = Color(0, 0, 255, 255);
        secondary_->SetData(&updatedSecondary, 1);

        Check(ThrowsContaining([&] { effect.SetUniformFloat("missing", 1.0f); }, "cannot find"),
              "missing uniform name rejects");
        Check(ThrowsContaining([&] { effect.SetUniformFloat("uVec2", 1.0f); }, "non-array float"),
              "wrong scalar/vector setter rejects");
        Check(ThrowsContaining([&] { effect.SetUniformVec4("cnaTint", 1, 1, 1, 1); }, "reserved"),
              "reserved per-draw cnaTint rejects public writes");
        Check(ThrowsContaining([&] { effect.SetUniformMat4("uMatrix", nullptr); }, "null"),
              "null matrix rejects before copying");
        Check(ThrowsContaining([&] { effect.SetUniformFloatArray("uFloats", floats, 1); }, "declared 2"),
              "wrong scalar-array count rejects");
        Check(ThrowsContaining([&] { effect.SetUniformVec2Array("uVectors", nullptr, 2); }, "null"),
              "null vector-array data rejects");

        auto* renderer = effect.GetEffectRendererPtr();
        Check(ThrowsContaining([&] { renderer->BindTexture(0, &primary_->GetRenderer()); }, "reserved"),
              "unit zero remains SpriteBatch-reserved");
        Check(ThrowsContaining([&] { renderer->BindTexture(2, &primary_->GetRenderer()); }, "does not declare"),
              "undeclared additional 2D child rejects");
        Check(ThrowsContaining([&] { renderer->BindTexture(1, nullptr); }, "null"),
              "null additional Texture2D rejects");

        // SKIA-149: this effect's own source never calls cnaSampleCubeEXT/cnaSampleVolumeEXT, so
        // it declares no cnaCubeFace*/cnaVolumeAtlas0 children -- binding either still rejects for
        // exactly this effect, not because cube/volume sampling is unsupported in general (see
        // Skia_CubeVolume_Effect_Binding for the positive case, an effect that does call them).
        TextureCube cube(device, 1, false, SurfaceFormat::Color);
        Texture3D volume(device, 1, 1, 1, false, SurfaceFormat::Color);
        Check(ThrowsContaining([&] { effect.SetTexture(1, cube); }, "cnaSampleCubeEXT"),
              "TextureCube child rejects explicitly for an effect that never calls cnaSampleCubeEXT");
        Check(ThrowsContaining([&] { effect.SetTexture(1, volume); }, "cnaSampleVolumeEXT"),
              "Texture3D child rejects explicitly for an effect that never calls cnaSampleVolumeEXT");

        const auto originalPixel = Render(effect, Color(255, 128, 255, 255));
        Check(originalPixel.first == Color(255, 128, 255, 255)
              && originalPixel.second == Color(17, 29, 43, 255),
              "all setters, matrix, tint, source rect, transform and post-bind texture update render exactly");

        secondary_->Dispose();
        Check(ThrowsContaining([&] {
                  spriteBatch_->Begin(
                      SpriteSortMode::Deferred, BlendState::Opaque,
                      nullptr, nullptr, nullptr, &effect);
              }, "SetTexture(1"),
              "disposed additional Texture2D expires safely before Begin");
        effect.SetTexture(1, *cloneSecondary_);

        std::unique_ptr<Effect> cloneBase(effect.Clone());
        auto* clone = dynamic_cast<ShaderEffect*>(cloneBase.get());
        Check(clone && clone->IsEffectValid(), "clone recompiles the same supported SkSL ABI");
        Check(ThrowsContaining([&] {
                  spriteBatch_->Begin(
                      SpriteSortMode::Deferred, BlendState::Opaque,
                      nullptr, nullptr, nullptr, clone);
              }, "SetTexture(1"),
              "clone does not alias the original additional texture binding");

        auto cloneTexture = Texture2D::CreateFromPixels(
            device, 1, 1, std::vector<std::uint8_t>{0, 0, 255, 255});
        clone->SetTexture(1, cloneTexture);
        const auto clonePixel = Render(*clone, Color::White);
        Check(clonePixel.first == Color(0, 0, 0, 255),
              "clone starts with an independent zero-initialized user-uniform block");
        Check(Render(effect, Color(255, 128, 255, 255)).first == Color(255, 128, 255, 255),
              "clone draws and failed validation do not mutate original effect state");

        ShaderEffect unsupportedUniform(device, kMarker, R"(
            uniform shader cnaTexture0;
            uniform float4 cnaTint;
            uniform float3x3 unsupportedMatrix;
            half4 main(float2 p) { return cnaTexture0.eval(p); }
        )");
        Check(!unsupportedUniform.IsEffectValid()
              && unsupportedUniform.GetEffectRendererPtr()->GetCompileError().find("unsupportedMatrix")
                  != std::string::npos,
              "unsupported reflected type rejects at compile/ABI validation");

        ShaderEffect unsupportedChild(device, kMarker, R"(
            uniform shader cnaTexture0;
            uniform shader cnaTexture8;
            uniform float4 cnaTint;
            half4 main(float2 p) { return cnaTexture0.eval(p) + cnaTexture8.eval(p); }
        )");
        Check(!unsupportedChild.IsEffectValid()
              && unsupportedChild.GetEffectRendererPtr()->GetCompileError().find("cnaTexture7")
                  != std::string::npos,
              "child outside the bounded cnaTexture0..7 namespace rejects");

        std::printf("=== %d checks, %d failures ===\n", checks_, failures_);
        Exit();
    }

private:
    [[nodiscard]] bool ThrowsContaining(const std::function<void()>& action,
                                        const std::string& needle) const
    {
        try
        {
            action();
        }
        catch (const std::exception& exception)
        {
            return std::string(exception.what()).find(needle) != std::string::npos;
        }
        return false;
    }

    [[nodiscard]] std::pair<Color, Color> Render(ShaderEffect& effect, const Color& tint)
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color(17, 29, 43, 255));
        spriteBatch_->Begin(
            SpriteSortMode::Deferred, BlendState::Opaque,
            const_cast<SamplerState*>(&SamplerState::PointClamp),
            nullptr, nullptr, &effect, Matrix::CreateTranslation(1.0f, 0.0f, 0.0f));
        spriteBatch_->Draw(
            *primary_, Rectangle(2, 1, 4, 4), Rectangle(1, 0, 1, 1), tint);
        spriteBatch_->End();

        Color inside = Color::Transparent;
        Color outside = Color::Transparent;
        const Rectangle insideRegion(4, 2, 1, 1);
        const Rectangle outsideRegion(2, 2, 1, 1);
        device.GetBackBufferData(&insideRegion, &inside, 0, 1);
        device.GetBackBufferData(&outsideRegion, &outside, 0, 1);
        return {inside, outside};
    }

    void Check(bool condition, const char* label)
    {
        ++checks_;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (!condition) ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> primary_;
    std::unique_ptr<Texture2D> secondary_;
    std::unique_ptr<Texture2D> cloneSecondary_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaSkslUniformTextureTest game;
    game.Run();
    return game.Result();
}
