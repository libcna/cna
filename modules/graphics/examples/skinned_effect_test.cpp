// SPDX-License-Identifier: MS-PL
// Task 24: SkinnedEffect property integration test.
//
// Tests WeightsPerVertex and BoneTransforms set/get using a real GraphicsDevice.
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <cstdio>
#include <cmath>
#include <vector>

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

static bool meq(const Matrix& a, const Matrix& b)
{
    const float* pa = &a.M11;
    const float* pb = &b.M11;
    for (int i = 0; i < 16; ++i)
        if (std::fabs(pa[i] - pb[i]) > 1e-5f) return false;
    return true;
}

class SkinnedEffectTest : public Game
{
protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        SkinnedEffect fx(device);

        // --- WeightsPerVertex ---
        check(fx.getWeightsPerVertexProperty() == 4, "WeightsPerVertex default 4");
        fx.setWeightsPerVertexProperty(1);
        check(fx.getWeightsPerVertexProperty() == 1, "WeightsPerVertex set 1");
        fx.setWeightsPerVertexProperty(2);
        check(fx.getWeightsPerVertexProperty() == 2, "WeightsPerVertex set 2");
        fx.setWeightsPerVertexProperty(4);
        check(fx.getWeightsPerVertexProperty() == 4, "WeightsPerVertex set 4");

        // --- MaxBones constant ---
        check(SkinnedEffect::MaxBones == 72, "MaxBones == 72");

        // --- BoneTransforms round-trip (1 bone) ---
        {
            std::vector<Matrix> bones1 = {Matrix::CreateTranslation(1.0f, 2.0f, 3.0f)};
            fx.SetBoneTransforms(bones1);
            auto got = fx.GetBoneTransforms(1);
            check(got.size() == 1, "GetBoneTransforms count 1");
            check(meq(got[0], bones1[0]), "BoneTransforms[0] round-trip");
        }

        // --- BoneTransforms round-trip (4 bones) ---
        {
            std::vector<Matrix> bones4 = {
                Matrix::CreateTranslation(1, 0, 0),
                Matrix::CreateTranslation(0, 2, 0),
                Matrix::CreateTranslation(0, 0, 3),
                Matrix::CreateScale(2.0f),
            };
            fx.SetBoneTransforms(bones4);
            auto got = fx.GetBoneTransforms(4);
            check(got.size() == 4, "GetBoneTransforms count 4");
            for (int i = 0; i < 4; ++i)
                check(meq(got[i], bones4[static_cast<std::size_t>(i)]),
                      "BoneTransforms[i] round-trip");
        }

        // --- World / View / Projection defaults ---
        check(fx.getWorldProperty()      == Matrix::getIdentityProperty(), "World default identity");
        check(fx.getViewProperty()       == Matrix::getIdentityProperty(), "View default identity");
        check(fx.getProjectionProperty() == Matrix::getIdentityProperty(), "Projection default identity");

        // --- World / View / Projection round-trip ---
        Matrix w = Matrix::CreateTranslation(7.0f, 8.0f, 9.0f);
        fx.setWorldProperty(w);
        check(fx.getWorldProperty() == w, "World round-trip");

        // --- GetTypeName ---
        check(fx.GetTypeName() == "Microsoft.Xna.Framework.Graphics.SkinnedEffect", "GetTypeName");

        // --- Techniques ---
        check(fx.getTechniquesProperty().getCountProperty() >= 1, "at least one technique");
        check(fx.getCurrentTechniqueProperty() != nullptr, "currentTechnique not null");

        Exit();
    }
};

int main(int, char**)
{
    auto* game = new SkinnedEffectTest();
    game->Run();
    delete game;
    if (g_failures == 0)
    {
        std::printf("SkinnedEffect: all checks PASS\n");
        return 0;
    }
    std::printf("SkinnedEffect: %d check(s) FAILED\n", g_failures);
    return 1;
}
