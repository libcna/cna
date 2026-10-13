// SPDX-License-Identifier: MS-PL
// A stock effect drawn from a vertex stream that lacks some of its shader's inputs either renders
// (the missing inputs defaulted) or refuses the draw -- it never draws nothing and reports success.
//
// Moved from the retired VULKAN suite (VULKAN-156). The fixture is a stride-20 Position +
// TextureCoordinate quad over a two-texel texture, drawn through each stock family in turn; a
// normal, a tangent and a bone palette are absent. XNA itself refuses such a draw from the Draw
// call (InvalidOperationException naming the missing element); a renderer that defaults the
// inputs instead renders the sampled texel. The third behaviour is the one this file exists to
// catch, because a test that compares a family with itself (rendertarget_effect_source_test.cpp)
// passes it: a family that rasterizes nothing agrees with itself perfectly.
//
// Every family is drawn twice: from a buffer that declares exactly Position and
// TextureCoordinate0 (what an XNA game hands it), and from VertexBuffer(device, count), CNA's
// declaration-less convenience buffer. A refusal counts only when Apply() or the draw call throws
// it; one that surfaces from the read-back afterwards came after the draw had reported success.
//
//   0  BasicEffect, declared (the harness control: it must render the texel)
//   A  BasicEffect, B AlphaTestEffect, C SkinnedEffect (no normal, weights or indices),
//   D  PbrEffect (no normal or tangent) -- each declared and declaration-less
//
// Exit code 0 = PASS, 1 = FAIL, 77 = skipped (no display or no 3D rendering).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "CNA/GraphicsCapability.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    const Color kLeft(230, 30, 30, 255);
    const Color kRight(30, 60, 230, 255);
    const Color kClear(0, 0, 0, 255);

    bool Near(const Color& got, const Color& want)
    {
        const int tolerance = 24;
        return std::abs(got.getRProperty() - want.getRProperty()) <= tolerance
            && std::abs(got.getGProperty() - want.getGProperty()) <= tolerance
            && std::abs(got.getBProperty() - want.getBProperty()) <= tolerance;
    }

    std::string Show(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    template <typename EffectType>
    void Neutral(EffectType& fx)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
    }
}

class StockEffectUnsuppliedInputTest final : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> strip_;
    bool skipped_ = false;

    // `mustRender` is the harness control: it may not take the refusal branch.
    void RunFamily(const std::string& name, Effect& effect, VertexBuffer& vb, bool mustRender = false)
    {
        auto& dev = getGraphicsDeviceProperty();
        std::string refusal;
        std::string late;
        Color got(0, 0, 0, 0);
        dev.Clear(kClear);
        try
        {
            effect.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        }
        catch (const std::exception& e)
        {
            refusal = e.what();
        }
        try
        {
            dev.SetVertexBuffer(nullptr);
            const Rectangle probe(kSize / 2, kSize / 2, 1, 1);
            dev.GetBackBufferData(&probe, &got, 0, 1);
        }
        catch (const std::exception& e)
        {
            late = e.what();
        }

        if (!late.empty())
        {
            Check(false, (name + ": the draw reported success and the refusal surfaced only "
                          "afterwards, from the read-back: " + late).c_str());
            return;
        }
        if (!refusal.empty())
        {
            Check(!mustRender, (name + " refuses the draw rather than guessing: " + refusal).c_str());
            return;
        }
        const std::string detail = Near(got, kClear)
            ? "drew NOTHING: the pixel is still the clear colour " + Show(got)
            : Show(got) + " (want " + Show(kLeft) + ")";
        Check(Near(got, kLeft), (name + " renders the sampled texel or refuses -- never draws "
                                 "nothing and reports success: " + detail).c_str());
    }

public:
    StockEffectUnsuppliedInputTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int ExitCode() const
    {
        return skipped_ ? CNA::Examples::kSkipExitCode : getResultProperty();
    }

    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();
        if (!dev.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        {
            std::printf("[SKIP] this renderer does not rasterize 3D stock effects\n");
            skipped_ = true;
            return;
        }
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

        strip_ = std::make_unique<Texture2D>(dev, 2, 1);
        const Color texels[2] = { kLeft, kRight };
        strip_->SetData(texels, 2);

        // Built byte by byte: a stride-20 record of Position and the LEFT texel's UV.
        std::uint8_t quad[6 * 20]{};
        constexpr float kX[6] = { -1.f, -1.f,  1.f, -1.f,  1.f,  1.f };
        constexpr float kY[6] = {  1.f, -1.f, -1.f,  1.f, -1.f,  1.f };
        for (int i = 0; i < 6; ++i)
        {
            const float pos[3] = { kX[i], kY[i], 0.0f };
            const float uv[2] = { 0.25f, 0.5f };
            std::memcpy(quad + i * 20, pos, sizeof(pos));
            std::memcpy(quad + i * 20 + 12, uv, sizeof(uv));
        }
        const VertexDeclaration declaration(20, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
        });
        VertexBuffer declared(dev, declaration, 6, BufferUsage::None);
        declared.SetDataRaw(quad, 6, 20);
        VertexBuffer bare(dev, 6);
        bare.SetDataRaw(quad, 6, 20);

        struct Source { const char* name; VertexBuffer* vb; };
        const Source sources[2] = { { "declared", &declared }, { "no declaration", &bare } };

        {
            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(strip_.get());
            fx.setLightingEnabledProperty(false);
            fx.setFogEnabledProperty(false);
            Neutral(fx);
            RunFamily("0 BasicEffect, declared (harness control)", fx, declared, true);
        }
        for (const Source& source : sources)
        {
            const std::string suffix = std::string(", ") + source.name;
            {
                BasicEffect fx(dev);
                fx.setTextureEnabledProperty(true);
                fx.setTextureProperty(strip_.get());
                fx.setLightingEnabledProperty(false);
                fx.setFogEnabledProperty(false);
                Neutral(fx);
                RunFamily("A BasicEffect (Position+UV is its own record)" + suffix, fx, *source.vb);
            }
            {
                AlphaTestEffect fx(dev);
                fx.setTextureProperty(strip_.get());
                Neutral(fx);
                RunFamily("B AlphaTestEffect" + suffix, fx, *source.vb);
            }
            {
                SkinnedEffect fx(dev);
                fx.setTextureProperty(strip_.get());
                Neutral(fx);
                RunFamily("C SkinnedEffect (no normal, weights or bone indices)" + suffix, fx,
                          *source.vb);
            }
            {
                PbrEffect fx(dev);
                fx.setTextureProperty(strip_.get());
                fx.setMetallicFactorProperty(0.0f);
                fx.setRoughnessFactorProperty(1.0f);
                fx.setBaseColorTextureIsSrgbEXTProperty(false);
                fx.setEncodeOutputToSrgbEXTProperty(false);
                fx.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
                fx.DirectionalLight0.setEnabledProperty(false);
                fx.DirectionalLight1.setEnabledProperty(false);
                fx.DirectionalLight2.setEnabledProperty(false);
                Neutral(fx);
                RunFamily("D PbrEffect (no normal, no tangent)" + suffix, fx, *source.vb);
            }
        }
    }
};

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    StockEffectUnsuppliedInputTest game;
    game.Run();
    return game.ExitCode();
}
