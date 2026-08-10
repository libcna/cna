// SPDX-License-Identifier: MS-PL
// SKIA-149: integrates the SKIA-144-148 cube/volume sampling formulas into the real public
// ShaderEffect/SpriteBatch API for the first time -- BindTextureCube/BindTexture3D no longer
// throw ThrowSkiaUnsupported3D, and cnaSampleCubeEXT/cnaSampleVolumeEXT are reachable from an
// author's own SkSL through SetTexture(1, TextureCube&)/SetTexture(1, Texture3D&) and a real
// SpriteBatch draw, with weak lifetime tracking identical to SetTexture(unit, Texture2D&).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
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

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const std::string kMarker = "CNA_SKIA_SKSL_V1";

    // cnaTexture0 is unconditionally required by the v1 ABI even though this effect's own colour
    // comes entirely from the cube sample; it stays declared but unused in main(), which SkSL
    // permits (matching ordinary unused-declaration behaviour).
    const std::string kCubeEffectSksl = R"(
        uniform shader cnaTexture0;
        uniform float4 cnaTint;
        uniform float3 cubeDirection;
        half4 main(float2 sourcePixel) {
            return cnaSampleCubeEXT(cubeDirection) * cnaTint;
        }
    )";

    const std::string kVolumeEffectSksl = R"(
        uniform shader cnaTexture0;
        uniform float4 cnaTint;
        uniform float3 volumeUvw;
        half4 main(float2 sourcePixel) {
            return cnaSampleVolumeEXT(volumeUvw) * cnaTint;
        }
    )";

    [[nodiscard]] bool SamePixel(Color a, Color b) noexcept
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }
}

class SkiaCubeVolumeEffectBindingTest final : public Game
{
public:
    SkiaCubeVolumeEffectBindingTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(4);
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
        primary_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color white = Color::White;
        primary_->SetData(&white, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        CheckCubeBinding();
        CheckVolumeBinding();
        CheckUnboundBeginRejects();

        std::printf("=== %d checks, %d failures ===\n", checks_, failures_);
        Exit();
    }

private:
    void CheckCubeBinding()
    {
        auto& device = getGraphicsDeviceProperty();
        TextureCube cube(device, 2, false, SurfaceFormat::Color);
        const Color faceColors[6] = {
            Color(255, 0, 0, 255), Color(0, 255, 0, 255), Color(0, 0, 255, 255),
            Color(255, 255, 0, 255), Color(255, 0, 255, 255), Color(0, 255, 255, 255),
        };
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX, CubeMapFace::PositiveY,
            CubeMapFace::NegativeY, CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (int i = 0; i < 6; ++i)
        {
            const Color pixels[4] = {faceColors[i], faceColors[i], faceColors[i], faceColors[i]};
            cube.SetData(faces[i], pixels, 4);
        }

        ShaderEffect effect(device, kMarker, kCubeEffectSksl);
        Check(effect.IsEffectValid(), "cube-sampling SkSL effect compiles");

        effect.SetTexture(1, cube);
        const Color center = SampleDirection(effect, 1.0f, 0.0f, 0.0f);
        Check(SamePixel(center, faceColors[0]),
              "SpriteBatch draw through cnaSampleCubeEXT samples the bound TextureCube's +X face "
              "exactly, end to end through the real public ShaderEffect/SetTexture(TextureCube) API");

        // Live-reference semantics: a face update after SetTexture but before the draw must be
        // visible, matching SetTexture(unit, Texture2D&)'s established contract exactly.
        const Color updatedFace[4] = {
            Color(10, 20, 30, 255), Color(10, 20, 30, 255),
            Color(10, 20, 30, 255), Color(10, 20, 30, 255),
        };
        cube.SetData(CubeMapFace::PositiveX, updatedFace, 4);
        const Color updated = SampleDirection(effect, 1.0f, 0.0f, 0.0f);
        Check(SamePixel(updated, Color(10, 20, 30, 255)),
              "cube sampling observes a face SetData issued after SetTexture but before the draw, "
              "never a stale snapshot");
    }

    void CheckVolumeBinding()
    {
        auto& device = getGraphicsDeviceProperty();
        Texture3D volume(device, 2, 2, 2, false, SurfaceFormat::Color);
        const Color slice0[4] = {
            Color(50, 50, 50, 255), Color(50, 50, 50, 255),
            Color(50, 50, 50, 255), Color(50, 50, 50, 255),
        };
        const Color slice1[4] = {
            Color(200, 200, 200, 255), Color(200, 200, 200, 255),
            Color(200, 200, 200, 255), Color(200, 200, 200, 255),
        };
        // SetData(level, left, top, right, bottom, front, back, data, startIndex, elementCount).
        volume.SetData(0, 0, 0, 2, 2, 0, 1, slice0, 0, 4);
        volume.SetData(0, 0, 0, 2, 2, 1, 2, slice1, 0, 4);

        ShaderEffect effect(device, kMarker, kVolumeEffectSksl);
        Check(effect.IsEffectValid(), "volume-sampling SkSL effect compiles");

        effect.SetTexture(1, volume);
        // w=0 lands exactly on slice 0's own centre (SKIA-148's corrected half-texel formula).
        const Color first = SampleVolume(effect, 0.5f, 0.5f, 0.0f);
        Check(SamePixel(first, Color(50, 50, 50, 255)),
              "SpriteBatch draw through cnaSampleVolumeEXT samples the bound Texture3D's slice 0 "
              "exactly, end to end through the real public ShaderEffect/SetTexture(Texture3D) API");

        const Color last = SampleVolume(effect, 0.5f, 0.5f, 1.0f);
        Check(SamePixel(last, Color(200, 200, 200, 255)),
              "the same effect samples slice 1 exactly at w=1.0");
    }

    void CheckUnboundBeginRejects()
    {
        auto& device = getGraphicsDeviceProperty();
        ShaderEffect effect(device, kMarker, kCubeEffectSksl);
        Check(effect.IsEffectValid(), "cube effect for the unbound-Begin check compiles");
        bool threw = false;
        std::string message;
        try
        {
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                                const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr,
                                nullptr, &effect, Matrix::getIdentityProperty());
            spriteBatch_->Draw(*primary_, Rectangle(0, 0, 1, 1), Color::White);
            spriteBatch_->End();
        }
        catch (const std::exception& exception)
        {
            threw = true;
            message = exception.what();
        }
        Check(threw && message.find("SetTexture(1, TextureCube)") != std::string::npos,
              "SpriteBatch::Begin rejects a cube-sampling effect with no SetTexture(1, "
              "TextureCube) bound, naming exactly what is missing");
    }

    [[nodiscard]] Color SampleDirection(ShaderEffect& effect, float x, float y, float z)
    {
        effect.SetUniformVec3("cubeDirection", x, y, z);
        return DrawAndReadback(effect);
    }

    [[nodiscard]] Color SampleVolume(ShaderEffect& effect, float u, float v, float w)
    {
        effect.SetUniformVec3("volumeUvw", u, v, w);
        return DrawAndReadback(effect);
    }

    [[nodiscard]] Color DrawAndReadback(ShaderEffect& effect)
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color::Transparent);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr,
                            &effect, Matrix::getIdentityProperty());
        spriteBatch_->Draw(*primary_, Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->End();
        Color pixel = Color::Transparent;
        const Rectangle region(2, 2, 1, 1);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    void Check(bool condition, const std::string& label)
    {
        ++checks_;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (!condition) ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> primary_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaCubeVolumeEffectBindingTest game;
    game.Run();
    return game.Result();
}
