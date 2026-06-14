// SPDX-License-Identifier: MS-PL
// Task 22: BasicEffect property integration test.
//
// Tests all property setters/getters for BasicEffect using a real GraphicsDevice.
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <cstdio>
#include <cmath>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static int g_failures = 0;

static void check(bool cond, const char* label)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", label);
        ++g_failures;
    }
}

static bool veq(const Vector3& a, const Vector3& b)
{
    return std::fabs(a.X - b.X) < 1e-5f &&
           std::fabs(a.Y - b.Y) < 1e-5f &&
           std::fabs(a.Z - b.Z) < 1e-5f;
}

class BasicEffectTest : public Game
{
protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        BasicEffect fx(device);

        // --- World / View / Projection ---
        Matrix world = Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
        fx.setWorldProperty(world);
        check(fx.getWorldProperty() == world, "World round-trip");

        Matrix view = Matrix::CreateLookAt(
            Vector3{0, 0, 5}, Vector3::Zero, Vector3{0, 1, 0});
        fx.setViewProperty(view);
        check(fx.getViewProperty() == view, "View round-trip");

        Matrix proj = Matrix::CreatePerspectiveFieldOfView(
            0.785398f, 1.333f, 0.1f, 100.0f);
        fx.setProjectionProperty(proj);
        check(fx.getProjectionProperty() == proj, "Projection round-trip");

        // Defaults on a fresh instance
        {
            BasicEffect fx2(device);
            check(fx2.getWorldProperty()      == Matrix::getIdentityProperty(), "World default identity");
            check(fx2.getViewProperty()       == Matrix::getIdentityProperty(), "View default identity");
            check(fx2.getProjectionProperty() == Matrix::getIdentityProperty(), "Projection default identity");
        }

        // --- VertexColorEnabled ---
        check(fx.VertexColorEnabled == true, "VertexColorEnabled default true");
        fx.VertexColorEnabled = false;
        check(fx.VertexColorEnabled == false, "VertexColorEnabled set false");
        fx.VertexColorEnabled = true;
        check(fx.VertexColorEnabled == true, "VertexColorEnabled set true");

        // --- TextureEnabled ---
        check(fx.getTextureEnabledProperty() == false, "TextureEnabled default false");
        fx.setTextureEnabledProperty(true);
        check(fx.getTextureEnabledProperty() == true, "TextureEnabled set true");
        fx.setTextureEnabledProperty(false);
        check(fx.getTextureEnabledProperty() == false, "TextureEnabled set false");

        // --- Texture ---
        check(fx.getTextureProperty() == nullptr, "Texture default nullptr");
        // (can't set a real texture without loading one; nullptr setter tested below)
        fx.setTextureProperty(nullptr);
        check(fx.getTextureProperty() == nullptr, "Texture set nullptr");

        // --- LightingEnabled ---
        check(fx.getLightingEnabledProperty() == false, "LightingEnabled default false");
        fx.setLightingEnabledProperty(true);
        check(fx.getLightingEnabledProperty() == true, "LightingEnabled set true");
        fx.setLightingEnabledProperty(false);
        check(fx.getLightingEnabledProperty() == false, "LightingEnabled set false");

        // --- AmbientLightColor ---
        check(veq(fx.getAmbientLightColorProperty(), Vector3::Zero), "AmbientLightColor default zero");
        const Vector3 amb{0.1f, 0.2f, 0.3f};
        fx.setAmbientLightColorProperty(amb);
        check(veq(fx.getAmbientLightColorProperty(), amb), "AmbientLightColor round-trip");

        // --- DiffuseColor ---
        check(veq(fx.getDiffuseColorProperty(), Vector3{1, 1, 1}), "DiffuseColor default white");
        const Vector3 diff{0.5f, 0.4f, 0.3f};
        fx.setDiffuseColorProperty(diff);
        check(veq(fx.getDiffuseColorProperty(), diff), "DiffuseColor round-trip");

        // --- EmissiveColor ---
        check(veq(fx.getEmissiveColorProperty(), Vector3::Zero), "EmissiveColor default zero");
        const Vector3 emis{0.1f, 0.1f, 0.1f};
        fx.setEmissiveColorProperty(emis);
        check(veq(fx.getEmissiveColorProperty(), emis), "EmissiveColor round-trip");

