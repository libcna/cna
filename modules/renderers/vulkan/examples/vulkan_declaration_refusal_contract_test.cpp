// SPDX-License-Identifier: MS-PL
//
// plan_vulkan.md VULKAN-202 (finding F-38) -- the declaration guard's refusal contract, asserted on
// this renderer for the first time.
//
// `REMED-GFX-DECL-GUARD` refuses a draw whose `VertexDeclaration` this renderer can infer no
// faithful native layout for, rather than reading the caller's bytes as something else. Three
// consecutive rows of plan_vulkan.md quote that refusal -- `VULKAN-198` measured it, `VULKAN-199`,
// `VULKAN-200` and `VULKAN-201` each moved what it accepts -- and none of them could point at a
// test of the refusal's own contract on this renderer, because the shared
// `DeclarationGuardTest` skips its four refusal legs here:
//
//     if (TranslatesDeclarations()) GTEST_SKIP() << "translates colliding declarations
//                                                   instead of refusing them";
//
// `TranslatesDeclarations()` lists Vulkan, and it is half right. This renderer translates every
// declaration one of its stock input tables can satisfy -- that is what `VULKAN-144` built and what
// the shared suite's own three colliding cases exercise -- and refuses the rest **by name**. A
// per-renderer boolean cannot say that, so the refusal contract went unasserted while the boundary
// it describes was being changed three times. Fixing the shared predicate is a cross-renderer
// change and belongs to `VULKAN-203`; this asserts the contract where it can be verified.
//
// The declaration used is Position + Normal + Tangent. Every stock family here either has no
// `Normal` in its table or wants a texture coordinate with it, and the PBR tables that do name a
// Tangent are reachable only for a PBR draw -- so no table can satisfy it and the guard fires.
//
//   A  a refused draw rasterizes NOTHING: the target still holds the clear colour, every pixel.
//   B  the refusal says why, and names the element it could not bind.
//   C  the refusal does not poison the device or the buffer allocator: the very next draw, through
//      a freshly allocated buffer that may well reuse the refused one's address, renders its own
//      declaration exactly.
//   D  the INDEXED route reaches the same boundary, with the same shape of message.
//   E  no validation message -- a draw refused above the API must not have touched it.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace {
constexpr int kN = 16;
const Color kClear(13, 17, 19, 255);
const Color kGreen(0, 255, 0, 255);

/// Position + Normal + Tangent, 36 bytes. No stock table here can satisfy it: the ones naming a
/// Normal also want a texture coordinate or a colour, and the PBR tables that name a Tangent are
/// reachable only for a PBR draw.
struct PositionNormalTangent
{
    Vector3 position;
    Vector3 normal;
    Vector3 tangent;
};
static_assert(sizeof(PositionNormalTangent) == 36, "the refused layout is the 36-byte one");
} // namespace

