// SPDX-License-Identifier: MS-PL
//
// TQ-012: XNA pipeline-state contracts the WebGPU renderer bakes into immutable pipelines, measured
// in pixels on a real device. Each case would fail if a state stopped reaching the pipeline, or if
// a pipeline cache key stopped telling two draws apart.
//
// Needs a WebGPU device, so a real window: run it only through the private runner,
//   tools/platform/run_gpu_tests_private.sh cmake-build-webgpu -R '^WebGPUPipelineStateContract'
// Expected colours are pure channels (0 or 255) chosen by hand, so no colour-space or rounding
// tolerance is involved; each is derived next to its check.

#if defined(CNA_RENDERER_WEBGPU) || defined(CNA_RENDERER_PRESENT_WEBGPU)

#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererTestGate.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

namespace
{
    constexpr int kSize = 8;

    GraphicsDevice MakeDevice()
    {
        PresentationParameters parameters = PresentationParameters::CreateWindowedEXT(800, 480);
        parameters.setBackBufferWidthProperty(kSize);
        parameters.setBackBufferHeightProperty(kSize);
        parameters.setDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
        return GraphicsDevice(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              parameters);
    }

    WebGPURenderer& RendererOf(GraphicsDevice& device)
    {
        auto* renderer = dynamic_cast<WebGPURenderer*>(&device.GetRenderer());
        if (renderer == nullptr)
            throw std::runtime_error("the active renderer is not WebGPURenderer");
        return *renderer;
    }

    /// Reads one texel of an unbound target.
    Color ReadTarget(RenderTarget2D& target, int x, int y)
    {
        Color pixel = Color::Transparent;
        const Rectangle rectangle(x, y, 1, 1);
        target.GetData(0, &rectangle, &pixel, 0, 1);
        return pixel;
    }

    std::string Describe(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + "," + std::to_string(c.getAProperty()) + ")";
    }

    /// A full-viewport quad at clip-space depth @p z, both triangles clockwise as displayed
    /// (XNA's front face).
    std::array<VertexPositionColor, 6> FullQuad(float z, const Color& color)
    {
        return {
            VertexPositionColor(Vector3(-1.0f,  1.0f, z), color),
            VertexPositionColor(Vector3( 1.0f,  1.0f, z), color),
            VertexPositionColor(Vector3(-1.0f, -1.0f, z), color),
            VertexPositionColor(Vector3( 1.0f,  1.0f, z), color),
            VertexPositionColor(Vector3( 1.0f, -1.0f, z), color),
            VertexPositionColor(Vector3(-1.0f, -1.0f, z), color),
        };
    }

    void DrawStockQuad(GraphicsDevice& device, float z, const Color& color)
    {
        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();
        const auto quad = FullQuad(z, color);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
    }

    // The legacy WGSL ShaderEffect contract (docs/webgpu-renderer.md, "Source ShaderEffect"):
    // vs_main/fs_main, one uniform block at binding 0 with World/View/Projection injected by name,
    // vertex locations in declaration order (VertexPositionColor: position 0, colour 1).
    const char* const kColorVertexWgsl = R"WGSL(
struct Uniforms {
    World: mat4x4f,
    View: mat4x4f,
    Projection: mat4x4f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
struct VOut {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
};
@vertex fn vs_main(@location(0) position: vec3f, @location(1) color: vec4f) -> VOut {
    var o: VOut;
    o.position = u.Projection * u.View * u.World * vec4f(position, 1.0);
    o.color = color;
    return o;
}
)WGSL";

    const char* const kColorFragmentWgsl = R"WGSL(
@fragment fn fs_main(@location(0) color: vec4f) -> @location(0) vec4f {
    return color;
}
)WGSL";

    const char* const kMatrixNames[] = {"World", "View", "Projection"};
    const int kMatrixOffsets[] = {0, 64, 128};

    void PrepareColorEffect(ShaderEffect& effect)
    {
        ASSERT_TRUE(effect.IsEffectValid()) << effect.GetCompileErrorEXT();
        effect.DeclareUniformBlockEXT(192, kMatrixNames, kMatrixOffsets, 3);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
    }

    void DrawEffectQuad(GraphicsDevice& device, ShaderEffect& effect, float z, const Color& color)
    {
        effect.Apply();
        const auto quad = FullQuad(z, color);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
    }

    // The legacy SpriteBatch WGSL contract: the fixed position/uv/colour layout, already in clip
    // space; the fragment returns the vertex colour so no texture or uniform decides the pixel.
    const char* const kSpriteVertexWgsl = R"WGSL(
struct VOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
};
@vertex fn vs_main(@location(0) position: vec3f, @location(1) uv: vec2f,
                   @location(2) color: vec4f) -> VOut {
    var o: VOut;
    o.position = vec4f(position, 1.0);
    o.uv = uv;
    o.color = color;
    return o;
}
)WGSL";

    const char* const kSpriteFragmentWgsl = R"WGSL(
@fragment fn fs_main(@location(0) uv: vec2f, @location(1) color: vec4f) -> @location(0) vec4f {
    return color;
}
)WGSL";

    DepthStencilState DepthState(bool enable, bool write, CompareFunction function)
    {
        DepthStencilState state;
        state.setDepthBufferEnableProperty(enable);
        state.setDepthBufferWriteEnableProperty(write);
        state.setDepthBufferFunctionProperty(function);
        return state;
    }
}

// ---------------------------------------------------------------------------------------------
// TwoSidedStencilMode: which winding receives which stencil state.
//
// XNA's DepthStencilState::Apply writes the CounterClockwise* fields to D3DRS_CCW_STENCIL*, which
// Direct3D 9 applies to counter-clockwise triangles, and the ordinary fields to clockwise ones; the
// shared TwoSidedStencilTest pins the same contract on Software and EasyGL. This is that fixture on
// WebGPU: both windings are drawn in one pass and each half's resulting stencil value is read back
// through three Equal probes (0 -> blue, 5 -> red, 255 -> lime).
// ---------------------------------------------------------------------------------------------