        // --- SpecularColor ---
        check(veq(fx.getSpecularColorProperty(), Vector3{1, 1, 1}), "SpecularColor default white");
        const Vector3 spec{0.8f, 0.7f, 0.6f};
        fx.setSpecularColorProperty(spec);
        check(veq(fx.getSpecularColorProperty(), spec), "SpecularColor round-trip");

        // --- SpecularPower ---
        check(std::fabs(fx.getSpecularPowerProperty() - 16.0f) < 1e-5f, "SpecularPower default 16");
        fx.setSpecularPowerProperty(64.0f);
        check(std::fabs(fx.getSpecularPowerProperty() - 64.0f) < 1e-5f, "SpecularPower set 64");

        // --- Alpha ---
        check(std::fabs(fx.getAlphaProperty() - 1.0f) < 1e-5f, "Alpha default 1");
        fx.setAlphaProperty(0.5f);
        check(std::fabs(fx.getAlphaProperty() - 0.5f) < 1e-5f, "Alpha set 0.5");

        // --- PreferPerPixelLighting ---
        check(fx.getPreferPerPixelLightingProperty() == false, "PreferPerPixelLighting default false");
        fx.setPreferPerPixelLightingProperty(true);
        check(fx.getPreferPerPixelLightingProperty() == true, "PreferPerPixelLighting set true");

        // --- Fog ---
        check(fx.getFogEnabledProperty() == false, "FogEnabled default false");
        fx.setFogEnabledProperty(true);
        check(fx.getFogEnabledProperty() == true, "FogEnabled set true");

        check(veq(fx.getFogColorProperty(), Vector3::Zero), "FogColor default zero");
        const Vector3 fogCol{0.5f, 0.5f, 0.5f};
        fx.setFogColorProperty(fogCol);
        check(veq(fx.getFogColorProperty(), fogCol), "FogColor round-trip");

        check(std::fabs(fx.getFogStartProperty() - 0.0f) < 1e-5f, "FogStart default 0");
        fx.setFogStartProperty(10.0f);
        check(std::fabs(fx.getFogStartProperty() - 10.0f) < 1e-5f, "FogStart set 10");

        check(std::fabs(fx.getFogEndProperty() - 1.0f) < 1e-5f, "FogEnd default 1");
        fx.setFogEndProperty(100.0f);
        check(std::fabs(fx.getFogEndProperty() - 100.0f) < 1e-5f, "FogEnd set 100");

        // --- DirectionalLight0 ---
        auto& light0 = fx.getDirectionalLight0Property();
        check(!light0.getEnabledProperty(), "DirectionalLight0 default disabled");
        const Vector3 dir{0.0f, -1.0f, 0.0f};
        light0.setDirectionProperty(dir);
        check(veq(light0.getDirectionProperty(), dir), "DirectionalLight0 direction round-trip");
        const Vector3 dcol{1.0f, 1.0f, 0.8f};
        light0.setDiffuseColorProperty(dcol);
        check(veq(light0.getDiffuseColorProperty(), dcol), "DirectionalLight0 diffuse round-trip");
        light0.setEnabledProperty(true);
        check(light0.getEnabledProperty(), "DirectionalLight0 enabled");

        // --- EnableDefaultLighting ---
        {
            BasicEffect fx3(device);
            fx3.EnableDefaultLighting();
            check(fx3.getLightingEnabledProperty(), "EnableDefaultLighting sets LightingEnabled");
            check(fx3.getDirectionalLight0Property().getEnabledProperty(), "EnableDefaultLighting enables light0");
            check(fx3.getDirectionalLight1Property().getEnabledProperty(), "EnableDefaultLighting enables light1");
            check(fx3.getDirectionalLight2Property().getEnabledProperty(), "EnableDefaultLighting enables light2");
            check(!veq(fx3.getAmbientLightColorProperty(), Vector3::Zero), "EnableDefaultLighting sets ambient");
        }

        // --- GetTypeName ---
        check(fx.GetTypeName() == "Microsoft.Xna.Framework.Graphics.BasicEffect", "GetTypeName");

        // --- Techniques ---
        check(fx.getTechniquesProperty().getCountProperty() >= 1, "at least one technique");
        check(fx.getCurrentTechniqueProperty() != nullptr, "currentTechnique not null");

        Exit();
    }
};

int main(int, char**)
{
    auto* game = new BasicEffectTest();
    game->Run();
    delete game;
    if (g_failures == 0)
    {
        std::printf("BasicEffect: all checks PASS\n");
        return 0;
    }
    std::printf("BasicEffect: %d check(s) FAILED\n", g_failures);
    return 1;
}
