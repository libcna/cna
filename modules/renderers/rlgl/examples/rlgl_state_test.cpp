// SPDX-License-Identifier: MS-PL
// plans/plan_rlgl.md RLGL-013: complete blend/depth/stencil/rasterizer state transitions are
// checked both as native GL snapshots and through pixels drawn by rlgl's test-only batch.

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "RlglBridge.hpp"

#include "common/PixelTestGame.hpp"
#include "common/SdlTestGraphicsServices.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 64;
    constexpr int kHeight = 48;

    constexpr int kZero = 0;
    constexpr int kOne = 1;
    constexpr int kSourceColor = 0x0300;
    constexpr int kInverseSourceColor = 0x0301;
    constexpr int kSourceAlpha = 0x0302;
    constexpr int kInverseSourceAlpha = 0x0303;
    constexpr int kDestinationAlpha = 0x0304;
    constexpr int kInverseDestinationAlpha = 0x0305;
    constexpr int kDestinationColor = 0x0306;
    constexpr int kInverseDestinationColor = 0x0307;
    constexpr int kSourceAlphaSaturation = 0x0308;
    constexpr int kBlendFactor = 0x8001;
    constexpr int kInverseBlendFactor = 0x8002;

    constexpr int kNever = 0x0200;
    constexpr int kLess = 0x0201;
    constexpr int kEqual = 0x0202;
    constexpr int kLessEqual = 0x0203;
    constexpr int kGreater = 0x0204;
    constexpr int kNotEqual = 0x0205;
    constexpr int kGreaterEqual = 0x0206;
    constexpr int kAlways = 0x0207;

    constexpr int kKeep = 0x1E00;
    constexpr int kReplace = 0x1E01;
    constexpr int kIncrement = 0x1E02;
    constexpr int kDecrement = 0x1E03;
    constexpr int kInvert = 0x150A;
    constexpr int kIncrementWrap = 0x8507;
    constexpr int kDecrementWrap = 0x8508;

    constexpr int kFunctionAdd = 0x8006;
    constexpr int kFunctionMin = 0x8007;
    constexpr int kFunctionMax = 0x8008;
    constexpr int kFunctionSubtract = 0x800A;
    constexpr int kFunctionReverseSubtract = 0x800B;
    constexpr int kFront = 0x0404;
    constexpr int kBack = 0x0405;
    constexpr int kLine = 0x1B01;
    constexpr int kFill = 0x1B02;

    [[nodiscard]] bool Near(const float left, const float right, const float epsilon = 0.001f)
    {
        return std::fabs(left - right) <= epsilon;
    }

    [[nodiscard]] bool IsBlack(const Color& color)
    {
        return color.getRProperty() <= 2 && color.getGProperty() <= 2 &&
            color.getBProperty() <= 2;
    }

    [[nodiscard]] bool IsWhite(const Color& color)
    {
        return color.getRProperty() >= 253 && color.getGProperty() >= 253 &&
            color.getBProperty() >= 253;
    }
}