namespace
{
    constexpr int kReference = 5;
    constexpr int kClockwiseX = 2;
    constexpr int kCounterClockwiseX = 6;
    constexpr int kProbeY = 4;

    enum class Stencil { Zero, Reference, Inverted, Other };

    const char* Name(Stencil value)
    {
        switch (value)
        {
            case Stencil::Zero:      return "0 (untouched)";
            case Stencil::Reference: return "5 (ordinary operation)";
            case Stencil::Inverted:  return "255 (counter-clockwise operation)";
            default:                 return "unrecognised";
        }
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
            VertexPositionColor(Vector3( 0.0f, -1.0f, 0.5f), color),
            VertexPositionColor(Vector3(-1.0f, -1.0f, 0.5f), color),
            VertexPositionColor(Vector3(-1.0f,  1.0f, 0.5f), color),
            VertexPositionColor(Vector3( 0.0f,  1.0f, 0.5f), color),
            VertexPositionColor(Vector3( 0.0f, -1.0f, 0.5f), color),
            VertexPositionColor(Vector3(-1.0f,  1.0f, 0.5f), color),
            VertexPositionColor(Vector3( 0.0f,  1.0f, 0.5f), color),
            VertexPositionColor(Vector3( 0.0f, -1.0f, 0.5f), color),
            VertexPositionColor(Vector3( 1.0f, -1.0f, 0.5f), color),
            VertexPositionColor(Vector3( 0.0f,  1.0f, 0.5f), color),
            VertexPositionColor(Vector3( 1.0f, -1.0f, 0.5f), color),
            VertexPositionColor(Vector3( 1.0f,  1.0f, 0.5f), color),
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices.data(), 0, 4);
    }

    // Replace for the ordinary operation, Invert for the counter-clockwise one, every other
    // operation Keep: exactly one field decides each winding's value.
    DepthStencilState TwoSidedPassState(bool twoSided)
    {
        DepthStencilState state;
        state.setDepthBufferEnableProperty(false);
        state.setDepthBufferWriteEnableProperty(false);
        state.setStencilEnableProperty(true);
        state.setTwoSidedStencilModeProperty(twoSided);
        state.setReferenceStencilProperty(kReference);
        state.setStencilFunctionProperty(CompareFunction::Always);
        state.setStencilPassProperty(StencilOperation::Replace);
        state.setStencilFailProperty(StencilOperation::Keep);
        state.setStencilDepthBufferFailProperty(StencilOperation::Keep);
        state.setCounterClockwiseStencilFunctionProperty(CompareFunction::Always);
        state.setCounterClockwiseStencilPassProperty(StencilOperation::Invert);
        state.setCounterClockwiseStencilFailProperty(StencilOperation::Keep);
        state.setCounterClockwiseStencilDepthBufferFailProperty(StencilOperation::Keep);
        return state;
    }

    DepthStencilState EqualProbe(int reference)
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

    Stencil Classify(const Color& pixel)
    {
        if (pixel == Color::Blue) return Stencil::Zero;
        if (pixel == Color::Red)  return Stencil::Reference;
        if (pixel == Color::Lime) return Stencil::Inverted;
        return Stencil::Other;
    }

    struct WindingResult
    {
        Stencil clockwise = Stencil::Other;
        Stencil counterClockwise = Stencil::Other;
    };

    WindingResult RunWindingScenario(GraphicsDevice& device, RenderTarget2D& target,
                                     const DepthStencilState& state, const RasterizerState& rasterizer)
    {
        device.SetRenderTarget(&target);
        device.setBlendStateProperty(BlendState::Opaque);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     Color::Black, 1.0f, 0);
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
        device.SetRenderTarget(nullptr);
        return {Classify(ReadTarget(target, kClockwiseX, kProbeY)),
                Classify(ReadTarget(target, kCounterClockwiseX, kProbeY))};
    }

    void ExpectWindings(const WindingResult& actual, Stencil clockwise, Stencil counterClockwise,
                        const char* scenario)
    {
        EXPECT_EQ(actual.clockwise, clockwise)
            << scenario << ": clockwise triangle left stencil " << Name(actual.clockwise)
            << ", expected " << Name(clockwise);
        EXPECT_EQ(actual.counterClockwise, counterClockwise)
            << scenario << ": counter-clockwise triangle left stencil " << Name(actual.counterClockwise)
            << ", expected " << Name(counterClockwise);
    }
}

TEST(WebGPUPipelineStateContract, TwoSidedStencilAppliesCounterClockwiseStateToCounterClockwiseTriangles)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 0,
                          RenderTargetUsage::PreserveContents);

    // Ordinary Replace(5) on the clockwise half, CounterClockwise Invert(0 -> 255) on the other.
    ExpectWindings(RunWindingScenario(device, target, TwoSidedPassState(true), RasterizerState::CullNone),
                   Stencil::Reference, Stencil::Inverted, "TwoSidedStencilMode=true");
    // With the mode off the ordinary state applies to both windings.
    ExpectWindings(RunWindingScenario(device, target, TwoSidedPassState(false), RasterizerState::CullNone),
                   Stencil::Reference, Stencil::Reference, "TwoSidedStencilMode=false");
}

TEST(WebGPUPipelineStateContract, CounterClockwiseStencilStateBelongsToTheWindingCullCounterClockwiseFaceRemoves)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 0,
                          RenderTargetUsage::PreserveContents);
    const DepthStencilState state = TwoSidedPassState(true);

    // Culling and two-sided stencil classify faces identically: removing the counter-clockwise
    // triangles removes exactly the ones that carried the counter-clockwise operation, and the other
    // way round.
    ExpectWindings(RunWindingScenario(device, target, state, RasterizerState::CullCounterClockwise),
                   Stencil::Reference, Stencil::Zero, "CullCounterClockwiseFace");
    ExpectWindings(RunWindingScenario(device, target, state, RasterizerState::CullClockwise),
                   Stencil::Zero, Stencil::Inverted, "CullClockwiseFace");
}

