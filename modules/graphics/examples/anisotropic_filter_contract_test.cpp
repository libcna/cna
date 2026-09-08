// SPDX-License-Identifier: MS-PL
// SOFTWARE-117: renderer-independent proof that TextureFilter::Anisotropic is directional rather
// than an alias for trilinear sampling. Distinct flat mip colours expose LOD selection without a
// subjective quality threshold. The fixture also covers both axes, SpriteBatch forwarding,
// independent DualTextureEffect slots and address modes through public XNA APIs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "CNA/GraphicsCapability.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kTarget = 4;

    struct VtxPT { float x, y, z, u, v; };
    static_assert(sizeof(VtxPT) == 20);
    struct VtxDualPT { float x, y, z, u0, v0, u1, v1; };
    static_assert(sizeof(VtxDualPT) == 28);

    const VertexDeclaration kPtDeclaration(
        20, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
        });
    const VertexDeclaration kDualDeclaration(
        28, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 0),
            VertexElement(20, VertexElementFormat::Vector2,
                          VertexElementUsage::TextureCoordinate, 1),
        });

    std::array<VtxPT, 6> Quad(float u0 = 0.0f, float v0 = 0.0f,
                              float u1 = 1.0f, float v1 = 1.0f)
    {
        return {{
            {-1.0f,  1.0f, 0.0f, u0, v0}, {-1.0f, -1.0f, 0.0f, u0, v1},
            { 1.0f, -1.0f, 0.0f, u1, v1}, {-1.0f,  1.0f, 0.0f, u0, v0},
            { 1.0f, -1.0f, 0.0f, u1, v1}, { 1.0f,  1.0f, 0.0f, u1, v0},
        }};
    }

    std::array<VtxDualPT, 6> DualQuad()
    {
        const auto source = Quad();
        std::array<VtxDualPT, 6> result{};
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = {source[i].x, source[i].y, source[i].z,
                         source[i].u, source[i].v, source[i].u, source[i].v};
        return result;
    }

    int ChannelDistance(const Color& a, const Color& b)
    {
        return std::max({
            std::abs(static_cast<int>(a.getRProperty()) - static_cast<int>(b.getRProperty())),
            std::abs(static_cast<int>(a.getGProperty()) - static_cast<int>(b.getGProperty())),
            std::abs(static_cast<int>(a.getBProperty()) - static_cast<int>(b.getBProperty()))});
    }
}

class AnisotropicFilterContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_ = false;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;
    std::vector<Color> levelColors_{
        Color(240, 32, 32, 255), Color(32, 240, 32, 255),
        Color(32, 32, 240, 255), Color(240, 240, 32, 255),
        Color(240, 32, 240, 255), Color(32, 240, 240, 255),
        Color(224, 224, 224, 255)};
    Texture2D horizontal_, vertical_, edge_, dualA_, dualB_;

    void Check(bool value, const std::string& label)
    {
        std::printf("[%s] %s\n", value ? "PASS" : "FAIL", label.c_str());
        ++total_;
        if (value) ++passed_;
    }

    static SamplerState Anisotropic(int maxAnisotropy,
                                    TextureAddressMode addressU = TextureAddressMode::Clamp)
    {
        SamplerState sampler;
        sampler.setFilterProperty(TextureFilter::Anisotropic);
        sampler.setAddressUProperty(addressU);
        sampler.setAddressVProperty(TextureAddressMode::Clamp);
        sampler.setMaxAnisotropyProperty(maxAnisotropy);
        return sampler;
    }

    static void SetState(GraphicsDevice& device)
    {
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);
    }

    static std::vector<Color> Read(RenderTarget2D& target)
    {
        std::vector<Color> pixels(kTarget * kTarget, Color::Transparent);
        target.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels;
    }

    std::vector<Color> Render3D(Texture2D& texture, const SamplerState& sampler,
                                const std::array<VtxPT, 6>& vertices = Quad())
    {
        GraphicsDevice& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, kTarget, kTarget, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&target);
        SetState(device);
        device.Clear(Color::Black);
        device.getSamplerStatesProperty()[0] = sampler;
        BasicEffect effect(device);
        effect.setTextureEnabledProperty(true);
        effect.setTextureProperty(&texture);
        effect.setLightingEnabledProperty(false);
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 2,
                                  kPtDeclaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return Read(target);
    }

    std::vector<Color> RenderSprite(Texture2D& texture, SamplerState sampler)
    {
        GraphicsDevice& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, kTarget, kTarget, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&target);
        SetState(device);
        device.Clear(Color::Black);
        SpriteBatch sprites(device);
        sprites.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &sampler,
                      &DepthStencilState::None, &RasterizerState::CullNone, nullptr,
                      Matrix::getIdentityProperty());
        sprites.Draw(texture, Rectangle(0, 0, kTarget, kTarget),
                     Rectangle(0, 0, texture.getWidthProperty(), texture.getHeightProperty()),
                     Color::White, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
        sprites.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return Read(target);
    }

    std::vector<Color> RenderDual(const SamplerState& slot0, const SamplerState& slot1)
    {
        GraphicsDevice& device = getGraphicsDeviceProperty();
        RenderTarget2D target(device, kTarget, kTarget, false, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
        device.SetRenderTarget(&target);
        SetState(device);
        device.Clear(Color::Black);
        device.getSamplerStatesProperty()[0] = slot0;
        device.getSamplerStatesProperty()[1] = slot1;
        DualTextureEffect effect(device);
        effect.setTextureProperty(&dualA_);
        effect.setTexture2Property(&dualB_);
        effect.Apply();
        const auto vertices = DualQuad();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 2,
                                  kDualDeclaration);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return Read(target);
    }

    bool AllNear(const std::vector<Color>& pixels, const Color& expected, int tolerance = 3) const
    {
        return std::all_of(pixels.begin(), pixels.end(), [&](const Color& pixel) {
            return ChannelDistance(pixel, expected) <= tolerance;
        });
    }

    static Color DualExpected(const Color& a, const Color& b)
    {
        const auto channel = [](int x, int y) {
            return std::clamp(static_cast<int>(std::lround(2.0 * x * y / 255.0)), 0, 255);
        };
        return Color(channel(a.getRProperty(), b.getRProperty()),
                     channel(a.getGProperty(), b.getGProperty()),
                     channel(a.getBProperty(), b.getBProperty()), 255);
    }

    static void FillMipTexture(Texture2D& texture, const std::vector<Color>& colors)
    {
        for (int level = 0; level < texture.getLevelCountProperty(); ++level)
        {
            const int width = std::max(1, texture.getWidthProperty() >> level);
            const int height = std::max(1, texture.getHeightProperty() >> level);
            std::vector<Color> pixels(static_cast<std::size_t>(width * height),
                                      colors[static_cast<std::size_t>(level)]);
            texture.SetData(level, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
        }
    }

    void LoadContent() override
    {
        GraphicsDevice& device = getGraphicsDeviceProperty();
        horizontal_ = Texture2D(device, 64, 4, true, SurfaceFormat::Color);
        vertical_ = Texture2D(device, 4, 64, true, SurfaceFormat::Color);
        FillMipTexture(horizontal_, levelColors_);
        FillMipTexture(vertical_, levelColors_);

        edge_ = Texture2D(device, 64, 4);
        std::vector<Color> edgePixels(64 * 4, Color::Red);
        for (int y = 0; y < 4; ++y)
            for (int x = 32; x < 64; ++x)
                edgePixels[static_cast<std::size_t>(y * 64 + x)] = Color::Blue;
        edge_.SetData(edgePixels.data(), static_cast<int>(edgePixels.size()));

        const std::vector<Color> aColors{
            Color(100, 50, 80), Color(90, 60, 75), Color(75, 70, 70),
            Color(60, 80, 65), Color(40, 90, 60), Color(30, 100, 55), Color(20, 110, 50)};
        const std::vector<Color> bColors{
            Color(60, 110, 40), Color(65, 95, 55), Color(70, 80, 70),
            Color(80, 60, 85), Color(90, 40, 100), Color(100, 30, 105), Color(110, 20, 110)};
        dualA_ = Texture2D(device, 64, 4, true, SurfaceFormat::Color);
        dualB_ = Texture2D(device, 64, 4, true, SurfaceFormat::Color);
        FillMipTexture(dualA_, aColors);
        FillMipTexture(dualB_, bColors);
    }

    void Draw(const GameTime&) override
    {
        if (done_) { Exit(); return; }
        done_ = true;
        GraphicsDevice& device = getGraphicsDeviceProperty();
        if (!device.SupportsCapability(CNA::GraphicsCapability::AnisotropicFiltering))
        {
            std::printf("[SKIP] renderer truthfully reports no anisotropic filtering\n");
            result_ = 0;
            Exit();
            return;
        }

        Check(horizontal_.getLevelCountProperty() == 7 && vertical_.getLevelCountProperty() == 7,
              "64x4 and 4x64 textures expose the complete seven-level mip chain");
        const auto h1 = Render3D(horizontal_, Anisotropic(1));
        const auto h4 = Render3D(horizontal_, Anisotropic(4));
        const auto h16 = Render3D(horizontal_, Anisotropic(16));
        Check(AllNear(h1, levelColors_[4]), "horizontal 16:1 at MaxAnisotropy=1 selects mip 4");
        Check(AllNear(h4, levelColors_[2]), "horizontal 16:1 at MaxAnisotropy=4 selects mip 2");
        Check(AllNear(h16, levelColors_[0]), "horizontal 16:1 at MaxAnisotropy=16 preserves mip 0");
        Check(AllNear(Render3D(vertical_, Anisotropic(16)), levelColors_[0]),
              "vertical 16:1 is filtered along V and preserves mip 0");
        Check(Render3D(horizontal_, Anisotropic(9999)) == h16,
              "an over-cap request clamps to the deterministic 16x ceiling");
        Check(Render3D(horizontal_, Anisotropic(0)) == h1,
              "a non-positive request clamps safely to 1x");

        const auto s1 = RenderSprite(horizontal_, Anisotropic(1));
        const auto s4 = RenderSprite(horizontal_, Anisotropic(4));
        const auto s16 = RenderSprite(horizontal_, Anisotropic(16));
        Check(AllNear(s1, levelColors_[4]) && AllNear(s4, levelColors_[2]) &&
                  AllNear(s16, levelColors_[0]),
              "SpriteBatch forwards MaxAnisotropy instead of hard-coding 1");
        Check(RenderSprite(horizontal_, Anisotropic(16)) == s16,
              "SpriteBatch anisotropy transitions restore byte-identical output");

        const auto dualHighLow = RenderDual(Anisotropic(16), Anisotropic(1));
        const auto dualLowHigh = RenderDual(Anisotropic(1), Anisotropic(16));
        Check(AllNear(dualHighLow, DualExpected(Color(100, 50, 80), Color(90, 40, 100)), 4),
              "DualTexture slot 0 keeps 16x while slot 1 independently uses 1x");
        Check(AllNear(dualLowHigh, DualExpected(Color(40, 90, 60), Color(60, 110, 40)), 4),
              "DualTexture slot 1 keeps 16x while slot 0 independently uses 1x");
        Check(dualHighLow != dualLowHigh,
              "swapping only per-slot MaxAnisotropy produces distinct output");

        const auto boundaryQuad = Quad(-0.125f, 0.0f, 0.875f, 1.0f);
        const auto clamp = Render3D(edge_, Anisotropic(16, TextureAddressMode::Clamp), boundaryQuad);
        const auto wrap = Render3D(edge_, Anisotropic(16, TextureAddressMode::Wrap), boundaryQuad);
        const Color clampProbe = clamp[8];
        const Color wrapProbe = wrap[8];
        std::printf("[INFO] boundary probes clamp=(%u,%u,%u) wrap=(%u,%u,%u)\n",
                    static_cast<unsigned>(clampProbe.getRProperty()),
                    static_cast<unsigned>(clampProbe.getGProperty()),
                    static_cast<unsigned>(clampProbe.getBProperty()),
                    static_cast<unsigned>(wrapProbe.getRProperty()),
                    static_cast<unsigned>(wrapProbe.getGProperty()),
                    static_cast<unsigned>(wrapProbe.getBProperty()));
        Check(clampProbe.getRProperty() > 220 && clampProbe.getBProperty() < 35,
              "Clamp keeps major-axis taps on the first texel edge");
        Check(static_cast<int>(wrapProbe.getBProperty()) >
                  static_cast<int>(clampProbe.getBProperty()) + 80 &&
              static_cast<int>(wrapProbe.getRProperty()) + 80 <
                  static_cast<int>(clampProbe.getRProperty()),
              "Wrap redirects negative major-axis taps toward the opposite edge");
        Check(clamp != wrap, "anisotropic taps retain the selected U address mode");

        std::printf("=== AnisotropicFilterContract: %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    AnisotropicFilterContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(16);
    }

    [[nodiscard]] int getResult() const { return result_; }
};

int main()
{
    AnisotropicFilterContractTest game;
    game.Run();
    return game.getResult();
}
