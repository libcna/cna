// SPDX-License-Identifier: MS-PL
// REMED-GFX-018: public GraphicsDevice.Clear/ClearOptions contract on Bgfx.
//
// A clear must affect exactly the requested buffers:
//   0                              -> no-op
//   Target                         -> colour
//   DepthBuffer                    -> depth
//   Stencil                        -> stencil
//   Target|DepthBuffer             -> colour + depth
//   Target|Stencil                 -> colour + stencil
//   DepthBuffer|Stencil            -> depth + stencil
//   Target|DepthBuffer|Stencil     -> all three
//
// Each case first establishes distinct PRE-EXISTING colour, depth, and stencil values, then issues
// the clear under test with different arguments and encodes surviving depth/stencil values into
// colour probes. Render targets retain that baseline across a normal producer frame; the
// non-preservable swapchain baseline is established earlier in the same ordered frame. Every
// observable result is read from one whole-framebuffer snapshot; no retry loop and no extra
// Present call are used.
//
// Coverage:
//   * all eight ClearOptions masks, plus all public Clear overloads;
//   * backbuffer and PreserveContents RenderTarget2D;
//   * Depth24Stencil8, Depth24, and depthless targets;
//   * non-default RGBA/depth/stencil arguments;
//   * repeated/interleaved clears in one frame;
//   * target isolation and A->B->A target transitions;
//   * full-target clears while a custom viewport and enabled scissor are active.
//
// CMake runs this same public regression through Bgfx's OpenGL and Vulkan renderer routes.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ClearOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/ColorWriteChannels.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kBackbufferWidth = 160;
    constexpr int kBackbufferHeight = 64;
    constexpr int kTargetWidth = 80;
    constexpr int kTargetHeight = 56;

    const Color kSeedColor(7, 13, 19, 37);
    const Color kBaselineColor(31, 73, 121, 183);
    const Color kRequestedColor(207, 91, 43, 97);
    const Color kGreen(0, 255, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kBlack(0, 0, 0, 255);
    const Color kWhite(255, 255, 255, 255);

    constexpr float kSeedDepth = 0.91f;
    constexpr float kBaselineDepth = 0.23f;
    constexpr float kRequestedDepth = 0.67f;
    constexpr float kSingleColorDepth = 0.73f;
    constexpr int kSeedStencil = 0x11;
    constexpr int kBaselineStencil = 0x2d;
    constexpr int kRequestedStencil = 0xa6;

    enum class Invocation
    {
        Options,
        FloatColor,
        ColorAndDepth,
        ColorAll
    };

    struct ClearCase
    {
        const char* name;
        Invocation invocation;
        ClearOptions options;
        bool clearsColor;
        bool clearsDepth;
        bool clearsStencil;
    };

    constexpr ClearOptions kNone = static_cast<ClearOptions>(0);
    const std::array<ClearCase, 8> kOptionCases = {{
        {"none", Invocation::Options, kNone, false, false, false},
        {"Target", Invocation::Options, ClearOptions::Target, true, false, false},
        {"DepthBuffer", Invocation::Options, ClearOptions::DepthBuffer, false, true, false},
        {"Stencil", Invocation::Options, ClearOptions::Stencil, false, false, true},
        {"Target|DepthBuffer", Invocation::Options,
            ClearOptions::Target | ClearOptions::DepthBuffer, true, true, false},
        {"Target|Stencil", Invocation::Options,
            ClearOptions::Target | ClearOptions::Stencil, true, false, true},
        {"DepthBuffer|Stencil", Invocation::Options,
            ClearOptions::DepthBuffer | ClearOptions::Stencil, false, true, true},
        {"Target|DepthBuffer|Stencil", Invocation::Options,
            ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
            true, true, true},
    }};

    const std::array<ClearCase, 3> kOverloadCases = {{
        {"Clear(float,float,float,float)", Invocation::FloatColor, kNone, true, false, false},
        {"Clear(Color,float)", Invocation::ColorAndDepth, kNone, true, true, false},
        {"Clear(Color)", Invocation::ColorAll, kNone, true, true, true},
    }};

    bool NearRgb(const Color& actual, const Color& expected, int tolerance = 5)
    {
        return std::abs(actual.getRProperty() - expected.getRProperty()) <= tolerance
            && std::abs(actual.getGProperty() - expected.getGProperty()) <= tolerance
            && std::abs(actual.getBProperty() - expected.getBProperty()) <= tolerance;
    }

    bool IsGreen(const Color& color)
    {
        return color.getGProperty() >= 220
            && color.getRProperty() <= 35
            && color.getBProperty() <= 35;
    }

    bool IsBlue(const Color& color)
    {
        return color.getBProperty() >= 220
            && color.getRProperty() <= 35
            && color.getGProperty() <= 35;
    }

    bool IsRed(const Color& color)
    {
        return color.getRProperty() >= 220
            && color.getGProperty() <= 35
            && color.getBProperty() <= 35;
    }

    Rectangle DepthPassRect(int width, int height)
    {
        return Rectangle(width * 1 / 20, height * 5 / 16, width * 3 / 20, height / 4);
    }

    Rectangle DepthRejectRect(int width, int height)
    {
        return Rectangle(width * 5 / 20, height * 5 / 16, width * 3 / 20, height / 4);
    }

    Rectangle StencilPassRect(int width, int height)
    {
        return Rectangle(width * 9 / 20, height * 5 / 16, width * 3 / 20, height / 4);
    }

    Rectangle StencilRejectRect(int width, int height)
    {
        return Rectangle(width * 13 / 20, height * 5 / 16, width * 3 / 20, height / 4);
    }

    Rectangle AlphaProbeRect(int width, int height)
    {
        return Rectangle(width * 17 / 20, height * 5 / 16, width * 2 / 20, height / 4);
    }

    std::pair<int, int> Center(const Rectangle& rectangle)
    {
        return {
            rectangle.X + rectangle.Width / 2,
            rectangle.Y + rectangle.Height / 2
        };
    }

    float ClipDepthForFramebufferDepth(float depth)
    {
        // A homogeneous clip-depth range is [0,1]; OpenGL maps clip [-1,1] to framebuffer [0,1].
        // Converting the probe positions lets the public depth test bracket the requested clear
        // value to +/-0.01 instead of merely distinguishing it from a hardcoded 1.0.
        //
        // REMED-GFX-129 false positive: this used to read `CNA_BGFX_RENDERER` unconditionally, a
        // variable only the two Bgfx CTest routes ever set. On any OTHER backend it was absent, so
        // the OpenGL conversion was applied regardless of that backend's real convention -- and on
        // Vulkan, whose range IS [0,1], both probes then landed on the same side of the cleared
        // value (0.32 and 0.36 against a 0.67 clear), so the "upper bracket rejects" half of every
        // depth-clearing case could never hold. Measured, not assumed: with the correct convention
        // the same probes bracket the value, and with the wrong one they do not.
#if defined(CNA_BACKEND_BGFX)
        const char* route = std::getenv("CNA_BGFX_RENDERER");
        const bool homogeneousDepth = route != nullptr && std::string(route) == "VULKAN";
#elif defined(CNA_BACKEND_VULKAN)
        const bool homogeneousDepth = true;
#else
        const bool homogeneousDepth = false;
#endif
        return homogeneousDepth ? depth : depth * 2.0f - 1.0f;
    }
}