TEST(WebGPUPipelineStateContract, TwoSidedStencilKeepsLinesOnTheOrdinaryState)
{
    // A line has no winding, so XNA's two-sided mode keeps it on the ordinary stencil state (the
    // shared stencil_matrix_contract_test's "two-sided mode ignores the CCW tuple for line
    // primitives"). WebGPU counts every line as front-facing, which is the slot the
    // counter-clockwise state occupies, so the renderer must not leave it there.
    //
    // Hand derivation: 8x8 Depth24Stencil8 target, stencil cleared to 0. A LineList across the
    // whole width through row 4's pixel centres (NDC y = 1 - 2 * 4.5 / 8 = -0.125, so no
    // boundary tie decides coverage) under ordinary Replace(5) / counter-clockwise Invert must leave
    // 5 at (4, 4); the counter-clockwise state would leave 255.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 0,
                          RenderTargetUsage::PreserveContents);
    device.SetRenderTarget(&target);
    device.setBlendStateProperty(BlendState::Opaque);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil, Color::Black, 1.0f, 0);
    device.setDepthStencilStateProperty(TwoSidedPassState(true));
    {
        BasicEffect effect(device);
        effect.VertexColorEnabled = true;
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.Apply();
        const std::array line = {
            VertexPositionColor(Vector3(-1.0f, -0.125f, 0.5f), Color::Black),
            VertexPositionColor(Vector3( 1.0f, -0.125f, 0.5f), Color::Black),
        };
        device.DrawUserPrimitives(PrimitiveType::LineList, line.data(), 0, 1);
    }
    device.setDepthStencilStateProperty(EqualProbe(0));
    DrawBothWindings(device, Color::Blue);
    device.setDepthStencilStateProperty(EqualProbe(kReference));
    DrawBothWindings(device, Color::Red);
    device.setDepthStencilStateProperty(EqualProbe(0xFF));
    DrawBothWindings(device, Color::Lime);
    device.SetRenderTarget(nullptr);
    const Stencil value = Classify(ReadTarget(target, 4, 4));
    EXPECT_EQ(value, Stencil::Reference) << "a two-sided LineList left stencil " << Name(value);
}

// ---------------------------------------------------------------------------------------------
// DepthBufferEnable = false disables depth buffering, writes included.
//
// XNA's DepthBufferEnable maps to D3DRS_ZENABLE: with it false the depth buffer is neither tested
// nor written, whatever DepthBufferWriteEnable says (XNA's DepthBufferWriteEnable defaults to true,
// so `new DepthStencilState { DepthBufferEnable = false }` is the ordinary way to get here). Every
// API FNA3D drives agrees: GL_DEPTH_TEST disabled writes nothing, and Vulkan writes depth only when
// depthTestEnable is set. WebGPU has no separate enable, so the renderer must not ask for a write.
//
// Hand derivation: clear depth to 1.0; draw red at z = 0.2 with the depth buffer disabled (it
// draws, but must leave depth at 1.0); draw green at z = 0.5 with LessEqual. 0.5 <= 1.0 passes, so
// the pixel is GREEN. Had the first draw written 0.2, 0.5 <= 0.2 would fail and RED would remain.
// ---------------------------------------------------------------------------------------------

TEST(WebGPUPipelineStateContract, DisabledDepthBufferWritesNoDepthOnTheStockRoute)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
                          RenderTargetUsage::PreserveContents);
    device.SetRenderTarget(&target);
    device.setBlendStateProperty(BlendState::Opaque);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);

    device.setDepthStencilStateProperty(DepthState(false, true, CompareFunction::LessEqual));
    DrawStockQuad(device, 0.2f, Color::Red);
    device.setDepthStencilStateProperty(DepthState(true, true, CompareFunction::LessEqual));
    DrawStockQuad(device, 0.5f, Color::Lime);
    device.SetRenderTarget(nullptr);

    const Color pixel = ReadTarget(target, kSize / 2, kSize / 2);
    EXPECT_EQ(pixel, Color::Lime) << "got " << Describe(pixel)
        << ": a draw with DepthBufferEnable=false wrote depth, so the later LessEqual draw failed";
}

TEST(WebGPUPipelineStateContract, DisabledDepthBufferWritesNoDepthOnTheShaderEffectRoute)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    ShaderEffect effect(device, kColorVertexWgsl, kColorFragmentWgsl);
    PrepareColorEffect(effect);
    RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
                          RenderTargetUsage::PreserveContents);
    device.SetRenderTarget(&target);
    device.setBlendStateProperty(BlendState::Opaque);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);

    device.setDepthStencilStateProperty(DepthState(false, true, CompareFunction::LessEqual));
    DrawEffectQuad(device, effect, 0.2f, Color::Red);
    device.setDepthStencilStateProperty(DepthState(true, true, CompareFunction::LessEqual));
    DrawEffectQuad(device, effect, 0.5f, Color::Lime);
    device.SetRenderTarget(nullptr);

    const Color pixel = ReadTarget(target, kSize / 2, kSize / 2);
    EXPECT_EQ(pixel, Color::Lime) << "got " << Describe(pixel)
        << ": a ShaderEffect draw with DepthBufferEnable=false wrote depth";
}

// ---------------------------------------------------------------------------------------------
// A ShaderEffect's pipeline cache must separate everything its pipeline bakes.
// ---------------------------------------------------------------------------------------------

