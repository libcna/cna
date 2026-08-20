// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md session 13: real depth + MSAA support for MRT (SetRenderTargets(count>1)).
//
// Before this fix, SetRenderTargets() never attached ANY depth/stencil buffer to its shared
// mrtFbo_, and always attached each target's single-sample colorTex directly even when the
// target was created with multiSampleCount>0 (silently discarding the requested antialiasing,
// no error, no fallback notice). This test proves both are now real:
//
// Check A/B -- depth-test occlusion across an MRT set: a nearer full-screen quad drawn first,
//   then a farther one covering the exact same pixels, written to BOTH bound attachments in the
//   SAME draw via a custom MRT shader -- the farther draw must be REJECTED by the depth test (both
//   attachments must still read back the nearer quad's colour), which could only happen if a real
//   depth buffer was actually attached and tested during MRT rendering (mirrors
//   OpenGL2_RenderTarget2D's own single-RT Check D, applied through MRT instead).
// Check C/D -- MSAA resolve across an MRT set: TWO MSAA-enabled targets (matching sample counts
//   -- real GL, like D3D/Vulkan, requires every simultaneously-bound attachment in one FBO to
//   share the same sample count; mixing an MSAA and a non-MSAA target in one MRT set is not a
//   valid GL framebuffer to begin with, GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE regardless of this
//   renderer's own resolve logic, confirmed empirically while developing this test) bound
//   together, a diagonal-edge triangle drawn once (same geometry reaches both attachments in the
//   same draw) -- BOTH targets' edge pixels must show a real resolve blend (not a flat colour)
//   after their own independent per-target blit (mirrors OpenGL2_RenderTarget2D's own single-RT
//   Check G methodology, applied through MRT).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRTSize = 32;

    // No World/View/Projection uniforms declared -- every vertex here is already supplied in
    // clip space directly (mirrors DrawFullscreenColorQuad's own established convention in
    // opengl2_rendertarget2d_test.cpp). Both attachments receive the SAME colour in one draw --
    // this test only needs to prove real depth/MSAA behaviour, not distinct per-attachment output
    // (already proven separately by OpenGL2_MRT).
    const char* kMrtVert =
        "attribute vec3 aPosition;"
        "void main(){gl_Position=vec4(aPosition,1.0);}";
    const char* kMrtFrag =
        "uniform vec4 uColor;"
        "void main(){gl_FragData[0]=uColor;gl_FragData[1]=uColor;}";

    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }
    std::string ColorStr(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }
    Color ReadRTPixel(RenderTarget2D& rt, int x, int y)
    {
        Color px(0, 0, 0, 0);
        const Rectangle rect(x, y, 1, 1);
        rt.GetData(0, &rect, &px, 0, 1);
        return px;
    }
}

