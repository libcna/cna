// SPDX-License-Identifier: MS-PL
// Geometry in the +X/+Y quadrant of clip space appears TOP-RIGHT, for every effect family and on
// both destinations: XNA's clip space is +Y up, and both GetBackBufferData and RenderTarget2D.
// GetData return rows top first. Moved from the retired VULKAN suite (REMED-GFX-011:
// vulkan_orientation_calibration/effects/rendertarget_test), where four effect families had lost
// their Y flip; one program now covers what the three did.
//
// Each family draws one bright quad confined to the +X/+Y clip quadrant over black and the four
// quadrant centres are sampled. The scene is asymmetric on both axes, so a mirror cannot hide:
//   BR lit -> vertical mirror, TL lit -> horizontal mirror, BL lit -> both.
//
// Families: BasicEffect (the reference), EnvironmentMapEffect, PbrEffect and SkinnedPbrEffect (CNA
// extensions; skipped where the renderer offers none) and a BasicEffect DrawInstancedPrimitives
// whose per-instance world matrix moves a centred quad into the quadrant (skipped without
// GraphicsCapability::Instancing). Each family runs twice: onto the backbuffer, read with
// GetBackBufferData, and into a RenderTarget2D, read with RenderTarget2D.GetData -- the absolute
// expectation is the same on both, so a double flip on the render-target path fails.
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTexture.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTangentTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "System/NotSupportedException.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr float kLo = 0.2f;
    constexpr float kHi = 0.9f;

    /// The four corners of the +X/+Y quad, as two triangles: TL, BL, BR, TL, BR, TR.
    struct QuadCorner { float x, y, u, v; };
    constexpr QuadCorner kCorners[6] = {
        {kLo, kHi, 0.0f, 0.0f}, {kLo, kLo, 0.0f, 1.0f}, {kHi, kLo, 1.0f, 1.0f},
        {kLo, kHi, 0.0f, 0.0f}, {kHi, kLo, 1.0f, 1.0f}, {kHi, kHi, 1.0f, 0.0f},
    };

    int Luma(const Color& c)
    {
        return (c.getRProperty() * 30 + c.getGProperty() * 59 + c.getBProperty() * 11) / 100;
    }

    std::string Show(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

    struct InstanceMatrix { float m[16]; };
}

class EffectOrientationTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<Texture2D> white_;
    std::unique_ptr<TextureCube> blackCube_;
    std::unique_ptr<RenderTarget2D> target_;
    bool done_ = false;
    int result_ = 0;

    /// Samples the four quadrant centres of a row-major, top-first kSize x kSize image.
    void CheckQuadrants(const std::vector<Color>& pixels, const std::string& family)
    {
        const auto at = [&](int x, int y) { return pixels[static_cast<std::size_t>(y * kSize + x)]; };
        const Color tr = at(3 * kSize / 4, kSize / 4);
        const Color tl = at(kSize / 4, kSize / 4);
        const Color br = at(3 * kSize / 4, 3 * kSize / 4);
        const Color bl = at(kSize / 4, 3 * kSize / 4);
        constexpr int kLit = 40;
        constexpr int kBg = 15;
        const bool ok = Luma(tr) >= kLit && Luma(tl) <= kBg && Luma(br) <= kBg && Luma(bl) <= kBg;
        std::printf("[%s] %s: TR=%s TL=%s BR=%s BL=%s\n", ok ? "PASS" : "FAIL", family.c_str(),
                    Show(tr).c_str(), Show(tl).c_str(), Show(br).c_str(), Show(bl).c_str());
        std::fflush(stdout);
        if (ok) return;
        result_ = 1;
        if (Luma(br) >= kLit) std::printf("       -> lit BOTTOM-RIGHT: vertical mirror\n");
        if (Luma(tl) >= kLit) std::printf("       -> lit TOP-LEFT: horizontal mirror\n");
        if (Luma(bl) >= kLit) std::printf("       -> lit BOTTOM-LEFT: both axes mirrored\n");
        if (Luma(tr) < kLit && Luma(br) < kLit && Luma(tl) < kLit && Luma(bl) < kLit)
            std::printf("       -> nothing drawn: orientation undetermined, not proven\n");
    }

    void ResetScene(GraphicsDevice& dev)
    {
        dev.Clear(Color(0, 0, 0, 255));
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
    }

    /// Runs @p draw onto the backbuffer and into the render target, checking both.
    void RunFamily(GraphicsDevice& dev, const std::string& family,
                   const std::function<void(GraphicsDevice&)>& draw)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kSize * kSize), Color(0, 0, 0, 0));

        try
        {
            ResetScene(dev);
            draw(dev);
            dev.GetBackBufferData(pixels.data(), 0, static_cast<int>(pixels.size()));
            CheckQuadrants(pixels, family + " / backbuffer");

            dev.SetRenderTarget(target_.get());
            ResetScene(dev);
            draw(dev);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            target_->GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
            CheckQuadrants(pixels, family + " / RenderTarget2D");
        }
        catch (const std::exception& e)
        {
            // A renderer with no metallic-roughness model refuses the PBR families at the draw, and
            // one whose stock effects take no per-instance input refuses the instanced leg (both
            // FNA3D); that is this family's skip, not a failure. Anything else still aborts.
            if (!CNA::Examples::Conformance::IsNamedPbrRefusal(e) &&
                !CNA::Examples::Conformance::IsNamedStockInstancingRefusal(e))
                throw;
            dev.setIndicesProperty(nullptr);
            dev.SetVertexBuffer(nullptr);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::printf("[SKIP] %s is refused by this renderer: %s\n", family.c_str(), e.what());
        }
    }

    static void Identity(IEffectMatrices& fx)
    {
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
    }

    template <typename TPbr>
    static void LightPbr(TPbr& fx, Texture2D& texture)
    {
        fx.setTextureProperty(&texture);
        fx.setNormalMapProperty(nullptr);
        fx.setMetallicFactorProperty(0.0f);
        fx.setRoughnessFactorProperty(1.0f);
        fx.setAmbientLightColorProperty(Vector3(0.6f, 0.6f, 0.6f));
        fx.getDirectionalLight0Property().setEnabledProperty(true);
        fx.getDirectionalLight0Property().setDirectionProperty(Vector3(0.0f, 0.0f, -1.0f));
        fx.getDirectionalLight0Property().setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.getDirectionalLight1Property().setEnabledProperty(false);
        fx.getDirectionalLight2Property().setEnabledProperty(false);
        Identity(fx);
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        white_ = std::make_unique<Texture2D>(dev, 1, 1);
        const Color white(255, 255, 255, 255);
        white_->SetData(&white, 1);
        blackCube_ = std::make_unique<TextureCube>(dev, 1, false, SurfaceFormat::Color);
        for (int face = 0; face < 6; ++face)
        {
            const Color black(0, 0, 0, 255);
            blackCube_->SetData(static_cast<CubeMapFace>(face), &black, 1);
        }
        target_ = std::make_unique<RenderTarget2D>(dev, kSize, kSize, false, SurfaceFormat::Color,
                                                   DepthFormat::None, 0,
                                                   RenderTargetUsage::DiscardContents);

        // ---- BasicEffect, the reference --------------------------------------------------------
        RunFamily(dev, "BasicEffect", [](GraphicsDevice& d) {
            BasicEffect fx(d);
            fx.setVertexColorEnabledProperty(true);
            Identity(fx);
            fx.Apply();
            std::array<VertexPositionColor, 6> quad{};
            for (int i = 0; i < 6; ++i)
                quad[static_cast<std::size_t>(i)] = VertexPositionColor(
                    Vector3(kCorners[i].x, kCorners[i].y, 0.0f), Color(255, 255, 255, 255));
            d.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
        });

        // ---- EnvironmentMapEffect -------------------------------------------------------------
        RunFamily(dev, "EnvironmentMapEffect", [this](GraphicsDevice& d) {
            EnvironmentMapEffect fx(d);
            fx.setTextureProperty(white_.get());
            fx.setEnvironmentMapProperty(blackCube_.get());
            fx.setEnvironmentMapAmountProperty(0.0f);
            fx.setEnvironmentMapSpecularProperty(Vector3::Zero);
            fx.setEmissiveColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            Identity(fx);
            fx.Apply();
            std::array<VertexPositionNormalTexture, 6> quad{};
            for (int i = 0; i < 6; ++i)
                quad[static_cast<std::size_t>(i)] = VertexPositionNormalTexture(
                    Vector3(kCorners[i].x, kCorners[i].y, 0.0f), Vector3(0.0f, 0.0f, 1.0f),
                    Vector2(kCorners[i].u, kCorners[i].v));
            d.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
        });

        // ---- PbrEffect (CNA extension) --------------------------------------------------------
        {
            bool offered = true;
            try
            {
                PbrEffect probe(dev);
            }
            catch (const System::NotSupportedException& e)
            {
                offered = false;
                std::printf("[SKIP] PbrEffect is not offered by this renderer: %s\n", e.what());
            }
            if (offered)
            {
                RunFamily(dev, "PbrEffect", [this](GraphicsDevice& d) {
                    PbrEffect fx(d);
                    LightPbr(fx, *white_);
                    fx.Apply();
                    std::array<VertexPositionNormalTangentTexture, 6> quad{};
                    for (int i = 0; i < 6; ++i)
                        quad[static_cast<std::size_t>(i)] = VertexPositionNormalTangentTexture(
                            Vector3(kCorners[i].x, kCorners[i].y, 0.0f),
                            Vector3(0.0f, 0.0f, 1.0f), Vector4(1.0f, 0.0f, 0.0f, 1.0f),
                            Vector2(kCorners[i].u, kCorners[i].v));
                    VertexBuffer vb(d, VertexPositionNormalTangentTexture::getVertexDeclarationStatic(),
                                    6, BufferUsage::None);
                    vb.SetData(quad.data(), 6);
                    d.SetVertexBuffer(&vb);
                    d.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                    d.SetVertexBuffer(nullptr);
                });
            }
        }

        // ---- SkinnedPbrEffect (CNA extension), identity bind pose --------------------------------
        {
            bool offered = true;
            try
            {
                SkinnedPbrEffect probe(dev);
            }
            catch (const System::NotSupportedException& e)
            {
                offered = false;
                std::printf("[SKIP] SkinnedPbrEffect is not offered by this renderer: %s\n",
                            e.what());
            }
            if (offered)
            {
                RunFamily(dev, "SkinnedPbrEffect", [this](GraphicsDevice& d) {
                    SkinnedPbrEffect fx(d);
                    LightPbr(fx, *white_);
                    fx.SetBoneTransforms({Matrix::getIdentityProperty()});
                    fx.setWeightsPerVertexProperty(1);
                    fx.Apply();
                    std::array<VertexPositionNormalTangentTextureSkinned, 6> quad{};
                    for (int i = 0; i < 6; ++i)
                        quad[static_cast<std::size_t>(i)] =
                            VertexPositionNormalTangentTextureSkinned(
                                Vector3(kCorners[i].x, kCorners[i].y, 0.0f),
                                Vector3(0.0f, 0.0f, 1.0f), Vector4(1.0f, 0.0f, 0.0f, 1.0f),
                                Vector2(kCorners[i].u, kCorners[i].v),
                                Vector4(1.0f, 0.0f, 0.0f, 0.0f), {0, 0, 0, 0});
                    VertexBuffer vb(
                        d, VertexPositionNormalTangentTextureSkinned::getVertexDeclarationStatic(), 6,
                        BufferUsage::None);
                    vb.SetData(quad.data(), 6);
                    d.SetVertexBuffer(&vb);
                    d.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
                    d.SetVertexBuffer(nullptr);
                });
            }
        }

        // ---- DrawInstancedPrimitives ------------------------------------------------------------
        if (!dev.SupportsCapability(CNA::GraphicsCapability::Instancing))
        {
            std::printf("[SKIP] instancing: GraphicsCapability::Instancing is false\n");
        }
        else
        {
            // A quad centred on the origin; the per-instance world matrix moves it into +X/+Y,
            // which also proves the instance transform composes in the right orientation.
            const float h = 0.35f;
            const VertexPositionColor quadVerts[4] = {
                {Vector3(-h, h, 0.0f), Color(255, 255, 255, 255)},
                {Vector3(-h, -h, 0.0f), Color(255, 255, 255, 255)},
                {Vector3(h, -h, 0.0f), Color(255, 255, 255, 255)},
                {Vector3(h, h, 0.0f), Color(255, 255, 255, 255)},
            };
            VertexBuffer perVertex(dev, VertexPositionColor::getVertexDeclarationStatic(), 4,
                                   BufferUsage::None);
            perVertex.SetData(quadVerts, 4);
            const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
            ib.SetData(indices, 6);
            InstanceMatrix instance{};
            instance.m[0] = instance.m[5] = instance.m[10] = instance.m[15] = 1.0f;
            instance.m[12] = 0.55f;
            instance.m[13] = 0.55f;
            const VertexDeclaration instanceDecl(64, {
                VertexElement(0, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 0),
                VertexElement(16, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 1),
                VertexElement(32, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 2),
                VertexElement(48, VertexElementFormat::Vector4, VertexElementUsage::BlendWeight, 3),
            });
            VertexBuffer perInstance(dev, instanceDecl, 1, BufferUsage::None);
            perInstance.SetData(&instance, 1);

            RunFamily(dev, "BasicEffect DrawInstancedPrimitives", [&](GraphicsDevice& d) {
                BasicEffect fx(d);
                fx.setVertexColorEnabledProperty(true);
                Identity(fx);
                fx.Apply();
                d.SetVertexBuffers({VertexBufferBinding(&perVertex, 0, 0),
                                    VertexBufferBinding(&perInstance, 0, 1)});
                d.setIndicesProperty(&ib);
                d.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 1);
                d.SetVertexBuffer(nullptr);
                d.setIndicesProperty(nullptr);
            });
        }

        Exit();
    }

public:
    EffectOrientationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("effect_orientation"))
        return CNA::Examples::Conformance::kSkipExitCode;
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    EffectOrientationTest game;
    game.Run();
    return game.getResult();
}