TEST(WebGPUPipelineStateContract, OneShaderEffectDrawsIntoTargetsWithAndWithoutADepthBuffer)
{
    // A WebGPU pipeline declares its depth-stencil format (or none), and a pass whose depth
    // attachment differs rejects it. One effect drawn into a target without depth and then into an
    // otherwise identical one with depth (and the reverse, on a fresh effect so neither order is
    // served by the other's cache) must therefore build two pipelines. Each draw fills its target
    // GREEN; any validation failure leaves the cleared BLACK and counts an uncaptured error.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    WebGPURenderer& renderer = RendererOf(device);
    RenderTarget2D noDepth(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::PreserveContents);
    RenderTarget2D withDepth(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
                             RenderTargetUsage::PreserveContents);
    device.setBlendStateProperty(BlendState::Opaque);
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::None);

    const auto drawInto = [&](ShaderEffect& effect, RenderTarget2D& target) {
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        DrawEffectQuad(device, effect, 0.5f, Color::Lime);
        device.SetRenderTarget(nullptr);
        return ReadTarget(target, kSize / 2, kSize / 2);
    };

    const std::size_t errorsBefore = renderer.GetUncapturedErrorCountEXT();
    {
        ShaderEffect effect(device, kColorVertexWgsl, kColorFragmentWgsl);
        PrepareColorEffect(effect);
        const Color first = drawInto(effect, noDepth);
        const Color second = drawInto(effect, withDepth);
        EXPECT_EQ(first, Color::Lime) << "no-depth target first: " << Describe(first);
        EXPECT_EQ(second, Color::Lime) << "depth target after a no-depth draw: " << Describe(second);
    }
    {
        ShaderEffect effect(device, kColorVertexWgsl, kColorFragmentWgsl);
        PrepareColorEffect(effect);
        const Color first = drawInto(effect, withDepth);
        const Color second = drawInto(effect, noDepth);
        EXPECT_EQ(first, Color::Lime) << "depth target first: " << Describe(first);
        EXPECT_EQ(second, Color::Lime) << "no-depth target after a depth draw: " << Describe(second);
    }
    EXPECT_EQ(renderer.GetUncapturedErrorCountEXT(), errorsBefore)
        << "a ShaderEffect pipeline was reused for a pass with a different depth attachment";
}

TEST(WebGPUPipelineStateContract, ShaderEffectHonoursAChangedDepthBiasOnTheSameEffect)
{
    // XNA's RasterizerState.DepthBias is added to each fragment's depth (D3DRS_DEPTHBIAS). WebGPU
    // bakes the bias into the pipeline, so a second draw that differs from the first only in its
    // bias needs a pipeline of its own.
    //
    // Hand derivation: clear depth to 1.0; RED at z = 0.5, bias 0, LessEqual + write -> depth 0.5.
    // GREEN at z = 0.5 with DepthBias = 0.25 -> 0.75 <= 0.5 fails -> RED stays. Reusing the first
    // pipeline (bias 0) would test 0.5 <= 0.5, pass, and leave GREEN. The stock BasicEffect route,
    // whose cache key carries the bias, is checked the same way as the control.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    ShaderEffect effect(device, kColorVertexWgsl, kColorFragmentWgsl);
    PrepareColorEffect(effect);
    RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
                          RenderTargetUsage::PreserveContents);
    RasterizerState biased;
    biased.setCullModeProperty(CullMode::None);
    biased.setDepthBiasProperty(0.25f);

    const auto run = [&](const std::function<void(float, const Color&)>& draw) {
        device.SetRenderTarget(&target);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        draw(0.5f, Color::Red);
        device.setRasterizerStateProperty(biased);
        draw(0.5f, Color::Lime);
        device.SetRenderTarget(nullptr);
        return ReadTarget(target, kSize / 2, kSize / 2);
    };

    const Color stock = run([&](float z, const Color& c) { DrawStockQuad(device, z, c); });
    EXPECT_EQ(stock, Color::Red) << "stock route: " << Describe(stock);
    const Color custom = run([&](float z, const Color& c) { DrawEffectQuad(device, effect, z, c); });
    EXPECT_EQ(custom, Color::Red) << "ShaderEffect route: " << Describe(custom)
        << " -- the biased draw reused the unbiased pipeline";
}

TEST(WebGPUPipelineStateContract, ShaderEffectDrawsHonourTheStencilTestAndWriteTheReference)
{
    // A DepthStencilState applies to every draw, whichever effect issues it (XNA sets the device
    // state; the effect only supplies shaders). docs/webgpu-renderer.md claims "complete
    // depth/stencil operations ... captured per draw".
    //
    // Hand derivation, Depth24Stencil8 target cleared to BLACK / stencil 0:
    //  1. a LIME ShaderEffect quad under StencilFunction = Never must draw nothing -> BLACK;
    //  2. a ShaderEffect quad under Always/Replace with ReferenceStencil 5 writes 5 everywhere, so a
    //     stock RED probe under Equal(5) then covers the target -> RED (with no stencil write the
    //     buffer stays 0 and the probe is rejected -> BLACK).
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    ShaderEffect effect(device, kColorVertexWgsl, kColorFragmentWgsl);
    PrepareColorEffect(effect);
    RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8, 0,
                          RenderTargetUsage::PreserveContents);
    device.setBlendStateProperty(BlendState::Opaque);
    device.setRasterizerStateProperty(RasterizerState::CullNone);

    DepthStencilState never;
    never.setDepthBufferEnableProperty(false);
    never.setStencilEnableProperty(true);
    never.setStencilFunctionProperty(CompareFunction::Never);
    device.SetRenderTarget(&target);
    device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil, Color::Black, 1.0f, 0);
    device.setDepthStencilStateProperty(never);
    DrawEffectQuad(device, effect, 0.5f, Color::Lime);
    device.SetRenderTarget(nullptr);
    const Color rejected = ReadTarget(target, kSize / 2, kSize / 2);
    EXPECT_EQ(rejected, Color::Black) << "ShaderEffect quad under StencilFunction=Never drew " << Describe(rejected);

    DepthStencilState stamp;
    stamp.setDepthBufferEnableProperty(false);
    stamp.setStencilEnableProperty(true);
    stamp.setStencilFunctionProperty(CompareFunction::Always);
    stamp.setStencilPassProperty(StencilOperation::Replace);
    stamp.setReferenceStencilProperty(kReference);
    device.SetRenderTarget(&target);
    device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil, Color::Black, 1.0f, 0);
    device.setDepthStencilStateProperty(stamp);
    DrawEffectQuad(device, effect, 0.5f, Color::Black);
    device.setDepthStencilStateProperty(EqualProbe(kReference));
    DrawStockQuad(device, 0.5f, Color::Red);
    // The pass begins with the device's live ReferenceStencil, so leave a different one live
    // (9, no draw): a stamp that does not apply its OWN captured reference would then write 9 and
    // the Equal(5) probe would be rejected.
    device.setDepthStencilStateProperty(EqualProbe(9));
    device.SetRenderTarget(nullptr);
    const Color probed = ReadTarget(target, kSize / 2, kSize / 2);
    EXPECT_EQ(probed, Color::Red) << "after a ShaderEffect stencil Replace(5), an Equal(5) probe gave "
                                  << Describe(probed);
}

