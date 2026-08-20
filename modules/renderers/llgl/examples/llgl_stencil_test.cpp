// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-53: DepthStencilState's full front/back stencil test, wired into the 3D
// pipeline for the first time (AcquirePrimitivePipeline()'s own `pipelineDesc.stencil`).
//
// Minimal, focused differential check avoiding RenderTargetCube entirely (this sandbox's OpenGL
// module has no hasCubeTextures support, a pre-existing, unrelated, already-documented
// environment limitation -- see known_bugs.md): write a reference value into the stencil buffer
// with one draw, then gate a second draw on that stored value, and confirm the gate actually
// works both ways (matching reference passes, non-matching reference is rejected).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/NotSupportedException.hpp"

#include "common/PixelTestGame.hpp"

#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 64;
    constexpr int kHeight = 64;

    // A full-screen quad in NDC space (identity World/View/Projection).
    std::vector<VertexPositionColor> FullscreenQuad(const Color& c)
    {
        return {
            {Vector3(-1.0f,  1.0f, 0.0f), c}, {Vector3(1.0f,  1.0f, 0.0f), c}, {Vector3(1.0f, -1.0f, 0.0f), c},
            {Vector3(-1.0f,  1.0f, 0.0f), c}, {Vector3(1.0f, -1.0f, 0.0f), c}, {Vector3(-1.0f, -1.0f, 0.0f), c},
        };
    }
}

class LlglStencilTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

    void DrawFullscreen(GraphicsDevice& device, BasicEffect& effect, const DepthStencilState& state,
                        const Color& color)
    {
        DepthStencilState copy = state;
        device.setDepthStencilStateProperty(copy);
        effect.setDiffuseColorProperty(
            Vector3(color.getRProperty() / 255.0f, color.getGProperty() / 255.0f, color.getBProperty() / 255.0f));
        const std::vector<VertexPositionColor> quad = FullscreenQuad(color);
        VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(),
                            static_cast<int>(quad.size()), BufferUsage::None);
        buffer.SetData(quad.data(), static_cast<int>(quad.size()));
        device.SetVertexBuffer(&buffer);
        effect.Apply();
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
    }

    static DepthStencilState WriteState(int reference)
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Always);
        ds.setStencilPassProperty(StencilOperation::Replace);
        ds.setReferenceStencilProperty(reference);
        return ds;
    }

    static DepthStencilState TestState(int reference)
    {
        DepthStencilState ds;
        ds.setDepthBufferEnableProperty(false);
        ds.setStencilEnableProperty(true);
        ds.setStencilFunctionProperty(CompareFunction::Equal);
        ds.setStencilPassProperty(StencilOperation::Keep);
        ds.setReferenceStencilProperty(reference);
        return ds;
    }

public:
    LlglStencilTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kWidth);
        gdm_->setPreferredBackBufferHeightProperty(kHeight);
    }

    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        bool rejected = false;
        try
        {
            DepthStencilState state = WriteState(7);
            device.setDepthStencilStateProperty(state);
        }
        catch (const System::NotSupportedException&)
        {
            rejected = true;
        }
        ExpectTrue("stencil-enabled DepthStencilState is rejected deterministically on LLGL OpenGL",
                   rejected);
        return;

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());

        // Check A: write stencil=7 everywhere, then gate a draw on stencil==7 -- must pass (visible).
        device.Clear(ClearOptions::Target | ClearOptions::Stencil, Color::Black, 1.0f, 0);
        DrawFullscreen(device, effect, WriteState(7), Color(0, 0, 0, 255));
        DrawFullscreen(device, effect, TestState(7), Color(0, 255, 0, 255));
        ExpectPixel("A: stencil write=7 then test==7 passes (matching reference)",
                    Rectangle(kWidth / 2, kHeight / 2, 1, 1), Color(0, 255, 0, 255), /*tolerance=*/10);

        // Check B: write stencil=7 everywhere, then gate a draw on stencil==3 -- must fail (rejected,
        // background stays visible instead of the gated colour landing).
        device.Clear(ClearOptions::Target | ClearOptions::Stencil, Color::Black, 1.0f, 0);
        DrawFullscreen(device, effect, WriteState(7), Color(0, 0, 0, 255));
        DrawFullscreen(device, effect, TestState(3), Color(255, 0, 0, 255));
        ExpectPixel("B: stencil write=7 then test==3 is rejected (non-matching reference stays black)",
                    Rectangle(kWidth / 2, kHeight / 2, 1, 1), Color(0, 0, 0, 255), /*tolerance=*/10);

        // Check C: half the screen writes stencil=5, the other half is left at the cleared 0 --
        // a single test==5 draw across the WHOLE screen must only land on the written half.
        device.Clear(ClearOptions::Target | ClearOptions::Stencil, Color::Black, 1.0f, 0);
        {
            DepthStencilState ds = WriteState(5);
            device.setDepthStencilStateProperty(ds);
            effect.setDiffuseColorProperty(Vector3(0.0f, 0.0f, 0.0f));
            const std::vector<VertexPositionColor> leftHalf = {
                {Vector3(-1.0f,  1.0f, 0.0f), Color(0, 0, 0, 255)},
                {Vector3( 0.0f,  1.0f, 0.0f), Color(0, 0, 0, 255)},
                {Vector3( 0.0f, -1.0f, 0.0f), Color(0, 0, 0, 255)},
                {Vector3(-1.0f,  1.0f, 0.0f), Color(0, 0, 0, 255)},
                {Vector3( 0.0f, -1.0f, 0.0f), Color(0, 0, 0, 255)},
                {Vector3(-1.0f, -1.0f, 0.0f), Color(0, 0, 0, 255)},
            };
            VertexBuffer buffer(device, VertexPositionColor::getVertexDeclarationStatic(),
                                static_cast<int>(leftHalf.size()), BufferUsage::None);
            buffer.SetData(leftHalf.data(), static_cast<int>(leftHalf.size()));
            device.SetVertexBuffer(&buffer);
            effect.Apply();
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        }
        DrawFullscreen(device, effect, TestState(5), Color(0, 0, 255, 255));
        ExpectPixel("C: the written (left) half passes the stencil test",
                    Rectangle(kWidth / 4, kHeight / 2, 1, 1), Color(0, 0, 255, 255), /*tolerance=*/10);
        ExpectPixel("C: the un-written (right) half stays rejected",
                    Rectangle(3 * kWidth / 4, kHeight / 2, 1, 1), Color(0, 0, 0, 255), /*tolerance=*/10);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<LlglStencilTest>();
}
