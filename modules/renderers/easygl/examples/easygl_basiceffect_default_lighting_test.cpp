// SPDX-License-Identifier: MS-PL
// Task 194: BasicEffect::EnableDefaultLighting() exact-constant integration test.
//
// Verifies that after calling EnableDefaultLighting() every property matches the
// XNA/FNA constants defined in EffectHelpers.cs::EnableDefaultLighting():
//
//   AmbientLightColor  = (0.05333332, 0.09882354, 0.1819608)
//   Light0 Direction   = (-0.5265408, -0.5735765, -0.6275069)
//   Light0 Diffuse     = (1,          0.9607844,  0.8078432)
//   Light0 Specular    = (1,          0.9607844,  0.8078432)
//   Light1 Direction   = (0.7198464,  0.3420201,  0.6040227)
//   Light1 Diffuse     = (0.9647059,  0.7607844,  0.4078432)
//   Light1 Specular    = (0,          0,          0)
//   Light2 Direction   = (0.4545195, -0.7660444,  0.4545195)
//   Light2 Diffuse     = (0.3231373,  0.3607844,  0.3937255)
//   Light2 Specular    = (0.3231373,  0.3607844,  0.3937255)
//
// No pixel readback needed — tests only property values.
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <cmath>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static bool veq(const Vector3& a, const Vector3& b, float tol = 1e-5f)
{
    return std::fabs(a.X - b.X) <= tol
        && std::fabs(a.Y - b.Y) <= tol
        && std::fabs(a.Z - b.Z) <= tol;
}

class DefaultLightingTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 0;

    void check(bool cond, const char* label, const Vector3& got, const Vector3& want)
    {
        if (cond)
            std::printf("[PASS] %s: got=(%.7f,%.7f,%.7f)\n", label,
                (double)got.X, (double)got.Y, (double)got.Z);
        else
        {
            std::printf("[FAIL] %s: got=(%.7f,%.7f,%.7f), expected=(%.7f,%.7f,%.7f)\n", label,
                (double)got.X, (double)got.Y, (double)got.Z,
                (double)want.X, (double)want.Y, (double)want.Z);
            result_ = 1;
        }
    }

    void checkBool(bool cond, const char* label)
    {
        if (cond) std::printf("[PASS] %s\n", label);
        else      { std::printf("[FAIL] %s\n", label); result_ = 1; }
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        BasicEffect fx(dev);
        fx.EnableDefaultLighting();

        checkBool(fx.getLightingEnabledProperty(), "LightingEnabled=true after EnableDefaultLighting");

        // Ambient
        const Vector3 kAmbient(0.05333332f, 0.09882354f, 0.1819608f);
        const Vector3 gotAmb = fx.getAmbientLightColorProperty();
        check(veq(gotAmb, kAmbient), "AmbientLightColor", gotAmb, kAmbient);

        // Light 0 — key light
        {
            auto& L = fx.getDirectionalLight0Property();
            checkBool(L.getEnabledProperty(), "Light0.Enabled");

            const Vector3 kDir(-0.5265408f, -0.5735765f, -0.6275069f);
            const Vector3 kDiff(1.0f, 0.9607844f, 0.8078432f);
            const Vector3 kSpec(1.0f, 0.9607844f, 0.8078432f);

            const Vector3 gotDir  = L.getDirectionProperty();
            const Vector3 gotDiff = L.getDiffuseColorProperty();
            const Vector3 gotSpec = L.getSpecularColorProperty();

            check(veq(gotDir,  kDir),  "Light0.Direction", gotDir,  kDir);
            check(veq(gotDiff, kDiff), "Light0.Diffuse",   gotDiff, kDiff);
            check(veq(gotSpec, kSpec), "Light0.Specular",  gotSpec, kSpec);
        }

        // Light 1 — fill light
        {
            auto& L = fx.getDirectionalLight1Property();
            checkBool(L.getEnabledProperty(), "Light1.Enabled");

            const Vector3 kDir(0.7198464f, 0.3420201f, 0.6040227f);
            const Vector3 kDiff(0.9647059f, 0.7607844f, 0.4078432f);
            const Vector3 kSpec = Vector3::Zero;

            const Vector3 gotDir  = L.getDirectionProperty();
            const Vector3 gotDiff = L.getDiffuseColorProperty();
            const Vector3 gotSpec = L.getSpecularColorProperty();

            check(veq(gotDir,  kDir),  "Light1.Direction", gotDir,  kDir);
            check(veq(gotDiff, kDiff), "Light1.Diffuse",   gotDiff, kDiff);
            check(veq(gotSpec, kSpec), "Light1.Specular",  gotSpec, kSpec);
        }

        // Light 2 — back light
        {
            auto& L = fx.getDirectionalLight2Property();
            checkBool(L.getEnabledProperty(), "Light2.Enabled");

            const Vector3 kDir(0.4545195f, -0.7660444f, 0.4545195f);
            const Vector3 kDiff(0.3231373f, 0.3607844f, 0.3937255f);
            const Vector3 kSpec(0.3231373f, 0.3607844f, 0.3937255f);

            const Vector3 gotDir  = L.getDirectionProperty();
            const Vector3 gotDiff = L.getDiffuseColorProperty();
            const Vector3 gotSpec = L.getSpecularColorProperty();

            check(veq(gotDir,  kDir),  "Light2.Direction", gotDir,  kDir);
            check(veq(gotDiff, kDiff), "Light2.Diffuse",   gotDiff, kDiff);
            check(veq(gotSpec, kSpec), "Light2.Specular",  gotSpec, kSpec);
        }

        Exit();
    }

public:
    DefaultLightingTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    DefaultLightingTest game;
    game.Run();
    return game.getResult();
}