// ---------------------------------------------------------------------------------------------
// SpriteBatch with a custom WGSL effect.
// ---------------------------------------------------------------------------------------------

TEST(WebGPUPipelineStateContract, OneSpriteEffectDrawsIntoTargetsWithAndWithoutADepthBuffer)
{
    // Same rule as the 3D ShaderEffect case above, on the SpriteBatch route's own pipeline cache.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    WebGPURenderer& renderer = RendererOf(device);
    SpriteBatch batch(device);
    Texture2D white = Texture2D::CreateFromPixels(device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    RenderTarget2D noDepth(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
                           RenderTargetUsage::PreserveContents);
    RenderTarget2D withDepth(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
                             RenderTargetUsage::PreserveContents);
    const Rectangle full(0, 0, kSize, kSize);

    const auto drawInto = [&](ShaderEffect& effect, RenderTarget2D& target) {
        device.SetRenderTarget(&target);
        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque, nullptr, nullptr, nullptr, &effect);
        batch.Draw(white, full, Color::Lime);
        batch.End();
        device.SetRenderTarget(nullptr);
        return ReadTarget(target, kSize / 2, kSize / 2);
    };

    const std::size_t errorsBefore = renderer.GetUncapturedErrorCountEXT();
    {
        ShaderEffect effect(device, kSpriteVertexWgsl, kSpriteFragmentWgsl);
        ASSERT_TRUE(effect.IsEffectValid()) << effect.GetCompileErrorEXT();
        const Color first = drawInto(effect, noDepth);
        const Color second = drawInto(effect, withDepth);
        EXPECT_EQ(first, Color::Lime) << "no-depth target first: " << Describe(first);
        EXPECT_EQ(second, Color::Lime) << "depth target after a no-depth batch: " << Describe(second);
    }
    {
        ShaderEffect effect(device, kSpriteVertexWgsl, kSpriteFragmentWgsl);
        ASSERT_TRUE(effect.IsEffectValid()) << effect.GetCompileErrorEXT();
        const Color first = drawInto(effect, withDepth);
        const Color second = drawInto(effect, noDepth);
        EXPECT_EQ(first, Color::Lime) << "depth target first: " << Describe(first);
        EXPECT_EQ(second, Color::Lime) << "no-depth target after a depth batch: " << Describe(second);
    }
    EXPECT_EQ(renderer.GetUncapturedErrorCountEXT(), errorsBefore)
        << "a SpriteBatch effect pipeline was reused for a pass with a different depth attachment";
}

TEST(WebGPUPipelineStateContract, SpriteBatchHonoursItsDepthStencilStateWithACustomEffect)
{
    // SpriteBatch.Begin applies the DepthStencilState it is given to every sprite, whichever effect
    // draws them (XNA SpriteBatch.Setup sets GraphicsDevice.DepthStencilState before the effect's
    // pass). A depth test that never passes therefore draws nothing.
    //
    // Hand derivation: depth target cleared to 1.0, BLACK; a full-target LIME sprite under
    // DepthBufferFunction = Never must leave BLACK. The stock sprite route is the control.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    SpriteBatch batch(device);
    Texture2D white = Texture2D::CreateFromPixels(device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    RenderTarget2D target(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
                          RenderTargetUsage::PreserveContents);
    ShaderEffect effect(device, kSpriteVertexWgsl, kSpriteFragmentWgsl);
    ASSERT_TRUE(effect.IsEffectValid()) << effect.GetCompileErrorEXT();
    const DepthStencilState never = DepthState(true, false, CompareFunction::Never);
    const DepthStencilState always = DepthState(true, false, CompareFunction::Always);
    const Rectangle full(0, 0, kSize, kSize);

    const auto run = [&](const DepthStencilState& depth, Effect* custom) {
        device.SetRenderTarget(&target);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, Color::Black, 1.0f, 0);
        batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque, nullptr, &depth, nullptr, custom);
        batch.Draw(white, full, Color::Lime);
        batch.End();
        device.SetRenderTarget(nullptr);
        return ReadTarget(target, kSize / 2, kSize / 2);
    };

    const Color stockNever = run(never, nullptr);
    EXPECT_EQ(stockNever, Color::Black) << "stock sprite under Never: " << Describe(stockNever);
    const Color customAlways = run(always, &effect);
    EXPECT_EQ(customAlways, Color::Lime) << "custom sprite under Always: " << Describe(customAlways);
    const Color customNever = run(never, &effect);
    EXPECT_EQ(customNever, Color::Black) << "custom sprite under Never: " << Describe(customNever)
        << " -- the custom-effect sprite pipeline ignores the batch's DepthStencilState";

    // The stencil half of the same state: on a Depth24Stencil8 target a sprite whose batch uses
    // StencilFunction = Never draws nothing either; Always draws.
    RenderTarget2D stencilTarget(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8,
                                 0, RenderTargetUsage::PreserveContents);
    const auto runStencil = [&](CompareFunction function, Effect* custom) {
        DepthStencilState state;
        state.setDepthBufferEnableProperty(false);
        state.setStencilEnableProperty(true);
        state.setStencilFunctionProperty(function);
        device.SetRenderTarget(&stencilTarget);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil, Color::Black,
                     1.0f, 0);
        batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque, nullptr, &state, nullptr, custom);
        batch.Draw(white, full, Color::Lime);
        batch.End();
        device.SetRenderTarget(nullptr);
        return ReadTarget(stencilTarget, kSize / 2, kSize / 2);
    };
    const Color stockStencilNever = runStencil(CompareFunction::Never, nullptr);
    EXPECT_EQ(stockStencilNever, Color::Black) << "stock sprite, stencil Never: " << Describe(stockStencilNever);
    const Color customStencilAlways = runStencil(CompareFunction::Always, &effect);
    EXPECT_EQ(customStencilAlways, Color::Lime) << "custom sprite, stencil Always: " << Describe(customStencilAlways);
    const Color customStencilNever = runStencil(CompareFunction::Never, &effect);
    EXPECT_EQ(customStencilNever, Color::Black) << "custom sprite, stencil Never: " << Describe(customStencilNever)
        << " -- the custom-effect sprite pipeline ignores the batch's stencil state";
}

