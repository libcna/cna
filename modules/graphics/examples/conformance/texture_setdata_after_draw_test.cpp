// SPDX-License-Identifier: MS-PL
// A SetData lands after the draws issued before it.
//
// XNA draws each call with the texture as it is at that call, so a texture changed between two
// draws of one frame shows its old contents in the first and its new contents in the second.
// SDLGPU and WEBGPU record a frame and replay it later while an upload goes out at once, so both
// draws used to show the new contents (handoff.md 5.1; the SpriteBatch Immediate case is
// spritebatch_sort_mode_semantics_test.cpp leg B). Each leg draws the left half with the texture
// red, changes it to blue, draws the right half, and reads both halves back:
//
//   A -- a Texture2D through BasicEffect, changed with the rectangle overload of SetData, which
//        updates the texture the first draw sampled in place.
//   B -- a TextureCube through EnvironmentMapEffect (EnvironmentMapAmount 1, no Fresnel, no
//        specular, so the environment map is the whole colour), every face changed with SetData.
//
// Exit code 0 = all PASS, 1 = at least one FAIL, 77 = the renderer makes no pixel claim.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kWhite(255, 255, 255, 255);

    /// The left half of clip space, then the right half, as two triangles each.
    template <typename Vertex, typename Make>
    void Half(Vertex (&out)[6], const float x0, Make make)
    {
        const float x1 = x0 + 1.0f;
        out[0] = make(x0, 1.0f, 0.0f, 0.0f);
        out[1] = make(x0, -1.0f, 0.0f, 1.0f);
        out[2] = make(x1, -1.0f, 1.0f, 1.0f);
        out[3] = make(x0, 1.0f, 0.0f, 0.0f);
        out[4] = make(x1, -1.0f, 1.0f, 1.0f);
        out[5] = make(x1, 1.0f, 1.0f, 0.0f);
    }
}

class TextureSetDataAfterDrawTest final : public Game
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

    static bool Near(const Color& got, const Color& want, int tol = 40)
    {
        return std::abs(got.getRProperty() - want.getRProperty()) <= tol &&
               std::abs(got.getGProperty() - want.getGProperty()) <= tol &&
               std::abs(got.getBProperty() - want.getBProperty()) <= tol;
    }

    static Color Read(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color px(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &px, 0, 1);
        return px;
    }

    void Judge(GraphicsDevice& dev, const char* leg)
    {
        const Color left = Read(dev, kSize / 4, kSize / 4);
        const Color right = Read(dev, 3 * kSize / 4, kSize / 4);
        check(Near(left, kRed) && Near(right, kBlue),
              std::string(leg) + ": the draw before SetData keeps the old contents and the one "
                  "after shows the new, left=" + Text(left) + " (want red) right=" + Text(right) +
                  " (want blue)");
    }

    void TextureLeg(GraphicsDevice& dev)
    {
        VertexPositionTexture left[6];
        VertexPositionTexture right[6];
        const auto make = [](float x, float y, float u, float v) {
            return VertexPositionTexture(Vector3(x, y, 0.0f), Vector2(u, v));
        };
        Half(left, -1.0f, make);
        Half(right, 0.0f, make);

        Texture2D texture(dev, 2, 2, false, SurfaceFormat::Color);
        const Color red[4] = {kRed, kRed, kRed, kRed};
        const Color blue[4] = {kBlue, kBlue, kBlue, kBlue};
        texture.SetData(red, 4);
        BasicEffect fx(dev);
        fx.setTextureEnabledProperty(true);
        fx.setTextureProperty(&texture);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

        dev.Clear(Color(0, 0, 0, 255));
        fx.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, left, 0, 2);
        // XNA refuses SetData on a texture bound to the device ("The texture resource is in
        // use"), so the slot the effect bound is cleared first, as a game must.
        dev.getTexturesProperty()(0, nullptr);
        const Rectangle whole(0, 0, 2, 2);
        texture.SetData(0, &whole, blue, 0, 4);
        fx.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, right, 0, 2);
        Judge(dev, "A Texture2D");
    }

    void CubeLeg(GraphicsDevice& dev)
    {
        VertexPositionNormalTexture left[6];
        VertexPositionNormalTexture right[6];
        const auto make = [](float x, float y, float u, float v) {
            return VertexPositionNormalTexture(Vector3(x, y, 0.0f), Vector3(0.0f, 0.0f, 1.0f),
                                               Vector2(u, v));
        };
        Half(left, -1.0f, make);
        Half(right, 0.0f, make);

        Texture2D base(dev, 1, 1, false, SurfaceFormat::Color);
        base.SetData(&kWhite, 1);
        TextureCube cube(dev, 1, false, SurfaceFormat::Color);
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX, CubeMapFace::PositiveY,
            CubeMapFace::NegativeY, CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };
        for (CubeMapFace face : faces) cube.SetData(face, &kRed, 1);

        EnvironmentMapEffect fx(dev);
        fx.setTextureProperty(&base);
        fx.setEnvironmentMapProperty(&cube);
        fx.setEmissiveColorProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setEnvironmentMapSpecularProperty(Vector3(0.0f, 0.0f, 0.0f));
        fx.setFresnelFactorProperty(0.0f);
        fx.setEnvironmentMapAmountProperty(1.0f);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

        dev.Clear(Color(0, 0, 0, 255));
        fx.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, left, 0, 2);
        dev.getTexturesProperty()(0, nullptr);
        dev.getTexturesProperty()(1, nullptr);
        for (CubeMapFace face : faces) cube.SetData(face, &kBlue, 1);
        fx.getCurrentTechniqueProperty()->getPassesProperty()[0]->Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, right, 0, 2);
        Judge(dev, "B TextureCube");
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

        TextureLeg(dev);
        CubeLeg(dev);

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    TextureSetDataAfterDrawTest()
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
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("texture_setdata_after_draw"))
        return CNA::Examples::Conformance::kSkipExitCode;
    TextureSetDataAfterDrawTest game;
    game.Run();
    return game.getResult();
}