class VulkanDeclarationRefusalContractTest final : public Game
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

    VulkanRenderer& Renderer()
    {
        return *dynamic_cast<VulkanRenderer*>(&getGraphicsDeviceProperty().GetRenderer());
    }

    static VertexDeclaration RefusedDeclaration()
    {
        return VertexDeclaration{
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Vector3, VertexElementUsage::Normal, 0),
            VertexElement(24, VertexElementFormat::Vector3, VertexElementUsage::Tangent, 0),
        };
    }

    static void FullScreen(PositionNormalTangent (&out)[6])
    {
        const Vector3 n(0.0f, 0.0f, 1.0f);
        const Vector3 t(1.0f, 0.0f, 0.0f);
        out[0] = { Vector3(-1.0f,  1.0f, 0.0f), n, t };
        out[1] = { Vector3(-1.0f, -1.0f, 0.0f), n, t };
        out[2] = { Vector3( 1.0f, -1.0f, 0.0f), n, t };
        out[3] = { Vector3(-1.0f,  1.0f, 0.0f), n, t };
        out[4] = { Vector3( 1.0f, -1.0f, 0.0f), n, t };
        out[5] = { Vector3( 1.0f,  1.0f, 0.0f), n, t };
    }

    void PrepareEffect(BasicEffect& fx)
    {
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.setVertexColorEnabledProperty(false);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
    }

    std::vector<Color> ReadAll(RenderTarget2D& rt)
    {
        std::vector<Color> px(static_cast<std::size_t>(kN * kN), Color(0, 0, 0, 0));
        rt.GetData(px.data(), 0, kN * kN);
        return px;
    }

    static bool AllAre(const std::vector<Color>& px, const Color& want)
    {
        for (const Color& p : px)
            if (p.getRProperty() != want.getRProperty() ||
                p.getGProperty() != want.getGProperty() ||
                p.getBProperty() != want.getBProperty())
                return false;
        return true;
    }

    static std::string Text(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) +
               "," + std::to_string(c.getBProperty()) + ")";
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();
        const std::size_t messagesBefore = Renderer().GetValidationMessagesEXT().size();
        dev.SetDepthTestEnabled(false);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);

        PositionNormalTangent quad[6];
        FullScreen(quad);

        // A + B -- the non-indexed route refuses, says why, and leaves the target untouched.
        std::string rejection;
        std::vector<Color> after;
        {
            RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            dev.Clear(kClear);
            VertexBuffer vb(dev, RefusedDeclaration(), 6, BufferUsage::WriteOnly);
            vb.SetDataRaw(quad, 6, static_cast<int>(sizeof(PositionNormalTangent)));
            BasicEffect fx(dev);
            PrepareEffect(fx);
            try {
                fx.Apply();
                dev.SetVertexBuffer(&vb);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            } catch (const std::exception& e) {
                rejection = e.what();
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            after = ReadAll(rt);
        }
        check(!rejection.empty(),
              "B the refusal says why: \"" + rejection.substr(0, 120) + "\"");
        check(!rejection.empty() && rejection.find("Normal") != std::string::npos,
              "B' and it names the element it could not bind");
        check(AllAre(after, kClear),
              "A a refused draw rasterized NOTHING: every pixel is still the clear colour, "
              "sample=" + Text(after[static_cast<std::size_t>(kN * kN / 2)]) + " (want " +
                  Text(kClear) + ")");

        // C -- the refusal did not poison the device or the buffer allocator.
        {
            RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            dev.Clear(kClear);
            const VertexPositionColor ok[6] = {
                { Vector3(-1.0f,  1.0f, 0.0f), kGreen },
                { Vector3(-1.0f, -1.0f, 0.0f), kGreen },
                { Vector3( 1.0f, -1.0f, 0.0f), kGreen },
                { Vector3(-1.0f,  1.0f, 0.0f), kGreen },
                { Vector3( 1.0f, -1.0f, 0.0f), kGreen },
                { Vector3( 1.0f,  1.0f, 0.0f), kGreen },
            };
            BasicEffect fx(dev);
            PrepareEffect(fx);
            fx.setVertexColorEnabledProperty(true);
            fx.Apply();
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, ok, 0, 2);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const std::vector<Color> good = ReadAll(rt);
            check(AllAre(good, kGreen),
                  "C a valid draw immediately after the refusal still renders, sample=" +
                      Text(good[static_cast<std::size_t>(kN * kN / 2)]) + " (want " +
                      Text(kGreen) + ")");
        }

        // D -- the indexed route reaches the same boundary.
        std::string indexedRejection;
        {
            RenderTarget2D rt(dev, kN, kN, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&rt);
            dev.Clear(kClear);
            VertexBuffer vb(dev, RefusedDeclaration(), 6, BufferUsage::WriteOnly);
            vb.SetDataRaw(quad, 6, static_cast<int>(sizeof(PositionNormalTangent)));
            const std::uint16_t idx[6] = { 0, 1, 2, 3, 4, 5 };
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::WriteOnly);
            ib.SetData(idx, 0, 6);
            BasicEffect fx(dev);
            PrepareEffect(fx);
            try {
                fx.Apply();
                dev.SetVertexBuffer(&vb);
                dev.setIndicesProperty(&ib);
                dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 0, 2);
            } catch (const std::exception& e) {
                indexedRejection = e.what();
            }
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            check(!indexedRejection.empty() &&
                      indexedRejection.find("Normal") != std::string::npos,
                  "D the indexed route reaches the same boundary: \"" +
                      indexedRejection.substr(0, 100) + "\"");
        }

        {
            const std::size_t afterMsgs = Renderer().GetValidationMessagesEXT().size();
            check(!VulkanRenderer::IsValidationActiveEXT() || afterMsgs == messagesBefore,
                  "E no validation message: " + std::to_string(messagesBefore) + " -> " +
                      std::to_string(afterMsgs));
        }

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanDeclarationRefusalContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int getResult() const { return fail_ == 0 ? 0 : 1; }
};

int main()
{
    VulkanDeclarationRefusalContractTest game;
    game.Run();
    return game.getResult();
}
