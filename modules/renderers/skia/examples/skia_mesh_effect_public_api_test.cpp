// SPDX-License-Identifier: MS-PL
// SKIA-157: integrates SKIA-144-156's SkVertices mesh ABI and GLSL-to-SkSL translator into the
// real public API for the first time -- a ShaderEffect constructed from CNA_SKIA_SKSL_MESH_V1
// source, SetUniformX/SetTexture, Clone(), and SpriteBatch::DrawMeshEXT() drawing real pixels.
// Every earlier task in this lineage (SKIA-153/154/155/156) deliberately stayed below this API.

#include "CNA/Internal/Renderers/Skia/SkiaGlslToSkslTranslatorEXT.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Skia::TranslateGlslToSkslEXT;

namespace
{
    const std::string kMeshMarker = "CNA_SKIA_SKSL_MESH_V1";

    [[nodiscard]] bool SamePixel(Color a, Color b) noexcept
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }
}

class SkiaMeshEffectPublicApiTest final : public Game
{
public:
    SkiaMeshEffectPublicApiTest()
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
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        CheckArbitraryGlslCannotSilentlyFallBack();
        CheckColoredOnlyMeshDraws();
        CheckSetUniformThroughPublicApi();
        CheckTexturedMeshDraws();
        CheckUnboundTextureChildThrows();
        CheckNonImmediateSortModeThrows();
        CheckClone();
        CheckTranslatedGlslRoute();

        std::printf("=== %d checks, %d failures ===\n", checks_, failures_);
        Exit();
    }