TEST(WebGPUPipelineStateContract, SpriteBatchDrawsIntoMrtSetsThatDifferOnlyInSlotOnesFormat)
{
    // A WebGPU pipeline's targets[] must match every bound attachment's format, not slot 0's alone
    // (the 3D families fold every slot since WEBGPU-202). XNA accepts a multi-target set whose
    // targets share size and pixel size, so {Color, Color} and {Color, Single} (both 32 bits per
    // texel) are both legal -- and a SpriteBatch drawn into each needs a pipeline of its own, on the
    // stock route and on the custom-effect route. Each batch fills slot 0 LIME over a BLACK clear.
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    if (!device.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets))
        GTEST_SKIP() << "no multi-target sets on this device";
    if (!device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Single))
        GTEST_SKIP() << "no Single render target on this device";
    WebGPURenderer& renderer = RendererOf(device);
    SpriteBatch batch(device);
    Texture2D white = Texture2D::CreateFromPixels(device, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    ShaderEffect effect(device, kSpriteVertexWgsl, kSpriteFragmentWgsl);
    ASSERT_TRUE(effect.IsEffectValid()) << effect.GetCompileErrorEXT();
    const Rectangle full(0, 0, kSize, kSize);

    const auto drawInto = [&](SurfaceFormat second, Effect* custom) {
        RenderTarget2D slot0(device, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
                             RenderTargetUsage::PreserveContents);
        RenderTarget2D slot1(device, kSize, kSize, false, second, DepthFormat::None, 0,
                             RenderTargetUsage::PreserveContents);
        device.SetRenderTargets({RenderTargetBinding(static_cast<Texture*>(&slot0)),
                                 RenderTargetBinding(static_cast<Texture*>(&slot1))});
        device.Clear(Color::Black);
        batch.Begin(SpriteSortMode::Immediate, &BlendState::Opaque, nullptr, nullptr, nullptr, custom);
        batch.Draw(white, full, Color::Lime);
        batch.End();
        device.SetRenderTarget(nullptr);
        return ReadTarget(slot0, kSize / 2, kSize / 2);
    };

    const std::size_t errorsBefore = renderer.GetUncapturedErrorCountEXT();
    for (Effect* custom : {static_cast<Effect*>(nullptr), static_cast<Effect*>(&effect)})
    {
        const char* route = custom == nullptr ? "stock" : "custom-effect";
        const Color allColour = drawInto(SurfaceFormat::Color, custom);
        EXPECT_EQ(allColour, Color::Lime) << route << " sprite into {Color, Color}: " << Describe(allColour);
        const Color mixed = drawInto(SurfaceFormat::Single, custom);
        EXPECT_EQ(mixed, Color::Lime) << route << " sprite into {Color, Single}: " << Describe(mixed);
    }
    EXPECT_EQ(renderer.GetUncapturedErrorCountEXT(), errorsBefore)
        << "a sprite pipeline built for one MRT set was reused for a set whose slot 1 differs";
}


#if defined(CNA_CNAEXT) || defined(CNA_WEBGPU_COMPILED_EFFECTS)

// HND-TQ-03: the descriptor and compiled routes do not use the legacy WGSL pipelines above.
#ifdef CNA_CNAEXT
#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#endif
#ifdef CNA_WEBGPU_COMPILED_EFFECTS
#include "CNA/TestSupport/CompiledEffectFixtures.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#endif
#include <memory>
#include <optional>

namespace
{
    enum class HndRoute { Descriptor3D, DescriptorSprite, Compiled3D, CompiledSprite };

    class WebGPUHndTq03 : public ::testing::TestWithParam<HndRoute> {};

