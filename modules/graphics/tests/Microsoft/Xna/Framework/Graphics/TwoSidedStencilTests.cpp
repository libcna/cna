// SPDX-License-Identifier: MS-PL
// plans/plan_graphics_shared_cleanup.md GSC-0002: DepthStencilState.TwoSidedStencilMode applies the
// CounterClockwise* stencil state to counter-clockwise triangles and the ordinary state to clockwise
// ones -- per field (function, pass, fail, depth-buffer fail), on the back buffer and on a render
// target, and on exactly the winding CullCounterClockwiseFace removes.
//
// XNA's DepthStencilState::Apply writes the CounterClockwise* fields to D3DRS_CCW_STENCIL*, which
// Direct3D 9 applies to counter-clockwise triangles. DirectX11 and DirectX12 had the two faces
// swapped (FrontCounterClockwise = TRUE with the CCW state on BackFace), which the corpus fixture
// DepthStencilState_StencilTwoSided saw only as one wrong column. Every case here draws both windings
// in one pass and reads both, so a swap, an ignored mode and a both-faces application each give a
// different, named result.

#include <gtest/gtest.h>

#include <array>
#include <functional>
#include <string>

#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using namespace CNA::Testing::Renderers;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;
    constexpr int kReference = 5;
    constexpr int kClockwiseX = 2;
    constexpr int kCounterClockwiseX = 6;
    constexpr int kProbeY = 4;

    // What a pixel's stencil value was found to be, by which probe colour survived.
    enum class Stencil
    {
        Zero,        // untouched
        Reference,   // Replace with kReference: the ordinary operation's signature
        Inverted,    // Invert of zero, 0xFF: the counter-clockwise operation's signature
        Other,
    };

    std::string Name(Stencil value)
    {
        switch (value)
        {
            case Stencil::Zero:      return "0 (untouched)";
            case Stencil::Reference: return "5 (ordinary operation)";
            case Stencil::Inverted:  return "255 (counter-clockwise operation)";
            default:                 return "unrecognised";
        }
    }

    GraphicsDevice MakeDevice()
    {
        PresentationParameters parameters;
        parameters.setBackBufferWidthProperty(kSize);
        parameters.setBackBufferHeightProperty(kSize);
        parameters.setDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        return GraphicsDevice(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              parameters);
    }

    // The left half is clockwise as displayed (XNA's ordinary face, kept by the default
    // CullCounterClockwiseFace), the right half counter-clockwise as displayed.
    void DrawBothWindings(GraphicsDevice& device, const Color& color)
    {
        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();
        const std::array vertices = {
            // clockwise, x in [-1, 0]
            VertexPositionColor(Vector3( 0.0f, -1.0f, 0.5f), color),
            VertexPositionColor(Vector3(-1.0f, -1.0f, 0.5f), color),
            VertexPositionColor(Vector3(-1.0f,  1.0f, 0.5f), color),
            VertexPositionColor(Vector3( 0.0f,  1.0f, 0.5f), color),
            VertexPositionColor(Vector3( 0.0f, -1.0f, 0.5f), color),
            VertexPositionColor(Vector3(-1.0f,  1.0f, 0.5f), color),
            // counter-clockwise, x in [0, 1]
            VertexPositionColor(Vector3( 0.0f,  1.0f, 0.5f), color),
            VertexPositionColor(Vector3( 0.0f, -1.0f, 0.5f), color),
            VertexPositionColor(Vector3( 1.0f, -1.0f, 0.5f), color),
            VertexPositionColor(Vector3( 0.0f,  1.0f, 0.5f), color),
            VertexPositionColor(Vector3( 1.0f, -1.0f, 0.5f), color),
            VertexPositionColor(Vector3( 1.0f,  1.0f, 0.5f), color),
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 4);
    }

    DepthStencilState TwoSidedState(const bool twoSided)
    {
        DepthStencilState state;
        state.setDepthBufferEnableProperty(false);
        state.setDepthBufferWriteEnableProperty(false);
        state.setStencilEnableProperty(true);
        state.setTwoSidedStencilModeProperty(twoSided);
        state.setReferenceStencilProperty(kReference);
        state.setStencilFunctionProperty(CompareFunction::Always);
        state.setStencilPassProperty(StencilOperation::Keep);
        state.setStencilFailProperty(StencilOperation::Keep);
        state.setStencilDepthBufferFailProperty(StencilOperation::Keep);
        state.setCounterClockwiseStencilFunctionProperty(CompareFunction::Always);
        state.setCounterClockwiseStencilPassProperty(StencilOperation::Keep);
        state.setCounterClockwiseStencilFailProperty(StencilOperation::Keep);
        state.setCounterClockwiseStencilDepthBufferFailProperty(StencilOperation::Keep);
        return state;
    }

    DepthStencilState EqualProbe(const int reference)
    {
        DepthStencilState state;
        state.setDepthBufferEnableProperty(false);
        state.setDepthBufferWriteEnableProperty(false);
        state.setStencilEnableProperty(true);
        state.setStencilFunctionProperty(CompareFunction::Equal);
        state.setReferenceStencilProperty(reference);
        state.setStencilPassProperty(StencilOperation::Keep);
        state.setStencilFailProperty(StencilOperation::Keep);
        return state;
    }

    struct Surface
    {
        std::function<Color(int x, int y)> read;
    };

    Stencil Classify(const Color& pixel)
    {
        if (pixel == Color::Blue)  return Stencil::Zero;
        if (pixel == Color::Red)   return Stencil::Reference;
        if (pixel == Color::Lime)  return Stencil::Inverted;
        return Stencil::Other;
    }

    struct WindingResult
    {
        Stencil clockwise = Stencil::Other;
        Stencil counterClockwise = Stencil::Other;
    };

    // Clears to stencil 0, runs the operation draw under `state` with `rasterizer`, then probes the
    // stencil value of each half with three Equal draws (0 blue, 5 red, 255 lime).
    WindingResult RunScenario(GraphicsDevice& device, const Surface& surface, const DepthStencilState& state,
                      const RasterizerState& rasterizer, const float clearDepth = 1.0f)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     Color::Black, clearDepth, 0);

        device.setRasterizerStateProperty(rasterizer);
        device.setDepthStencilStateProperty(state);
        DrawBothWindings(device, Color::Black);

        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(EqualProbe(0));
        DrawBothWindings(device, Color::Blue);
        device.setDepthStencilStateProperty(EqualProbe(kReference));
        DrawBothWindings(device, Color::Red);
        device.setDepthStencilStateProperty(EqualProbe(0xFF));
        DrawBothWindings(device, Color::Lime);

        return {Classify(surface.read(kClockwiseX, kProbeY)),
                Classify(surface.read(kCounterClockwiseX, kProbeY))};
    }

    void ExpectWindings(const WindingResult& actual, const Stencil clockwise,
                        const Stencil counterClockwise, const char* scenario)
    {
        EXPECT_EQ(actual.clockwise, clockwise)
            << scenario << ": clockwise triangle left stencil " << Name(actual.clockwise)
            << ", expected " << Name(clockwise);
        EXPECT_EQ(actual.counterClockwise, counterClockwise)
            << scenario << ": counter-clockwise triangle left stencil "
            << Name(actual.counterClockwise) << ", expected " << Name(counterClockwise);
    }

    Surface BackBuffer(GraphicsDevice& device)
    {
        return {[&device](const int x, const int y) {
            Color pixel = Color::Transparent;
            const Rectangle rectangle(x, y, 1, 1);
            device.GetBackBufferData(&rectangle, &pixel, 0, 1);
            return pixel;
        }};
    }

    // The field under test gets Replace (ordinary) and Invert (counter-clockwise); every other
    // operation stays Keep, so exactly one field decides each winding's value.
    enum class Field { Pass, Fail, DepthBufferFail };

    DepthStencilState PerFieldState(const Field field)
    {
        DepthStencilState state = TwoSidedState(true);
        switch (field)
        {
            case Field::Pass:
                state.setStencilPassProperty(StencilOperation::Replace);
                state.setCounterClockwiseStencilPassProperty(StencilOperation::Invert);
                break;
            case Field::Fail:
                state.setStencilFunctionProperty(CompareFunction::Never);
                state.setCounterClockwiseStencilFunctionProperty(CompareFunction::Never);
                state.setStencilFailProperty(StencilOperation::Replace);
                state.setCounterClockwiseStencilFailProperty(StencilOperation::Invert);
                break;
            case Field::DepthBufferFail:
                state.setDepthBufferEnableProperty(true);
                state.setDepthBufferFunctionProperty(CompareFunction::Never);
                state.setStencilDepthBufferFailProperty(StencilOperation::Replace);
                state.setCounterClockwiseStencilDepthBufferFailProperty(StencilOperation::Invert);
                break;
        }
        return state;
    }
}

