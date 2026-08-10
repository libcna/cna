// SPDX-License-Identifier: MS-PL
// Task 23: AlphaTestEffect property integration test.
//
// Tests all property setters/getters for AlphaTestEffect using a real GraphicsDevice.
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <cstdio>
#include <cmath>

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

class AlphaTestEffectTest : public Game
{
protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        AlphaTestEffect fx(device);

        // --- World / View / Projection defaults ---
        check(fx.getWorldProperty()      == Matrix::getIdentityProperty(), "World default identity");
        check(fx.getViewProperty()       == Matrix::getIdentityProperty(), "View default identity");
        check(fx.getProjectionProperty() == Matrix::getIdentityProperty(), "Projection default identity");

        // --- World / View / Projection round-trip ---
        Matrix world = Matrix::CreateTranslation(4.0f, 5.0f, 6.0f);
        fx.setWorldProperty(world);
        check(fx.getWorldProperty() == world, "World round-trip");

        Matrix view = Matrix::CreateLookAt(
            Vector3{0, 0, 10}, Vector3::Zero, Vector3{0, 1, 0});
        fx.setViewProperty(view);
        check(fx.getViewProperty() == view, "View round-trip");

        Matrix proj = Matrix::CreatePerspectiveFieldOfView(
            0.785398f, 1.333f, 0.1f, 100.0f);
        fx.setProjectionProperty(proj);
        check(fx.getProjectionProperty() == proj, "Projection round-trip");

        // --- AlphaFunction ---
        check(fx.getAlphaFunctionProperty() == CompareFunction::Greater, "AlphaFunction default Greater");
        fx.setAlphaFunctionProperty(CompareFunction::LessEqual);
        check(fx.getAlphaFunctionProperty() == CompareFunction::LessEqual, "AlphaFunction set LessEqual");
        fx.setAlphaFunctionProperty(CompareFunction::Always);
        check(fx.getAlphaFunctionProperty() == CompareFunction::Always, "AlphaFunction set Always");
        fx.setAlphaFunctionProperty(CompareFunction::Never);
        check(fx.getAlphaFunctionProperty() == CompareFunction::Never, "AlphaFunction set Never");

        // --- ReferenceAlpha ---
        check(fx.getReferenceAlphaProperty() == 0, "ReferenceAlpha default 0");
        fx.setReferenceAlphaProperty(128);
        check(fx.getReferenceAlphaProperty() == 128, "ReferenceAlpha set 128");
        fx.setReferenceAlphaProperty(255);
        check(fx.getReferenceAlphaProperty() == 255, "ReferenceAlpha set 255");
        fx.setReferenceAlphaProperty(0);
        check(fx.getReferenceAlphaProperty() == 0, "ReferenceAlpha set 0");

        // --- DiffuseColor ---
        check(veq(fx.getDiffuseColorProperty(), Vector3{1, 1, 1}), "DiffuseColor default white");
        const Vector3 diff{0.3f, 0.5f, 0.7f};
        fx.setDiffuseColorProperty(diff);
        check(veq(fx.getDiffuseColorProperty(), diff), "DiffuseColor round-trip");

        // --- Alpha ---
        check(std::fabs(fx.getAlphaProperty() - 1.0f) < 1e-5f, "Alpha default 1");
        fx.setAlphaProperty(0.25f);
        check(std::fabs(fx.getAlphaProperty() - 0.25f) < 1e-5f, "Alpha set 0.25");
        fx.setAlphaProperty(0.0f);
        check(std::fabs(fx.getAlphaProperty() - 0.0f) < 1e-5f, "Alpha set 0");

        // --- VertexColorEnabled ---
        check(fx.getVertexColorEnabledProperty() == false, "VertexColorEnabled default false");
        fx.setVertexColorEnabledProperty(true);
        check(fx.getVertexColorEnabledProperty() == true, "VertexColorEnabled set true");
        fx.setVertexColorEnabledProperty(false);
        check(fx.getVertexColorEnabledProperty() == false, "VertexColorEnabled set false");

        // --- Texture ---
        check(fx.getTextureProperty() == nullptr, "Texture default nullptr");
        fx.setTextureProperty(nullptr);
        check(fx.getTextureProperty() == nullptr, "Texture set nullptr");

        // --- Fog ---
        check(fx.getFogEnabledProperty() == false, "FogEnabled default false");
        fx.setFogEnabledProperty(true);
        check(fx.getFogEnabledProperty() == true, "FogEnabled set true");

        check(veq(fx.getFogColorProperty(), Vector3::Zero), "FogColor default zero");
        const Vector3 fogCol{0.8f, 0.8f, 0.8f};
        fx.setFogColorProperty(fogCol);
        check(veq(fx.getFogColorProperty(), fogCol), "FogColor round-trip");

        check(std::fabs(fx.getFogStartProperty() - 0.0f) < 1e-5f, "FogStart default 0");
        fx.setFogStartProperty(5.0f);
        check(std::fabs(fx.getFogStartProperty() - 5.0f) < 1e-5f, "FogStart set 5");

        check(std::fabs(fx.getFogEndProperty() - 1.0f) < 1e-5f, "FogEnd default 1");
        fx.setFogEndProperty(50.0f);
        check(std::fabs(fx.getFogEndProperty() - 50.0f) < 1e-5f, "FogEnd set 50");

        // --- GetTypeName ---
        check(fx.GetTypeName() == "Microsoft.Xna.Framework.Graphics.AlphaTestEffect", "GetTypeName");

        // --- Techniques ---
        check(fx.getTechniquesProperty().getCountProperty() >= 1, "at least one technique");
        check(fx.getCurrentTechniqueProperty() != nullptr, "currentTechnique not null");

        Exit();
    }
};

int main(int, char**)
{
    auto* game = new AlphaTestEffectTest();
    game->Run();
    delete game;
    if (g_failures == 0)
    {
        std::printf("AlphaTestEffect: all checks PASS\n");
        return 0;
    }
    std::printf("AlphaTestEffect: %d check(s) FAILED\n", g_failures);
    return 1;
}
