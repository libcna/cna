// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-156 (found while scoping VULKAN-152): what a stock family does
// when the vertex stream does not supply one of its shader's inputs.
//
// The fixture is the one `rendertarget_effect_source_test.cpp` uses everywhere: a stride-20
// `VertexPositionTexture` quad with NO VertexDeclaration, drawn through each stock family in turn
// over a two-texel texture. Position and TextureCoordinate0 are present; a normal, a tangent and
// a bone palette are not.
//
// A renderer has three possible behaviours here and only two of them are acceptable:
//
//   * render the sampled texel -- the input was defaulted, which is what an unbound GL attribute
//     does and what EasyGL therefore did for years;
//   * refuse the draw BY NAME -- the Vulkan-shaped answer, since a shader input with no
//     `VkVertexInputAttributeDescription` reads undefined data;
//   * draw NOTHING and report success.
//
// The third is the one this file exists to catch, because it is invisible to the conformance suite
// that motivated this row: `CheckConsumer` there compares two draws OF THE SAME FAMILY, one
// sampling a render target and one sampling a Texture2D, and asserts they agree. A family that
// rasterizes nothing agrees with itself perfectly, 32 samples out of 32, and passes -- which is
// exactly what `Vulkan_RenderTarget_EffectSource`'s leg B1 was doing for SkinnedEffect before
// VULKAN-156.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerStateCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionTexture.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    constexpr int kSize = 64;
    const Color kLeft (230, 30, 30, 255);
    const Color kRight(30, 60, 230, 255);
    const Color kClear(0, 0, 0, 255);
}

class VulkanUnsuppliedInputContractTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> strip_;
    int  pass_ = 0;
    int  fail_ = 0;
    bool done_ = false;

    void check(bool ok, const std::string& label, const std::string& detail)
    {
        std::printf("[%s] %s: %s\n", ok ? "PASS" : "FAIL", label.c_str(), detail.c_str());
        std::fflush(stdout);
        if (ok) ++pass_; else ++fail_;
    }

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

    static bool Near(const Color& got, const Color& want)
    {
        const int tol = 24;
        return std::abs(int(got.getRProperty()) - int(want.getRProperty())) <= tol
            && std::abs(int(got.getGProperty()) - int(want.getGProperty())) <= tol
            && std::abs(int(got.getBProperty()) - int(want.getBProperty())) <= tol;
    }
    static std::string Show(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }

    Color ReadCentre(GraphicsDevice& dev)
    {
        Color got(0, 0, 0, 0);
        const Rectangle probe(kSize / 2, kSize / 2, 1, 1);
        dev.GetBackBufferData(&probe, &got, 0, 1);
        return got;
    }

    /// Draws the stride-20 quad with `effect` and reports what happened.
    void RunFamily(GraphicsDevice& dev, const char* name, Effect& effect, VertexBuffer& vb)
    {
        Color got(0, 0, 0, 0);
        std::string refusal;
        try {
            dev.Clear(kClear);
            effect.Apply();
            dev.SetVertexBuffer(&vb);
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            got = ReadCentre(dev);
        } catch (const std::exception& e) {
            refusal = e.what();
        }

        if (!refusal.empty()) {
            check(true, std::string(name) + " refuses by name rather than guessing",
                  refusal);
            return;
        }
        const bool drewTheTexel = Near(got, kLeft);
        const bool drewNothing  = Near(got, kClear);
        check(drewTheTexel,
              std::string(name) + " either renders the sampled texel or refuses -- never draws "
              "nothing and reports success",
              drewNothing ? "drew NOTHING: the pixel is still the clear colour " + Show(got)
                          : Show(got) + " (expected " + Show(kLeft) + ")");
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetDepthTestEnabled(false);
        dev.getSamplerStatesProperty()[0] = SamplerState::PointClamp;

        strip_ = std::make_unique<Texture2D>(dev, 2, 1);
        const Color texels[2] = { kLeft, kRight };
        strip_->SetData(texels, 2);

        // The same fixture rendertarget_effect_source_test.cpp uses: a stride-20
        // position+texture quad with no declaration, its UV on the LEFT texel.
        //
        // Built byte by byte on purpose. `sizeof(VertexPositionTexture)` is 32 here, not 20 --
        // the type carries a vtable -- so handing an array of them to SetDataRaw with a stride of
        // 20 would upload every vertex but the first from the middle of its predecessor. That
        // mistake renders a plausible-looking quad, which is exactly why it is worth avoiding by
        // construction rather than by remembering.
        std::uint8_t quad[6 * 20]{};
        constexpr float kX[6] = { -1.f, -1.f,  1.f, -1.f,  1.f,  1.f };
        constexpr float kY[6] = {  1.f, -1.f, -1.f,  1.f, -1.f,  1.f };
        for (int i = 0; i < 6; ++i) {
            std::uint8_t* v = quad + i * 20;
            const float pos[3] = { kX[i], kY[i], 0.0f };
            const float uv[2]  = { 0.25f, 0.5f };
            std::memcpy(v + 0,  pos, sizeof(pos));
            std::memcpy(v + 12, uv,  sizeof(uv));
        }
        VertexBuffer vb(dev, 6);
        vb.SetDataRaw(quad, 6, 20);

        const auto neutral = [](auto& fx) {
            fx.setWorldProperty(Matrix::getIdentityProperty());
            fx.setViewProperty(Matrix::getIdentityProperty());
            fx.setProjectionProperty(Matrix::getIdentityProperty());
        };

        {   // A0: the same bytes WITH a declaration, as a harness control.
            VertexDeclaration decl(
                20,
                {
                    VertexElement(0,  VertexElementFormat::Vector3,
                                  VertexElementUsage::Position, 0),
                    VertexElement(12, VertexElementFormat::Vector2,
                                  VertexElementUsage::TextureCoordinate, 0),
                });
            VertexBuffer declared(dev, decl, 6, BufferUsage::None);
            declared.SetDataRaw(quad, 6, 20);
            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(strip_.get());
            fx.setLightingEnabledProperty(false);
            fx.setFogEnabledProperty(false);
            neutral(fx);
            RunFamily(dev, "A0 BasicEffect WITH a declaration (harness control)", fx, declared);
        }
        {   // A: BasicEffect. Stride 20 is its own canonical textured record -- the control.
            BasicEffect fx(dev);
            fx.setTextureEnabledProperty(true);
            fx.setTextureProperty(strip_.get());
            fx.setLightingEnabledProperty(false);
            fx.setFogEnabledProperty(false);
            neutral(fx);
            RunFamily(dev, "A BasicEffect (stride 20 is its own record)", fx, vb);
        }
        {   // B: AlphaTestEffect, likewise position+UV only.
            AlphaTestEffect fx(dev);
            fx.setTextureProperty(strip_.get());
            neutral(fx);
            RunFamily(dev, "B AlphaTestEffect", fx, vb);
        }
        {   // C: SkinnedEffect. Its shader wants a normal, weights and bone indices, none of which
            //    this record carries.
            SkinnedEffect fx(dev);
            fx.setTextureProperty(strip_.get());
            neutral(fx);
            RunFamily(dev, "C SkinnedEffect (no normal, no weights, no bone indices)", fx, vb);
        }
        {   // D: PbrEffect. Its shader wants a normal and a tangent.
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
            neutral(fx);
            RunFamily(dev, "D PbrEffect (no normal, no tangent)", fx, vb);
        }

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "E no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanUnsuppliedInputContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanUnsuppliedInputContractTest g;
    g.Run();
    return g.getResult();
}
