// SPDX-License-Identifier: MS-PL
// Task 184: Effect::Clone() independence test.
//
// Verifies that Clone() produces a separate Effect instance with copies of
// all fields, and that mutations on the clone do not affect the original and
// vice-versa.  Uses AlphaTestEffect because it has a complete Clone() and
// readable scalar/vector properties that survive the round-trip without GPU
// involvement.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static bool approx(float a, float b) { return (a - b) < 0.001f && (b - a) < 0.001f; }
static bool vec3eq(Vector3 a, Vector3 b)
{
    return approx(a.X, b.X) && approx(a.Y, b.Y) && approx(a.Z, b.Z);
}

class EffectCloneTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        GraphicsDevice& device = getGraphicsDeviceProperty();

        AlphaTestEffect original(device);
        original.setAlphaProperty(0.5f);
        original.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));

        std::unique_ptr<Effect> cloneBase(original.Clone());
        auto* clone = dynamic_cast<AlphaTestEffect*>(cloneBase.get());

        check(clone != nullptr,
              "Clone() returns an AlphaTestEffect (dynamic_cast succeeds)");
        check(clone != &original,
              "Clone() returns a different object");

        check(approx(clone->getAlphaProperty(), 0.5f),
              "Clone: Alpha matches original after Clone()");
        check(vec3eq(clone->getDiffuseColorProperty(), Vector3(1.0f, 0.0f, 0.0f)),
              "Clone: DiffuseColor matches original after Clone()");

        // Mutate the clone — original must be unaffected.
        clone->setAlphaProperty(1.0f);
        clone->setDiffuseColorProperty(Vector3(0.0f, 0.0f, 1.0f));

        check(approx(original.getAlphaProperty(), 0.5f),
              "Original: Alpha unchanged after clone mutation");
        check(vec3eq(original.getDiffuseColorProperty(), Vector3(1.0f, 0.0f, 0.0f)),
              "Original: DiffuseColor unchanged after clone mutation");

        // Mutate the original — clone must be unaffected.
        original.setAlphaProperty(0.25f);

        check(approx(clone->getAlphaProperty(), 1.0f),
              "Clone: Alpha unchanged after original mutation");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    EffectCloneTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    EffectCloneTest game;
    game.Run();
    return game.getResult();
}