TEST(TwoSidedStencilTest, EachCounterClockwiseOperationAppliesOnlyToCounterClockwiseTriangles)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGL4, OpenGLES3, DirectX11, DirectX12, Vulkan);
    GraphicsDevice device = MakeDevice();
    const Surface surface = BackBuffer(device);

    ExpectWindings(RunScenario(device, surface, PerFieldState(Field::Pass), RasterizerState::CullNone),
                   Stencil::Reference, Stencil::Inverted, "StencilPass");
    ExpectWindings(RunScenario(device, surface, PerFieldState(Field::Fail), RasterizerState::CullNone),
                   Stencil::Reference, Stencil::Inverted, "StencilFail");
    ExpectWindings(RunScenario(device, surface, PerFieldState(Field::DepthBufferFail), RasterizerState::CullNone),
                   Stencil::Reference, Stencil::Inverted, "StencilDepthBufferFail");
}

TEST(TwoSidedStencilTest, CounterClockwiseStencilFunctionSelectsByWinding)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGL4, OpenGLES3, DirectX11, DirectX12, Vulkan);
    GraphicsDevice device = MakeDevice();
    const Surface surface = BackBuffer(device);

    // Both windings pass through the same Replace, so only the function differs by face.
    DepthStencilState ordinaryPasses = TwoSidedState(true);
    ordinaryPasses.setStencilPassProperty(StencilOperation::Replace);
    ordinaryPasses.setCounterClockwiseStencilPassProperty(StencilOperation::Replace);
    ordinaryPasses.setCounterClockwiseStencilFunctionProperty(CompareFunction::Never);
    ExpectWindings(RunScenario(device, surface, ordinaryPasses, RasterizerState::CullNone),
                   Stencil::Reference, Stencil::Zero, "ordinary Always, counter-clockwise Never");

    DepthStencilState counterClockwisePasses = TwoSidedState(true);
    counterClockwisePasses.setStencilPassProperty(StencilOperation::Replace);
    counterClockwisePasses.setCounterClockwiseStencilPassProperty(StencilOperation::Replace);
    counterClockwisePasses.setStencilFunctionProperty(CompareFunction::Never);
    ExpectWindings(RunScenario(device, surface, counterClockwisePasses, RasterizerState::CullNone),
                   Stencil::Zero, Stencil::Reference, "ordinary Never, counter-clockwise Always");
}

