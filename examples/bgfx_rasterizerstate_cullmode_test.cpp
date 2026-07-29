// SPDX-License-Identifier: MS-PL
// REMED-GFX-017: permanent public winding/cull-state contract regression for Bgfx.
//
// Public CNA/XNA contract (the enum names identify the face being culled):
//   None                         -> both windings survive
//   CullClockwiseFace            -> clockwise is culled; counter-clockwise survives
//   CullCounterClockwiseFace     -> counter-clockwise is culled; clockwise survives
// RasterizerState(), RasterizerState::CullCounterClockwise, and a new GraphicsDevice all use
// CullCounterClockwiseFace.
//
// The canonical geometry is one pair of geometrically identical triangles. The left RED triangle
// has negative NDC signed area (the clockwise/front winding established by the real XNA oracle);
// the right GREEN triangle is the same shape with its order reversed. Both are submitted together,
// so every frame contains a visible freshness witness even when one winding is intentionally
// culled. A distinctive non-black clear color is also sampled outside the geometry. This avoids
// false culling results from Bgfx's asynchronous render-target-to-backbuffer sampling path.
//
// Coverage:
//   * public default/presets and all three CullMode values;
//   * backbuffer and RenderTarget2D with identical public winding semantics;
//   * None -> CullClockwise -> None and
//     CullCounterClockwise -> CullClockwise -> CullCounterClockwise in one submitted frame;
//   * target -> backbuffer -> target restoration;
//   * BasicEffect + DrawUserPrimitives (stock-effect/direct-3D path);
//   * SpriteBatch visibility after an opposing 3D cull state, with every relevant state explicit.
//
// CMake registers this executable for Bgfx's OpenGL route and its Vulkan route. The program also
// prints the active route through the backend's normal startup diagnostics.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 128;
    constexpr int kHeight = 96;

    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kBackbufferClear(3, 5, 7, 255);

    enum class StateSource
    {
        DeviceDefault,
        DefaultConstructed,
        CullNonePreset,
        CullClockwisePreset,
        CullCounterClockwisePreset,
    };

    struct StateCase
    {
        const char* name;
        StateSource source;
        CullMode mode;
    };

    struct PairPixels
    {
        Color clockwise;
        Color counterClockwise;
        Color clear;
    };

    Color ClearFor(int id)
    {
        return Color(11 + (id * 17) % 71,
                     13 + (id * 19) % 67,
                     17 + (id * 23) % 61,
                     255);
    }

    bool Near(const Color& actual, const Color& expected, int tolerance = 8)
    {
        auto channelNear = [tolerance](int a, int b)
        {
            return std::abs(a - b) <= tolerance;
        };
        return channelNear(actual.getRProperty(), expected.getRProperty())
            && channelNear(actual.getGProperty(), expected.getGProperty())
            && channelNear(actual.getBProperty(), expected.getBProperty());
    }

    RasterizerState ExplicitRasterizer(CullMode mode)
    {
        RasterizerState result;
        result.setCullModeProperty(mode);
        result.setFillModeProperty(FillMode::Solid);
        result.setScissorTestEnableProperty(false);
        result.setDepthBiasProperty(0.0f);
        result.setSlopeScaleDepthBiasProperty(0.0f);
        return result;
    }

    RasterizerState StateFor(StateSource source)
    {
        switch (source)
        {
        case StateSource::CullNonePreset:
            return RasterizerState::CullNone;
        case StateSource::CullClockwisePreset:
            return RasterizerState::CullClockwise;
        case StateSource::CullCounterClockwisePreset:
            return RasterizerState::CullCounterClockwise;
        case StateSource::DefaultConstructed:
        case StateSource::DeviceDefault:
        default:
            return RasterizerState();
        }
    }

    void ApplyExplicitCommonState(GraphicsDevice& device, int width, int height)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setViewportProperty(Viewport(0, 0, width, height));
        device.setScissorRectangleProperty(Rectangle(0, 0, width, height));
    }

    void ConfigureEffect(BasicEffect& effect)
    {
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.VertexColorEnabled = true;
        effect.Apply();
    }

    // Two translated copies of one right triangle. RED is negative signed area in NDC; GREEN is
    // the identical triangle with its order reversed.
    void DrawCanonicalPair(GraphicsDevice& device)
    {
        const VertexPositionColor vertices[6] = {
            {Vector3(-0.1f, -0.7f, 0.0f), kRed},
            {Vector3(-0.9f, -0.7f, 0.0f), kRed},
            {Vector3(-0.9f,  0.7f, 0.0f), kRed},

            {Vector3( 0.1f,  0.7f, 0.0f), kGreen},
            {Vector3( 0.1f, -0.7f, 0.0f), kGreen},
            {Vector3( 0.9f, -0.7f, 0.0f), kGreen},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    // A smaller canonical pair centered in one of three horizontal cells.
    void DrawCellPair(GraphicsDevice& device, float centerX)
    {
        const float redX0 = centerX - 0.28f;
        const float redX1 = centerX - 0.04f;
        const float greenX0 = centerX + 0.04f;
        const float greenX1 = centerX + 0.28f;
        constexpr float y0 = -0.55f;
        constexpr float y1 = 0.55f;

        const VertexPositionColor vertices[6] = {
            {Vector3(redX1, y0, 0.0f), kRed},
            {Vector3(redX0, y0, 0.0f), kRed},
            {Vector3(redX0, y1, 0.0f), kRed},

            {Vector3(greenX0, y1, 0.0f), kGreen},
            {Vector3(greenX0, y0, 0.0f), kGreen},
            {Vector3(greenX1, y0, 0.0f), kGreen},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    int PixelX(float ndcX)
    {
        return static_cast<int>((ndcX + 1.0f) * 0.5f * static_cast<float>(kWidth));
    }

    int PixelY(float ndcY)
    {
        return static_cast<int>((1.0f - ndcY) * 0.5f * static_cast<float>(kHeight));
    }

    const Color& AtNdc(const std::vector<Color>& pixels, float x, float y)
    {
        return pixels[static_cast<std::size_t>(PixelY(y)) * kWidth + PixelX(x)];
    }
}

class BgfxRasterizerStateCullModeTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<RenderTarget2D> renderTarget_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> whiteTexture_;
    int passed_ = 0;
    int failed_ = 0;
    bool done_ = false;
    int result_ = 1;

    void Check(bool condition, const char* label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
        if (condition)
            ++passed_;
        else
            ++failed_;
    }

    void CheckColor(const Color& actual, const Color& expected, const char* label)
    {
        const bool ok = Near(actual, expected);
        std::printf("[%s] %s: (%d,%d,%d) expected (%d,%d,%d)\n",
                    ok ? "PASS" : "FAIL", label,
                    actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
                    expected.getRProperty(), expected.getGProperty(), expected.getBProperty());
        if (ok)
            ++passed_;
        else
            ++failed_;
    }

    std::vector<Color> ReadBackbuffer(GraphicsDevice& device)
    {
        std::vector<Color> result(static_cast<std::size_t>(kWidth) * kHeight,
                                  Color(0, 0, 0, 0));
        const Rectangle whole(0, 0, kWidth, kHeight);
        device.GetBackBufferData(&whole, result.data(), 0, kWidth * kHeight);
        return result;
    }

    PairPixels ProbeCanonicalPair(const std::vector<Color>& pixels)
    {
        // Triangle centroids are (-0.6333,-0.2333) and (+0.3667,-0.2333).
        return {
            AtNdc(pixels, -0.6333f, -0.2333f),
            AtNdc(pixels,  0.3667f, -0.2333f),
            AtNdc(pixels, -0.98f,    0.92f),
        };
    }

    void ApplyCase(GraphicsDevice& device, const StateCase& stateCase)
    {
        if (stateCase.source != StateSource::DeviceDefault)
            device.setRasterizerStateProperty(StateFor(stateCase.source));
    }

    std::vector<Color> RenderPairToBackbuffer(GraphicsDevice& device,
                                               const StateCase& stateCase,
                                               const Color& clear)
    {
        ApplyExplicitCommonState(device, kWidth, kHeight);
        ApplyCase(device, stateCase);
        device.Clear(clear);

        BasicEffect effect(device);
        ConfigureEffect(effect);
        DrawCanonicalPair(device);
        return ReadBackbuffer(device);
    }

    void BlitRenderTargetToBackbuffer(GraphicsDevice& device)
    {
        ApplyExplicitCommonState(device, kWidth, kHeight);
        device.Clear(kBackbufferClear);

        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = ExplicitRasterizer(CullMode::None);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point,
                            &noDepth, &noCull);
        spriteBatch_->Draw(*renderTarget_, Rectangle(0, 0, kWidth, kHeight),
                            Rectangle(0, 0, kWidth, kHeight), Color::White);
        spriteBatch_->End();
    }

    std::vector<Color> RenderPairToTarget(GraphicsDevice& device,
                                          const StateCase& stateCase,
                                          const Color& clear)
    {
        device.SetRenderTarget(renderTarget_.get());
        ApplyExplicitCommonState(device, kWidth, kHeight);
        ApplyCase(device, stateCase);
        device.Clear(clear);

        BasicEffect effect(device);
        ConfigureEffect(effect);
        DrawCanonicalPair(device);

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        // REMED-GFX-155: this used to be followed by `device.Present();`, commented "Bgfx orders
        // views by id; flush the off-screen view before the backbuffer samples it." That was a
        // correct diagnosis of a real defect and an accidental workaround for it -- bgfx sorted a
        // frame's draws by numeric view id, and the backbuffer owns the lowest one, so the consumer
        // ran before its producer unless a frame boundary separated them. The backend now orders
        // views by public submission order, so the extra frame is gone and this fixture measures
        // culling in the ordinary same-frame sequence a game would write.
        BlitRenderTargetToBackbuffer(device);
        return ReadBackbuffer(device);
    }

    void CheckPair(const PairPixels& actual, CullMode mode, const Color& clear,
                   const char* targetName, const char* stateName)
    {
        char label[160];
        const Color& expectedClockwise =
            mode == CullMode::CullClockwiseFace ? clear : kRed;
        const Color& expectedCounterClockwise =
            mode == CullMode::CullCounterClockwiseFace ? clear : kGreen;

        std::snprintf(label, sizeof(label), "%s / %s / clockwise RED",
                      targetName, stateName);
        CheckColor(actual.clockwise, expectedClockwise, label);
        std::snprintf(label, sizeof(label), "%s / %s / counter-clockwise GREEN",
                      targetName, stateName);
        CheckColor(actual.counterClockwise, expectedCounterClockwise, label);
        std::snprintf(label, sizeof(label), "%s / %s / deterministic clear sentinel",
                      targetName, stateName);
        CheckColor(actual.clear, clear, label);
    }

    std::vector<Color> RenderTransition(GraphicsDevice& device,
                                        const std::array<CullMode, 3>& sequence,
                                        const Color& clear,
                                        bool renderTarget)
    {
        if (renderTarget)
            device.SetRenderTarget(renderTarget_.get());

        ApplyExplicitCommonState(device, kWidth, kHeight);
        device.Clear(clear);
        BasicEffect effect(device);
        ConfigureEffect(effect);

        constexpr std::array<float, 3> centers = {-0.66f, 0.0f, 0.66f};
        for (std::size_t i = 0; i < sequence.size(); ++i)
        {
            device.setRasterizerStateProperty(ExplicitRasterizer(sequence[i]));
            DrawCellPair(device, centers[i]);
        }

        if (renderTarget)
        {
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            // REMED-GFX-155: the `device.Present();` that used to sit here is gone for the same
            // reason as in RenderPairIntoTarget above.
            BlitRenderTargetToBackbuffer(device);
        }
        return ReadBackbuffer(device);
    }

    void CheckTransition(const std::vector<Color>& pixels,
                         const std::array<CullMode, 3>& sequence,
                         const Color& clear,
                         const char* labelPrefix)
    {
        constexpr std::array<float, 3> centers = {-0.66f, 0.0f, 0.66f};
        constexpr float sampleY = -0.1833f;
        char label[176];

        for (std::size_t i = 0; i < sequence.size(); ++i)
        {
            const Color& clockwise = AtNdc(pixels, centers[i] - 0.20f, sampleY);
            const Color& counterClockwise = AtNdc(pixels, centers[i] + 0.12f, sampleY);
            const Color& expectedClockwise =
                sequence[i] == CullMode::CullClockwiseFace ? clear : kRed;
            const Color& expectedCounterClockwise =
                sequence[i] == CullMode::CullCounterClockwiseFace ? clear : kGreen;

            std::snprintf(label, sizeof(label), "%s cell %zu clockwise", labelPrefix, i);
            CheckColor(clockwise, expectedClockwise, label);
            std::snprintf(label, sizeof(label), "%s cell %zu counter-clockwise",
                          labelPrefix, i);
            CheckColor(counterClockwise, expectedCounterClockwise, label);
        }

        std::snprintf(label, sizeof(label), "%s deterministic clear sentinel", labelPrefix);
        CheckColor(AtNdc(pixels, -0.98f, 0.92f), clear, label);
    }

    void CheckPublicContract(GraphicsDevice& device)
    {
        Check(static_cast<int>(CullMode::None) == 0,
              "CullMode.None ordinal is 0");
        Check(static_cast<int>(CullMode::CullClockwiseFace) == 1,
              "CullMode.CullClockwiseFace ordinal is 1");
        Check(static_cast<int>(CullMode::CullCounterClockwiseFace) == 2,
              "CullMode.CullCounterClockwiseFace ordinal is 2");
        Check(RasterizerState::CullNone.getCullModeProperty() == CullMode::None,
              "RasterizerState.CullNone maps to CullMode.None");
        Check(RasterizerState::CullClockwise.getCullModeProperty()
                  == CullMode::CullClockwiseFace,
              "RasterizerState.CullClockwise maps to CullClockwiseFace");
        Check(RasterizerState::CullCounterClockwise.getCullModeProperty()
                  == CullMode::CullCounterClockwiseFace,
              "RasterizerState.CullCounterClockwise maps to CullCounterClockwiseFace");

        const RasterizerState constructed;
        Check(constructed.getCullModeProperty() == CullMode::CullCounterClockwiseFace,
              "default RasterizerState culls counter-clockwise faces");

        const RasterizerState& deviceDefault = device.getRasterizerStateProperty();
        Check(deviceDefault.getCullModeProperty() == CullMode::CullCounterClockwiseFace,
              "new GraphicsDevice defaults to CullCounterClockwiseFace");
        Check(deviceDefault.getFillModeProperty() == FillMode::Solid,
              "new GraphicsDevice default FillMode remains Solid");
        Check(!deviceDefault.getScissorTestEnableProperty(),
              "new GraphicsDevice default scissor remains disabled");
        Check(deviceDefault.getDepthBiasProperty() == 0.0f,
              "new GraphicsDevice default DepthBias remains zero");
        Check(deviceDefault.getSlopeScaleDepthBiasProperty() == 0.0f,
              "new GraphicsDevice default SlopeScaleDepthBias remains zero");
    }

    void CheckModesOnBothTargets(GraphicsDevice& device)
    {
        // DeviceDefault must run before any SpriteBatch or explicit rasterizer assignment.
        const std::array<StateCase, 5> cases = {{
            {"GraphicsDevice default", StateSource::DeviceDefault,
             CullMode::CullCounterClockwiseFace},
            {"RasterizerState.CullNone", StateSource::CullNonePreset, CullMode::None},
            {"RasterizerState.CullClockwise", StateSource::CullClockwisePreset,
             CullMode::CullClockwiseFace},
            {"RasterizerState.CullCounterClockwise",
             StateSource::CullCounterClockwisePreset,
             CullMode::CullCounterClockwiseFace},
            {"default RasterizerState()", StateSource::DefaultConstructed,
             CullMode::CullCounterClockwiseFace},
        }};

        int clearId = 1;
        for (const StateCase& stateCase : cases)
        {
            const Color backbufferClear = ClearFor(clearId++);
            const PairPixels backbuffer =
                ProbeCanonicalPair(RenderPairToBackbuffer(device, stateCase, backbufferClear));
            CheckPair(backbuffer, stateCase.mode, backbufferClear,
                      "backbuffer", stateCase.name);

            // The first RT case is still the untouched GraphicsDevice default: the preceding
            // backbuffer case does not alter RasterizerState.
            const Color targetClear = ClearFor(clearId++);
            const PairPixels target =
                ProbeCanonicalPair(RenderPairToTarget(device, stateCase, targetClear));
            CheckPair(target, stateCase.mode, targetClear,
                      "RenderTarget2D", stateCase.name);
        }
    }

    void CheckStateTransitions(GraphicsDevice& device)
    {
        const std::array<CullMode, 3> noneClockwiseNone = {
            CullMode::None,
            CullMode::CullClockwiseFace,
            CullMode::None,
        };
        const std::array<CullMode, 3> counterClockwiseClockwiseCounterClockwise = {
            CullMode::CullCounterClockwiseFace,
            CullMode::CullClockwiseFace,
            CullMode::CullCounterClockwiseFace,
        };

        Color clear = ClearFor(20);
        CheckTransition(RenderTransition(device, noneClockwiseNone, clear, false),
                        noneClockwiseNone, clear,
                        "backbuffer None->Clockwise->None");
        clear = ClearFor(21);
        CheckTransition(RenderTransition(device, noneClockwiseNone, clear, true),
                        noneClockwiseNone, clear,
                        "RenderTarget2D None->Clockwise->None");

        clear = ClearFor(22);
        CheckTransition(
            RenderTransition(device, counterClockwiseClockwiseCounterClockwise, clear, false),
            counterClockwiseClockwiseCounterClockwise, clear,
            "backbuffer CounterClockwise->Clockwise->CounterClockwise");
        clear = ClearFor(23);
        CheckTransition(
            RenderTransition(device, counterClockwiseClockwiseCounterClockwise, clear, true),
            counterClockwiseClockwiseCounterClockwise, clear,
            "RenderTarget2D CounterClockwise->Clockwise->CounterClockwise");
    }

    void CheckTargetBackbufferTarget(GraphicsDevice& device)
    {
        const StateCase targetA{"target A CullNone", StateSource::CullNonePreset, CullMode::None};
        const StateCase backbuffer{"backbuffer CullClockwise",
                                   StateSource::CullClockwisePreset,
                                   CullMode::CullClockwiseFace};
        const StateCase targetB{"target B CullCounterClockwise",
                                StateSource::CullCounterClockwisePreset,
                                CullMode::CullCounterClockwiseFace};

        Color clear = ClearFor(30);
        CheckPair(ProbeCanonicalPair(RenderPairToTarget(device, targetA, clear)),
                  targetA.mode, clear, "target->backbuffer->target [target A]", targetA.name);
        clear = ClearFor(31);
        CheckPair(ProbeCanonicalPair(RenderPairToBackbuffer(device, backbuffer, clear)),
                  backbuffer.mode, clear, "target->backbuffer->target [backbuffer]",
                  backbuffer.name);
        clear = ClearFor(32);
        CheckPair(ProbeCanonicalPair(RenderPairToTarget(device, targetB, clear)),
                  targetB.mode, clear, "target->backbuffer->target [target B]", targetB.name);
    }

    void CheckSpriteBatchVisibility(GraphicsDevice& device)
    {
        const Color clear = ClearFor(40);
        ApplyExplicitCommonState(device, kWidth, kHeight);
        // Establish the opposite cull state first. Begin must apply the supplied state by value;
        // the sprite must not inherit this one.
        device.setRasterizerStateProperty(
            ExplicitRasterizer(CullMode::CullClockwiseFace));
        device.Clear(clear);

        SamplerState point = SamplerState::PointClamp;
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState spriteRasterizer =
            ExplicitRasterizer(CullMode::CullCounterClockwiseFace);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point,
                            &noDepth, &spriteRasterizer);
        spriteBatch_->Draw(*whiteTexture_, Rectangle(32, 24, 64, 48), kBlue);
        spriteBatch_->End();

        const std::vector<Color> pixels = ReadBackbuffer(device);
        CheckColor(pixels[48 * kWidth + 64], kBlue,
                   "SpriteBatch remains visible with explicit established rasterizer state");
        CheckColor(pixels[4 * kWidth + 4], clear,
                   "SpriteBatch control preserves deterministic clear outside sprite");
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        GraphicsDevice& device = getGraphicsDeviceProperty();
        renderTarget_ = std::make_unique<RenderTarget2D>(device, kWidth, kHeight);
        spriteBatch_ = std::make_unique<SpriteBatch>(device);
        whiteTexture_ = std::make_unique<Texture2D>(device, 1, 1);
        const Color white = Color::White;
        whiteTexture_->SetData(&white, 1);
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        GraphicsDevice& device = getGraphicsDeviceProperty();
        CheckPublicContract(device);
        CheckModesOnBothTargets(device);
        CheckStateTransitions(device);
        CheckTargetBackbufferTarget(device);
        CheckSpriteBatchVisibility(device);

        std::printf("\nResult: %d/%d PASS\n", passed_, passed_ + failed_);
        result_ = failed_ == 0 ? 0 : 1;
        Exit();
    }

public:
    BgfxRasterizerStateCullModeTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(kWidth);
        graphics_->setPreferredBackBufferHeightProperty(kHeight);
    }

    int GetResult() const
    {
        return result_;
    }
};

int main()
{
    BgfxRasterizerStateCullModeTest game;
    game.Run();
    return game.GetResult();
}