    class HndDraw
    {
    public:
        HndDraw(GraphicsDevice& device, HndRoute route) : device_(device), route_(route), batch_(device),
            white_(Texture2D::CreateFromPixels(device, 1, 1, std::vector<std::uint8_t>{255,255,255,255}))
        {
#ifdef CNA_CNAEXT
            if (route == HndRoute::Descriptor3D || route == HndRoute::DescriptorSprite)
            {
                // No uniforms or textures decide the result: forward the vertex colour unchanged.
                // Descriptor sprites supply pixel-position numerators and clip W, unlike 3D.
                // The renderer classifies a pair by its declared resources; the zero-weighted
                // scalar block at @group(3) @binding(0) selects the descriptor contract (a pair
                // declaring none is the legacy vs_main/fs_main contract).
                const std::string vertex = route == HndRoute::Descriptor3D ? R"WGSL(
struct S { v: array<vec4f, 8> };
@group(3) @binding(0) var<uniform> s: S;
struct O { @builtin(position) p: vec4f, @location(0) c: vec4f };
@vertex fn main(@location(0) p: vec3f, @location(1) c: vec4f) -> O {
    var o: O; o.p=vec4f(p,1); o.c=c+s.v[0]*0.0; return o;
})WGSL" : R"WGSL(
struct S { v: array<vec4f, 8> };
@group(3) @binding(0) var<uniform> s: S;
struct O { @builtin(position) p: vec4f, @location(0) c: vec4f };
@vertex fn main(@location(0) p: vec4f, @location(1) uv: vec2f,
                @location(2) c: vec4f) -> O {
    var o: O; o.p=vec4f(p.x/4-p.w,p.w-p.y/4,p.z,p.w); o.c=c+s.v[0]*0.0; return o;
})WGSL";
                const CNA::Graphics::ShaderCodeEXT vs(CNA::ShaderLanguageEXT::Wgsl,
                    CNA::ShaderStageEXT::Vertex, "main", "HND-TQ-03.vert", vertex);
                const CNA::Graphics::ShaderCodeEXT fs(CNA::ShaderLanguageEXT::Wgsl,
                    CNA::ShaderStageEXT::Fragment, "main", "HND-TQ-03.frag", R"WGSL(
@fragment fn main(@location(0) c: vec4f) -> @location(0) vec4f { return c; }
)WGSL");
                descriptor_ = std::make_unique<ShaderEffect>(device, vs, fs);
                if (!descriptor_->IsEffectValid())
                    throw std::runtime_error(descriptor_->GetCompileErrorEXT());
            }
#endif
#ifdef CNA_WEBGPU_COMPILED_EFFECTS
            if (route == HndRoute::Compiled3D || route == HndRoute::CompiledSprite)
            {
                // The fixture has no render-state assignments: Apply only binds the shaders.
                compiled_ = std::make_unique<Effect>(device, CNA::TestSupport::BuildSyntheticDrawableEffect());
                Matrix transform = Matrix::getIdentityProperty();
                if (route == HndRoute::CompiledSprite)
                {
                    // Hand map: pixel (0,0)->NDC(-1,+1), (8,8)->(+1,-1); retain z.
                    transform.M11 = 2.0f / kSize;
                    transform.M22 = -2.0f / kSize;
                    transform.M41 = -1.0f;
                    transform.M42 = 1.0f;
                }
                compiled_->getParametersProperty()["Transform"]->SetValue(transform);
            }
#endif
        }

        void Draw(float z, const Color& color, bool sloped = false)
        {
            Effect* effect = descriptor_.get();
#ifdef CNA_WEBGPU_COMPILED_EFFECTS
            if (compiled_)
            {
                // The synthetic pixel program returns Tint exactly; use only 0 and 1 channels.
                compiled_->getParametersProperty()["Tint"]->SetValue(Vector4(
                    color.getRProperty()/255.0f, color.getGProperty()/255.0f,
                    color.getBProperty()/255.0f, color.getAProperty()/255.0f));
                effect = compiled_.get();
            }
#endif
            if (effect == nullptr) throw std::runtime_error("HND-TQ-03 route was not compiled in");
            if (route_ == HndRoute::DescriptorSprite || route_ == HndRoute::CompiledSprite)
            {
                const auto depth = device_.getDepthStencilStateProperty();
                batch_.Begin(SpriteSortMode::Immediate, &BlendState::Opaque, nullptr, &depth,
                             &RasterizerState::CullNone, effect);
                batch_.Draw(white_, Rectangle(0,0,kSize,kSize), std::nullopt, color, 0.0f,
                            Vector2(0.0f,0.0f), SpriteEffects::None, z);
                batch_.End();
            }
            else
            {
                auto quad = FullQuad(z, color);
                if (sloped)
                    for (auto& v : quad) v.Position.Z += 0.25f * v.Position.X;
#ifdef CNA_WEBGPU_COMPILED_EFFECTS
                if (compiled_)
                    compiled_->getTechniquesProperty()[0]->getPassesProperty()[1]->Apply();
                else
#endif
                    descriptor_->Apply();
                device_.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
            }
        }
    private:
        GraphicsDevice& device_;
        HndRoute route_;
        SpriteBatch batch_;
        Texture2D white_;
        std::unique_ptr<ShaderEffect> descriptor_;
#ifdef CNA_WEBGPU_COMPILED_EFFECTS
        std::unique_ptr<Effect> compiled_;
#endif
    };

    void HndBind(GraphicsDevice& device, RenderTarget2D& target)
    {
        device.SetRenderTarget(&target);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     Color::Black, 1.0f, 0);
    }

    std::string HndRouteName(const ::testing::TestParamInfo<HndRoute>& p)
    {
        switch (p.param)
        {
            case HndRoute::Descriptor3D: return "Descriptor3D";
            case HndRoute::DescriptorSprite: return "DescriptorSprite";
            case HndRoute::Compiled3D: return "Compiled3D";
            case HndRoute::CompiledSprite: return "CompiledSprite";
        }
        return "Unknown";
    }
}

TEST_P(WebGPUHndTq03, DisabledDepthDoesNotWrite)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    HndDraw draw(device, GetParam());
    RenderTarget2D target(device,kSize,kSize,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8,0,
                          RenderTargetUsage::PreserveContents);
    HndBind(device,target);
    device.setDepthStencilStateProperty(DepthState(false,true,CompareFunction::LessEqual));
    draw.Draw(0.2f,Color::Red);
    device.setDepthStencilStateProperty(DepthState(true,true,CompareFunction::LessEqual));
    draw.Draw(0.5f,Color::Lime);
    device.SetRenderTarget(nullptr);
    // Hand oracle: disabled depth leaves clear 1; .5 <= 1 passes -> LIME.
    // Defect writes .2; .5 <= .2 fails -> RED.
    EXPECT_EQ(ReadTarget(target,4,3),Color::Lime);
}

