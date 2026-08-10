// SPDX-License-Identifier: MS-PL
// REMED-GFX-148: BlendState::Additive is SourceAlpha/One for colour and alpha, with Add as
// both functions. The destination is therefore never attenuated:
//
//   rgb = clamp(src.rgb * src.a + dst.rgb)
//   a   = clamp(src.a   * src.a + dst.a)
//
// The asymmetric byte values below keep every unsaturated result away from a half-byte rounding
// boundary. The same exact bytes are therefore expected from Software's truncating RGBA8 store and
// from the GPU controls' UNORM stores. Saturating cases are paired with unsaturated cases so a
// source-only equation cannot hide behind white, and the partial-alpha cases distinguish a second
// source-alpha multiplication from premultiplied AlphaBlend input.
//
// REMED-GFX-233 also makes the persistent Position+Color cases a compatibility oracle for
// VertexBuffer(device, count): that legacy convenience buffer has an empty public declaration but
// a real 16-byte typed-upload stride, which ordinary submission must not replace with zero.

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
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/NotSupportedException.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
#if defined(CNA_RENDERER_SOFTWARE)
    constexpr const char* kRendererName = "SOFTWARE";
    constexpr bool kRasterizes = true;
    constexpr bool kBackbufferReadback = true;
#elif defined(CNA_RENDERER_EASYGL)
    constexpr const char* kRendererName = "EASYGL";
    constexpr bool kRasterizes = true;
    constexpr bool kBackbufferReadback = true;
#elif defined(CNA_RENDERER_VULKAN)
    constexpr const char* kRendererName = "VULKAN";
    constexpr bool kRasterizes = true;
    constexpr bool kBackbufferReadback = true;
#elif defined(CNA_RENDERER_SDL_GPU)
    constexpr const char* kRendererName = "SDL_GPU";
    constexpr bool kRasterizes = true;
    constexpr bool kBackbufferReadback = false;
#elif defined(CNA_RENDERER_BGFX)
    constexpr const char* kRendererName = "BGFX";
    constexpr bool kRasterizes = true;
    constexpr bool kBackbufferReadback = true;
#elif defined(CNA_RENDERER_WEBGPU)
    constexpr const char* kRendererName = "WEBGPU";
    constexpr bool kRasterizes = true;
    constexpr bool kBackbufferReadback = true;
#elif defined(CNA_RENDERER_HEADLESS)
    constexpr const char* kRendererName = "HEADLESS";
    constexpr bool kRasterizes = false;
    constexpr bool kBackbufferReadback = false;
#else
#error "REMED-GFX-148: this renderer has no declared Additive control contract."
#endif

    constexpr int kW = 12;
    constexpr int kH = 8;

    const Color kDestination(11, 31, 55, 51);
    const Color kPartialSource(100, 52, 204, 64);
    const Color kPartialAdditive(36, 44, 106, 67);
    const Color kZeroDestinationAdditive(25, 13, 51, 16);
    const Color kNonPremultipliedResult(33, 36, 92, 54);
    const Color kPremultipliedSource(25, 13, 51, 64);
    const Color kAlphaBlendResult(33, 36, 92, 102);
    const Color kOpaqueResult(100, 52, 204, 64);

    std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(static_cast<int>(c.getRProperty())) + "," +
               std::to_string(static_cast<int>(c.getGProperty())) + "," +
               std::to_string(static_cast<int>(c.getBProperty())) + "," +
               std::to_string(static_cast<int>(c.getAProperty())) + ")";
    }

    bool Same(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() &&
               a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty() &&
               a.getAProperty() == b.getAProperty();
    }
}

class AdditiveBlendContractTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D white_;
    int frame_ = 0;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void CheckColor(const Color& got, const Color& expected, const std::string& label)
    {
        Check(Same(got, expected), label + " expected " + ColorText(expected) + " got " + ColorText(got));
    }

    static void SetOrdinaryDrawState(GraphicsDevice& dev)
    {
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
    }

    void DrawSprite(GraphicsDevice& dev, const BlendState& state, const Color& source,
                    const Rectangle& destination = Rectangle(0, 0, kW, kH))
    {
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        sb.Begin(SpriteSortMode::Deferred, state, &point, &noDepth, &noCull);
        sb.Draw(white_, destination, Rectangle(0, 0, 1, 1), source);
        sb.End();
    }

    static Color ReadTarget(RenderTarget2D& target, int x = kW / 2, int y = kH / 2)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kW) * kH, Color(0xCD, 0xCD, 0xCD, 0xCD));
        target.GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels[static_cast<std::size_t>(y) * kW + x];
    }

    Color RunSpriteTarget(GraphicsDevice& dev, const BlendState& state, const Color& destination,
                          const Color& source, int draws = 1)
    {
        RenderTarget2D target(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&target);
        SetOrdinaryDrawState(dev);
        dev.Clear(destination);
        for (int i = 0; i < draws; ++i) DrawSprite(dev, state, source);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return ReadTarget(target);
    }

    enum class DirectPath
    {
        DrawPrimitives,
        DrawIndexedPrimitives,
        DrawUserPrimitives,
        DrawUserIndexedPrimitives,
    };

    void DrawDirect(GraphicsDevice& dev, DirectPath path, const Color& source,
                    bool* usedLegacyEmptyDeclaration = nullptr)
    {
        const VertexPositionColor vertices[3] = {
            {Vector3(-2.0f,  2.0f, 0.5f), source},
            {Vector3(-2.0f, -2.0f, 0.5f), source},
            {Vector3( 2.0f,  0.0f, 0.5f), source},
        };
        const std::uint16_t indices[3] = {0, 1, 2};

        BasicEffect effect(dev);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.VertexColorEnabled = true;
        effect.Apply();

        if (path == DirectPath::DrawUserPrimitives)
        {
            dev.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 1);
            return;
        }
        if (path == DirectPath::DrawUserIndexedPrimitives)
        {
            dev.DrawUserIndexedPrimitives(PrimitiveType::TriangleList,
                                          vertices, 0, 3, indices, 0, 1);
            return;
        }

        VertexBuffer vb(dev, 3);
        if (usedLegacyEmptyDeclaration != nullptr)
        {
            const VertexDeclaration& declaration = vb.getVertexDeclarationProperty();
            *usedLegacyEmptyDeclaration = declaration.GetVertexElements().empty() &&
                                          declaration.getVertexStrideProperty() == 0;
        }
        vb.SetData(vertices, 3);
        dev.SetVertexBuffer(&vb);
        if (path == DirectPath::DrawPrimitives)
        {
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        }
        else
        {
            IndexBuffer ib(dev, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            ib.SetData(indices, 0, 3);
            dev.SetIndexBuffer(&ib);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 3, 0, 1);
            dev.SetIndexBuffer(nullptr);
        }
        dev.SetVertexBuffer(nullptr);
    }

    Color RunDirectTarget(GraphicsDevice& dev, DirectPath path,
                          bool* usedLegacyEmptyDeclaration = nullptr)
    {
        RenderTarget2D target(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&target);
        SetOrdinaryDrawState(dev);
        dev.Clear(kDestination);
        dev.setBlendStateProperty(BlendState::Additive);
        DrawDirect(dev, path, kPartialSource, usedLegacyEmptyDeclaration);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        return ReadTarget(target);
    }

    void ContractDefinitions()
    {
        const BlendState& a = BlendState::Additive;
        Check(a.getColorSourceBlendProperty() == Blend::SourceAlpha &&
              a.getColorDestinationBlendProperty() == Blend::One &&
              a.getAlphaSourceBlendProperty() == Blend::SourceAlpha &&
              a.getAlphaDestinationBlendProperty() == Blend::One &&
              a.getColorBlendFunctionProperty() == BlendFunction::Add &&
              a.getAlphaBlendFunctionProperty() == BlendFunction::Add,
              "A1 public Additive definition is SourceAlpha/One + Add separately for colour and alpha");
    }

    void HeadlessControl(GraphicsDevice& dev)
    {
        RenderTarget2D target(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::DiscardContents);
        dev.SetRenderTarget(&target);
        dev.Clear(kDestination);
        DrawSprite(dev, BlendState::Additive, kPartialSource);
        dev.setBlendStateProperty(BlendState::Opaque);
        dev.setBlendStateProperty(BlendState::Additive);
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        bool rejected = false;
        try
        {
            (void) ReadTarget(target);
        }
        catch (const System::NotSupportedException&)
        {
            rejected = true;
        }
        Check(rejected, "H1 same Additive -> Opaque -> Additive sequence is honestly non-rasterizing");
    }

    void Matrix(GraphicsDevice& dev)
    {
        // Source/destination factor and alpha-channel matrix.
        CheckColor(RunSpriteTarget(dev, BlendState::Additive, kDestination,
                                   Color(61, 29, 87, 255)),
                   Color(72, 60, 142, 255),
                   "B1 opaque source adds the nonzero destination");
        CheckColor(RunSpriteTarget(dev, BlendState::Additive, kDestination, kPartialSource),
                   kPartialAdditive,
                   "B2 partial source applies SourceAlpha once and adds the full destination");
        CheckColor(RunSpriteTarget(dev, BlendState::Additive, kDestination,
                                   Color(197, 83, 41, 0)),
                   kDestination,
                   "B3 zero-alpha source contributes zero to colour and alpha");
        CheckColor(RunSpriteTarget(dev, BlendState::Additive, kDestination,
                                   Color(61, 29, 87, 255)),
                   Color(72, 60, 142, 255),
                   "B4 full-alpha source contributes its complete RGBA value");
        CheckColor(RunSpriteTarget(dev, BlendState::Additive, Color(0, 0, 0, 0), kPartialSource),
                   kZeroDestinationAdditive,
                   "B5 zero destination exposes source RGB and alpha factors independently");
        CheckColor(RunSpriteTarget(dev, BlendState::Additive, Color(241, 247, 233, 247),
                                   kPartialSource),
                   Color(255, 255, 255, 255),
                   "B6 saturated destination clamps after the complete equation");

        // Repetition and ordering use unsaturated channels so conversion between draws is visible.
        CheckColor(RunSpriteTarget(dev, BlendState::Additive, kDestination, kPartialSource, 2),
                   Color(61, 57, 157, 83),
                   "C1 two Additive draws accumulate the stored destination twice");
        {
            RenderTarget2D target(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&target);
            dev.Clear(kDestination);
            DrawSprite(dev, BlendState::Additive, kPartialSource);
            DrawSprite(dev, BlendState::Additive, Color(54, 168, 90, 85));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            CheckColor(ReadTarget(target), Color(54, 100, 136, 95),
                       "C2 different source colours accumulate in public draw order");
        }

        // Preset comparison pins premultiplied versus non-premultiplied interpretation.
        CheckColor(RunSpriteTarget(dev, BlendState::Additive, kDestination, kPartialSource),
                   kPartialAdditive, "D1 Additive result");
        CheckColor(RunSpriteTarget(dev, BlendState::AlphaBlend, kDestination,
                                   kPremultipliedSource),
                   kAlphaBlendResult, "D2 AlphaBlend consumes premultiplied RGB without multiplying it again");
        CheckColor(RunSpriteTarget(dev, BlendState::Opaque, kDestination, kPartialSource),
                   kOpaqueResult, "D3 Opaque replaces all four destination channels");
        CheckColor(RunSpriteTarget(dev, BlendState::NonPremultiplied, kDestination, kPartialSource),
                   kNonPremultipliedResult,
                   "D4 NonPremultiplied attenuates both source and destination with separate alpha result");

        // A -> B -> A proves consecutive state application and return-to-Additive behavior.
        {
            RenderTarget2D target(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&target);
            dev.Clear(kDestination);
            DrawSprite(dev, BlendState::Additive, kPartialSource, Rectangle(0, 0, 4, kH));
            DrawSprite(dev, BlendState::Opaque, kPartialSource, Rectangle(4, 0, 4, kH));
            DrawSprite(dev, BlendState::Additive, kPartialSource, Rectangle(8, 0, 4, kH));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            CheckColor(ReadTarget(target, 1, 4), kPartialAdditive,
                       "E1 Additive before a consecutive Opaque draw");
            CheckColor(ReadTarget(target, 5, 4), kOpaqueResult,
                       "E2 consecutive Opaque draw does not inherit Additive");
            CheckColor(ReadTarget(target, 9, 4), kPartialAdditive,
                       "E3 returning to Additive does not retain Opaque");
        }

        // SpriteBatch copies its Begin state. Mutating the caller's value after Begin must not
        // replace the batch's captured Additive state.
        {
            RenderTarget2D target(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&target);
            dev.Clear(kDestination);
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            DepthStencilState noDepth = DepthStencilState::None;
            RasterizerState noCull = RasterizerState::CullNone;
            BlendState captured = BlendState::Additive;
            sb.Begin(SpriteSortMode::Deferred, captured, &point, &noDepth, &noCull);
            sb.Draw(white_, Rectangle(0, 0, kW, kH), Rectangle(0, 0, 1, 1), kPartialSource);
            captured.setColorSourceBlendProperty(Blend::One);
            captured.setAlphaSourceBlendProperty(Blend::One);
            captured.setColorDestinationBlendProperty(Blend::Zero);
            captured.setAlphaDestinationBlendProperty(Blend::Zero);
            sb.End();
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            CheckColor(ReadTarget(target), kPartialAdditive,
                       "E4 SpriteBatch keeps the Additive value captured at Begin");
        }

        // Two different destination pixels receive the same source in one draw. A cached/stale
        // destination would make them equal; a source-only result would make both kZeroDestination.
        {
            RenderTarget2D target(dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                                  RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(&target);
            dev.Clear(Color(0, 0, 0, 0));
            DrawSprite(dev, BlendState::Opaque, Color(7, 19, 43, 23), Rectangle(0, 0, 6, kH));
            DrawSprite(dev, BlendState::Opaque, Color(71, 37, 5, 47), Rectangle(6, 0, 6, kH));
            DrawSprite(dev, BlendState::Additive, kPartialSource);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            CheckColor(ReadTarget(target, 2, 4), Color(32, 32, 94, 39),
                       "F1 Additive loads the left destination pixel");
            CheckColor(ReadTarget(target, 9, 4), Color(96, 50, 56, 63),
                       "F2 Additive loads the distinct right destination pixel without staleness");
        }

        // All effect-aware public primitive entry points share the same blend stage.
        // REMED-GFX-233: the persistent-buffer pair deliberately uses CNA's legacy convenience
        // constructor, whose public declaration is empty even though typed SetData uploads packed
        // 16-byte Position+Color records. Pin that precondition as well as the rendered pixels so
        // replacing the fixture with an explicit declaration cannot hide a zero-stride regression.
        bool usedLegacyEmptyDeclaration = false;
        const Color legacyNonIndexed = RunDirectTarget(
            dev, DirectPath::DrawPrimitives, &usedLegacyEmptyDeclaration);
        Check(usedLegacyEmptyDeclaration,
              "G1 persistent VertexBuffer fixture has the legacy empty/zero-stride declaration");
        CheckColor(legacyNonIndexed, kPartialAdditive,
                   "G2 legacy empty-declaration VertexBuffer DrawPrimitives path");
        CheckColor(RunDirectTarget(dev, DirectPath::DrawIndexedPrimitives), kPartialAdditive,
                   "G3 legacy empty-declaration VertexBuffer DrawIndexedPrimitives path");
        CheckColor(RunDirectTarget(dev, DirectPath::DrawUserPrimitives), kPartialAdditive,
                   "G4 DrawUserPrimitives path");
        CheckColor(RunDirectTarget(dev, DirectPath::DrawUserIndexedPrimitives), kPartialAdditive,
                   "G5 DrawUserIndexedPrimitives path");

        // Explicit resource disposal is exercised after a real producer/readback cycle. Reaching
        // main's return then exercises SpriteBatch/effect/device teardown under the sanitizers.
        {
            auto target = std::make_unique<RenderTarget2D>(
                dev, kW, kH, false, SurfaceFormat::Color, DepthFormat::None, 0,
                RenderTargetUsage::DiscardContents);
            dev.SetRenderTarget(target.get());
            dev.Clear(kDestination);
            DrawSprite(dev, BlendState::Additive, kPartialSource);
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            const Color beforeDispose = ReadTarget(*target);
            target->Dispose();
            Check(Same(beforeDispose, kPartialAdditive) && target->getIsDisposedProperty(),
                  "I1 RenderTarget2D producer/readback is exact before explicit disposal");
        }
    }

    void BackbufferFrame(GraphicsDevice& dev)
    {
        if (!kBackbufferReadback)
        {
            if (frame_ == 1)
                Check(true, "J1 backbuffer readback unavailable on this control renderer; RT path is authoritative");
            return;
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        SetOrdinaryDrawState(dev);
        dev.Clear(kDestination);
        DrawSprite(dev, BlendState::Additive, kPartialSource,
                   Rectangle(0, 0, getGraphicsDeviceProperty().getViewportProperty().getWidthProperty(),
                             getGraphicsDeviceProperty().getViewportProperty().getHeightProperty()));
        const auto& vp = dev.getViewportProperty();
        const Rectangle region(vp.getWidthProperty() / 2, vp.getHeightProperty() / 2, 1, 1);
        Color got(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &got, 0, 1);
        CheckColor(got, kPartialAdditive,
                   "J" + std::to_string(frame_) + " backbuffer Additive result on repeated frame " +
                   std::to_string(frame_));
    }

protected:
    void Initialize() override
    {
        auto& dev = getGraphicsDeviceProperty();
        white_ = Texture2D(dev, 1, 1);
        const Color white = Color::White;
        white_.SetData(&white, 1);
        Game::Initialize();
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        if (frame_ == 1)
        {
            std::printf("[INFO] REMED-GFX-148 Additive contract on %s\n", kRendererName);
            ContractDefinitions();
            if (!kRasterizes)
            {
                HeadlessControl(dev);
                result_ = (passCount_ == totalCount_) ? 0 : 1;
                std::printf("[INFO] %s: %d/%d checks passed\n", kRendererName, passCount_, totalCount_);
                Exit();
                return;
            }
            Matrix(dev);
        }

        BackbufferFrame(dev);
        if (frame_ == 3)
        {
            white_.Dispose();
            Check(white_.getIsDisposedProperty(),
                  "K1 source texture disposed before clean game/device teardown");
            result_ = (passCount_ == totalCount_) ? 0 : 1;
            std::printf("[INFO] %s: %d/%d checks passed across %d frames\n",
                        kRendererName, passCount_, totalCount_, frame_);
            Exit();
        }
    }

public:
    AdditiveBlendContractTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(48);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    AdditiveBlendContractTest game;
    game.Run();
    return game.Result();
}
