// SPDX-License-Identifier: MS-PL
// OPENGL1 renderer: BasicEffect.DirectionalLight1/DirectionalLight2 (plans/plan_opengl1.md item 14,
// EasyGL parity).
//
// Before this, DrawInternal only ever configured GL_LIGHT0 -- GpuDrawParams already carried
// light1Dir/light1Diffuse/light2Dir/light2Diffuse (FillGpuDrawParams populates them from
// BasicEffect.DirectionalLight1/DirectionalLight2), but nothing on the OPENGL1 side ever read
// them, so a game enabling DirectionalLight1/2 silently got no extra lighting contribution at
// all.
//
// Method: DirectionalLight0 is explicitly DISABLED (so this test cannot pass "by accident" via
// the already-working light0 path), AmbientLightColor/EmissiveColor are black (isolating exactly
// the two new lights' contribution), and material DiffuseColor is white. DirectionalLight1 is red,
// DirectionalLight2 is blue, both shining straight at a camera-facing quad's normal (N.L=1 for
// both) -- GL fixed-function lighting sums each enabled light's contribution, so the correct
// result is exactly magenta ((1,0,0)+(0,0,1) clamped to (1,0,1)), not black (both new lights
// silently dropped) or a single pure red/blue (only one of the two actually wired up).
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kViewport = 32;
}

class OpenGL1DirectionalLight12Test : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int  pass_ = 0;
    int  fail_ = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++pass_; else ++fail_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.Clear(Color::Black);

        Matrix world = Matrix::getIdentityProperty();
        Matrix view  = Matrix::getIdentityProperty();
        Matrix proj  = Matrix::CreateOrthographicOffCenter(0.0f, (float)kViewport, (float)kViewport, 0.0f, -1.0f, 1.0f);

        // Camera-facing quad (normal +Z), covering the whole viewport.
        const VertexPositionNormalTexture q[6] = {
            { Vector3(0.0f, 0.0f, 0.0f), Vector3(0, 0, 1), Vector2(0, 0) },
            { Vector3(0.0f, (float)kViewport, 0.0f), Vector3(0, 0, 1), Vector2(0, 1) },
            { Vector3((float)kViewport, (float)kViewport, 0.0f), Vector3(0, 0, 1), Vector2(1, 1) },
            { Vector3(0.0f, 0.0f, 0.0f), Vector3(0, 0, 1), Vector2(0, 0) },
            { Vector3((float)kViewport, (float)kViewport, 0.0f), Vector3(0, 0, 1), Vector2(1, 1) },
            { Vector3((float)kViewport, 0.0f, 0.0f), Vector3(0, 0, 1), Vector2(1, 0) },
        };

        BasicEffect fx(dev);
        fx.setWorldProperty(world);
        fx.setViewProperty(view);
        fx.setProjectionProperty(proj);
        fx.setTextureEnabledProperty(false);
        fx.setLightingEnabledProperty(true);
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setAlphaProperty(1.0f);

        // Isolate light1/light2: light0 explicitly disabled so this cannot pass via the
        // already-working GL_LIGHT0 path alone.
        fx.getDirectionalLight0Property().setEnabledProperty(false);

        DirectionalLight& l1 = fx.getDirectionalLight1Property();
        l1.setEnabledProperty(true);
        l1.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f)); // shines toward -Z, hits the +Z-facing quad head-on.
        l1.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));

        DirectionalLight& l2 = fx.getDirectionalLight2Property();
        l2.setEnabledProperty(true);
        l2.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        l2.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 1.0f));

        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);

        const Rectangle centReg(kViewport / 2, kViewport / 2, 1, 1);
        Color centPx(0, 0, 0, 0);
        dev.GetBackBufferData(&centReg, &centPx, 0, 1);
        std::printf("centre=(%d,%d,%d)\n", centPx.getRProperty(), centPx.getGProperty(), centPx.getBProperty());

        Check(centPx.getRProperty() >= 200, "DirectionalLight1 (red) contributes -- centre R channel is bright");
        Check(centPx.getBProperty() >= 200, "DirectionalLight2 (blue) contributes -- centre B channel is bright");
        Check(centPx.getGProperty() <= 20, "no G contribution (both lights' G channel is 0) -- not just a flat white fallback");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    OpenGL1DirectionalLight12Test()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kViewport);
        gdm_->setPreferredBackBufferHeightProperty(kViewport);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    OpenGL1DirectionalLight12Test game;
    game.Run();
    return game.getResult();
}
