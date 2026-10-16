// SPDX-License-Identifier: MS-PL
//
// A RenderTargetCube drawn into and then sampled by direction keeps "up" as up.
//
// The classic XNA dynamic reflection: render the scene into the six faces of a RenderTargetCube, then hand the
// cube to EnvironmentMapEffect. Each side face is drawn red above and blue below (+Y all red, -Y all blue),
// so what a reflecting surface shows says which way the faces are stored:
//
//   A  a surface whose normal tilts up reflects the upper half of a side face    -> red
//   B  a surface whose normal tilts down reflects the lower half of a side face  -> blue
//   C  a surface reflecting exactly across a side/top boundary stays red, with no
//      opposite-edge texel pulled in by mandatory GLES seamless cube filtering
//
// GL's rasterizer fills a render target bottom-up, so an unmodified GL sampler reads a rendered face upside
// down (that is what made EasyGL show the museum's reflection sphere with the floor in both hemispheres).
// Direct3D, SDL_GPU, WebGPU and the software renderer store the rendered face top-down. One shared source,
// no renderer-specific knowledge: it passes where the orientation is right and fails where it is not.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace {
constexpr int kSize = 64;
constexpr int kFace = 16;
} // namespace

class EnvironmentMapRenderTargetCubeOrientationTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int pass_ = 0;
    int fail_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ok ? ++pass_ : ++fail_;
    }

    static std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    Color ReadPixel(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle reg(x, y, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&reg, &px, 0, 1);
        return px;
    }

    /// A quad in the z = 0 plane whose normals tilt up at the top edge and down at the bottom edge.
    /// Seen from +Z the reflection vector therefore leaves the upper part pointing up and the lower part down.
    static void TiltedQuad(VertexPositionNormalTexture (&quad)[6])
    {
        const Vector3 up(0.0f, 0.25f, 1.0f), down(0.0f, -0.25f, 1.0f);
        const auto n = [](Vector3 v) { v.Normalize(); return v; };
        const VertexPositionNormalTexture tl(Vector3(-1.5f, 1.5f, 0.0f), n(up), Vector2(0, 0));
        const VertexPositionNormalTexture tr(Vector3(1.5f, 1.5f, 0.0f), n(up), Vector2(1, 0));
        const VertexPositionNormalTexture bl(Vector3(-1.5f, -1.5f, 0.0f), n(down), Vector2(0, 1));
        const VertexPositionNormalTexture br(Vector3(1.5f, -1.5f, 0.0f), n(down), Vector2(1, 1));
        const VertexPositionNormalTexture v[6] = {tl, bl, br, tl, br, tr};
        for (int i = 0; i < 6; ++i) quad[i] = v[i];
    }

    /// With the eye on +Z, this normal reflects the centre pixel exactly along +Y/+Z.
    static void EdgeQuad(VertexPositionNormalTexture (&quad)[6])
    {
        Vector3 edge(0.0f, 0.41421356f, 1.0f);
        edge.Normalize();
        const VertexPositionNormalTexture tl(Vector3(-1.5f, 1.5f, 0.0f), edge, Vector2(0, 0));
        const VertexPositionNormalTexture tr(Vector3(1.5f, 1.5f, 0.0f), edge, Vector2(1, 0));
        const VertexPositionNormalTexture bl(Vector3(-1.5f, -1.5f, 0.0f), edge, Vector2(0, 1));
        const VertexPositionNormalTexture br(Vector3(1.5f, -1.5f, 0.0f), edge, Vector2(1, 1));
        const VertexPositionNormalTexture v[6] = {tl, bl, br, tl, br, tr};
        for (int i = 0; i < 6; ++i) quad[i] = v[i];
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        const std::vector<std::uint8_t> whitePixel = {255, 255, 255, 255};
        Texture2D white = Texture2D::CreateFromPixels(dev, 1, 1, whitePixel);

        RenderTargetCube cube(dev, kFace, false, SurfaceFormat::Color, DepthFormat::None);
        SpriteBatch batch(dev);
        const CubeMapFace faces[6] = {CubeMapFace::PositiveX, CubeMapFace::NegativeX, CubeMapFace::PositiveY,
                                      CubeMapFace::NegativeY, CubeMapFace::PositiveZ, CubeMapFace::NegativeZ};
        for (int i = 0; i < 6; ++i)
        {
            dev.SetRenderTarget(&cube, faces[i]);
            dev.Clear(Color(0, 0, 0, 255));
            batch.Begin(SpriteSortMode::Deferred, &BlendState::Opaque, &SamplerState::PointClamp, nullptr, nullptr);
            if (i == 2) batch.Draw(white, Rectangle(0, 0, kFace, kFace), Color(255, 0, 0, 255));
            else if (i == 3) batch.Draw(white, Rectangle(0, 0, kFace, kFace), Color(0, 0, 255, 255));
            else
            {
                batch.Draw(white, Rectangle(0, 0, kFace, kFace / 2), Color(255, 0, 0, 255));
                batch.Draw(white, Rectangle(0, kFace / 2, kFace, kFace / 2), Color(0, 0, 255, 255));
            }
            batch.End();
        }
        dev.SetRenderTarget(nullptr);

        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetDepthTestEnabled(false);

        VertexPositionNormalTexture quad[6];
        TiltedQuad(quad);
        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&white);
        fx.setEnvironmentMapProperty(&cube);
        fx.setEnvironmentMapAmountProperty(1.0f);
        fx.setFresnelFactorProperty(0.0f);
        fx.setDiffuseColorProperty(Vector3(0, 0, 0));
        fx.setEmissiveColorProperty(Vector3(0, 0, 0));
        fx.setEnvironmentMapSpecularProperty(Vector3(0, 0, 0));
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0, 0, 3), Vector3::Zero, Vector3::Up));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(MathHelper::ToRadians(60.0f), 1.0f, 0.1f, 20.0f));
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);

        const Color above = ReadPixel(dev, kSize / 2, kSize / 2 - 10);
        const Color below = ReadPixel(dev, kSize / 2, kSize / 2 + 10);
        const auto near = [](const Color& c, int r, int g, int b) {
            return std::abs(c.getRProperty() - r) < 60 && std::abs(c.getGProperty() - g) < 60 && std::abs(c.getBProperty() - b) < 60;
        };
        check(near(above, 255, 0, 0),
              "A normals tilted up reflect the upper half of a rendered side face (red): got " + Text(above));
        check(near(below, 0, 0, 255),
              "B normals tilted down reflect the lower half of a rendered side face (blue): got " + Text(below));

        dev.Clear(Color(0, 0, 0, 255));
        EdgeQuad(quad);
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        const Color edge = ReadPixel(dev, kSize / 2, kSize / 2);
        check(near(edge, 255, 0, 0),
              "C a rendered cube's +Y/+Z edge does not sample a flipped opposite edge: got " + Text(edge));

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    EnvironmentMapRenderTargetCubeOrientationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    EnvironmentMapRenderTargetCubeOrientationTest game;
    game.Run();
    return game.getResult();
}
