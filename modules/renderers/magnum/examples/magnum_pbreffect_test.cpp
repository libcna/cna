// SPDX-License-Identifier: MS-PL
// plans/plan_magnum.md MAGNUM-53: PbrEffect pixel test for the MAGNUM renderer.
//
// A fully analytic setup, so every expected byte is derived from the glTF metallic-roughness BRDF
// rather than captured from a run: the eye sits on +Z looking straight down -Z at a flat quad in
// the z=0 plane, a single directional light shines the same way, and the surface normal is
// (0,0,1) -- so N, V, L and H all coincide at the sampled centre pixel and every dot product in
// the BRDF is exactly 1.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    /// The stride-48 layout: position, normal, tangent (w = bitangent handedness), coordinate.
    struct PbrVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrVertex) == 48, "the PBR layout must stay 48 bytes");
}

class MagnumPbrEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> deviceManager_;
    Texture2D whiteTexture_;
    Texture2D redTexture_;
    Texture2D tiltedNormalMap_;
    bool done_ = false;
    int passed_ = 0;
    int failed_ = 0;

    void Check(const char* label, Color got, Color want, int tolerance = 8)
    {
        const auto close = [tolerance](int a, int b) { return std::abs(a - b) <= tolerance; };
        if (close(got.getRProperty(), want.getRProperty())
            && close(got.getGProperty(), want.getGProperty())
            && close(got.getBProperty(), want.getBProperty()))
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                        got.getRProperty(), got.getGProperty(), got.getBProperty());
            ++passed_;
            return;
        }
        std::printf("[FAIL] %s: got=(%d,%d,%d), expected~(%d,%d,%d)\n", label,
                    got.getRProperty(), got.getGProperty(), got.getBProperty(),
                    want.getRProperty(), want.getGProperty(), want.getBProperty());
        ++failed_;
    }

    Color Render(GraphicsDevice& device, VertexBuffer& buffer, Texture2D* baseColor,
                 Texture2D* normalMap, float roughness, float metallic, const Vector3& ambient)
    {
        PbrEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                    Vector3(0.0f, 1.0f, 0.0f)));
        effect.setProjectionProperty(
            Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
        effect.setTextureProperty(baseColor);
        effect.setNormalMapProperty(normalMap);
        effect.setRoughnessFactorProperty(roughness);
        effect.setMetallicFactorProperty(metallic);
        effect.setAmbientLightColorProperty(ambient);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.DirectionalLight0.setEnabledProperty(true);

        device.Clear(Color(0, 255, 0, 255));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        effect.Apply();
        device.SetVertexBuffer(&buffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);

        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&centre, &pixel, 0, 1);
        return pixel;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        whiteTexture_ = Texture2D::CreateFromPixels(device, 1, 1,
                                                    std::vector<uint8_t>{255, 255, 255, 255});
        redTexture_ = Texture2D::CreateFromPixels(device, 1, 1,
                                                  std::vector<uint8_t>{255, 0, 0, 255});
        // (255,128,128) decodes through the shader's rgb*2-1 to ~(1,0,0): a tangent-space normal
        // tilted a full 90 degrees toward +X.
        tiltedNormalMap_ = Texture2D::CreateFromPixels(device, 1, 1,
                                                       std::vector<uint8_t>{255, 128, 128, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        // A quad spanning [-1,1] in the z=0 plane, facing +Z, tangent (1,0,0) with a +1
        // handedness. With an identity world and the camera above, world (0,0,0) lands exactly on
        // the sampled centre pixel.
        const PbrVertex quad[6] = {
            {-1,  1, 0,  0, 0, 1,  1, 0, 0, 1,  0, 0},
            {-1, -1, 0,  0, 0, 1,  1, 0, 0, 1,  0, 1},
            { 1, -1, 0,  0, 0, 1,  1, 0, 0, 1,  1, 1},
            {-1,  1, 0,  0, 0, 1,  1, 0, 0, 1,  0, 0},
            { 1, -1, 0,  0, 0, 1,  1, 0, 0, 1,  1, 1},
            { 1,  1, 0,  0, 0, 1,  1, 0, 0, 1,  1, 0},
        };
        VertexBuffer buffer(device, 6);
        buffer.SetDataRaw(quad, 6, static_cast<int>(sizeof(PbrVertex)));

        // Fully rough, non-metallic, white albedo, no ambient. With every dot product at 1 the
        // BRDF reduces to kd*albedo/pi + specular = 0.96/pi + 0.003183 = 0.308761.
        // Encoding that linear result for the UNORM backbuffer gives byte 151.
        const Color rough =
            Render(device, buffer, &whiteTexture_, nullptr, 1.0f, 0.0f, Vector3::Zero);
        Check("rough=1.0 metallic=0.0 white: direct light only", rough, Color(151, 151, 151, 255));

        // Halving the roughness sharpens the specular lobe: 0.356507 linear -> byte 161 sRGB. Strictly brighter
        // than the case above, which is what proves RoughnessFactor is read at all rather than
        // being a constant baked into the shader.
        const Color sharper =
            Render(device, buffer, &whiteTexture_, nullptr, 0.5f, 0.0f, Vector3::Zero);
        Check("rough=0.5 metallic=0.0 white: sharper lobe", sharper, Color(161, 161, 161, 255));
        if (sharper.getRProperty() > rough.getRProperty())
        {
            std::printf("[PASS] RoughnessFactor changes the BRDF: %d > %d\n",
                        sharper.getRProperty(), rough.getRProperty());
            ++passed_;
        }
        else
        {
            std::printf("[FAIL] RoughnessFactor changes the BRDF: %d is not > %d\n",
                        sharper.getRProperty(), rough.getRProperty());
            ++failed_;
        }

        // Fully metallic red: the diffuse lobe vanishes with albedo*(1-metallic), leaving only a
        // red-tinted specular one whose F0 IS the albedo. 0.079577 linear -> byte 80 sRGB.
        const Color metal =
            Render(device, buffer, &redTexture_, nullptr, 1.0f, 1.0f, Vector3::Zero);
        Check("metallic=1.0 red: diffuse gone, tinted specular only", metal, Color(80, 0, 0, 255));

        // The same albedo non-metallic: full Lambertian diffuse plus a thin achromatic F0=0.04
        // specular, so the green and blue channels carry that specular alone.
        const Color dielectric =
            Render(device, buffer, &redTexture_, nullptr, 1.0f, 0.0f, Vector3::Zero);
        Check("metallic=0.0 red: full diffuse plus thin specular", dielectric, Color(151, 10, 10, 255));

        // A tangent-space normal tilted 90 degrees turns the surface edge-on to both the light and
        // the eye, zeroing the direct term entirely -- so what is left is the ambient term alone,
        // ambientColor * albedo * occlusion with occlusion 1 (no map bound). This is what proves
        // the normal map is genuinely sampled and perturbs N rather than being ignored.
        const Color perturbed = Render(device, buffer, &whiteTexture_, &tiltedNormalMap_,
                                       1.0f, 0.0f, Vector3(0.2f, 0.3f, 0.4f));
        Check("tilted normal map: direct light zeroed, ambient only",
              perturbed, Color(124, 149, 170, 255));

        std::printf("\nResult: %d/%d PASS\n", passed_, passed_ + failed_);
        Exit();
    }

public:
    MagnumPbrEffectTest()
    {
        deviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        deviceManager_->setPreferredBackBufferWidthProperty(kSize);
        deviceManager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int GetResult() const { return failed_ == 0 ? 0 : 1; }
};

int main()
{
    MagnumPbrEffectTest game;
    game.Run();
    return game.GetResult();
}
