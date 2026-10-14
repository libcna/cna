// SPDX-License-Identifier: MS-PL
// SkinnedEffect.VertexColorEnabled (a CNA extension; XNA's SkinnedEffect has no vertex-colour
// variant) folds the vertex colour into the lit diffuse exactly as XNA's own vertex-colour effects
// do: it scales the diffuse, not the specular, and the saturate comes after the fold.
//
// Moved from the retired VULKAN suite (CNB-67, VULKAN-205, VULKAN-200). The camera sits at (0,0,3)
// looking at a +Z quad through the origin and light 0 points along (0,0,-1), so at the centre
// pixel N = L = V and NdotL = 1. One identity bone with weight 1 keeps skinning out of the answer.
//
//   A  VertexColorEnabled off: diffuse (0.8,0.6,0.4) x light (0.5) = (0.4,0.3,0.2) -> (102,77,51);
//      the vertex colour (200,100,50) is ignored
//   B  on: (0.4,0.3,0.2) x (200,100,50)/255 -> (80,30,10), and B differs from A
//   F  specular only (no ambient, no light diffuse): a 20% grey vertex colour gives the same
//      highlight as a white one. XNA (measured on BasicEffect, its vertex-colour family, by
//      spikes/xna-vertex-color-specular-spike/) adds Specular * colour.a after the colour is folded
//      into the diffuse, so only the colour's ALPHA can scale the highlight
//   G  ambient 1.8 x diffuse 1 with a 50% vertex colour: Direct3D 9 saturates the colour already
//      folded in, clamp(1.8 x 0.502) = 0.904 -> 230, not clamp(1.8) x 0.502 -> 128
//
// Exit code 0 = PASS, 1 = FAIL, 77 = skipped (no display or no 3D rendering).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
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
#include <memory>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    struct SkinnedColorVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(SkinnedColorVertex) == 56);

    VertexDeclaration SkinnedColorDeclaration()
    {
        return VertexDeclaration(56, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 0),
            VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
            VertexElement(48, VertexElementFormat::Byte4, VertexElementUsage::BlendIndices, 0),
            VertexElement(52, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
    }

    bool Matches(const Color& got, const Color& want, int tolerance = 8)
    {
        return std::abs(got.getRProperty() - want.getRProperty()) <= tolerance
            && std::abs(got.getGProperty() - want.getGProperty()) <= tolerance
            && std::abs(got.getBProperty() - want.getBProperty()) <= tolerance;
    }

    struct Lighting
    {
        Vector3 ambient = Vector3::Zero;
        Vector3 diffuseColor = Vector3(0.8f, 0.6f, 0.4f);
        Vector3 lightDiffuse = Vector3(0.5f, 0.5f, 0.5f);
        Vector3 specularColor = Vector3::Zero;
        Vector3 lightSpecular = Vector3::Zero;
        float specularPower = 32.0f;
    };
}