TEST(TwoSidedStencilTest, DisabledTwoSidedModeAppliesOrdinaryStateToBothWindings)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGL4, OpenGLES3, DirectX11, DirectX12, Vulkan);
    GraphicsDevice device = MakeDevice();
    const Surface surface = BackBuffer(device);

    DepthStencilState state = PerFieldState(Field::Pass);
    state.setTwoSidedStencilModeProperty(false);
    // A counter-clockwise function that would reject everything, to prove it is ignored too.
    state.setCounterClockwiseStencilFunctionProperty(CompareFunction::Never);
    ExpectWindings(RunScenario(device, surface, state, RasterizerState::CullNone),
                   Stencil::Reference, Stencil::Reference, "TwoSidedStencilMode=false");
}

TEST(TwoSidedStencilTest, CounterClockwiseStateBelongsToTheWindingCullCounterClockwiseFaceRemoves)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGL4, OpenGLES3, DirectX11, DirectX12, Vulkan);
    GraphicsDevice device = MakeDevice();
    const Surface surface = BackBuffer(device);
    const DepthStencilState state = PerFieldState(Field::Pass);

    // Culling and two-sided stencil classify faces identically: removing the counter-clockwise
    // triangles removes exactly the ones that carried the counter-clockwise operation.
    ExpectWindings(RunScenario(device, surface, state, RasterizerState::CullCounterClockwise),
                   Stencil::Reference, Stencil::Zero, "CullCounterClockwiseFace");
    ExpectWindings(RunScenario(device, surface, state, RasterizerState::CullClockwise),
                   Stencil::Zero, Stencil::Inverted, "CullClockwiseFace");
}

TEST(TwoSidedStencilTest, RenderTargetUsesTheSameWindingAsTheBackBuffer)
{
    CNA_SKIP_IF_RENDERER_IS_NONE_OF(Software, OpenGL33, OpenGL4, OpenGLES3, DirectX11, DirectX12, Vulkan);
    GraphicsDevice device = MakeDevice();
    RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color,
                          DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::PreserveContents);
    device.SetRenderTarget(&target);
    const Surface surface{[&device, &target](const int x, const int y) {
        // GetData on a bound target is refused, so the probe unbinds and rebinds; PreserveContents
        // keeps the colour and the probes are the last draws before each read.
        device.SetRenderTarget(nullptr);
        Color pixel = Color::Transparent;
        const Rectangle rectangle(x, y, 1, 1);
        target.GetData(0, &rectangle, &pixel, 0, 1);
        device.SetRenderTarget(&target);
        return pixel;
    }};

    ExpectWindings(RunScenario(device, surface, PerFieldState(Field::Pass), RasterizerState::CullNone),
                   Stencil::Reference, Stencil::Inverted, "render target, StencilPass");
    device.SetRenderTarget(nullptr);
}
