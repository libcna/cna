// SPDX-License-Identifier: MS-PL
// GLTF-343/344: Bgfx pixel witnesses for KHR_materials_ior and factor-only
// KHR_materials_specular. This mirrors easygl_pbr_fresnel_factors_test.cpp, but performs one
// backbuffer read per freshly submitted draw because Bgfx readback is asynchronous.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <type_traits>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    struct PbrVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrVertex) == 48);

    struct SkinnedPbrVertex
    {
        PbrVertex base;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedPbrVertex) == 68);

    std::vector<PbrVertex> Quad(bool grazing)
    {
        const float nx = grazing ? 0.9950372f : 0.0f;
        const float nz = grazing ? 0.0995037f : 1.0f;
        const float tx = grazing ? 0.0f : 1.0f;
        const float ty = grazing ? 1.0f : 0.0f;
        const auto vertex = [=](float x, float y, float u, float v)
        {
            return PbrVertex{x, y, 0.0f, nx, 0.0f, nz, tx, ty, 0.0f, 1.0f, u, v};
        };
        const PbrVertex tl = vertex(-1.0f,  1.0f, 0.0f, 0.0f);
        const PbrVertex bl = vertex(-1.0f, -1.0f, 0.0f, 1.0f);
        const PbrVertex br = vertex( 1.0f, -1.0f, 1.0f, 1.0f);
        const PbrVertex tr = vertex( 1.0f,  1.0f, 1.0f, 0.0f);
        return {tl, bl, br, tl, br, tr};
    }

    std::vector<SkinnedPbrVertex> SkinnedQuad()
    {
        std::vector<SkinnedPbrVertex> result;
        for (const PbrVertex& vertex : Quad(false))
            result.push_back({vertex, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0});
        return result;
    }

    bool Matches(const Color& got, const Color& expected, int tolerance)
    {
        const auto close = [tolerance](int a, int b) { return std::abs(a - b) <= tolerance; };
        return close(got.getRProperty(), expected.getRProperty())
            && close(got.getGProperty(), expected.getGProperty())
            && close(got.getBProperty(), expected.getBProperty());
    }
}

class BgfxPbrFresnelFactorsTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D white_;
    int pass_ = 0;
    int fail_ = 0;

    template <typename Effect>
    Color Render(GraphicsDevice& device, VertexBuffer& vertices, bool extension, bool grazing)
    {
        Effect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f)));
        effect.setProjectionProperty(Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 10.0f));
        effect.setTextureProperty(&white_);
        effect.setDiffuseColorProperty(Vector3::Zero);
        effect.setAmbientLightColorProperty(Vector3::Zero);
        effect.setEmissiveFactorProperty(Vector3::Zero);
        effect.setMetallicFactorProperty(0.0f);
        effect.setRoughnessFactorProperty(1.0f);
        effect.setEncodeOutputToSrgbEXTProperty(true);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3::One);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);

        if (grazing)
        {
            Vector3 direction(-0.2f, 0.0f, 0.98f);
            direction.Normalize();
            effect.DirectionalLight0.setDirectionProperty(direction);
            effect.setIorEXTProperty(1.5f);
            if (extension)
            {
                effect.setSpecularFactorEXTProperty(0.3f);
                effect.setSpecularColorFactorEXTProperty(
                    Vector3(10.0f / 3.0f, 10.0f / 3.0f, 10.0f / 3.0f));
            }
        }
        else
        {
            effect.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
            if (extension)
            {
                effect.setIorEXTProperty(2.0f);
                effect.setSpecularFactorEXTProperty(0.3f);
                effect.setSpecularColorFactorEXTProperty(Vector3(0.25f, 1.0f, 12.0f));
            }
        }

        if constexpr (std::is_same_v<Effect, SkinnedPbrEffect>)
        {
            effect.SetBoneTransforms({Matrix::getIdentityProperty()});
            effect.setWeightsPerVertexProperty(1);
        }

        Color got(0, 255, 0, 255);
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            device.Clear(Color(0, 255, 0, 255));
            effect.Apply();
            device.SetVertexBuffer(&vertices);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            const Rectangle center(kSize / 2, kSize / 2, 1, 1);
            device.GetBackBufferData(&center, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 255 || got.getBProperty() != 0)
                break;
        }
        return got;
    }

    void Check(const char* name, const Color& got, const Color& expected, int tolerance)
    {
        const bool ok = Matches(got, expected, tolerance);
        std::printf("[%s] %s: got=(%d,%d,%d) want=(%d,%d,%d) tol=%d\n",
                    ok ? "PASS" : "FAIL", name,
                    got.getRProperty(), got.getGProperty(), got.getBProperty(),
                    expected.getRProperty(), expected.getGProperty(), expected.getBProperty(),
                    tolerance);
        ok ? ++pass_ : ++fail_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        white_ = Texture2D::CreateFromPixels(
            getGraphicsDeviceProperty(), 1, 1, {255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        DepthStencilState depthState;
        depthState.setDepthBufferEnableProperty(false);
        device.setDepthStencilStateProperty(depthState);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const std::vector<PbrVertex> rigid = Quad(false);
        VertexBuffer rigidBuffer(device, static_cast<int>(rigid.size()));
        rigidBuffer.SetDataRaw(rigid.data(), static_cast<int>(rigid.size()), sizeof(PbrVertex));
        Check("PbrEffect core dielectric F0",
              Render<PbrEffect>(device, rigidBuffer, false, false), Color(11, 11, 11, 255), 3);
        Check("PbrEffect IOR/specular F0",
              Render<PbrEffect>(device, rigidBuffer, true, false), Color(2, 9, 43, 255), 4);

        const std::vector<SkinnedPbrVertex> skinned = SkinnedQuad();
        VertexBuffer skinnedBuffer(device, static_cast<int>(skinned.size()));
        skinnedBuffer.SetDataRaw(
            skinned.data(), static_cast<int>(skinned.size()), sizeof(SkinnedPbrVertex));
        Check("SkinnedPbrEffect core dielectric F0",
              Render<SkinnedPbrEffect>(device, skinnedBuffer, false, false),
              Color(11, 11, 11, 255), 3);
        Check("SkinnedPbrEffect IOR/specular F0",
              Render<SkinnedPbrEffect>(device, skinnedBuffer, true, false),
              Color(2, 9, 43, 255), 4);

        const std::vector<PbrVertex> grazing = Quad(true);
        VertexBuffer grazingBuffer(device, static_cast<int>(grazing.size()));
        grazingBuffer.SetDataRaw(
            grazing.data(), static_cast<int>(grazing.size()), sizeof(PbrVertex));
        const Color coreF90 = Render<PbrEffect>(device, grazingBuffer, false, true);
        const Color reducedF90 = Render<PbrEffect>(device, grazingBuffer, true, true);
        Check("core F90=1 grazing response", coreF90, Color(34, 34, 34, 255), 5);
        Check("specular F90=.3 grazing response", reducedF90, Color(16, 16, 16, 255), 5);
        const bool separated = coreF90.getRProperty() >= reducedF90.getRProperty() + 12;
        std::printf("[%s] changing F90 while holding F0 fixed changes the shader result\n",
                    separated ? "PASS" : "FAIL");
        separated ? ++pass_ : ++fail_;

        device.SetVertexBuffer(nullptr);
        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxPbrFresnelFactorsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int Result() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxPbrFresnelFactorsTest game;
    game.Run();
    return game.Result();
}
