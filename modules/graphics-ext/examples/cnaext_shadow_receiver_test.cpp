// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2236: stock-shadow receiver oracle independent of caster shaders.

#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectMatrices.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/PunctualLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShadowCascadeStateEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::GraphicsCapability;

namespace
{
    constexpr int kFrame = 96;

    struct LitVertex { float px, py, pz, nx, ny, nz, u, v; };
    static_assert(sizeof(LitVertex) == 32);
    struct SkinnedVertex {
        float px, py, pz, nx, ny, nz, u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedVertex) == 52);
    struct PbrVertex { float px, py, pz, nx, ny, nz, tx, ty, tz, tw, u, v; };
    static_assert(sizeof(PbrVertex) == 48);
    struct SkinnedPbrVertex {
        float px, py, pz, nx, ny, nz, tx, ty, tz, tw, u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedPbrVertex) == 68);

    template<typename Vertex> std::array<Vertex, 6> Quad();

    template<> std::array<LitVertex, 6> Quad()
    {
        return {{
            {-1,-1,0, 0,0,1, 0,1}, { 1,-1,0, 0,0,1, 1,1},
            { 1, 1,0, 0,0,1, 1,0}, {-1,-1,0, 0,0,1, 0,1},
            { 1, 1,0, 0,0,1, 1,0}, {-1, 1,0, 0,0,1, 0,0},
        }};
    }

    template<> std::array<SkinnedVertex, 6> Quad()
    {
        const auto base = Quad<LitVertex>();
        std::array<SkinnedVertex, 6> out{};
        for (std::size_t i = 0; i < out.size(); ++i) {
            const auto& v = base[i];
            out[i] = {v.px,v.py,v.pz,v.nx,v.ny,v.nz,v.u,v.v, 1,0,0,0, 0,0,0,0};
        }
        return out;
    }

    template<> std::array<PbrVertex, 6> Quad()
    {
        const auto base = Quad<LitVertex>();
        std::array<PbrVertex, 6> out{};
        for (std::size_t i = 0; i < out.size(); ++i) {
            const auto& v = base[i];
            out[i] = {v.px,v.py,v.pz,v.nx,v.ny,v.nz, 1,0,0,1, v.u,v.v};
        }
        return out;
    }

    template<> std::array<SkinnedPbrVertex, 6> Quad()
    {
        const auto base = Quad<PbrVertex>();
        std::array<SkinnedPbrVertex, 6> out{};
        for (std::size_t i = 0; i < out.size(); ++i) {
            const auto& v = base[i];
            out[i] = {v.px,v.py,v.pz,v.nx,v.ny,v.nz,v.tx,v.ty,v.tz,v.tw,v.u,v.v,
                      1,0,0,0, 0,0,0,0};
        }
        return out;
    }

    template<typename Vertex>
    std::unique_ptr<VertexBuffer> MakeQuadBuffer(GraphicsDevice& device)
    {
        const auto vertices = Quad<Vertex>();
        auto buffer = std::make_unique<VertexBuffer>(device, static_cast<int>(vertices.size()));
        buffer->SetDataRaw(vertices.data(), static_cast<int>(vertices.size()),
                           static_cast<int>(sizeof(Vertex)));
        return buffer;
    }
}