class OpenGL2MrtDepthMsaaTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<RenderTarget2D> depthA_;
    std::unique_ptr<RenderTarget2D> depthB_;
    std::unique_ptr<RenderTarget2D> msaaColorA_;
    std::unique_ptr<RenderTarget2D> msaaColorB_;

    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    static void DrawMrtColor(GraphicsDevice& dev, ShaderEffect& fx, const Color& color,
                             const VertexPositionColor* verts, int primCount)
    {
        fx.SetUniformVec4("uColor", color.getRProperty() / 255.0f, color.getGProperty() / 255.0f,
                          color.getBProperty() / 255.0f, color.getAProperty() / 255.0f);
        fx.Apply();
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, verts, 0, primCount);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        depthA_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                    DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);
        depthB_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                    DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::DiscardContents);
        msaaColorA_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                        DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
        msaaColorB_ = std::make_unique<RenderTarget2D>(dev, kRTSize, kRTSize, false, SurfaceFormat::Color,
                                                        DepthFormat::None, 4, RenderTargetUsage::DiscardContents);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setBlendStateProperty(BlendState::Opaque);

        ShaderEffect fxMrt(dev, kMrtVert, kMrtFrag);
        fxMrt.setWorldProperty(Matrix::getIdentityProperty());
        fxMrt.setViewProperty(Matrix::getIdentityProperty());
        fxMrt.setProjectionProperty(Matrix::getIdentityProperty());

        // Check A/B: depth-test occlusion across an MRT set.
        dev.setDepthStencilStateProperty(DepthStencilState::Default);
        dev.SetRenderTargets({ RenderTargetBinding(depthA_.get()), RenderTargetBinding(depthB_.get()) });
        dev.Clear(Color::Black);

        const VertexPositionColor nearQuad[6] = {
            {Vector3(-1.0f, 1.0f, -0.5f), Color::White}, {Vector3(1.0f, -1.0f, -0.5f), Color::White}, {Vector3(-1.0f, -1.0f, -0.5f), Color::White},
            {Vector3(-1.0f, 1.0f, -0.5f), Color::White}, {Vector3(1.0f, 1.0f, -0.5f), Color::White},  {Vector3(1.0f, -1.0f, -0.5f), Color::White},
        };
        const VertexPositionColor farQuad[6] = {
            {Vector3(-1.0f, 1.0f, 0.5f), Color::White}, {Vector3(1.0f, -1.0f, 0.5f), Color::White}, {Vector3(-1.0f, -1.0f, 0.5f), Color::White},
            {Vector3(-1.0f, 1.0f, 0.5f), Color::White}, {Vector3(1.0f, 1.0f, 0.5f), Color::White},  {Vector3(1.0f, -1.0f, 0.5f), Color::White},
        };
        DrawMrtColor(dev, fxMrt, Color::Blue, nearQuad, 2); // nearer, first
        DrawMrtColor(dev, fxMrt, Color::Red, farQuad, 2);   // farther, second -- must be rejected

        dev.SetRenderTargets({});

        const Color gotA = ReadRTPixel(*depthA_, kRTSize / 2, kRTSize / 2);
        const Color gotB = ReadRTPixel(*depthB_, kRTSize / 2, kRTSize / 2);
        Check(Matches(gotA, Color::Blue, 10),
              "MRT attachment 0 keeps the nearer quad's colour despite a farther overwrite (real depth test): got=" + ColorStr(gotA));
        Check(Matches(gotB, Color::Blue, 10),
              "MRT attachment 1 keeps the nearer quad's colour despite a farther overwrite (real depth test): got=" + ColorStr(gotB));

        // Check C/D: MSAA resolve across an MRT set -- two 4x-MSAA targets bound together (matching
        // sample counts, the only valid mixed-MSAA-with-something-else combination GL itself
        // allows in one FBO -- see this test file's own header comment), same diagonal-edge
        // geometry reaching both attachments in the same MRT draw call.
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.SetRenderTargets({ RenderTargetBinding(msaaColorA_.get()), RenderTargetBinding(msaaColorB_.get()) });
        dev.Clear(Color::Blue);

        const VertexPositionColor diag[3] = {
            {Vector3(-1.0f, -1.0f, 0.0f), Color::White}, {Vector3(-1.0f, 1.0f, 0.0f), Color::White}, {Vector3(1.0f, 1.0f, 0.0f), Color::White},
        };
        DrawMrtColor(dev, fxMrt, Color::White, diag, 1);

        dev.SetRenderTargets({});

        // Probe several pixels straddling the diagonal (not just the exact center) -- exactly
        // which pixel has genuinely mixed sample coverage depends on the driver's own MSAA
        // sample-point pattern, matching OpenGL2_RenderTarget2D Check G's own reasoning.
        const int kProbeCoords[3] = { kRTSize / 2 - 1, kRTSize / 2, kRTSize / 2 + 1 };
        bool msaaAAnyBlended = false;
        Color msaaASample(0, 0, 0, 0);
        for (int c : kProbeCoords)
        {
            const Color px = ReadRTPixel(*msaaColorA_, c, c);
            if (!Matches(px, Color::White, 5) && !Matches(px, Color::Blue, 5)) { msaaAAnyBlended = true; msaaASample = px; }
        }
        bool msaaBAnyBlended = false;
        Color msaaBSample(0, 0, 0, 0);
        for (int c : kProbeCoords)
        {
            const Color px = ReadRTPixel(*msaaColorB_, c, c);
            if (!Matches(px, Color::White, 5) && !Matches(px, Color::Blue, 5)) { msaaBAnyBlended = true; msaaBSample = px; }
        }
        Check(msaaAAnyBlended, "MRT attachment 0 (4x MSAA): at least one diagonal-edge pixel is a real resolve blend: got=" + ColorStr(msaaASample));
        Check(msaaBAnyBlended, "MRT attachment 1 (4x MSAA): at least one diagonal-edge pixel is a real resolve blend: got=" + ColorStr(msaaBSample));

        result_ = (passCount_ == 4) ? 0 : 1;
        Exit();
    }

public:
    OpenGL2MrtDepthMsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2MrtDepthMsaaTest game;
    game.Run();
    return game.getResult();
}
