// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-149: the instanced route's PER-VERTEX layout comes from the caller's
// VertexDeclaration too.
//
// REMED-GFX-212 gave the route a position+colour vertex shader and chose between it and the
// position-only one from the byte stride alone -- 16 and 24 carry a COLOR0 at offset 12, every
// other stride does not. That is the guess REMED-GFX-234 removed from the BasicEffect bundle, and
// it is wrong in both directions:
//
//   * a Position+Colour record at a stride the table does not list loses its colour silently, and
//   * a padded position-only record at stride 16 gets a colour attribute aimed at its padding.
//
// The per-INSTANCE stream is untouched: its four matrix columns are a second binding at locations
// 4..7, are not declaration-derived, and `MultiStreamVertexInput` stays false.
//
// Every leg draws the same full-screen quad, one identity instance, VertexColorEnabled = true and
// DiffuseColor = white, so the pixel is the vertex colour when one is bound and white when none
// is. Each record carries a DECOY colour at offset 12 -- the offset the stride table would have
// used -- in a different colour from the real one, so a stride-driven implementation cannot
// accidentally agree with a declaration-driven one.
//
//   A  stride 20, Color0 declared at 16: renders the DECLARED colour. The stride table has a
//      layout for 20 (position+texture) and no colour in it at all, so this record was refused
//      before and now draws.
//   B  the same 20 bytes with NO declaration: still position-only, so white. The pair is the
//      point -- the declaration is the only thing that differs.
//   C  stride 16, Color0 declared at 12 (canonical): unchanged, renders the vertex colour.
//   D  stride 16, a declaration naming Position ONLY: white, not the decoy at 12. This is the
//      half that fails if the declaration reaches the attribute offsets but not the choice of
//      PROGRAM, which is the defect REMED-GFX-234 names.
//   E  C and D are the same stride and must not share a pipeline; redrawing A must not build one.
//   F  No validation messages.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::Vulkan::VulkanRenderer;

namespace
{
    constexpr int kSize = 64;

    /// The colour a correctly-bound COLOR0 produces, and the one a stride-table guess would.
    const Color kReal (230, 30, 30, 255);
    const Color kDecoy(30, 230, 30, 255);
    /// DiffuseColor with no vertex colour mixed in.
    const Color kPlain(255, 255, 255, 255);

    struct Corner { float x, y; };
    constexpr Corner kQuad[4] = {
        { -1.f,  1.f }, {  1.f,  1.f }, {  1.f, -1.f }, { -1.f, -1.f },
    };
    constexpr std::uint16_t kIndices[6] = { 0, 1, 2, 0, 2, 3 };

    void PutFloat3(std::uint8_t* at, float a, float b, float c)
    { const float v[3] = { a, b, c }; std::memcpy(at, v, sizeof(v)); }

    void PutColor(std::uint8_t* at, const Color& c)
    { at[0] = c.getRProperty(); at[1] = c.getGProperty();
      at[2] = c.getBProperty(); at[3] = c.getAProperty(); }
}

class VulkanDeclaredInstancedLayoutTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
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

    static bool Matches(const Color& got, const Color& want)
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

    /// Four quad vertices of `stride` bytes: position at 0, the decoy colour always at 12, and the
    /// real one at `realColorOffset` when that is not 12 as well.
    static std::vector<std::uint8_t> BuildRecords(int stride, int realColorOffset)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(4 * stride), 0);
        for (int i = 0; i < 4; ++i) {
            std::uint8_t* v = bytes.data() + i * stride;
            PutFloat3(v + 0, kQuad[i].x, kQuad[i].y, 0.0f);
            PutColor(v + 12, kDecoy);
            if (realColorOffset >= 0) PutColor(v + realColorOffset, kReal);
        }
        return bytes;
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

        // One identity per-instance world matrix, declared the way XNA's instancing samples do.
        const VertexDeclaration instanceDecl(
            64,
            {
                VertexElement(0,  VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 1),
                VertexElement(16, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 2),
                VertexElement(32, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 3),
                VertexElement(48, VertexElementFormat::Vector4,
                              VertexElementUsage::TextureCoordinate, 4),
            });
        const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        VertexBuffer instanceVb(dev, instanceDecl, 1, BufferUsage::None);
        instanceVb.SetDataRaw(identity.data(), 1, 64);

        IndexBuffer ib(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        ib.SetData(kIndices, 6);
        dev.SetIndexBuffer(&ib);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setLightingEnabledProperty(false);
        fx.setTextureEnabledProperty(false);
        fx.setFogEnabledProperty(false);
        fx.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        fx.setAlphaProperty(1.0f);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());

        /// One instanced draw of `vb`, returning the centre pixel, or the refusal text.
        const auto drawInstanced = [&](VertexBuffer& vb, std::string& how) {
            Color got(0, 0, 0, 0);
            how.clear();
            try {
                dev.Clear(Color(0, 0, 0, 255));
                fx.Apply();
                dev.SetVertexBuffers({VertexBufferBinding(&vb, 0, 0),
                                      VertexBufferBinding(&instanceVb, 0, 1)});
                dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 1);
                got = ReadCentre(dev);
            } catch (const std::exception& e) {
                how = e.what();
            }
            return got;
        };

        // ---- A: stride 20, the colour declared at 16 -------------------------------------------
        {
            const VertexDeclaration decl(
                20,
                {
                    VertexElement(0,  VertexElementFormat::Vector3,
                                  VertexElementUsage::Position, 0),
                    VertexElement(16, VertexElementFormat::Color, VertexElementUsage::Color, 0),
                });
            VertexBuffer vb(dev, decl, 4, BufferUsage::None);
            const auto bytes = BuildRecords(20, 16);
            vb.SetDataRaw(bytes.data(), 4, 20);
            std::string how;
            const Color got = drawInstanced(vb, how);
            check(how.empty() && Matches(got, kReal),
                  "A stride 20 renders the colour its declaration placed at 16",
                  how.empty() ? Show(got) + " (expected " + Show(kReal) + ", decoy at 12 is "
                                    + Show(kDecoy) + ")"
                              : "refused: " + how);

            // ---- B: the same bytes with no declaration -----------------------------------------
            VertexBuffer bare(dev, 4);
            bare.SetDataRaw(bytes.data(), 4, 20);
            std::string bareHow;
            const Color bareGot = drawInstanced(bare, bareHow);
            check(bareHow.empty() && Matches(bareGot, kPlain),
                  "B the same bytes with no declaration stay position-only",
                  bareHow.empty() ? Show(bareGot) + " (expected " + Show(kPlain) + ")"
                                  : "refused: " + bareHow);
        }

        // ---- C and D: one stride, two declarations ---------------------------------------------
        const std::size_t instancedBefore = Renderer().GetInstancedPipelineCacheSizeEXT();
        {
            const VertexDeclaration decl(
                16,
                {
                    VertexElement(0,  VertexElementFormat::Vector3,
                                  VertexElementUsage::Position, 0),
                    VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
                });
            VertexBuffer vb(dev, decl, 4, BufferUsage::None);
            const auto bytes = BuildRecords(16, 12);
            vb.SetDataRaw(bytes.data(), 4, 16);
            std::string how;
            const Color got = drawInstanced(vb, how);
            check(how.empty() && Matches(got, kReal),
                  "C the canonical stride-16 record still renders its vertex colour",
                  how.empty() ? Show(got) + " (expected " + Show(kReal) + ")" : "refused: " + how);
        }
        {
            const VertexDeclaration decl(
                16,
                {
                    VertexElement(0, VertexElementFormat::Vector3,
                                  VertexElementUsage::Position, 0),
                });
            VertexBuffer vb(dev, decl, 4, BufferUsage::None);
            const auto bytes = BuildRecords(16, -1);   // only the decoy, at 12
            vb.SetDataRaw(bytes.data(), 4, 16);
            std::string how;
            const Color got = drawInstanced(vb, how);
            check(how.empty() && Matches(got, kPlain),
                  "D a stride-16 declaration naming no colour does not bind its padding",
                  how.empty() ? Show(got) + " (expected " + Show(kPlain) + "; the stride table's "
                                    "offset 12 holds " + Show(kDecoy) + ")"
                              : "refused: " + how);
        }
        const std::size_t instancedAfter = Renderer().GetInstancedPipelineCacheSizeEXT();

        // ---- E: the cache separates them, and does not grow on a repeat ------------------------
        check(instancedAfter >= instancedBefore + 2,
              "E two stride-16 declarations left two Instanced3D pipeline entries",
              std::to_string(instancedBefore) + " -> " + std::to_string(instancedAfter));
        {
            const VertexDeclaration decl(
                20,
                {
                    VertexElement(0,  VertexElementFormat::Vector3,
                                  VertexElementUsage::Position, 0),
                    VertexElement(16, VertexElementFormat::Color, VertexElementUsage::Color, 0),
                });
            VertexBuffer vb(dev, decl, 4, BufferUsage::None);
            const auto bytes = BuildRecords(20, 16);
            vb.SetDataRaw(bytes.data(), 4, 20);
            const std::size_t before = Renderer().GetInstancedPipelineCacheSizeEXT();
            std::string how;
            (void)drawInstanced(vb, how);
            const std::size_t after = Renderer().GetInstancedPipelineCacheSizeEXT();
            check(how.empty() && after == before,
                  "E redrawing leg A's declaration builds no further pipeline",
                  how.empty() ? std::to_string(before) + " -> " + std::to_string(after)
                              : "refused: " + how);
        }

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "F no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanDeclaredInstancedLayoutTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanDeclaredInstancedLayoutTest g;
    g.Run();
    return g.getResult();
}
