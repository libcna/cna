// SPDX-License-Identifier: MS-PL
// GLTF-213: real Bgfx pixel coverage for the PBR colour-space declarations.
//
// This is the Bgfx adaptation of easygl_pbr_srgb_transfer_test.cpp. Bgfx does not implement
// GraphicsDevice::SetDepthTestEnabled(), so the test uses DepthStencilState instead. It also
// performs one backbuffer read for every fresh draw, matching Bgfx's established readback
// convention. The six discriminating cases are otherwise identical and run on both the rigid
// and identity-skinned PBR programs.

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

#include <array>
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

    PbrVertex MakePbrVertex(float x, float y, float u, float v)
    {
        return {x, y, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, u, v};
    }

    std::vector<PbrVertex> RigidQuad()
    {
        const PbrVertex tl = MakePbrVertex(-1.0f,  1.0f, 0.0f, 0.0f);
        const PbrVertex bl = MakePbrVertex(-1.0f, -1.0f, 0.0f, 1.0f);
        const PbrVertex br = MakePbrVertex( 1.0f, -1.0f, 1.0f, 1.0f);
        const PbrVertex tr = MakePbrVertex( 1.0f,  1.0f, 1.0f, 0.0f);
        return {tl, bl, br, tl, br, tr};
    }

    std::vector<SkinnedPbrVertex> SkinnedQuad()
    {
        std::vector<SkinnedPbrVertex> result;
        for (const PbrVertex& vertex : RigidQuad())
            result.push_back({vertex, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0});
        return result;
    }

    enum class TransferCase
    {
        BaseDecode,
        BaseBypass,
        BaseFactor,
        EmissiveDecode,
        EmissiveBypass,
        BasePlusEmissive,
    };

    struct Expectation
    {
        TransferCase testCase;
        const char* name;
        Color expected;
        int tolerance;
    };

    const std::array<Expectation, 6> kExpectations = {{
        {TransferCase::BaseDecode,       "base sRGB decode",                  Color(128, 128, 128, 255), 1},
        {TransferCase::BaseBypass,       "base linear bypass",               Color(188, 188, 188, 255), 1},
        {TransferCase::BaseFactor,       "base linear factor after decode",  Color( 92,  92,  92, 255), 2},
        {TransferCase::EmissiveDecode,   "emissive sRGB decode",              Color(128, 128, 128, 255), 1},
        {TransferCase::EmissiveBypass,   "emissive linear bypass",           Color(188, 188, 188, 255), 1},
        {TransferCase::BasePlusEmissive, "base plus emissive in linear space", Color(112, 112, 112, 255), 2},
    }};

    template <typename Effect>
    void Configure(Effect& effect, TransferCase testCase, Texture2D& black, Texture2D& midGrey)
    {
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setMetallicFactorProperty(0.0f);
        effect.setRoughnessFactorProperty(1.0f);
        effect.setEncodeOutputToSrgbEXTProperty(true);
        effect.DirectionalLight0.setEnabledProperty(false);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);

        effect.setTextureProperty(&midGrey);
        effect.setDiffuseColorProperty(Vector3::One);
        effect.setAmbientLightColorProperty(Vector3::One);
        effect.setEmissiveMapProperty(nullptr);
        effect.setEmissiveFactorProperty(Vector3::Zero);
        effect.setBaseColorTextureIsSrgbEXTProperty(true);
        effect.setEmissiveTextureIsSrgbEXTProperty(true);

        switch (testCase)
        {
        case TransferCase::BaseDecode:
            break;
        case TransferCase::BaseBypass:
            effect.setBaseColorTextureIsSrgbEXTProperty(false);
            break;
        case TransferCase::BaseFactor:
            effect.setDiffuseColorProperty(Vector3(0.5f, 0.5f, 0.5f));
            break;
        case TransferCase::EmissiveDecode:
            effect.setTextureProperty(&black);
            effect.setEmissiveMapProperty(&midGrey);
            effect.setEmissiveFactorProperty(Vector3::One);
            break;
        case TransferCase::EmissiveBypass:
            effect.setTextureProperty(&black);
            effect.setEmissiveMapProperty(&midGrey);
            effect.setEmissiveFactorProperty(Vector3::One);
            effect.setEmissiveTextureIsSrgbEXTProperty(false);
            break;
        case TransferCase::BasePlusEmissive:
            effect.setDiffuseColorProperty(Vector3(0.25f, 0.25f, 0.25f));
            effect.setEmissiveMapProperty(&midGrey);
            effect.setEmissiveFactorProperty(Vector3(0.5f, 0.5f, 0.5f));
            break;
        }
    }

    bool Matches(const Color& got, const Color& expected, int tolerance)
    {
        const auto close = [tolerance](int a, int b) { return std::abs(a - b) <= tolerance; };
        return close(got.getRProperty(), expected.getRProperty())
            && close(got.getGProperty(), expected.getGProperty())
            && close(got.getBProperty(), expected.getBProperty());
    }
}

class BgfxPbrSrgbTransferTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D black_;
    Texture2D midGrey_;
    int pass_ = 0;
    int fail_ = 0;

    template <typename Effect>
    Color Render(GraphicsDevice& device, VertexBuffer& vertices, TransferCase testCase)
    {
        Effect effect(device);
        Configure(effect, testCase, black_, midGrey_);
        if constexpr (std::is_same_v<Effect, SkinnedPbrEffect>)
        {
            effect.SetBoneTransforms({Matrix::getIdentityProperty()});
            effect.setWeightsPerVertexProperty(1);
        }

        Color got(0, 0, 0, 0);
        for (int attempt = 0; attempt < 20; ++attempt)
        {
            device.Clear(Color(0, 255, 0, 255));
            effect.Apply();
            device.SetVertexBuffer(&vertices);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            const Rectangle center(kSize / 2, kSize / 2, 1, 1);
            device.GetBackBufferData(&center, &got, 0, 1);
            if (got.getRProperty() != 0 || got.getGProperty() != 0 || got.getBProperty() != 0)
                break;
        }
        return got;
    }

    void Check(const char* program, const Expectation& expectation, const Color& got)
    {
        const bool ok = Matches(got, expectation.expected, expectation.tolerance);
        std::printf("[%s] %s %s: got=(%d,%d,%d) want=(%d,%d,%d) tol=%d\n",
                    ok ? "PASS" : "FAIL", program, expectation.name,
                    got.getRProperty(), got.getGProperty(), got.getBProperty(),
                    expectation.expected.getRProperty(), expectation.expected.getGProperty(),
                    expectation.expected.getBProperty(), expectation.tolerance);
        ok ? ++pass_ : ++fail_;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        black_ = Texture2D::CreateFromPixels(device, 1, 1, {0, 0, 0, 255});
        midGrey_ = Texture2D::CreateFromPixels(device, 1, 1, {128, 128, 128, 255});
    }

    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        DepthStencilState depthState;
        depthState.setDepthBufferEnableProperty(false);
        device.setDepthStencilStateProperty(depthState);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const std::vector<PbrVertex> rigid = RigidQuad();
        VertexBuffer rigidBuffer(device, static_cast<int>(rigid.size()));
        rigidBuffer.SetDataRaw(rigid.data(), static_cast<int>(rigid.size()), sizeof(PbrVertex));
        for (const Expectation& expectation : kExpectations)
            Check("PbrEffect", expectation,
                  Render<PbrEffect>(device, rigidBuffer, expectation.testCase));

        const std::vector<SkinnedPbrVertex> skinned = SkinnedQuad();
        VertexBuffer skinnedBuffer(device, static_cast<int>(skinned.size()));
        skinnedBuffer.SetDataRaw(
            skinned.data(), static_cast<int>(skinned.size()), sizeof(SkinnedPbrVertex));
        for (const Expectation& expectation : kExpectations)
            Check("SkinnedPbrEffect", expectation,
                  Render<SkinnedPbrEffect>(device, skinnedBuffer, expectation.testCase));

        device.SetVertexBuffer(nullptr);
        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        Exit();
    }

public:
    BgfxPbrSrgbTransferTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int Result() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    BgfxPbrSrgbTransferTest game;
    game.Run();
    return game.Result();
}
