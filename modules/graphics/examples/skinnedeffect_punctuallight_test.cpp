// SPDX-License-Identifier: MS-PL
// SkinnedEffect.PunctualLightEXT pixel witness: identity skinning must preserve the same point,
// range and spot-cone behavior as BasicEffect.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/PunctualLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

}

class SkinnedEffectPunctualLightTest final : public Game
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

    int Shade(GraphicsDevice& dev, Texture2D& texture, const PunctualLightEXT& light)
    {
        SkinnedEffect fx(dev);
        fx.setTextureProperty(&texture);
        fx.setWeightsPerVertexProperty(1);
        fx.SetBoneTransforms(std::vector<Matrix>{Matrix::getIdentityProperty()});
        fx.setAmbientLightColorProperty(Vector3::Zero);
        fx.setDiffuseColorProperty(Vector3::One);
        fx.setEmissiveColorProperty(Vector3::Zero);
        fx.setSpecularColorProperty(Vector3::Zero);
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0, -1, 0));
        fx.DirectionalLight0.setDiffuseColorProperty(Vector3(0.02f, 0.02f, 0.02f));
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setPunctualLightEXT(light);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0, 0, 3), Vector3::Zero, Vector3::Up));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::ToRadians(60.0f), 1.0f, 0.1f, 20.0f));

        const Vector3 normal(0, 0, 1);
        const Vector4 weight(1, 0, 0, 0);
        const std::array<std::uint8_t, 4> indices{0, 0, 0, 0};
        const VertexPositionNormalTextureSkinned quad[6] = {
            {Vector3(-1.5f,  1.5f, 0), normal, Vector2(0, 0), weight, indices},
            {Vector3(-1.5f, -1.5f, 0), normal, Vector2(0, 1), weight, indices},
            {Vector3( 1.5f, -1.5f, 0), normal, Vector2(1, 1), weight, indices},
            {Vector3(-1.5f,  1.5f, 0), normal, Vector2(0, 0), weight, indices},
            {Vector3( 1.5f, -1.5f, 0), normal, Vector2(1, 1), weight, indices},
            {Vector3( 1.5f,  1.5f, 0), normal, Vector2(1, 0), weight, indices},
        };
        VertexBuffer vb(dev, VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(),
                        6, BufferUsage::None);
        vb.SetData(quad, 6);

        dev.Clear(Color::Black);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&centre, &pixel, 0, 1);
        return pixel.getRProperty();
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        Texture2D texture(dev, 1, 1);
        const Color white = Color::White;
        texture.SetData(&white, 1);

        PunctualLightEXT none;
        PunctualLightEXT point;
        point.Kind = PunctualLightKindEXT::Point;
        point.Position = Vector3(0, 0, 1);
        point.Range = 10.0f;
        point.DiffuseColor = Vector3::One * 6.0f;
        PunctualLightEXT far = point;
        far.Range = 0.5f;
        PunctualLightEXT spotAt = point;
        spotAt.Kind = PunctualLightKindEXT::Spot;
        spotAt.Direction = Vector3(0, 0, -1);
        spotAt.OuterAngle = MathHelper::ToRadians(40.0f);
        spotAt.InnerAngle = MathHelper::ToRadians(20.0f);
        PunctualLightEXT spotAway = spotAt;
        spotAway.Direction = Vector3(0, 0, 1);

        const int a = Shade(dev, texture, none);
        const int b = Shade(dev, texture, point);
        const int c = Shade(dev, texture, far);
        const int d = Shade(dev, texture, spotAt);
        const int e = Shade(dev, texture, spotAway);
        check(a < 40, "A no punctual light: the skinned wall stays dark (" + std::to_string(a) + ")");
        check(b > 120, "B point light survives identity skinning (" + std::to_string(b) + ")");
        check(c < 40, "C out-of-range point light contributes nothing (" + std::to_string(c) + ")");
        check(d > 120, "D aimed spot survives identity skinning (" + std::to_string(d) + ")");
        check(e < 40, "E spot aimed away contributes nothing (" + std::to_string(e) + ")");

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    SkinnedEffectPunctualLightTest()
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
    SkinnedEffectPunctualLightTest game;
    game.Run();
    return game.getResult();
}