class ShadowReceiverExample final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> white_;
    std::unique_ptr<Texture2D> black_;
    std::unique_ptr<TextureCube> blackCube_;
    std::unique_ptr<VertexBuffer> litQuad_;
    std::unique_ptr<VertexBuffer> skinnedQuad_;
    std::unique_ptr<VertexBuffer> pbrQuad_;
    std::unique_ptr<VertexBuffer> skinnedPbrQuad_;
    int passed_ = 0;
    int checked_ = 0;
    int result_ = 1;

    void check(bool ok, const std::string& label)
    {
        ++checked_;
        if (ok) ++passed_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
    }

    static void ConfigureMatrices(IEffectMatrices& effect)
    {
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::CreateLookAt(
            Vector3(0, 0, 3), Vector3::Zero, Vector3(0, 1, 0)));
        effect.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            1.0f, 1.0f, 0.1f, 100.0f));
    }

    template<typename EffectType> static void ConfigureLights(EffectType& effect)
    {
        ConfigureMatrices(effect);
        effect.setLightingEnabledProperty(true);
        effect.setAmbientLightColorProperty(Vector3(0.2f, 0.2f, 0.2f));
        effect.setDiffuseColorProperty(Vector3(1, 1, 1));
        effect.setFogEnabledProperty(false);
        effect.DirectionalLight0.setEnabledProperty(true);
        effect.DirectionalLight0.setDirectionProperty(Vector3(0, 0, -1));
        effect.DirectionalLight0.setDiffuseColorProperty(Vector3(0.7f, 0.7f, 0.7f));
        effect.DirectionalLight0.setSpecularColorProperty(Vector3::Zero);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
    }

    void DrawEffect(GraphicsDevice& device, Effect& effect, VertexBuffer& vertices)
    {
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setBlendStateProperty(BlendState::Opaque);
        effect.Apply();
        device.SetVertexBuffer(&vertices);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
    }

    Color Render(GraphicsDevice& device, Effect& effect, VertexBuffer& vertices)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame);
        device.Clear(Color::Black);
        DrawEffect(device, effect, vertices);
        device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
        return pixels[static_cast<std::size_t>(kFrame / 2) * kFrame + kFrame / 2];
    }

    static int Luma(const Color& c)
    {
        return (c.getRProperty() + c.getGProperty() + c.getBProperty()) / 3;
    }

    template<typename EffectType>
    void CheckDirectionalFamily(GraphicsDevice& device, EffectType& effect,
                                VertexBuffer& vertices, const char* name)
    {
        const int lit = Luma(Render(device, effect, vertices));
        effect.setShadowMapEXT(black_.get());
        effect.setLightViewProjectionEXT(Matrix::getIdentityProperty());
        effect.setShadowsEnabledEXT(true);
        const int shadowed = Luma(Render(device, effect, vertices));
        effect.setShadowsEnabledEXT(false);
        const int disabled = Luma(Render(device, effect, vertices));
        std::printf("    %s: lit %d, black-map %d, disabled %d\n", name, lit, shadowed, disabled);
        check(shadowed < lit - 20 && shadowed > 20,
              std::string(name) + " removes direct light but preserves ambient");
        check(std::abs(disabled - lit) <= 4,
              std::string(name) + " ignores an attached map when reception is disabled");
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        if (!device.SupportsCapability(GraphicsCapability::ThreeD) ||
            !device.SupportsShadowSamplingEXT()) {
            std::printf("SKIP: this renderer has no stock shadow receiver\n");
            std::exit(77);
        }

        white_ = std::make_unique<Texture2D>(device, 2, 2);
        black_ = std::make_unique<Texture2D>(device, 2, 2);
        const std::array<Color, 4> whitePixels{Color::White, Color::White, Color::White, Color::White};
        const std::array<Color, 4> blackPixels{Color::Black, Color::Black, Color::Black, Color::Black};
        white_->SetData(whitePixels.data(), 4);
        black_->SetData(blackPixels.data(), 4);
        blackCube_ = std::make_unique<TextureCube>(device, 2, false, SurfaceFormat::Color);
        for (int face = 0; face < 6; ++face)
            blackCube_->SetData(static_cast<CubeMapFace>(face), blackPixels.data(), 4);

        litQuad_ = MakeQuadBuffer<LitVertex>(device);
        skinnedQuad_ = MakeQuadBuffer<SkinnedVertex>(device);
        pbrQuad_ = MakeQuadBuffer<PbrVertex>(device);
        skinnedPbrQuad_ = MakeQuadBuffer<SkinnedPbrVertex>(device);

        BasicEffect basic(device);
        ConfigureLights(basic);
        basic.setTextureEnabledProperty(true);
        basic.setTextureProperty(white_.get());
        basic.setSpecularColorProperty(Vector3::Zero);
        // Deliberately request XNA's Gouraud default. Directional reception must force the
        // per-pixel path, as EasyGL does, rather than interpolate visibility per corner.
        basic.setPreferPerPixelLightingProperty(false);
        CheckDirectionalFamily(device, basic, *litQuad_, "BasicEffect");

        SkinnedEffect skinned(device);
        ConfigureLights(skinned);
        skinned.setTextureProperty(white_.get());
        skinned.setSpecularColorProperty(Vector3::Zero);
        skinned.setPreferPerPixelLightingProperty(false);
        skinned.setWeightsPerVertexProperty(1);
        skinned.SetBoneTransforms(std::vector<Matrix>{Matrix::getIdentityProperty()});
        CheckDirectionalFamily(device, skinned, *skinnedQuad_, "SkinnedEffect");

        PbrEffect pbr(device);
        ConfigureLights(pbr);
        pbr.setTextureProperty(white_.get());
        pbr.setMetallicFactorProperty(0.0f);
        pbr.setRoughnessFactorProperty(0.6f);
        pbr.setBaseColorTextureIsSrgbEXTProperty(false);
        pbr.setEncodeOutputToSrgbEXTProperty(false);
        CheckDirectionalFamily(device, pbr, *pbrQuad_, "PbrEffect");

        SkinnedPbrEffect skinnedPbr(device);
        ConfigureLights(skinnedPbr);
        skinnedPbr.setTextureProperty(white_.get());
        skinnedPbr.setMetallicFactorProperty(0.0f);
        skinnedPbr.setRoughnessFactorProperty(0.6f);
        skinnedPbr.setBaseColorTextureIsSrgbEXTProperty(false);
        skinnedPbr.setEncodeOutputToSrgbEXTProperty(false);
        skinnedPbr.setWeightsPerVertexProperty(1);
        skinnedPbr.SetBoneTransforms(std::vector<Matrix>{Matrix::getIdentityProperty()});
        CheckDirectionalFamily(device, skinnedPbr, *skinnedPbrQuad_, "SkinnedPbrEffect");

        basic.setShadowMapEXT(white_.get());
        basic.setShadowsEnabledEXT(true);
        basic.setPreferPerPixelLightingProperty(true);
        ShadowCascadeStateEXT cascade;
        cascade.Count = 1;
        cascade.WorldToAtlas[0] = Matrix::getIdentityProperty();
        cascade.SplitDistance[0] = 100.0f;
        cascade.CameraView = Matrix::getIdentityProperty();
        cascade.DebugTint = true;
        basic.setShadowCascadesEXT(cascade);
        const Color tinted = Render(device, basic, *litQuad_);
        std::printf("    cascade debug: (%u,%u,%u)\n", tinted.getRProperty(),
                    tinted.getGProperty(), tinted.getBProperty());
        check(tinted.getRProperty() > tinted.getGProperty() + 20 &&
              tinted.getRProperty() > tinted.getBProperty() + 20,
              "the cascade state selects and debug-tints the first atlas slice");

        // Punctual sampling is per-pixel in EasyGL. Disable the sun and compare the same lamp
        // with and without a constant-black distance map.
        basic.setShadowCascadesEXT(ShadowCascadeStateEXT{});
        basic.setShadowsEnabledEXT(false);
        basic.DirectionalLight0.setEnabledProperty(false);
        basic.setAmbientLightColorProperty(Vector3(0.05f, 0.05f, 0.05f));
        PunctualLightEXT point;
        point.Kind = PunctualLightKindEXT::Point;
        point.Position = Vector3(0, 0, 2);
        point.DiffuseColor = Vector3(8, 8, 8);
        point.Range = 10.0f;
        basic.setPunctualLightEXT(point);
        const int pointLit = Luma(Render(device, basic, *litQuad_));
        point.ShadowCube = blackCube_.get();
        basic.setPunctualLightEXT(point);
        const int pointShadowed = Luma(Render(device, basic, *litQuad_));
        std::printf("    point: lit %d, black-cube %d\n", pointLit, pointShadowed);
        check(pointShadowed < pointLit - 20,
              "a point light samples its cube distance shadow");

        PunctualLightEXT spot;
        spot.Kind = PunctualLightKindEXT::Spot;
        spot.Position = Vector3(0, 0, 2);
        spot.Direction = Vector3(0, 0, -1);
        spot.DiffuseColor = Vector3(8, 8, 8);
        spot.Range = 10.0f;
        spot.InnerAngle = 0.4f;
        spot.OuterAngle = 0.8f;
        spot.ShadowViewProjection = Matrix::getIdentityProperty();
        basic.setPunctualLightEXT(spot);
        const int spotLit = Luma(Render(device, basic, *litQuad_));
        spot.ShadowMap = black_.get();
        basic.setPunctualLightEXT(spot);
        const int spotShadowed = Luma(Render(device, basic, *litQuad_));
        std::printf("    spot: lit %d, black-map %d\n", spotLit, spotShadowed);
        check(spotShadowed < spotLit - 20,
              "a spot light samples its projected 3x3 distance shadow");

        std::printf("%d/%d checks passed\n", passed_, checked_);
        result_ = passed_ == checked_ ? 0 : 1;
        Exit();
    }

public:
    ShadowReceiverExample()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kFrame);
        gdm_->setPreferredBackBufferHeightProperty(kFrame);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int result() const noexcept { return result_; }
};

int main()
{
    try {
        ShadowReceiverExample game;
        game.Run();
        return game.result();
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
