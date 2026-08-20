// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-17: real backbuffer (window-level) MSAA for the OpenGL4 graphics renderer --
// GraphicsRendererCreateArgs::multiSampleCount was previously accepted but ignored entirely
// (GetMultiSampleCount() always reported 0, PreferMultiSampling had no effect).
//
// Methodology (matches this project's own established MSAA differential test family --
// easygl_rendertarget2d_msaa_test.cpp, vulkan_msaa_test.cpp, webgpu_msaa_test.cpp): a solid-fill
// readback alone cannot distinguish "MSAA genuinely happened" from "MSAA was silently ignored" --
// both produce an identical flat-colour result away from any edge. Render a diagonal-edged
// triangle instead and read back a full centre row: a hard, unblended binary red/black transition
// proves no AA happened; genuinely blended (intermediate grayscale-ish) pixel values at that same
// edge prove a real multisample resolve occurred.
//
// A Game's own GraphicsDevice member is unconditionally default-constructed with
// MultiSampleCount=0 before any derived Game subclass or GraphicsDeviceManager code can run
// (see GraphicsDevice::RecreateRendererForMultiSampleCount's own doc comment) -- so setting
// GraphicsDeviceManager.PreferMultiSampling in this test's constructor would never reach the
// FIRST renderer at all. And unlike Vulkan/WebGPU (which support ApplyMultiSampleCount()
// reconfiguring an EXISTING renderer at runtime via GraphicsDeviceManager.ApplyChanges()),
// OpenGL4's backbuffer MSAA -- like EasyGL's own equivalent -- is fixed at renderer construction
// time only (a manually-managed multisample FBO; see OpenGL4Renderer's own
// ApplyMultiSampleCount doc comment for why). This test therefore uses the same CNAEXT-only
// `GraphicsDevice::RecreateRendererForMultiSampleCount()` escape hatch Vulkan's own pre-Task-902
// MSAA tests used for the identical reason (vulkan_rendertarget2d_msaa_test.cpp,
// vulkan_basiceffect_textured_msaa_test.cpp): Check A's triangle draw+readback happens entirely
// inside Initialize() (so no GPU resource crosses the renderer-recreate boundary), then the
// renderer is recreated with MultiSampleCount=8, then Check B repeats the identical draw against
// the new, MSAA-enabled renderer.
//
// Check A -- MultiSampleCount off (the default): the diagonal edge is a hard binary transition.
// Check B -- after RecreateRendererForMultiSampleCount(8): GetMultiSampleCountProperty() reports a
//   real (non-zero, device-clamped) sample count, and the SAME diagonal-edge geometry now shows
//   genuinely blended intermediate pixels -- not the hard transition Check A saw.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include "common/PixelTestGame.hpp"

#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    bool IsBinary(const std::vector<Color>& row)
    {
        for (const auto& c : row)
        {
            const int v = c.getRProperty();
            if (v > 40 && v < 215) return false;
        }
        return true;
    }

    bool HasIntermediate(const std::vector<Color>& row)
    {
        for (const auto& c : row)
        {
            const int v = c.getRProperty();
            if (v > 40 && v < 215) return true;
        }
        return false;
    }

    // Renders a diagonal-edged white triangle to the backbuffer (at whatever MultiSampleCount is
    // currently engaged) and returns the full centre-row pixel colours. Self-contained: every GPU
    // resource it creates is a local that goes out of scope at the end of this call.
    std::vector<Color> RenderAndReadRow(GraphicsDevice& dev)
    {
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.Clear(Color::Black);

        const Color white(255, 255, 255, 255);
        const VertexPositionColor tri[3] = {
            {Vector3(-1.0f, 1.0f, 0.0f), white},
            {Vector3(1.0f, 1.0f, 0.0f), white},
            {Vector3(-1.0f, -1.0f, 0.0f), white},
        };
        VertexBuffer vb(dev, VertexPositionColor::getVertexDeclarationStatic(), 3, BufferUsage::None);
        vb.SetData(tri, 0, 3);

        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.Apply();

        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);

        std::vector<Color> row(kSize, Color(0, 0, 0, 0));
        const Rectangle reg(0, kSize / 2, kSize, 1);
        dev.GetBackBufferData(&reg, row.data(), 0, kSize);
        return row;
    }
}

class OpenGL4MsaaTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::vector<Color> noMsaaRow_;

protected:
    void Initialize() override
    {
        Game::Initialize();
        // Check A's draw+readback happens here, entirely before RecreateRendererForMultiSampleCount
        // destroys and rebuilds the renderer -- see this file's header comment for why.
        noMsaaRow_ = RenderAndReadRow(getGraphicsDeviceProperty());
        getGraphicsDeviceProperty().RecreateRendererForMultiSampleCount(8);
    }

    void RunTest() override
    {
        Check(IsBinary(noMsaaRow_),
              "Check A: MultiSampleCount off -- diagonal edge is a hard binary transition");

        auto& dev = getGraphicsDeviceProperty();
        const int applied = dev.getPresentationParametersProperty().getMultiSampleCountProperty();
        Check(applied > 0,
              "Check B: RecreateRendererForMultiSampleCount(8) -- GetMultiSampleCount() reports a real, non-zero, device-clamped value");

        const std::vector<Color> msaaRow = RenderAndReadRow(dev);
        Check(HasIntermediate(msaaRow),
              "Check B: RecreateRendererForMultiSampleCount(8) -- diagonal edge now has genuinely blended intermediate pixels");
    }

public:
    OpenGL4MsaaTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4MsaaTest>();
}