private:
    // -- "arbitrary EasyGL GLSL still cannot silently fall back" --
    void CheckArbitraryGlslCannotSilentlyFallBack()
    {
        auto& device = getGraphicsDeviceProperty();
        // Real, untranslated GLSL syntax (in/out top-level declarations, void main()) -- not
        // valid SkSL 100 -- constructed directly against the mesh marker, bypassing the
        // translator entirely.
        const std::string rawGlsl =
            "in vec2 vUV;\nout vec4 FragColor;\nvoid main() { FragColor = vec4(1.0); }\n";
        ShaderEffect effect(device, kMeshMarker, rawGlsl);
        Check(!effect.IsEffectValid(),
              "raw, untranslated GLSL source constructed directly against the mesh marker fails "
              "to compile -- it is never silently accepted as SkSL");
    }

    // -- colored-only mesh: no texture child, uniform-driven output --
    void CheckColoredOnlyMeshDraws()
    {
        auto& device = getGraphicsDeviceProperty();
        const std::string source = "half4 main(float2 p) { return half4(0.2, 0.6, 1.0, 1.0); }";
        ShaderEffect effect(device, kMeshMarker, source);
        Check(effect.IsEffectValid(), "a colored-only mesh effect compiles through the public ShaderEffect API");

        const Vector2 positions[4] = {{0.0f, 0.0f}, {8.0f, 0.0f}, {8.0f, 8.0f}, {0.0f, 8.0f}};
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        device.Clear(Color::Transparent);
        spriteBatch_->Begin(SpriteSortMode::Immediate, BlendState::Opaque);
        spriteBatch_->DrawMeshEXT(effect, positions, nullptr, nullptr, 4, indices, 6);
        spriteBatch_->End();

        Color pixel = Color::Transparent;
        const Rectangle region(4, 4, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        Check(SamePixel(pixel, Color(51, 153, 255, 255)),
              "SpriteBatch::DrawMeshEXT renders a colored-only mesh effect's exact declared colour "
              "through the real public API end to end");
    }

    // -- SetUniformX flows through the public ShaderEffect API to a real draw --
    void CheckSetUniformThroughPublicApi()
    {
        auto& device = getGraphicsDeviceProperty();
        const std::string source =
            "uniform float3 uColour;\nhalf4 main(float2 p) { return half4(uColour, 1.0); }";
        ShaderEffect effect(device, kMeshMarker, source);
        Check(effect.IsEffectValid(), "a uniform-only mesh effect compiles");
        effect.SetUniformVec3("uColour", 0.4f, 0.8f, 0.1f);

        const Vector2 positions[4] = {{0.0f, 0.0f}, {8.0f, 0.0f}, {8.0f, 8.0f}, {0.0f, 8.0f}};
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        device.Clear(Color::Transparent);
        spriteBatch_->Begin(SpriteSortMode::Immediate, BlendState::Opaque);
        spriteBatch_->DrawMeshEXT(effect, positions, nullptr, nullptr, 4, indices, 6);
        spriteBatch_->End();

        Color pixel = Color::Transparent;
        const Rectangle region(4, 4, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        Check(SamePixel(pixel, Color(102, 204, 26, 255)),
              "ShaderEffect::SetUniformVec3 (the public API) reaches the real draw exactly");
    }

    // -- a real texture child, bound through the public ShaderEffect::SetTexture(Texture2D&) --
    void CheckTexturedMeshDraws()
    {
        auto& device = getGraphicsDeviceProperty();
        const std::string source =
            "uniform shader cnaTexture0;\nhalf4 main(float2 p) { return cnaTexture0.eval(p); }";
        ShaderEffect effect(device, kMeshMarker, source);
        Check(effect.IsEffectValid(), "a single-texture-child mesh effect compiles");

        Texture2D texture(device, 1, 1);
        const Color solid(30, 40, 50, 255);
        texture.SetData(&solid, 1);
        effect.SetTexture(0, texture);

        const Vector2 positions[4] = {{0.0f, 0.0f}, {8.0f, 0.0f}, {8.0f, 8.0f}, {0.0f, 8.0f}};
        const Vector2 uvs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        device.Clear(Color::Transparent);
        spriteBatch_->Begin(SpriteSortMode::Immediate, BlendState::Opaque);
        spriteBatch_->DrawMeshEXT(effect, positions, nullptr, uvs, 4, indices, 6);
        spriteBatch_->End();

        Color pixel = Color::Transparent;
        const Rectangle region(4, 4, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        Check(SamePixel(pixel, solid),
              "a texture child bound through the public ShaderEffect::SetTexture(Texture2D&) "
              "samples its exact colour through a real DrawMeshEXT draw");
    }

    // -- a declared-but-unbound texture child throws before drawing --
    void CheckUnboundTextureChildThrows()
    {
        auto& device = getGraphicsDeviceProperty();
        const std::string source =
            "uniform shader cnaTexture0;\nhalf4 main(float2 p) { return cnaTexture0.eval(p); }";
        ShaderEffect effect(device, kMeshMarker, source);
        Check(effect.IsEffectValid(), "a single-texture-child mesh effect compiles (unbound check)");

        const Vector2 positions[4] = {{0.0f, 0.0f}, {8.0f, 0.0f}, {8.0f, 8.0f}, {0.0f, 8.0f}};
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        spriteBatch_->Begin(SpriteSortMode::Immediate, BlendState::Opaque);
        bool threw = false;
        try
        {
            spriteBatch_->DrawMeshEXT(effect, positions, nullptr, nullptr, 4, indices, 6);
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        spriteBatch_->End();
        Check(threw, "DrawMeshEXT throws for a declared-but-unbound cnaTexture0 child, before any drawing");
    }

    // -- DrawMeshEXT requires SpriteSortMode::Immediate --
    void CheckNonImmediateSortModeThrows()
    {
        auto& device = getGraphicsDeviceProperty();
        const std::string source = "half4 main(float2 p) { return half4(1.0); }";
        ShaderEffect effect(device, kMeshMarker, source);
        Check(effect.IsEffectValid(), "a colored-only mesh effect compiles (sort-mode check)");

        const Vector2 positions[4] = {{0.0f, 0.0f}, {8.0f, 0.0f}, {8.0f, 8.0f}, {0.0f, 8.0f}};
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque);
        bool threw = false;
        try
        {
            spriteBatch_->DrawMeshEXT(effect, positions, nullptr, nullptr, 4, indices, 6);
        }
        catch (const std::runtime_error&)
        {
            threw = true;
        }
        spriteBatch_->End();
        Check(threw, "DrawMeshEXT throws under SpriteSortMode::Deferred -- it does not participate "
                     "in the deferred sort/batch queue");
    }

    // -- Clone() produces an independently-functioning mesh effect with isolated uniform state --
    void CheckClone()
    {
        auto& device = getGraphicsDeviceProperty();
        const std::string source =
            "uniform float uValue;\nhalf4 main(float2 p) { return half4(uValue, 0.0, 0.0, 1.0); }";
        ShaderEffect original(device, kMeshMarker, source);
        Check(original.IsEffectValid(), "the original mesh effect compiles (clone check)");
        original.SetUniformFloat("uValue", 0.2f);

        std::unique_ptr<Effect> cloned(original.Clone());
        auto* clonedShaderEffect = dynamic_cast<ShaderEffect*>(cloned.get());
        Check(clonedShaderEffect != nullptr && clonedShaderEffect->IsEffectValid(),
              "Clone() produces a valid, independently-compiled mesh ShaderEffect");
        if (!clonedShaderEffect) return;
        clonedShaderEffect->SetUniformFloat("uValue", 0.8f);

        const Vector2 positions[4] = {{0.0f, 0.0f}, {8.0f, 0.0f}, {8.0f, 8.0f}, {0.0f, 8.0f}};
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        device.Clear(Color::Transparent);
        spriteBatch_->Begin(SpriteSortMode::Immediate, BlendState::Opaque);
        spriteBatch_->DrawMeshEXT(original, positions, nullptr, nullptr, 4, indices, 6);
        spriteBatch_->End();
        Color originalPixel = Color::Transparent;
        const Rectangle region(4, 4, 1, 1);
        device.GetBackBufferData(&region, &originalPixel, 0, 1);

        device.Clear(Color::Transparent);
        spriteBatch_->Begin(SpriteSortMode::Immediate, BlendState::Opaque);
        spriteBatch_->DrawMeshEXT(*clonedShaderEffect, positions, nullptr, nullptr, 4, indices, 6);
        spriteBatch_->End();
        Color clonedPixel = Color::Transparent;
        device.GetBackBufferData(&region, &clonedPixel, 0, 1);

        Check(originalPixel.getRProperty() != clonedPixel.getRProperty()
                  && originalPixel.getRProperty() == static_cast<int>(0.2f * 255.0f + 0.5f)
                  && clonedPixel.getRProperty() == static_cast<int>(0.8f * 255.0f + 0.5f),
              "the original and its clone render their own independently-set uniform values -- "
              "Clone() isolates mutable state, matching every other ShaderEffect's contract");
    }

    // -- the SKIA-155 GLSL-to-SkSL translator's output reaches a real public draw for the first
    // time: the accepted dual_textured-core subset, translated then constructed as a real mesh
    // ShaderEffect and drawn through DrawMeshEXT --
    void CheckTranslatedGlslRoute()
    {
        auto& device = getGraphicsDeviceProperty();
        const std::string acceptedGlsl = R"(
            uniform sampler2D uTexture;
            uniform sampler2D uTexture2;
            uniform vec4 uDiffuseColor;
            in vec2 vUV;
            out vec4 FragColor;
            void main() {
                vec4 base = texture(uTexture, vUV);
                base.rgb *= 2.0;
                FragColor = base * texture(uTexture2, vUV) * uDiffuseColor;
            }
        )";
        const auto translated = TranslateGlslToSkslEXT(acceptedGlsl);
        Check(translated.success, "the accepted dual_textured-core GLSL subset translates successfully");
        if (!translated.success) return;

        ShaderEffect effect(device, kMeshMarker, translated.sksl);
        Check(effect.IsEffectValid(),
              "the translated SkSL compiles as a real mesh ShaderEffect through the public API");
        if (!effect.IsEffectValid()) return;

        Texture2D tex0(device, 1, 1);
        Texture2D tex1(device, 1, 1);
        const Color colour0(60, 60, 60, 255);
        const Color colour1(200, 200, 200, 255);
        tex0.SetData(&colour0, 1);
        tex1.SetData(&colour1, 1);
        effect.SetTexture(0, tex0);
        effect.SetTexture(1, tex1);
        effect.SetUniformVec4("uDiffuseColor", 1.0f, 1.0f, 1.0f, 1.0f);

        const Vector2 positions[4] = {{0.0f, 0.0f}, {8.0f, 0.0f}, {8.0f, 8.0f}, {0.0f, 8.0f}};
        const Vector2 uvs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        device.Clear(Color::Transparent);
        spriteBatch_->Begin(SpriteSortMode::Immediate, BlendState::Opaque);
        spriteBatch_->DrawMeshEXT(effect, positions, nullptr, uvs, 4, indices, 6);
        spriteBatch_->End();

        Color pixel = Color::Transparent;
        const Rectangle region(4, 4, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        const int expected = (60 * 2) * 200 / 255;
        const auto near = [](int actual, int expectedValue) {
            const int diff = actual - expectedValue;
            return (diff < 0 ? -diff : diff) <= 3;
        };
        Check(near(pixel.getRProperty(), expected) && near(pixel.getGProperty(), expected)
                  && near(pixel.getBProperty(), expected),
              "a GLSL-source effect -- translated by SKIA-155, compiled and bound through the real "
              "public ShaderEffect API, drawn through SpriteBatch::DrawMeshEXT -- renders the exact "
              "dual_textured formula through real pixels, the full pipeline end to end for the "
              "first time");
    }

    void Check(bool condition, const std::string& label)
    {
        ++checks_;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition) ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaMeshEffectPublicApiTest game;
    game.Run();
    return game.Result();
}