class SkinnedEffectVertexColorTest final : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> white_;
    bool skipped_ = false;

    Color Render(bool vertexColorEnabled, std::uint8_t r, std::uint8_t g, std::uint8_t b,
                 const Lighting& lighting)
    {
        auto& dev = getGraphicsDeviceProperty();
        SkinnedEffect fx(dev);
        fx.setTextureProperty(white_.get());
        fx.setAmbientLightColorProperty(lighting.ambient);
        fx.setDiffuseColorProperty(lighting.diffuseColor);
        fx.setEmissiveColorProperty(Vector3::Zero);
        fx.setSpecularColorProperty(lighting.specularColor);
        fx.setSpecularPowerProperty(lighting.specularPower);
        fx.VertexColorEnabled = vertexColorEnabled;
        fx.DirectionalLight0.setEnabledProperty(true);
        fx.DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        fx.DirectionalLight0.setDiffuseColorProperty(lighting.lightDiffuse);
        fx.DirectionalLight0.setSpecularColorProperty(lighting.lightSpecular);
        fx.DirectionalLight1.setEnabledProperty(false);
        fx.DirectionalLight2.setEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::CreateLookAt(Vector3(0.0f, 0.0f, 3.0f), Vector3::Zero,
                                                Vector3(0.0f, 1.0f, 0.0f)));
        fx.setProjectionProperty(Matrix::CreatePerspectiveFieldOfView(
            MathHelper::PiOver4, 1.0f, 0.1f, 100.0f));
        fx.SetBoneTransforms(std::vector<Matrix>{ Matrix::getIdentityProperty() });
        fx.setWeightsPerVertexProperty(1);

        const SkinnedColorVertex verts[6] = {
            { -4,  4, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, r, g, b, 255 },
            { -4, -4, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, r, g, b, 255 },
            {  4, -4, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, r, g, b, 255 },
            { -4,  4, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, r, g, b, 255 },
            {  4, -4, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, r, g, b, 255 },
            {  4,  4, 0, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 0, r, g, b, 255 },
        };
        VertexBuffer vb(dev, SkinnedColorDeclaration(), 6, BufferUsage::None);
        vb.SetDataRaw(verts, 6, sizeof(SkinnedColorVertex));

        dev.Clear(Color(0, 0, 0, 255));
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.SetVertexBuffer(&vb);
        fx.Apply();
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        const Rectangle centre(kSize / 2, kSize / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&centre, &got, 0, 1);
        return got;
    }

    void Report(bool ok, const char* label, const Color& got, const char* want)
    {
        std::printf("[%s] %s: got (%d,%d,%d), want %s\n", ok ? "PASS" : "FAIL", label,
                    got.getRProperty(), got.getGProperty(), got.getBProperty(), want);
        if (!ok) MarkFailedEXT();
    }

public:
    SkinnedEffectVertexColorTest()
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
        white_ = std::make_unique<Texture2D>(dev, 1, 1);
        const Color white(255, 255, 255, 255);
        white_->SetData(&white, 1);

        const Lighting base;
        const Color a = Render(false, 200, 100, 50, base);
        Report(Matches(a, Color(102, 77, 51, 255)),
               "A VertexColorEnabled off: the vertex colour is ignored", a, "(102,77,51)");
        const Color b = Render(true, 200, 100, 50, base);
        Report(Matches(b, Color(80, 30, 10, 255)),
               "B VertexColorEnabled on: lit diffuse x vertex colour", b, "(80,30,10)");
        Report(!Matches(a, b), "B differs from A: the flag really gates the multiply", b,
               "!= A");

        Lighting specularOnly;
        specularOnly.diffuseColor = Vector3::One;
        specularOnly.lightDiffuse = Vector3::Zero;
        specularOnly.specularColor = Vector3::One;
        specularOnly.lightSpecular = Vector3::One;
        specularOnly.specularPower = 1.0f;
        const Color specWhite = Render(true, 255, 255, 255, specularOnly);
        const Color specDark = Render(true, 51, 51, 51, specularOnly);
        Report(specWhite.getRProperty() > 60,
               "F control: the specular-only scene has a highlight to measure", specWhite, "> 60");
        Report(Matches(specDark, specWhite),
               "F a 20% grey vertex colour does not dim the specular highlight", specDark,
               "the white-vertex-colour highlight");

        Lighting overbright;
        overbright.ambient = Vector3(1.8f, 1.8f, 1.8f);
        overbright.diffuseColor = Vector3::One;
        overbright.lightDiffuse = Vector3::Zero;
        const Color clamped = Render(true, 128, 128, 128, overbright);
        Report(Matches(clamped, Color(230, 230, 230, 255)),
               "G the vertex colour is folded in before the saturate: clamp(1.8 x 0.502)", clamped,
               "(230,230,230)");
        Report(clamped.getRProperty() > 180,
               "G and not clamp(1.8) x 0.502, which would read (128,128,128)", clamped, "> 180");
    }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("skinned_effect_vertex_color"))
        return CNA::Examples::Conformance::kSkipExitCode;
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    SkinnedEffectVertexColorTest game;
    game.Run();
    return game.ExitCode();
}
