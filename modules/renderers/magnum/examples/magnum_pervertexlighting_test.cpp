// SPDX-License-Identifier: MS-PL
// plans/plan_magnum.md MAGNUM-60: BasicEffect.PreferPerPixelLighting for the MAGNUM renderer.
//
// The two lighting families cannot be told apart by a diffuse term on a flat quad -- the normal is
// constant, so interpolating the light result and recomputing it give the same answer everywhere.
// A SPECULAR highlight can: the half vector depends on the eye position, which varies across the
// surface, and a high specular power makes that variation sharply non-linear.
//
// The rig here puts the highlight exactly at the quad's centre and nowhere near its corners. Per
// pixel the centre reads the full highlight; per vertex the shader only ever evaluates the four
// corners, where the highlight has already fallen to almost nothing, and interpolates that across
// the middle. XNA's own default is the per-vertex one, which is what makes this a fidelity gap
// rather than a quality setting.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <array>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    /// Half-extent of the quad in world units. The eye sits this close to it, so the eye vector
    /// swings hard between the centre and a corner -- which is the whole discriminator.
    constexpr float kHalf = 1.0f;
    constexpr float kEyeDistance = 2.0f;

    /// High enough that the corners' half vectors, already ~0.95 against the normal, fall to a few
    /// percent -- a low power would leave the two families within noise of each other.
    constexpr float kSpecularPower = 64.0f;
}

class MagnumPerVertexLightingTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> deviceManager_;
    std::unique_ptr<BasicEffect> effect_;
    bool done_ = false;
    int result_ = 0;

    void Pass(const char* label, const char* detail)
    {
        std::printf("[PASS] %s: %s\n", label, detail);
    }

    void Fail(const char* label, const char* detail)
    {
        std::printf("[FAIL] %s: %s\n", label, detail);
        result_ = 1;
    }

    Color ReadAt(GraphicsDevice& device, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    /// Renders the quad once and returns the centre and a corner-adjacent sample.
    void RenderQuad(GraphicsDevice& device, bool perPixel, Color& centre, Color& corner)
    {
        device.Clear(Color(0, 0, 0, 255));
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        effect_->setPreferPerPixelLightingProperty(perPixel);
        effect_->Apply();

        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const std::array<VertexPositionNormalTexture, 6> quad = {
            VertexPositionNormalTexture(Vector3(-kHalf, -kHalf, 0.0f), normal, Vector2(0, 1)),
            VertexPositionNormalTexture(Vector3(-kHalf,  kHalf, 0.0f), normal, Vector2(0, 0)),
            VertexPositionNormalTexture(Vector3( kHalf,  kHalf, 0.0f), normal, Vector2(1, 0)),
            VertexPositionNormalTexture(Vector3(-kHalf, -kHalf, 0.0f), normal, Vector2(0, 1)),
            VertexPositionNormalTexture(Vector3( kHalf,  kHalf, 0.0f), normal, Vector2(1, 0)),
            VertexPositionNormalTexture(Vector3( kHalf, -kHalf, 0.0f), normal, Vector2(1, 1)),
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);

        centre = ReadAt(device, kSize / 2, kSize / 2);
        // Three pixels in from the quad's own corner, still well inside it.
        corner = ReadAt(device, 3, 3);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        effect_ = std::make_unique<BasicEffect>(device);

        // An orthographic projection keeps the quad filling the viewport regardless of the eye
        // distance, so the eye can sit close enough for the half vector to actually swing.
        effect_->setWorldProperty(Matrix::getIdentityProperty());
        effect_->setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, kEyeDistance),
                                                      Vector3(0.0f, 0.0f, 0.0f),
                                                      Vector3(0.0f, 1.0f, 0.0f)));
        effect_->setProjectionProperty(
            Matrix::CreateOrthographic(2.0f * kHalf, 2.0f * kHalf, 0.1f, 10.0f));

        effect_->setLightingEnabledProperty(true);
        effect_->setAmbientLightColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect_->setDiffuseColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect_->setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        effect_->setSpecularColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect_->setSpecularPowerProperty(kSpecularPower);
        effect_->setAlphaProperty(1.0f);

        // One light, aimed straight at the quad so its half vector at the centre coincides with
        // the normal. Diffuse is black: only the specular term is being measured.
        DirectionalLight& light0 = effect_->getDirectionalLight0Property();
        light0.setEnabledProperty(true);
        light0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        light0.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        light0.setSpecularColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect_->getDirectionalLight1Property().setEnabledProperty(false);
        effect_->getDirectionalLight2Property().setEnabledProperty(false);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        char detail[224];

        Color pixelCentre(0, 0, 0, 0);
        Color pixelCorner(0, 0, 0, 0);
        RenderQuad(device, true, pixelCentre, pixelCorner);

        Color vertexCentre(0, 0, 0, 0);
        Color vertexCorner(0, 0, 0, 0);
        RenderQuad(device, false, vertexCentre, vertexCorner);

        std::snprintf(detail, sizeof(detail), "centre reads %d", pixelCentre.getRProperty());
        if (pixelCentre.getRProperty() >= 230)
            Pass("per pixel: the highlight lands where the half vector peaks", detail);
        else
            Fail("per pixel: the highlight lands where the half vector peaks", detail);

        std::snprintf(detail, sizeof(detail), "corner reads %d", pixelCorner.getRProperty());
        if (pixelCorner.getRProperty() <= 40)
            Pass("per pixel: the highlight falls off away from the centre", detail);
        else
            Fail("per pixel: the highlight falls off away from the centre", detail);

        // The four corners are the only places the vertex stage evaluates, and the highlight has
        // already collapsed there -- so interpolating them leaves the centre dark. A per-vertex
        // family that quietly reused the per-pixel program would light the centre instead.
        std::snprintf(detail, sizeof(detail),
                      "centre reads %d against the per-pixel family's %d",
                      vertexCentre.getRProperty(), pixelCentre.getRProperty());
        if (vertexCentre.getRProperty() <= 60)
            Pass("per vertex: the centre only ever sees the interpolated corners", detail);
        else
            Fail("per vertex: the centre only ever sees the interpolated corners", detail);

        std::snprintf(detail, sizeof(detail), "%d per pixel vs %d per vertex",
                      pixelCentre.getRProperty(), vertexCentre.getRProperty());
        if (pixelCentre.getRProperty() > vertexCentre.getRProperty() + 100)
            Pass("PreferPerPixelLighting selects a different program", detail);
        else
            Fail("PreferPerPixelLighting selects a different program", detail);

        // Near a corner the two families must AGREE: that is where the vertex stage sampled, so a
        // difference there would mean the shared lighting function had drifted between the two
        // rather than only its evaluation frequency changing.
        const int gap = std::abs(pixelCorner.getRProperty() - vertexCorner.getRProperty());
        std::snprintf(detail, sizeof(detail), "%d per pixel vs %d per vertex",
                      pixelCorner.getRProperty(), vertexCorner.getRProperty());
        if (gap <= 12)
            Pass("both families share one lighting formula", detail);
        else
            Fail("both families share one lighting formula", detail);

        Exit();
    }

public:
    MagnumPerVertexLightingTest()
    {
        deviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        deviceManager_->setPreferredBackBufferWidthProperty(kSize);
        deviceManager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    MagnumPerVertexLightingTest game;
    game.Run();
    return game.GetResult();
}