class RlglStateTest final : public CNA::Examples::PixelTestGame
{
public:
    RlglStateTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kWidth);
        graphics_->setPreferredBackBufferHeightProperty(kHeight);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        graphics_->setSynchronizeWithVerticalRetraceProperty(false);
    }

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        auto& renderer = static_cast<CNA::Internal::Renderers::Rlgl::RlglRenderer&>(
            device.GetRenderer());
        namespace Bridge = CNA::Internal::Renderers::Rlgl::Bridge;

        const std::array expectedBlendFactors{
            kOne, kZero, kSourceColor, kInverseSourceColor,
            kSourceAlpha, kInverseSourceAlpha,
            kDestinationColor, kInverseDestinationColor,
            kDestinationAlpha, kInverseDestinationAlpha,
            kBlendFactor, kInverseBlendFactor, kSourceAlphaSaturation};
        CNA::Internal::Renderers::BlendWriteState allWrites;
        bool blendFactorsExact = true;
        for (int ordinal = 0; ordinal < static_cast<int>(expectedBlendFactors.size()); ++ordinal)
        {
            renderer.ApplyBlendState(ordinal, 0, 0, 0, 0, 0, allWrites);
            const auto state = Bridge::GetPipelineSnapshotForTesting();
            blendFactorsExact = blendFactorsExact && state.blendEnabled &&
                state.colorSourceBlend == expectedBlendFactors[ordinal];
        }
        Check(blendFactorsExact, "all Blend source-factor ordinals map to exact GL state");

        const std::array expectedBlendFunctions{
            kFunctionAdd, kFunctionSubtract, kFunctionReverseSubtract,
            kFunctionMax, kFunctionMin};
        bool blendFunctionsExact = true;
        for (int ordinal = 0; ordinal < static_cast<int>(expectedBlendFunctions.size()); ++ordinal)
        {
            renderer.ApplyBlendState(4, 4, 5, 5, ordinal, ordinal, allWrites);
            const auto state = Bridge::GetPipelineSnapshotForTesting();
            blendFunctionsExact = blendFunctionsExact &&
                state.colorBlendFunction == expectedBlendFunctions[ordinal] &&
                state.alphaBlendFunction == expectedBlendFunctions[ordinal];
        }
        Check(blendFunctionsExact,
              "all BlendFunction ordinals map independently to exact GL equations");

        BlendState customBlend;
        customBlend.setColorSourceBlendProperty(Blend::SourceColor);
        customBlend.setColorDestinationBlendProperty(Blend::InverseSourceColor);
        customBlend.setAlphaSourceBlendProperty(Blend::DestinationAlpha);
        customBlend.setAlphaDestinationBlendProperty(Blend::InverseDestinationAlpha);
        customBlend.setColorBlendFunctionProperty(BlendFunction::ReverseSubtract);
        customBlend.setAlphaBlendFunctionProperty(BlendFunction::Min);
        customBlend.setColorWriteChannelsProperty(
            ColorWriteChannels::Red | ColorWriteChannels::Blue);
        customBlend.setColorWriteChannels1Property(ColorWriteChannels::Green);
        customBlend.setColorWriteChannels2Property(ColorWriteChannels::Alpha);
        customBlend.setColorWriteChannels3Property(ColorWriteChannels::None);
        customBlend.setBlendFactorProperty(Color(17, 31, 47, 63));
        customBlend.setMultiSampleMaskProperty(static_cast<int>(0x13579BDFu));
        device.setBlendStateProperty(customBlend);
        const auto customBlendNative = Bridge::GetPipelineSnapshotForTesting();
        Check(customBlendNative.blendEnabled &&
                  customBlendNative.colorSourceBlend == kSourceColor &&
                  customBlendNative.colorDestinationBlend == kInverseSourceColor &&
                  customBlendNative.alphaSourceBlend == kDestinationAlpha &&
                  customBlendNative.alphaDestinationBlend == kInverseDestinationAlpha &&
                  customBlendNative.colorBlendFunction == kFunctionReverseSubtract &&
                  customBlendNative.alphaBlendFunction == kFunctionMin &&
                  customBlendNative.colorWriteMasks == std::array<int, 4>{5, 2, 8, 0} &&
                  customBlendNative.sampleMaskEnabled &&
                  customBlendNative.sampleMask == 0x13579BDFu &&
                  Near(customBlendNative.blendFactor[0], 17.0f / 255.0f) &&
                  Near(customBlendNative.blendFactor[3], 63.0f / 255.0f),
              "BlendState publishes separate factors/equations, four masks, sample mask, and factor");

        const std::array expectedCompareFunctions{
            kAlways, kNever, kLess, kLessEqual,
            kEqual, kGreaterEqual, kGreater, kNotEqual};
        bool depthFunctionsExact = true;
        for (int ordinal = 0; ordinal < static_cast<int>(expectedCompareFunctions.size()); ++ordinal)
        {
            renderer.ApplyDepthStencilState(
                true, (ordinal & 1) == 0, ordinal,
                false, 0, 0, 0, 0, 0xFF, 0xFF, 0,
                false, 0, 0, 0, 0);
            const auto state = Bridge::GetPipelineSnapshotForTesting();
            depthFunctionsExact = depthFunctionsExact && state.depthTestEnabled &&
                state.depthWriteEnabled == ((ordinal & 1) == 0) &&
                state.depthFunction == expectedCompareFunctions[ordinal] &&
                !state.stencilTestEnabled;
        }
        Check(depthFunctionsExact,
              "all CompareFunction ordinals and depth-write transitions reach GL exactly");

        const std::array expectedStencilOperations{
            kKeep, kZero, kReplace, kIncrementWrap,
            kDecrementWrap, kIncrement, kDecrement, kInvert};
        bool stencilOperationsExact = true;
        for (int ordinal = 0; ordinal < static_cast<int>(expectedStencilOperations.size()); ++ordinal)
        {
            renderer.ApplyDepthStencilState(
                false, false, 0,
                true, 0, ordinal, ordinal, ordinal,
                0x7F, 0x3F, 5, false, 0, 0, 0, 0);
            const auto state = Bridge::GetPipelineSnapshotForTesting();
            stencilOperationsExact = stencilOperationsExact && state.stencilTestEnabled &&
                state.frontStencilFail == expectedStencilOperations[ordinal] &&
                state.frontStencilDepthFail == expectedStencilOperations[ordinal] &&
                state.frontStencilPass == expectedStencilOperations[ordinal] &&
                state.backStencilFail == expectedStencilOperations[ordinal] &&
                state.backStencilDepthFail == expectedStencilOperations[ordinal] &&
                state.backStencilPass == expectedStencilOperations[ordinal];
        }
        Check(stencilOperationsExact,
              "all StencilOperation ordinals map to front/back GL operations");

        renderer.ApplyDepthStencilState(
            true, false, 6,
            true, 2, 2, 3, 4,
            0x5A, 0xA5, 7, true, 6, 7, 1, 5);
        auto twoSided = Bridge::GetPipelineSnapshotForTesting();
        const bool twoSidedExact = twoSided.depthFunction == kGreater &&
            twoSided.frontStencilFunction == kLess &&
            twoSided.frontStencilReference == 7 &&
            twoSided.frontStencilReadMask == 0x5Au &&
            twoSided.frontStencilWriteMask == 0xA5u &&
            twoSided.frontStencilFail == kIncrementWrap &&
            twoSided.frontStencilDepthFail == kDecrementWrap &&
            twoSided.frontStencilPass == kReplace &&
            twoSided.backStencilFunction == kGreater &&
            twoSided.backStencilReference == 7 &&
            twoSided.backStencilReadMask == 0x5Au &&
            twoSided.backStencilWriteMask == 0xA5u &&
            twoSided.backStencilFail == kZero &&
            twoSided.backStencilDepthFail == kIncrement &&
            twoSided.backStencilPass == kInvert;
        renderer.SetReferenceStencil(11);
        twoSided = Bridge::GetPipelineSnapshotForTesting();
        Check(twoSidedExact && twoSided.frontStencilReference == 11 &&
                  twoSided.backStencilReference == 11 &&
                  twoSided.frontStencilFunction == kLess &&
                  twoSided.backStencilFunction == kGreater,
              "two-sided stencil and standalone reference changes preserve both face functions");

        renderer.ApplyRasterizerState(1, 1, true, 0.25f, 2.5f);
        const auto rasterA = Bridge::GetPipelineSnapshotForTesting();
        renderer.ApplyRasterizerState(2, 0, false, 0.0f, 0.0f);
        const auto rasterB = Bridge::GetPipelineSnapshotForTesting();
        Check(rasterA.cullEnabled && rasterA.cullFace == kBack &&
                  rasterA.scissorEnabled && rasterA.polygonMode == kLine &&
                  rasterA.polygonOffsetFillEnabled &&
                  Near(rasterA.polygonOffsetFactor, 2.5f) &&
                  rasterA.polygonOffsetUnits > 1.0f &&
                  rasterB.cullEnabled && rasterB.cullFace == kFront &&
                  !rasterB.scissorEnabled && rasterB.polygonMode == kFill &&
                  Near(rasterB.polygonOffsetFactor, 0.0f) &&
                  Near(rasterB.polygonOffsetUnits, 0.0f),
              "RasterizerState transitions cull face, fill, scissor, and scaled depth bias");

        Texture2D premultipliedRed(device, 1, 1);
        const Color halfRed(128, 0, 0, 128);
        premultipliedRed.SetData(&halfRed, 1);
        Texture2D white(device, 1, 1);
        const Color opaqueWhite(255, 255, 255, 255);
        white.SetData(&opaqueWhite, 1);

        const auto draw = [&](Texture2D& texture) {
            texture.GetRenderer().BindGL(0);
            renderer.ApplySamplerState(0, 1, 1, 1, 1);
            renderer.ApplySamplerMipState(0, 0, 0.0f);
            Bridge::DrawBoundTextureSampleForTesting(0.5f, 0.5f, kWidth, kHeight);
            Bridge::BindTexture2D(0, 0);
        };
        const auto readPixel = [&](const int x, const int y) {
            Color pixel;
            const Rectangle rectangle(x, y, 1, 1);
            device.GetBackBufferData(&rectangle, &pixel, 0, 1);
            return pixel;
        };

        const auto drawInset = [&] {
            white.GetRenderer().BindGL(0);
            renderer.ApplySamplerState(0, 1, 1, 1, 1);
            Bridge::DrawBoundTextureRectangleForTesting(
                0.5f, 0.5f, 8.0f, 8.0f, 48.0f, 32.0f, kWidth, kHeight);
            Bridge::BindTexture2D(0, 0);
        };

        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        renderer.ApplyRasterizerState(1, 0, false, 0.0f, 0.0f);
        device.Clear(Color(0, 0, 0, 255));
        drawInset();
        const Color clockwiseCull = readPixel(32, 18);
        renderer.ApplyRasterizerState(2, 0, false, 0.0f, 0.0f);
        device.Clear(Color(0, 0, 0, 255));
        drawInset();
        const Color counterClockwiseCull = readPixel(32, 18);
        renderer.ApplyRasterizerState(0, 1, false, 0.0f, 0.0f);
        device.Clear(Color(0, 0, 0, 255));
        drawInset();
        const Color wireInterior = readPixel(32, 18);
        std::array<Color, kWidth * kHeight> wirePixels{};
        device.GetBackBufferData(wirePixels.data(), static_cast<int>(wirePixels.size()));
        const bool wireHasFragments = std::any_of(
            wirePixels.begin(), wirePixels.end(), IsWhite);
        renderer.ApplyRasterizerState(0, 0, false, 0.0f, 0.0f);
        device.Clear(Color(0, 0, 0, 255));
        drawInset();
        Check(IsWhite(clockwiseCull) && IsBlack(counterClockwiseCull) &&
                  wireHasFragments && IsBlack(wireInterior) &&
                  IsWhite(readPixel(32, 18)),
              "cull winding and solid/wire fill modes change real rasterized pixels");

        renderer.ApplyRasterizerState(0, 0, false, 0.0f, 0.0f);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::AlphaBlend);
        device.Clear(Color(0, 0, 255, 255));
        draw(premultipliedRed);
        const Color alphaBlend = readPixel(kWidth / 2, kHeight / 2);
        device.setBlendStateProperty(BlendState::Opaque);
        device.Clear(Color(0, 0, 255, 255));
        draw(premultipliedRed);
        const Color opaque = readPixel(kWidth / 2, kHeight / 2);
        Check(alphaBlend.getRProperty() >= 126 && alphaBlend.getRProperty() <= 130 &&
                  alphaBlend.getBProperty() >= 125 && alphaBlend.getBProperty() <= 130 &&
                  alphaBlend.getAProperty() >= 253 &&
                  opaque.getRProperty() >= 126 && opaque.getRProperty() <= 130 &&
                  opaque.getBProperty() <= 2 && opaque.getAProperty() >= 126 &&
                  opaque.getAProperty() <= 130,
              "AlphaBlend uses premultiplied factors and Opaque disables blending after it");

        BlendState redOnly = BlendState::Opaque;
        redOnly.setColorWriteChannelsProperty(ColorWriteChannels::Red);
        device.setBlendStateProperty(redOnly);
        device.Clear(Color(10, 20, 30, 40));
        draw(premultipliedRed);
        const Color masked = readPixel(kWidth / 2, kHeight / 2);
        Check(masked.getRProperty() >= 126 && masked.getRProperty() <= 130 &&
                  masked.getGProperty() == 20 && masked.getBProperty() == 30 &&
                  masked.getAProperty() == 40,
              "color-write mask survives Clear restoration and restricts a real draw");

        device.setBlendStateProperty(BlendState::Opaque);
        RasterizerState clipped;
        clipped.setCullModeProperty(CullMode::None);
        clipped.setScissorTestEnableProperty(true);
        device.Clear(Color(0, 0, 0, 255));
        device.setRasterizerStateProperty(clipped);
        device.setScissorRectangleProperty(Rectangle(16, 12, 32, 24));
        draw(white);
        const Color scissorInside = readPixel(32, 24);
        const Color scissorOutside = readPixel(4, 4);
        const auto scissorNative = Bridge::GetPipelineSnapshotForTesting();
        Check(IsWhite(scissorInside) && IsBlack(scissorOutside) &&
                  scissorNative.scissorEnabled &&
                  scissorNative.scissorBox == std::array<int, 4>{16, 12, 32, 24},
              "scissor enable and top-left rectangle clip a real rlgl draw");

        clipped.setScissorTestEnableProperty(false);
        device.setRasterizerStateProperty(clipped);
        DepthStencilState depthNever;
        depthNever.setDepthBufferEnableProperty(true);
        depthNever.setDepthBufferWriteEnableProperty(false);
        depthNever.setDepthBufferFunctionProperty(CompareFunction::Never);
        device.setDepthStencilStateProperty(depthNever);
        device.Clear(Color(0, 0, 0, 255));
        renderer.ClearDepth(1.0f);
        draw(white);
        const Color rejectedByDepth = readPixel(32, 24);
        depthNever.setDepthBufferFunctionProperty(CompareFunction::Always);
        device.setDepthStencilStateProperty(depthNever);
        draw(white);
        Check(IsBlack(rejectedByDepth) && IsWhite(readPixel(32, 24)),
              "depth CompareFunction transition rejects then accepts real fragments");

        DepthStencilState stencilWriter;
        stencilWriter.setDepthBufferEnableProperty(false);
        stencilWriter.setDepthBufferWriteEnableProperty(false);
        stencilWriter.setStencilEnableProperty(true);
        stencilWriter.setStencilFunctionProperty(CompareFunction::Always);
        stencilWriter.setStencilPassProperty(StencilOperation::Replace);
        stencilWriter.setStencilMaskProperty(0xFF);
        stencilWriter.setStencilWriteMaskProperty(0xFF);
        stencilWriter.setReferenceStencilProperty(3);
        device.setDepthStencilStateProperty(stencilWriter);
        renderer.ClearStencil(0);
        device.Clear(Color(0, 0, 0, 255));
        draw(white);
        const std::uint8_t writtenStencil =
            Bridge::ReadStencilForTesting(32, 24, kHeight);
        // GraphicsDevice.Clear(Color) also clears depth and stencil, matching XNA. Clear only the
        // color plane here so the value written by the first pass survives into the read pass.
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);

        DepthStencilState stencilReader = stencilWriter;
        stencilReader.setStencilFunctionProperty(CompareFunction::Equal);
        stencilReader.setStencilPassProperty(StencilOperation::Keep);
        stencilReader.setStencilWriteMaskProperty(0);
        stencilReader.setReferenceStencilProperty(4);
        device.setDepthStencilStateProperty(stencilReader);
        draw(white);
        const Color rejectedByStencil = readPixel(32, 24);
        device.setReferenceStencilProperty(3);
        draw(white);
        const Color acceptedByStencil = readPixel(32, 24);
        const auto stencilNative = Bridge::GetPipelineSnapshotForTesting();
        Check(writtenStencil == 3 && IsBlack(rejectedByStencil) &&
                  IsWhite(acceptedByStencil) && stencilNative.stencilTestEnabled &&
                  stencilNative.frontStencilReference == 3 &&
                  stencilNative.backStencilReference == 3,
              "stencil write/read and standalone ReferenceStencil gate real fragments");
    }

private:
    std::unique_ptr<GraphicsDeviceManager> graphics_;
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    RlglStateTest game;
    game.Run();
    return game.getResultProperty();
}