TEST_P(WebGPUHndTq03, StencilStateAndCapturedReference)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    HndDraw draw(device, GetParam());
    RenderTarget2D target(device,kSize,kSize,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8,0,
                          RenderTargetUsage::PreserveContents);
    DepthStencilState never;
    never.setDepthBufferEnableProperty(false);
    never.setStencilEnableProperty(true);
    never.setStencilFunctionProperty(CompareFunction::Never);
    HndBind(device,target);
    device.setDepthStencilStateProperty(never);
    draw.Draw(0.5f,Color::Lime);
    device.SetRenderTarget(nullptr);
    // Hand oracle: Never rejects every fragment, so BLACK stays. Missing stencil yields LIME.
    EXPECT_EQ(ReadTarget(target,4,3),Color::Black);

    DepthStencilState stamp;
    stamp.setDepthBufferEnableProperty(false);
    stamp.setStencilEnableProperty(true);
    stamp.setStencilFunctionProperty(CompareFunction::Always);
    stamp.setStencilPassProperty(StencilOperation::Replace);
    stamp.setReferenceStencilProperty(5);
    HndBind(device,target);
    device.setDepthStencilStateProperty(stamp);
    draw.Draw(0.5f,Color::Black);
    device.setDepthStencilStateProperty(EqualProbe(5));
    DrawStockQuad(device,0.5f,Color::Red);
    device.setDepthStencilStateProperty(EqualProbe(9)); // live state differs at deferred replay
    device.SetRenderTarget(nullptr);
    // Hand oracle: Replace writes captured 5; Equal(5) passes -> RED. Missing/reference=9 -> BLACK.
    EXPECT_EQ(ReadTarget(target,4,3),Color::Red);
}

TEST_P(WebGPUHndTq03, DepthAttachmentVariants)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    RenderTarget2D none(device,kSize,kSize,false,SurfaceFormat::Color,DepthFormat::None,0,
                        RenderTargetUsage::PreserveContents);
    RenderTarget2D depth(device,kSize,kSize,false,SurfaceFormat::Color,DepthFormat::Depth24,0,
                         RenderTargetUsage::PreserveContents);
    const auto errors = RendererOf(device).GetUncapturedErrorCountEXT();
    for (bool reverse : {false,true})
    {
        HndDraw draw(device,GetParam()); // fresh cache for either order
        for (RenderTarget2D* target : {reverse ? &depth : &none, reverse ? &none : &depth})
        {
            device.SetRenderTarget(target);
            device.Clear(Color::Black);
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setBlendStateProperty(BlendState::Opaque);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            draw.Draw(0.5f,Color::Lime);
            device.SetRenderTarget(nullptr);
            // Unconditional full coverage, opaque 0/1 output -> LIME in either attachment variant.
            EXPECT_EQ(ReadTarget(*target,4,3),Color::Lime);
        }
    }
    EXPECT_EQ(RendererOf(device).GetUncapturedErrorCountEXT(),errors);
}

#ifdef CNA_CNAEXT
INSTANTIATE_TEST_SUITE_P(Descriptor,WebGPUHndTq03,
    ::testing::Values(HndRoute::Descriptor3D,HndRoute::DescriptorSprite),HndRouteName);

TEST(WebGPUHndTq03Descriptor, BothBiasFieldsSelectDistinctPipelines)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    RenderTarget2D target(device,kSize,kSize,false,SurfaceFormat::Color,DepthFormat::Depth24Stencil8,0,
                          RenderTargetUsage::PreserveContents);
    for (bool slope : {false,true})
    {
        SCOPED_TRACE(slope ? "SlopeScaleDepthBias" : "DepthBias");
        HndDraw draw(device,HndRoute::Descriptor3D);
        HndBind(device,target);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        draw.Draw(0.5f,Color::Red,slope);
        RasterizerState biased;
        biased.setCullModeProperty(CullMode::None);
        if (slope) biased.setSlopeScaleDepthBiasProperty(4.0f);
        else biased.setDepthBiasProperty(0.25f);
        device.setRasterizerStateProperty(biased);
        draw.Draw(0.5f,Color::Lime,slope);
        device.SetRenderTarget(nullptr);
        // Hand oracle: constant .5 + .25 > stored .5. Sloped dz/dx=.5/8=.0625;
        // slope bias=4*.0625=.25 > 0. Both reject GREEN, leaving RED.
        // Without either key dimension the unbiased pipeline passes equal depths -> LIME.
        EXPECT_EQ(ReadTarget(target,4,3),Color::Red);
    }
}

TEST(WebGPUHndTq03Descriptor, SpriteMrtSecondaryFormatVariants)
{
    CNA_SKIP_IF_RENDERER_IS_NOT(CNA::GraphicsRendererType::WebGPU);
    GraphicsDevice device = MakeDevice();
    ASSERT_TRUE(device.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets));
    ASSERT_TRUE(device.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Single));
    RenderTarget2D slot0(device,kSize,kSize,false,SurfaceFormat::Color,DepthFormat::None,0,
                         RenderTargetUsage::PreserveContents);
    RenderTarget2D color(device,kSize,kSize,false,SurfaceFormat::Color,DepthFormat::None,0,
                         RenderTargetUsage::PreserveContents);
    RenderTarget2D single(device,kSize,kSize,false,SurfaceFormat::Single,DepthFormat::None,0,
                          RenderTargetUsage::PreserveContents);
    const auto errors = RendererOf(device).GetUncapturedErrorCountEXT();
    for (bool reverse : {false,true})
    {
        HndDraw draw(device,HndRoute::DescriptorSprite);
        for (RenderTarget2D* second : {reverse ? &single : &color,reverse ? &color : &single})
        {
            device.SetRenderTargets({RenderTargetBinding(static_cast<Texture*>(&slot0)),
                                     RenderTargetBinding(static_cast<Texture*>(second))});
            device.Clear(Color::Black);
            device.setDepthStencilStateProperty(DepthStencilState::None);
            draw.Draw(0.5f,Color::Lime);
            device.SetRenderTarget(nullptr);
            // The fragment writes slot 0 only; opaque full coverage -> LIME regardless of slot 1.
            EXPECT_EQ(ReadTarget(slot0,4,3),Color::Lime);
        }
    }
    EXPECT_EQ(RendererOf(device).GetUncapturedErrorCountEXT(),errors);
}
#endif
#ifdef CNA_WEBGPU_COMPILED_EFFECTS
INSTANTIATE_TEST_SUITE_P(Compiled,WebGPUHndTq03,
    ::testing::Values(HndRoute::Compiled3D,HndRoute::CompiledSprite),HndRouteName);
#endif

#endif // CNA_CNAEXT || CNA_WEBGPU_COMPILED_EFFECTS

#endif