class BgfxGraphicsDeviceClearOptionsTest final : public Game
{
    enum class Suite
    {
        Backbuffer,
        OrderedClearsTest,
        TargetDepthStencil,
        TargetDepthOnly,
        TargetDepthless,
        IsolationPrepare,
        IsolationTest,
        IsolationObserve,
        Done
    };

    enum class CaseStage
    {
        Prepare,
        Test,
        Observe
    };

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<BasicEffect> effect_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<RenderTarget2D> depthStencilTarget_;
    std::unique_ptr<RenderTarget2D> depthOnlyTarget_;
    std::unique_ptr<RenderTarget2D> depthlessTarget_;
    std::unique_ptr<RenderTarget2D> targetA_;
    std::unique_ptr<RenderTarget2D> targetB_;

    Suite suite_ = Suite::Backbuffer;
    CaseStage caseStage_ = CaseStage::Prepare;
    std::size_t caseIndex_ = 0;
    int passed_ = 0;
    int failed_ = 0;
    bool depthlessInvalidDepthChecked_ = false;

    static RasterizerState ExplicitRasterizer(bool scissorEnabled)
    {
        RasterizerState state;
        state.setCullModeProperty(CullMode::None);
        state.setFillModeProperty(FillMode::Solid);
        state.setScissorTestEnableProperty(scissorEnabled);
        state.setDepthBiasProperty(0.0f);
        state.setSlopeScaleDepthBiasProperty(0.0f);
        return state;
    }

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (condition)
            ++passed_;
        else
            ++failed_;
    }

    void CheckColor(const Color& actual, const Color& expected, const std::string& label)
    {
        const bool ok = NearRgb(actual, expected);
        std::printf("[%s] %s: got=(%d,%d,%d,%d), expected RGB=(%d,%d,%d)\n",
                    ok ? "PASS" : "FAIL", label.c_str(),
                    actual.getRProperty(), actual.getGProperty(),
                    actual.getBProperty(), actual.getAProperty(),
                    expected.getRProperty(), expected.getGProperty(), expected.getBProperty());
        if (ok)
            ++passed_;
        else
            ++failed_;
    }

    void ApplyEffect()
    {
        effect_->setWorldProperty(Matrix::getIdentityProperty());
        effect_->setViewProperty(Matrix::getIdentityProperty());
        effect_->setProjectionProperty(Matrix::getIdentityProperty());
        effect_->VertexColorEnabled = true;
        effect_->Apply();
    }

    void ApplyFullRasterState(GraphicsDevice& device, int width, int height)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(ExplicitRasterizer(true));
        device.setViewportProperty(Viewport(0, 0, width, height));
        device.setScissorRectangleProperty(Rectangle(0, 0, width, height));
    }

    void ApplyRestrictedClearState(GraphicsDevice& device, int width, int height)
    {
        device.setBlendStateProperty(BlendState::Opaque);

        DepthStencilState restrictiveDepthStencil;
        restrictiveDepthStencil.setDepthBufferEnableProperty(true);
        restrictiveDepthStencil.setDepthBufferWriteEnableProperty(false);
        restrictiveDepthStencil.setDepthBufferFunctionProperty(CompareFunction::Never);
        restrictiveDepthStencil.setStencilEnableProperty(true);
        restrictiveDepthStencil.setStencilFunctionProperty(CompareFunction::Never);
        restrictiveDepthStencil.setStencilWriteMaskProperty(0);
        device.setDepthStencilStateProperty(restrictiveDepthStencil);

        device.setRasterizerStateProperty(ExplicitRasterizer(true));
        Viewport restricted(width / 4, height / 4, width / 2, height / 2);
        restricted.setMinDepthProperty(0.11f);
        restricted.setMaxDepthProperty(kSingleColorDepth);
        device.setViewportProperty(restricted);
        device.setScissorRectangleProperty(
            Rectangle(width * 3 / 8, height * 3 / 8, width / 4, height / 4));
    }

    void DrawRect(GraphicsDevice& device, const Rectangle& rectangle,
                  int targetWidth, int targetHeight, float depth, const Color& color)
    {
        const float left = 2.0f * static_cast<float>(rectangle.X)
            / static_cast<float>(targetWidth) - 1.0f;
        const float right = 2.0f * static_cast<float>(rectangle.X + rectangle.Width)
            / static_cast<float>(targetWidth) - 1.0f;
        const float top = 1.0f - 2.0f * static_cast<float>(rectangle.Y)
            / static_cast<float>(targetHeight);
        const float bottom = 1.0f - 2.0f * static_cast<float>(rectangle.Y + rectangle.Height)
            / static_cast<float>(targetHeight);

        const VertexPositionColor vertices[6] = {
            {Vector3(left, top, depth), color},
            {Vector3(right, top, depth), color},
            {Vector3(left, bottom, depth), color},
            {Vector3(left, bottom, depth), color},
            {Vector3(right, top, depth), color},
            {Vector3(right, bottom, depth), color},
        };
        ApplyEffect();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    void StampBaseline(GraphicsDevice& device, int width, int height,
                       bool hasDepth, bool hasStencil,
                       const Color& color = kBaselineColor,
                       float depth = kBaselineDepth,
                       int stencil = kBaselineStencil)
    {
        ApplyFullRasterState(device, width, height);
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer | ClearOptions::Stencil,
                     kSeedColor, kSeedDepth, kSeedStencil);

        DepthStencilState stamp;
        stamp.setDepthBufferEnableProperty(hasDepth);
        stamp.setDepthBufferWriteEnableProperty(hasDepth);
        stamp.setDepthBufferFunctionProperty(CompareFunction::Always);
        stamp.setStencilEnableProperty(hasStencil);
        stamp.setStencilFunctionProperty(CompareFunction::Always);
        stamp.setStencilPassProperty(StencilOperation::Replace);
        stamp.setStencilFailProperty(StencilOperation::Replace);
        stamp.setStencilDepthBufferFailProperty(StencilOperation::Replace);
        stamp.setStencilMaskProperty(0xff);
        stamp.setStencilWriteMaskProperty(0xff);
        stamp.setReferenceStencilProperty(stencil);
        device.setDepthStencilStateProperty(stamp);
        device.setBlendStateProperty(BlendState::Opaque);
        DrawRect(device, Rectangle(0, 0, width, height), width, height, depth, color);
    }

    bool InvokeClear(GraphicsDevice& device, const ClearCase& clearCase)
    {
        try
        {
            switch (clearCase.invocation)
            {
            case Invocation::Options:
                device.Clear(clearCase.options, kRequestedColor,
                             kRequestedDepth, kRequestedStencil);
                break;
            case Invocation::FloatColor:
                device.Clear(
                    static_cast<float>(kRequestedColor.getRProperty()) / 255.0f,
                    static_cast<float>(kRequestedColor.getGProperty()) / 255.0f,
                    static_cast<float>(kRequestedColor.getBProperty()) / 255.0f,
                    static_cast<float>(kRequestedColor.getAProperty()) / 255.0f);
                break;
            case Invocation::ColorAndDepth:
                device.Clear(kRequestedColor, kRequestedDepth);
                break;
            case Invocation::ColorAll:
                device.Clear(kRequestedColor);
                break;
            }
            return true;
        }
        catch (const std::exception& exception)
        {
            std::printf("[FAIL] %s threw: %s\n", clearCase.name, exception.what());
            ++failed_;
            return false;
        }
    }

    void DrawDepthProbes(GraphicsDevice& device, int width, int height,
                         float expectedDepth, bool depthCameFromClear)
    {
        DepthStencilState depthTest;
        depthTest.setDepthBufferEnableProperty(true);
        depthTest.setDepthBufferWriteEnableProperty(false);
        depthTest.setDepthBufferFunctionProperty(CompareFunction::Less);
        depthTest.setStencilEnableProperty(false);
        device.setDepthStencilStateProperty(depthTest);
        device.setBlendStateProperty(BlendState::Opaque);

        // A depth written by the identity-projection draw is comparable against nearby identity
        // depths on every route. A native clear value is already in framebuffer-depth space, so
        // convert the +/-0.01 framebuffer probes back to each route's clip-depth convention.
        // This tightly verifies the caller's non-default value rather than only detecting the old
        // hardcoded 1.0 clear.
        const float passingDepth = depthCameFromClear
            ? ClipDepthForFramebufferDepth(expectedDepth - 0.01f)
            : expectedDepth - 0.01f;
        const float rejectingDepth = depthCameFromClear
            ? ClipDepthForFramebufferDepth(expectedDepth + 0.01f)
            : expectedDepth + 0.01f;
        DrawRect(device, DepthPassRect(width, height), width, height,
                 passingDepth, kGreen);
        DrawRect(device, DepthRejectRect(width, height), width, height,
                 rejectingDepth, kRed);
    }

    void DrawStencilProbes(GraphicsDevice& device, int width, int height,
                           int expectedStencil, int wrongStencil)
    {
        DepthStencilState stencilTest;
        stencilTest.setDepthBufferEnableProperty(false);
        stencilTest.setDepthBufferWriteEnableProperty(false);
        stencilTest.setStencilEnableProperty(true);
        stencilTest.setStencilFunctionProperty(CompareFunction::Equal);
        stencilTest.setStencilPassProperty(StencilOperation::Keep);
        stencilTest.setStencilFailProperty(StencilOperation::Keep);
        stencilTest.setStencilDepthBufferFailProperty(StencilOperation::Keep);
        stencilTest.setStencilMaskProperty(0xff);
        stencilTest.setStencilWriteMaskProperty(0xff);
        stencilTest.setReferenceStencilProperty(expectedStencil);
        device.setDepthStencilStateProperty(stencilTest);
        device.setBlendStateProperty(BlendState::Opaque);
        DrawRect(device, StencilPassRect(width, height), width, height, 0.5f, kBlue);

        stencilTest.setReferenceStencilProperty(wrongStencil);
        device.setDepthStencilStateProperty(stencilTest);
        DrawRect(device, StencilRejectRect(width, height), width, height, 0.5f, kRed);
    }

    void DrawAlphaProbe(GraphicsDevice& device, int width, int height)
    {
        BlendState destinationAlpha;
        destinationAlpha.setColorSourceBlendProperty(Blend::DestinationAlpha);
        destinationAlpha.setColorDestinationBlendProperty(Blend::Zero);
        destinationAlpha.setColorBlendFunctionProperty(BlendFunction::Add);
        destinationAlpha.setAlphaSourceBlendProperty(Blend::One);
        destinationAlpha.setAlphaDestinationBlendProperty(Blend::Zero);
        destinationAlpha.setAlphaBlendFunctionProperty(BlendFunction::Add);
        destinationAlpha.setColorWriteChannelsProperty(ColorWriteChannels::All);
        device.setBlendStateProperty(destinationAlpha);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        DrawRect(device, AlphaProbeRect(width, height), width, height, 0.5f, kWhite);
    }

    struct ExpectedState
    {
        Color color;
        float depth;
        int stencil;
    };

    ExpectedState ExpectedFor(const ClearCase& clearCase,
                              bool hasDepth, bool hasStencil) const
    {
        ExpectedState result{
            clearCase.clearsColor ? kRequestedColor : kBaselineColor,
            kBaselineDepth,
            kBaselineStencil
        };
        if (hasDepth && clearCase.clearsDepth)
            result.depth = clearCase.invocation == Invocation::ColorAll
                ? kSingleColorDepth
                : kRequestedDepth;
        if (hasStencil && clearCase.clearsStencil)
            result.stencil = clearCase.invocation == Invocation::ColorAll
                ? 0
                : kRequestedStencil;
        return result;
    }

    ExpectedState RenderRequestedCase(GraphicsDevice& device, const ClearCase& clearCase,
                                      int width, int height,
                                      bool hasDepth, bool hasStencil,
                                      bool drawAlphaProbe)
    {
        ApplyRestrictedClearState(device, width, height);
        const bool invoked = InvokeClear(device, clearCase);
        Check(invoked, std::string(clearCase.name) + " / public invocation accepted");

        const ExpectedState expected = ExpectedFor(clearCase, hasDepth, hasStencil);
        ApplyFullRasterState(device, width, height);
        if (hasDepth)
            DrawDepthProbes(
                device, width, height, expected.depth, clearCase.clearsDepth);
        if (hasStencil)
        {
            const int wrongStencil = expected.stencil == kBaselineStencil
                ? kRequestedStencil
                : kBaselineStencil;
            DrawStencilProbes(device, width, height, expected.stencil, wrongStencil);
        }
        if (drawAlphaProbe)
            DrawAlphaProbe(device, width, height);
        return expected;
    }

    std::vector<Color> ReadWholeBackbuffer(GraphicsDevice& device)
    {
        std::vector<Color> pixels(
            static_cast<std::size_t>(kBackbufferWidth) * kBackbufferHeight,
            Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, kBackbufferWidth, kBackbufferHeight);
        device.GetBackBufferData(
            &whole, pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels;
    }

    static const Color& Pixel(const std::vector<Color>& pixels, int x, int y)
    {
        return pixels[static_cast<std::size_t>(y) * kBackbufferWidth + x];
    }

    void VerifyCase(const std::vector<Color>& pixels, const ClearCase& clearCase,
                    const ExpectedState& expected,
                    int width, int height, int offsetX,
                    bool hasDepth, bool hasStencil, bool alphaWasProbed,
                    const char* targetName)
    {
        const std::string prefix = std::string(targetName) + " / " + clearCase.name;
        CheckColor(Pixel(pixels, offsetX + 2, 2), expected.color,
                   prefix + " / colour at top-left outside clear viewport");
        CheckColor(Pixel(pixels, offsetX + width - 3, height - 3), expected.color,
                   prefix + " / colour at bottom-right outside clear viewport");

        if (hasDepth)
        {
            const auto passPoint = Center(DepthPassRect(width, height));
            const auto rejectPoint = Center(DepthRejectRect(width, height));
            Check(IsGreen(Pixel(pixels, offsetX + passPoint.first, passPoint.second)),
                  prefix + " / depth lower bracket passes");
            CheckColor(Pixel(pixels, offsetX + rejectPoint.first, rejectPoint.second),
                       expected.color, prefix + " / depth upper bracket rejects");
        }

        if (hasStencil)
        {
            const auto passPoint = Center(StencilPassRect(width, height));
            const auto rejectPoint = Center(StencilRejectRect(width, height));
            Check(IsBlue(Pixel(pixels, offsetX + passPoint.first, passPoint.second)),
                  prefix + " / exact stencil value passes");
            CheckColor(Pixel(pixels, offsetX + rejectPoint.first, rejectPoint.second),
                       expected.color, prefix + " / distinct stencil value rejects");
        }

        if (alphaWasProbed)
        {
            const auto alphaPoint = Center(AlphaProbeRect(width, height));
            const Color& actual = Pixel(
                pixels, offsetX + alphaPoint.first, alphaPoint.second);
            const int expectedAlpha = expected.color.getAProperty();
            const bool alphaOk =
                std::abs(actual.getRProperty() - expectedAlpha) <= 6
                && std::abs(actual.getGProperty() - expectedAlpha) <= 6
                && std::abs(actual.getBProperty() - expectedAlpha) <= 6;
            Check(alphaOk, prefix + " / clear alpha preserved via DestinationAlpha probe");
        }
    }

    void BlitTargets(GraphicsDevice& device, RenderTarget2D& first,
                     RenderTarget2D* second = nullptr)
    {
        device.Clear(ClearOptions::Target, kBlack, 1.0f, 0);
        ApplyFullRasterState(device, kBackbufferWidth, kBackbufferHeight);

        SamplerState sampler = SamplerState::PointClamp;
        DepthStencilState depth = DepthStencilState::None;
        RasterizerState rasterizer = ExplicitRasterizer(true);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                            &sampler, &depth, &rasterizer, nullptr,
                            Matrix::getIdentityProperty());
        spriteBatch_->Draw(
            first, Rectangle(0, 0, kTargetWidth, kTargetHeight),
            Rectangle(0, 0, kTargetWidth, kTargetHeight), Color::White);
        if (second)
        {
            spriteBatch_->Draw(
                *second, Rectangle(kTargetWidth, 0, kTargetWidth, kTargetHeight),
                Rectangle(0, 0, kTargetWidth, kTargetHeight), Color::White);
        }
        spriteBatch_->End();
    }

    const ClearCase& CurrentBackbufferCase() const
    {
        if (caseIndex_ < kOptionCases.size())
            return kOptionCases[caseIndex_];
        return kOverloadCases[caseIndex_ - kOptionCases.size()];
    }

    void AdvanceBackbufferCase()
    {
        ++caseIndex_;
        caseStage_ = CaseStage::Prepare;
        if (caseIndex_ == kOptionCases.size() + kOverloadCases.size())
        {
            caseIndex_ = 0;
            suite_ = Suite::OrderedClearsTest;
        }
    }

    void RunBackbufferSuite(GraphicsDevice& device)
    {
        const ClearCase& clearCase = CurrentBackbufferCase();
        // Backbuffer contents are not guaranteed to survive swapchain presentation. Establish the
        // distinct pre-existing values earlier in THIS frame, then exercise bgfx's ordered view
        // segmentation and take one snapshot of the resulting frame.
        StampBaseline(
            device, kBackbufferWidth, kBackbufferHeight, true, true);
        const ExpectedState expected = RenderRequestedCase(
            device, clearCase, kBackbufferWidth, kBackbufferHeight,
            true, true, false);
        const std::vector<Color> pixels = ReadWholeBackbuffer(device);
        VerifyCase(pixels, clearCase, expected,
                   kBackbufferWidth, kBackbufferHeight, 0,
                   true, true, false, "backbuffer");
        AdvanceBackbufferCase();
    }

    void RunOrderedClearTest(GraphicsDevice& device)
    {
        StampBaseline(
            device, kBackbufferWidth, kBackbufferHeight, true, true);
        ApplyFullRasterState(device, kBackbufferWidth, kBackbufferHeight);
        const Rectangle marker(
            kBackbufferWidth * 2 / 20, kBackbufferHeight * 11 / 16,
            kBackbufferWidth * 3 / 20, kBackbufferHeight / 5);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        DrawRect(device, marker, kBackbufferWidth, kBackbufferHeight, 0.5f, kRed);

        device.Clear(ClearOptions::DepthBuffer, kRequestedColor,
                     kRequestedDepth, kRequestedStencil);
        DepthStencilState depthTest;
        depthTest.setDepthBufferEnableProperty(true);
        depthTest.setDepthBufferWriteEnableProperty(false);
        depthTest.setDepthBufferFunctionProperty(CompareFunction::Less);
        device.setDepthStencilStateProperty(depthTest);
        const Rectangle depthMarker(
            kBackbufferWidth * 8 / 20, kBackbufferHeight * 11 / 16,
            kBackbufferWidth * 3 / 20, kBackbufferHeight / 5);
        DrawRect(device, depthMarker, kBackbufferWidth, kBackbufferHeight,
                 0.0f, kGreen);

        device.Clear(ClearOptions::Stencil, kRequestedColor,
                     0.42f, kRequestedStencil);
        DepthStencilState stencilTest;
        stencilTest.setDepthBufferEnableProperty(false);
        stencilTest.setStencilEnableProperty(true);
        stencilTest.setStencilFunctionProperty(CompareFunction::Equal);
        stencilTest.setStencilPassProperty(StencilOperation::Keep);
        stencilTest.setReferenceStencilProperty(kRequestedStencil);
        device.setDepthStencilStateProperty(stencilTest);
        const Rectangle stencilMarker(
            kBackbufferWidth * 14 / 20, kBackbufferHeight * 11 / 16,
            kBackbufferWidth * 3 / 20, kBackbufferHeight / 5);
        DrawRect(device, stencilMarker, kBackbufferWidth, kBackbufferHeight,
                 0.5f, kBlue);

        const std::vector<Color> pixels = ReadWholeBackbuffer(device);
        const auto markerPoint = Center(marker);
        const auto depthPoint = Center(depthMarker);
        const auto stencilPoint = Center(stencilMarker);
        Check(IsRed(Pixel(pixels, markerPoint.first, markerPoint.second)),
              "ordered clears / colour drawn before depth-only and stencil-only clears survives");
        Check(IsGreen(Pixel(pixels, depthPoint.first, depthPoint.second)),
              "ordered clears / depth-only clear executes before its depth-tested draw");
        Check(IsBlue(Pixel(pixels, stencilPoint.first, stencilPoint.second)),
              "ordered clears / stencil-only clear executes before its stencil-tested draw");
        CheckColor(Pixel(pixels, 2, 2), kBaselineColor,
                   "ordered clears / untouched colour remains the pre-existing colour");
        suite_ = Suite::TargetDepthStencil;
        caseIndex_ = 0;
        caseStage_ = CaseStage::Prepare;
    }

    RenderTarget2D& TargetForSuite()
    {
        switch (suite_)
        {
        case Suite::TargetDepthStencil:
            return *depthStencilTarget_;
        case Suite::TargetDepthOnly:
            return *depthOnlyTarget_;
        case Suite::TargetDepthless:
            return *depthlessTarget_;
        default:
            return *depthStencilTarget_;
        }
    }

    bool SuiteHasDepth() const
    {
        return suite_ != Suite::TargetDepthless;
    }

    bool SuiteHasStencil() const
    {
        return suite_ == Suite::TargetDepthStencil;
    }

    const char* SuiteTargetName() const
    {
        switch (suite_)
        {
        case Suite::TargetDepthStencil:
            return "RenderTarget2D Depth24Stencil8";
        case Suite::TargetDepthOnly:
            return "RenderTarget2D Depth24";
        case Suite::TargetDepthless:
            return "RenderTarget2D None";
        default:
            return "RenderTarget2D";
        }
    }

    void AdvanceTargetSuite()
    {
        ++caseIndex_;
        caseStage_ = CaseStage::Prepare;
        if (caseIndex_ != kOptionCases.size())
            return;

        caseIndex_ = 0;
        if (suite_ == Suite::TargetDepthStencil)
            suite_ = Suite::TargetDepthOnly;
        else if (suite_ == Suite::TargetDepthOnly)
            suite_ = Suite::TargetDepthless;
        else
            suite_ = Suite::IsolationPrepare;
    }

    void CheckDepthlessInvalidDepth(GraphicsDevice& device)
    {
        if (depthlessInvalidDepthChecked_)
            return;
        depthlessInvalidDepthChecked_ = true;
        bool threw = false;
        try
        {
            device.Clear(ClearOptions::DepthBuffer, kRequestedColor, -0.01f, 0);
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        Check(threw,
              "RenderTarget2D None / invalid requested depth is validated before unavailable-buffer masking");
    }

    void RunTargetSuite(GraphicsDevice& device)
    {
        RenderTarget2D& target = TargetForSuite();
        const bool hasDepth = SuiteHasDepth();
        const bool hasStencil = SuiteHasStencil();
        const ClearCase& clearCase = kOptionCases[caseIndex_];

        if (caseStage_ == CaseStage::Prepare)
        {
            device.SetRenderTarget(&target);
            StampBaseline(
                device, kTargetWidth, kTargetHeight, hasDepth, hasStencil);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            caseStage_ = CaseStage::Test;
            return;
        }

        if (caseStage_ == CaseStage::Test)
        {
            device.SetRenderTarget(&target);
            if (!hasDepth)
                CheckDepthlessInvalidDepth(device);
            RenderRequestedCase(
                device, clearCase, kTargetWidth, kTargetHeight,
                hasDepth, hasStencil, true);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            caseStage_ = CaseStage::Observe;
            return;
        }

        BlitTargets(device, target);
        const std::vector<Color> pixels = ReadWholeBackbuffer(device);
        const ExpectedState expected = ExpectedFor(
            clearCase, hasDepth, hasStencil);
        VerifyCase(pixels, clearCase, expected,
                   kTargetWidth, kTargetHeight, 0,
                   hasDepth, hasStencil, true, SuiteTargetName());
        AdvanceTargetSuite();
    }

    void RunIsolationPrepare(GraphicsDevice& device)
    {
        device.SetRenderTarget(targetA_.get());
        StampBaseline(device, kTargetWidth, kTargetHeight, true, true,
                      Color(43, 101, 157, 149), 0.19f, 0x35);
        device.SetRenderTarget(targetB_.get());
        StampBaseline(device, kTargetWidth, kTargetHeight, true, true,
                      Color(151, 67, 29, 213), 0.31f, 0x47);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        suite_ = Suite::IsolationTest;
    }

    void DrawIsolationProbes(GraphicsDevice& device, int width, int height,
                             float expectedDepth, bool depthCameFromClear,
                             int expectedStencil)
    {
        DrawDepthProbes(
            device, width, height, expectedDepth, depthCameFromClear);
        DrawStencilProbes(
            device, width, height, expectedStencil,
            expectedStencil == 0x35 ? 0x47 : 0x35);
    }

    void RunIsolationTest(GraphicsDevice& device)
    {
        const Color colorA(17, 191, 83, 173);
        const Color colorB(181, 37, 139, 109);

        device.SetRenderTarget(targetA_.get());
        ApplyRestrictedClearState(device, kTargetWidth, kTargetHeight);
        device.Clear(ClearOptions::Target, colorA, 0.88f, 0x7a);

        device.SetRenderTarget(targetB_.get());
        ApplyRestrictedClearState(device, kTargetWidth, kTargetHeight);
        device.Clear(ClearOptions::Target, colorB, 0.77f, 0x6b);

        // A -> B -> A: the depth clear must apply to A only and must not redirect either target's
        // already-recorded clear through a reused bgfx view.
        device.SetRenderTarget(targetA_.get());
        ApplyRestrictedClearState(device, kTargetWidth, kTargetHeight);
        device.Clear(ClearOptions::DepthBuffer, kRequestedColor, 0.59f, 0x5c);
        ApplyFullRasterState(device, kTargetWidth, kTargetHeight);
        DrawIsolationProbes(
            device, kTargetWidth, kTargetHeight, 0.59f, true, 0x35);

        device.SetRenderTarget(targetB_.get());
        ApplyRestrictedClearState(device, kTargetWidth, kTargetHeight);
        device.Clear(ClearOptions::Stencil, kRequestedColor, 0.84f, 0x93);
        ApplyFullRasterState(device, kTargetWidth, kTargetHeight);
        DrawIsolationProbes(
            device, kTargetWidth, kTargetHeight, 0.31f, false, 0x93);

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        suite_ = Suite::IsolationObserve;
    }

    void RunIsolationObserve(GraphicsDevice& device)
    {
        const Color colorA(17, 191, 83, 173);
        const Color colorB(181, 37, 139, 109);
        BlitTargets(device, *targetA_, targetB_.get());
        const std::vector<Color> pixels = ReadWholeBackbuffer(device);

        const auto depthPass = Center(DepthPassRect(kTargetWidth, kTargetHeight));
        const auto stencilPass = Center(StencilPassRect(kTargetWidth, kTargetHeight));
        CheckColor(Pixel(pixels, 2, 2), colorA,
                   "target isolation A->B->A / A colour belongs only to A");
        CheckColor(Pixel(pixels, kTargetWidth + 2, 2), colorB,
                   "target isolation A->B->A / B colour belongs only to B");
        Check(IsGreen(Pixel(pixels, depthPass.first, depthPass.second)),
              "target isolation A->B->A / A receives its later depth-only clear");
        Check(IsBlue(Pixel(pixels, stencilPass.first, stencilPass.second)),
              "target isolation A->B->A / A preserves its stencil");
        Check(IsGreen(Pixel(
                  pixels, kTargetWidth + depthPass.first, depthPass.second)),
              "target isolation A->B->A / B preserves its depth");
        Check(IsBlue(Pixel(
                  pixels, kTargetWidth + stencilPass.first, stencilPass.second)),
              "target isolation A->B->A / B receives its stencil-only clear");

        suite_ = Suite::Done;
        std::printf("=== GFX-018: %d/%d PASS ===\n",
                    passed_, passed_ + failed_);
        Exit();
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        effect_ = std::make_unique<BasicEffect>(device);
        spriteBatch_ = std::make_unique<SpriteBatch>(device);

        depthStencilTarget_ = std::make_unique<RenderTarget2D>(
            device, kTargetWidth, kTargetHeight, false, SurfaceFormat::Color,
            DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::PreserveContents);
        depthOnlyTarget_ = std::make_unique<RenderTarget2D>(
            device, kTargetWidth, kTargetHeight, false, SurfaceFormat::Color,
            DepthFormat::Depth24, 0, RenderTargetUsage::PreserveContents);
        depthlessTarget_ = std::make_unique<RenderTarget2D>(
            device, kTargetWidth, kTargetHeight, false, SurfaceFormat::Color,
            DepthFormat::None, 0, RenderTargetUsage::PreserveContents);
        targetA_ = std::make_unique<RenderTarget2D>(
            device, kTargetWidth, kTargetHeight, false, SurfaceFormat::Color,
            DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::PreserveContents);
        targetB_ = std::make_unique<RenderTarget2D>(
            device, kTargetWidth, kTargetHeight, false, SurfaceFormat::Color,
            DepthFormat::Depth24Stencil8, 0, RenderTargetUsage::PreserveContents);
    }

    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        switch (suite_)
        {
        case Suite::Backbuffer:
            RunBackbufferSuite(device);
            break;
        case Suite::OrderedClearsTest:
            RunOrderedClearTest(device);
            break;
        case Suite::TargetDepthStencil:
        case Suite::TargetDepthOnly:
        case Suite::TargetDepthless:
            RunTargetSuite(device);
            break;
        case Suite::IsolationPrepare:
            RunIsolationPrepare(device);
            break;
        case Suite::IsolationTest:
            RunIsolationTest(device);
            break;
        case Suite::IsolationObserve:
            RunIsolationObserve(device);
            break;
        case Suite::Done:
            break;
        }
    }

public:
    BgfxGraphicsDeviceClearOptionsTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kBackbufferWidth);
        graphics_->setPreferredBackBufferHeightProperty(kBackbufferHeight);
        graphics_->setPreferredDepthStencilFormatProperty(
            DepthFormat::Depth24Stencil8);
    }

    int GetResult() const
    {
        return failed_ == 0 ? 0 : 1;
    }
};

int main()
{
    BgfxGraphicsDeviceClearOptionsTest game;
    game.Run();
    return game.GetResult();
}
