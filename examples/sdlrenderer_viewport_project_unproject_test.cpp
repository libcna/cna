// SPDX-License-Identifier: MS-PL
// Task 710: Verify Viewport get/set round-trip and Project/Unproject math on SDL_Renderer.
//
// Viewport::Project/Unproject are pure, renderer-agnostic math (see ViewportTests.cpp's own
// extensive unit-test coverage — identity/non-identity matrices, round-trips, inverted depth,
// etc.) with no GPU dependency at all, so this task's genuinely NEW, SDL_Renderer-specific
// contribution is an INTEGRATION check: tying that already-verified math to the REAL, LIVE
// GraphicsDevice::Viewport this renderer actually reports, using a genuine 2D orthographic
// projection (the "2D orthographic case" this row's own hint calls out) — the kind of
// world<->screen transform a 2D game would actually use for e.g. converting mouse coordinates.
//
// Also specifically exercises a NON-ZERO Viewport X/Y offset, which every existing
// ViewportTests.cpp Project/Unproject case omits (all use Viewport(0, 0, w, h)) — a genuinely
// uncovered combination before this test.
//
// Exit code 0 = all checks PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <cmath>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr float kEps = 0.01f;

    bool Near(float a, float b, float eps = kEps) { return std::fabs(a - b) <= eps; }
}

class SdlViewportProjectUnprojectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    bool done_   = false;
    int  result_ = 0;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (!ok) result_ = 1;
    }

    void checkVec3(bool ok, const char* label, const Vector3& got, const Vector3& want)
    {
        std::printf("[%s] %s: got=(%.3f,%.3f,%.3f) want=(%.3f,%.3f,%.3f)\n",
                     ok ? "PASS" : "FAIL", label, got.X, got.Y, got.Z, want.X, want.Y, want.Z);
        if (!ok) result_ = 1;
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

        // --- get/set round-trip against the REAL device, including a non-zero X/Y offset. ---
        const Viewport initial = dev.getViewportProperty();
        check(initial.getXProperty() == 0 && initial.getYProperty() == 0
                  && initial.getWidthProperty() == dev.getPresentationParametersProperty().getBackBufferWidthProperty()
                  && initial.getHeightProperty() == dev.getPresentationParametersProperty().getBackBufferHeightProperty(),
              "Initial Viewport matches the configured backbuffer size, X=Y=0");

        Viewport custom(8, 4, 32, 16);
        dev.setViewportProperty(custom);
        const Viewport roundTripped = dev.getViewportProperty();
        check(roundTripped.getXProperty() == 8 && roundTripped.getYProperty() == 4
                  && roundTripped.getWidthProperty() == 32 && roundTripped.getHeightProperty() == 16,
              "A custom Viewport (non-zero X/Y offset) round-trips exactly through GraphicsDevice");

        // --- 2D orthographic Project/Unproject against the live, custom Viewport. ---
        const Viewport& vp = roundTripped;
        const float W = static_cast<float>(vp.getWidthProperty());
        const float H = static_cast<float>(vp.getHeightProperty());
        const Matrix proj = Matrix::CreateOrthographicOffCenter(0.0f, W, H, 0.0f, 0.0f, 1.0f);
        const Matrix view = Matrix::getIdentityProperty();
        const Matrix world = Matrix::getIdentityProperty();

        const Vector3 topLeftWorld(0.0f, 0.0f, 0.0f);
        const Vector3 bottomRightWorld(W, H, 0.0f);
        const Vector3 centerWorld(W / 2.0f, H / 2.0f, 0.0f);

        const Vector3 topLeftScreen = vp.Project(topLeftWorld, proj, view, world);
        checkVec3(Near(topLeftScreen.X, static_cast<float>(vp.getXProperty()))
                      && Near(topLeftScreen.Y, static_cast<float>(vp.getYProperty())),
                  "World (0,0) projects to the Viewport's own top-left corner (X,Y), offset included",
                  topLeftScreen, Vector3(static_cast<float>(vp.getXProperty()), static_cast<float>(vp.getYProperty()), 0.0f));

        const Vector3 bottomRightScreen = vp.Project(bottomRightWorld, proj, view, world);
        checkVec3(Near(bottomRightScreen.X, static_cast<float>(vp.getXProperty() + vp.getWidthProperty()))
                      && Near(bottomRightScreen.Y, static_cast<float>(vp.getYProperty() + vp.getHeightProperty())),
                  "World (W,H) projects to the Viewport's own bottom-right corner (X+Width,Y+Height)",
                  bottomRightScreen, Vector3(static_cast<float>(vp.getXProperty() + vp.getWidthProperty()),
                                              static_cast<float>(vp.getYProperty() + vp.getHeightProperty()), 0.0f));

        const Vector3 centerScreen = vp.Project(centerWorld, proj, view, world);
        checkVec3(Near(centerScreen.X, vp.getXProperty() + W / 2.0f) && Near(centerScreen.Y, vp.getYProperty() + H / 2.0f),
                  "World center projects to the Viewport's own centre",
                  centerScreen, Vector3(vp.getXProperty() + W / 2.0f, vp.getYProperty() + H / 2.0f, 0.0f));

        // --- Round-trip: Unproject each screen point back to its original world point. ---
        const Vector3 backToTopLeft = vp.Unproject(topLeftScreen, proj, view, world);
        checkVec3(Near(backToTopLeft.X, topLeftWorld.X) && Near(backToTopLeft.Y, topLeftWorld.Y),
                  "Unproject(Project(top-left)) round-trips to the original world point",
                  backToTopLeft, topLeftWorld);

        const Vector3 backToBottomRight = vp.Unproject(bottomRightScreen, proj, view, world);
        checkVec3(Near(backToBottomRight.X, bottomRightWorld.X) && Near(backToBottomRight.Y, bottomRightWorld.Y),
                  "Unproject(Project(bottom-right)) round-trips to the original world point",
                  backToBottomRight, bottomRightWorld);

        Exit();
    }

public:
    SdlViewportProjectUnprojectTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(32);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    SdlViewportProjectUnprojectTest game;
    game.Run();
    return game.getResult();
}
