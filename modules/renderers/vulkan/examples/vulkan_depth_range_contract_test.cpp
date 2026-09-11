// SPDX-License-Identifier: MS-PL
// plans/plan_vulkan.md VULKAN-098 (finding F-19): this renderer's clip-space depth range is XNA's,
// and the reference renderer's is not.
//
// XNA is a Direct3D 9 programming model, and D3D9 maps clip space to depth over **[0, 1]** -- z = 0
// is the near plane. OpenGL maps it over [-1, 1], so a vertex at z = 0 lands at depth **0.5**, and
// EasyGL leaves that in place. Vulkan's native range is [0, 1], so this renderer is the one that
// agrees with XNA. F-19 recorded that as `VULKAN_STRONGER`.
//
// It was inferred from a depth-BIAS experiment, which is the wrong shape of evidence for a claim
// about a whole range: a bias test can only ever pin one point. This file measures the mapping
// positively, across it.
//
//   A  The truth table. Five z values against four cleared depths, under an identity projection and
//      DepthStencilState::Default (LessEqual, depth write on). A quad is visible exactly when
//      `z <= clearedDepth`, which is what "[0, 1], z is the depth" MEANS. Twenty checks.
//
//      This is what discriminates the two ranges, and the endpoints are where it bites: at
//      clearedDepth 0.4, z = 0 must be VISIBLE. Under OpenGL's range z = 0 is depth 0.5 and the
//      quad would be rejected. Ordering alone would not have caught it -- both ranges are
//      monotonic, so a test that only asked "does 0.25 occlude 0.75" passes on either.
//
//   B  z = -0.5 is CLIPPED, never visible at any cleared depth. Under OpenGL's range it is a
//      perfectly ordinary depth of 0.25 and would draw. This is the near plane's position stated as
//      a fact about geometry rather than about comparisons.
//
//   C  z = 1.5 is clipped too -- the far plane is where XNA puts it.
//
//   D  No validation messages.
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
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include "CNA/Internal/Renderers/Vulkan/VulkanRenderer.hpp"

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
    /// The quad's colour, and the cleared colour it has to replace to count as visible.
    const Color kDrawn(230, 30, 30, 255);
    const Color kCleared(0, 0, 0, 255);

    struct Corner { float x, y; };
    constexpr Corner kQuad[6] = {
        { -1.f,  1.f }, { -1.f, -1.f }, {  1.f, -1.f },
        { -1.f,  1.f }, {  1.f, -1.f }, {  1.f,  1.f },
    };
}

class VulkanDepthRangeContractTest : public Game
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

    static std::string Show(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }

    /// Clears colour AND depth to `clearedDepth`, draws a full-screen quad at `z`, and reports
    /// whether the quad reached the centre pixel.
    bool DrawnAt(GraphicsDevice& dev, BasicEffect& fx, float z, float clearedDepth)
    {
        std::uint8_t bytes[6 * 16]{};
        for (int i = 0; i < 6; ++i) {
            std::uint8_t* v = bytes + i * 16;
            const float pos[3] = { kQuad[i].x, kQuad[i].y, z };
            std::memcpy(v + 0, pos, sizeof(pos));
            v[12] = kDrawn.getRProperty(); v[13] = kDrawn.getGProperty();
            v[14] = kDrawn.getBProperty(); v[15] = kDrawn.getAProperty();
        }
        VertexDeclaration decl(16, {
            VertexElement(0,  VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color,   VertexElementUsage::Color,    0)});
        VertexBuffer vb(dev, decl, 6, BufferUsage::None);
        vb.SetDataRaw(bytes, 6, 16);

        dev.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, kCleared, clearedDepth, 0);
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        dev.SetVertexBuffer(nullptr);

        Color got(0, 0, 0, 0);
        const Rectangle at(kSize / 2, kSize / 2, 1, 1);
        dev.GetBackBufferData(&at, &got, 0, 1);
        const int tol = 24;
        return std::abs(int(got.getRProperty()) - int(kDrawn.getRProperty())) <= tol
            && std::abs(int(got.getGProperty()) - int(kDrawn.getGProperty())) <= tol
            && std::abs(int(got.getBProperty()) - int(kDrawn.getBProperty())) <= tol;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::Default);   // LessEqual, depth write on

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

        // ---- A: the truth table -----------------------------------------------------------------
        constexpr float kZ[5]       = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
        constexpr float kCleared_[4] = { 0.1f, 0.4f, 0.6f, 0.9f };
        int agreed = 0;
        int total = 0;
        std::string firstMismatch;
        for (float z : kZ) {
            for (float cd : kCleared_) {
                // XNA/D3D9: depth IS z over [0,1], and DepthStencilState::Default compares
                // LessEqual, so the quad survives exactly when z <= clearedDepth.
                const bool expected = z <= cd + 1e-6f;
                const bool got = DrawnAt(dev, fx, z, cd);
                ++total;
                if (got == expected) { ++agreed; }
                else if (firstMismatch.empty()) {
                    char buf[160];
                    std::snprintf(buf, sizeof(buf),
                                  "first at z=%.2f clearedDepth=%.2f: %s, XNA's [0,1] says %s",
                                  z, cd, got ? "drawn" : "rejected",
                                  expected ? "drawn" : "rejected");
                    firstMismatch = buf;
                }
            }
        }
        check(agreed == total,
              "A depth is z over [0,1], asserted against LessEqual at every cell of a 5x4 table",
              std::to_string(agreed) + "/" + std::to_string(total) + " cells agree"
                  + (firstMismatch.empty() ? "" : "; " + firstMismatch));

        // The single cell that separates the two ranges, called out so a failure names itself.
        check(DrawnAt(dev, fx, 0.0f, 0.4f),
              "A z=0 is the NEAR plane: it survives a depth cleared to 0.4",
              "under OpenGL's [-1,1] range z=0 would be depth 0.5 and be rejected here");
        check(!DrawnAt(dev, fx, 0.75f, 0.6f),
              "A z=0.75 is rejected by a depth cleared to 0.6",
              "the far half of the range maps where XNA says, not compressed into it");

        // ---- B and C: the planes themselves ------------------------------------------------------
        bool anyNegative = false;
        for (float cd : kCleared_) { if (DrawnAt(dev, fx, -0.5f, cd)) anyNegative = true; }
        check(!anyNegative,
              "B z=-0.5 is clipped at every cleared depth, so the near plane is at 0 and not at -1",
              "under OpenGL's range it would be an ordinary depth of 0.25 and would draw");

        bool anyBeyondFar = false;
        for (float cd : kCleared_) { if (DrawnAt(dev, fx, 1.5f, cd)) anyBeyondFar = true; }
        check(!anyBeyondFar, "C z=1.5 is clipped, so the far plane is at 1", "");

        const auto& messages = Renderer().GetValidationMessagesEXT();
        check(messages.empty(), "D no validation messages",
              messages.empty() ? "0 captured"
                               : std::to_string(messages.size()) + " captured, first: "
                                     + messages.front());

        std::printf("=== %d/%d PASS ===\n", pass_, pass_ + fail_);
        std::fflush(stdout);
        Exit();
    }

public:
    VulkanDepthRangeContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return fail_ > 0 ? 1 : 0; }
};

int main()
{
    VulkanDepthRangeContractTest g;
    g.Run();
    return g.getResult();
}
