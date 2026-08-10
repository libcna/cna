// SPDX-License-Identifier: MS-PL
// REMED-GFX-030: permanent public depth-state regression for the Software CPU rasterizer.
//
// This test separates the three independent parts of the public depth contract:
//   * DepthBufferEnable controls whether the stored depth rejects a fragment.
//   * DepthBufferWriteEnable controls whether a passing fragment replaces stored depth.
//   * DepthBufferFunction selects all eight public CompareFunction relations.
//
// Every draw goes through GraphicsDevice/DepthStencilState and real triangle rasterization. Exact,
// distinct colors and depth clear values make color rejection and stored-depth mutation separately
// observable. Coverage includes both Software fragment writers, strict/inclusive equality,
// A->B->A state capture, full active-target depth clears, Viewport depth mapping, DepthBias, and
// isolated backbuffer/RenderTarget2D depth storage across explicit target transitions.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidth = 96;
    constexpr int kHeight = 72;
    constexpr float kDepthBias = 3000000.0f; // 3e6 * 2^-24 ~= 0.179 in Software window depth.

    const Color kClear(11, 17, 23, 255);
    const Color kOtherClear(29, 31, 37, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kCyan(0, 255, 255, 255);
    const Color kMagenta(255, 0, 255, 255);
    const Color kYellow(255, 255, 0, 255);

    bool Exact(const Color& actual, const Color& expected)
    {
        return actual.getRProperty() == expected.getRProperty() &&
               actual.getGProperty() == expected.getGProperty() &&
               actual.getBProperty() == expected.getBProperty() &&
               actual.getAProperty() == expected.getAProperty();
    }

    DepthStencilState MakeDepthState(bool enable, bool write, CompareFunction function)
    {
        DepthStencilState state;
        state.setDepthBufferEnableProperty(enable);
        state.setDepthBufferWriteEnableProperty(write);
        state.setDepthBufferFunctionProperty(function);
        return state;
    }

    void SetRasterizer(GraphicsDevice& device, float depthBias = 0.0f,
                       float slopeScaleDepthBias = 0.0f)
    {
        RasterizerState state;
        state.setCullModeProperty(CullMode::None);
        state.setDepthBiasProperty(depthBias);
        state.setSlopeScaleDepthBiasProperty(slopeScaleDepthBias);
        device.setRasterizerStateProperty(state);
    }

    void SetFullViewport(GraphicsDevice& device, int width, int height)
    {
        device.setViewportProperty(Viewport(0, 0, width, height));
    }

    void ClearTargetAndDepth(GraphicsDevice& device, const Color& color, float depth)
    {
        device.Clear(ClearOptions::Target | ClearOptions::DepthBuffer, color, depth, 0);
    }

    void DrawQuad(GraphicsDevice& device, BasicEffect& effect, float x0, float x1, float depth,
                  const Color& color, bool buffered = false)
    {
        const VertexPositionColor vertices[6] = {
            {Vector3(x0,  1.0f, depth), color},
            {Vector3(x0, -1.0f, depth), color},
            {Vector3(x1, -1.0f, depth), color},
            {Vector3(x0,  1.0f, depth), color},
            {Vector3(x1, -1.0f, depth), color},
            {Vector3(x1,  1.0f, depth), color},
        };

        effect.Apply();
        if (!buffered)
        {
            device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
            return;
        }

        // VertexBuffer + DrawPrimitives selects Software's generalized shaded fragment writer,
        // while DrawUserPrimitives above selects the direct colored writer.
        VertexBuffer vertexBuffer(device, 6);
        vertexBuffer.SetData(vertices, 6);
        device.SetVertexBuffer(&vertexBuffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
    }

    Color ReadPixel(GraphicsDevice& device, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    std::vector<Color> ReadAll(GraphicsDevice& device, int width, int height)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                                  Color(0, 0, 0, 0));
        const Rectangle region(0, 0, width, height);
        device.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels;
    }

    bool AllExact(const std::vector<Color>& pixels, const Color& expected)
    {
        return std::all_of(pixels.begin(), pixels.end(),
                           [&](const Color& pixel) { return Exact(pixel, expected); });
    }
}

class SoftwareDepthStateTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    bool done_ = false;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (ok)
            ++passed_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        GraphicsDevice& device = getGraphicsDeviceProperty();
        device.setBlendStateProperty(BlendState::Opaque);
        SetRasterizer(device);
        SetFullViewport(device, kWidth, kHeight);

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.VertexColorEnabled = true;

        // Public preset contract.
        Check(DepthStencilState::None.getDepthBufferEnableProperty() == false &&
              DepthStencilState::None.getDepthBufferWriteEnableProperty() == false &&
              DepthStencilState::None.getDepthBufferFunctionProperty() == CompareFunction::LessEqual,
              "preset None = depth test disabled, writes disabled, LessEqual retained");
        Check(DepthStencilState::Default.getDepthBufferEnableProperty() == true &&
              DepthStencilState::Default.getDepthBufferWriteEnableProperty() == true &&
              DepthStencilState::Default.getDepthBufferFunctionProperty() == CompareFunction::LessEqual,
              "preset Default = depth test enabled, writes enabled, LessEqual");
        Check(DepthStencilState::DepthRead.getDepthBufferEnableProperty() == true &&
              DepthStencilState::DepthRead.getDepthBufferWriteEnableProperty() == false &&
              DepthStencilState::DepthRead.getDepthBufferFunctionProperty() == CompareFunction::LessEqual,
              "preset DepthRead = depth test enabled, writes disabled, LessEqual");

        // Testing and writes enabled: the first near fragment writes and rejects the later far one.
        ClearTargetAndDepth(device, kClear, 1.0f);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        DrawQuad(device, effect, -1.0f, 1.0f, 0.2f, kRed);
        DrawQuad(device, effect, -1.0f, 1.0f, 0.8f, kGreen);
        Check(Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kRed),
              "Default: near red writes depth and rejects later far green");

        // Testing enabled, writes disabled: near is visible but leaves clear depth. The farther
        // Default draw therefore passes and writes 0.8, which then rejects an even farther 0.9 draw.
        ClearTargetAndDepth(device, kOtherClear, 1.0f);
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        DrawQuad(device, effect, -1.0f, 1.0f, 0.2f, kRed);
        Check(Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kRed),
              "DepthRead: passing near red still writes color");
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        DrawQuad(device, effect, -1.0f, 1.0f, 0.8f, kGreen);
        Check(Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kGreen),
              "DepthRead: write-disabled near does not prevent a later farther write");
        DrawQuad(device, effect, -1.0f, 1.0f, 0.9f, kBlue);
        Check(Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kGreen),
              "DepthRead: later far green wrote 0.8 and rejects still-farther blue");

        // Every public CompareFunction, including strict/inclusive equality boundaries.
        const auto RenderCompare = [&](CompareFunction function, float incoming, float stored,
                                       bool buffered = false) {
            SetRasterizer(device);
            SetFullViewport(device, kWidth, kHeight);
            ClearTargetAndDepth(device, kClear, stored);
            device.setDepthStencilStateProperty(MakeDepthState(true, true, function));
            DrawQuad(device, effect, -1.0f, 1.0f, incoming, kGreen, buffered);
            return ReadPixel(device, kWidth / 2, kHeight / 2);
        };

        Check(Exact(RenderCompare(CompareFunction::Always, 0.9f, 0.1f), kGreen),
              "Compare Always passes a farther fragment");
        Check(Exact(RenderCompare(CompareFunction::Never, 0.1f, 0.9f), kClear),
              "Compare Never rejects a nearer fragment");
        Check(Exact(RenderCompare(CompareFunction::Less, 0.3f, 0.5f), kGreen),
              "Compare Less passes strictly nearer");
        Check(Exact(RenderCompare(CompareFunction::Less, 0.5f, 0.5f), kClear),
              "Compare Less rejects equal depth");
        Check(Exact(RenderCompare(CompareFunction::LessEqual, 0.5f, 0.5f), kGreen),
              "Compare LessEqual passes equal depth");
        Check(Exact(RenderCompare(CompareFunction::Greater, 0.7f, 0.5f), kGreen),
              "Compare Greater passes strictly farther");
        Check(Exact(RenderCompare(CompareFunction::Greater, 0.5f, 0.5f), kClear),
              "Compare Greater rejects equal depth");
        Check(Exact(RenderCompare(CompareFunction::GreaterEqual, 0.5f, 0.5f), kGreen),
              "Compare GreaterEqual passes equal depth");
        Check(Exact(RenderCompare(CompareFunction::Equal, 0.5f, 0.5f), kGreen),
              "Compare Equal passes equal depth");
        Check(Exact(RenderCompare(CompareFunction::Equal, 0.3f, 0.5f), kClear),
              "Compare Equal rejects unequal depth");
        Check(Exact(RenderCompare(CompareFunction::NotEqual, 0.3f, 0.5f), kGreen),
              "Compare NotEqual passes unequal depth");
        Check(Exact(RenderCompare(CompareFunction::NotEqual, 0.5f, 0.5f), kClear),
              "Compare NotEqual rejects equal depth");

        // A failed fragment modifies neither color nor stored depth. The Greater control reveals
        // whether the failed 0.8 incorrectly replaced the original 0.4.
        ClearTargetAndDepth(device, kOtherClear, 0.4f);
        device.setDepthStencilStateProperty(MakeDepthState(true, true, CompareFunction::Less));
        DrawQuad(device, effect, -1.0f, 1.0f, 0.8f, kGreen);
        Check(Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kOtherClear),
              "failed depth fragment leaves color unchanged");
        device.setDepthStencilStateProperty(MakeDepthState(true, true, CompareFunction::Greater));
        DrawQuad(device, effect, -1.0f, 1.0f, 0.6f, kBlue);
        Check(Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kBlue),
              "failed depth fragment leaves stored depth unchanged");

        // Depth testing disabled: stored 0.2 cannot reject 0.9, and no depth write occurs. The
        // custom write=true state proves DepthBufferEnable controls the whole depth operation, as on
        // the reference GL/D3D renderers, rather than only bypassing comparison.
        ClearTargetAndDepth(device, kClear, 0.2f);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        DrawQuad(device, effect, -1.0f, 1.0f, 0.9f, kGreen);
        Check(Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kGreen),
              "None: disabled depth testing allows a later far fragment");
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        DrawQuad(device, effect, -1.0f, 1.0f, 0.5f, kBlue);
        Check(Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kGreen),
              "None: disabled writes preserve stored depth for restored Default");

        ClearTargetAndDepth(device, kOtherClear, 0.2f);
        device.setDepthStencilStateProperty(MakeDepthState(false, true, CompareFunction::Always));
        DrawQuad(device, effect, -1.0f, 1.0f, 0.9f, kRed);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        DrawQuad(device, effect, -1.0f, 1.0f, 0.5f, kBlue);
        Check(Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kRed),
              "DepthBufferEnable=false suppresses depth storage even when write=true");

        // Generalized shaded path: independently lock write-disable and compare-function behavior.
        ClearTargetAndDepth(device, kClear, 1.0f);
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        DrawQuad(device, effect, -1.0f, 1.0f, 0.2f, kRed, true);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        DrawQuad(device, effect, -1.0f, 1.0f, 0.8f, kGreen, true);
        Check(Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kGreen),
              "shaded writer honors DepthRead write-disable");
        Check(Exact(RenderCompare(CompareFunction::Never, 0.1f, 0.9f, true), kClear),
              "shaded writer honors CompareFunction::Never");

        // A->B->A restoration and per-draw capture. All columns are read only after the final state
        // change, so an implementation that reads one later live state for earlier draws is exposed.
        constexpr float kThird = 1.0f / 3.0f;
        ClearTargetAndDepth(device, kClear, 1.0f);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        DrawQuad(device, effect, -1.0f, -kThird, 0.2f, kRed);
        DrawQuad(device, effect, -1.0f, -kThird, 0.8f, kGreen);
        device.setDepthStencilStateProperty(DepthStencilState::DepthRead);
        DrawQuad(device, effect, -kThird, kThird, 0.2f, kRed);
        DrawQuad(device, effect, -kThird, kThird, 0.8f, kGreen);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        DrawQuad(device, effect, kThird, 1.0f, 0.2f, kRed);
        DrawQuad(device, effect, kThird, 1.0f, 0.8f, kGreen);
        Check(Exact(ReadPixel(device, kWidth / 6, kHeight / 2), kRed) &&
              Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kGreen) &&
              Exact(ReadPixel(device, 5 * kWidth / 6, kHeight / 2), kRed),
              "A->B->A capture: Default -> DepthRead -> Default");

        ClearTargetAndDepth(device, kOtherClear, 1.0f);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        DrawQuad(device, effect, -1.0f, -kThird, 0.2f, kRed);
        DrawQuad(device, effect, -1.0f, -kThird, 0.8f, kGreen);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        DrawQuad(device, effect, -kThird, kThird, 0.2f, kRed);
        DrawQuad(device, effect, -kThird, kThird, 0.8f, kGreen);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        DrawQuad(device, effect, kThird, 1.0f, 0.2f, kRed);
        DrawQuad(device, effect, kThird, 1.0f, 0.8f, kGreen);
        Check(Exact(ReadPixel(device, kWidth / 6, kHeight / 2), kGreen) &&
              Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kRed) &&
              Exact(ReadPixel(device, 5 * kWidth / 6, kHeight / 2), kGreen),
              "A->B->A capture: None -> Default -> None");

        ClearTargetAndDepth(device, kClear, 0.5f);
        device.setDepthStencilStateProperty(MakeDepthState(true, true, CompareFunction::Less));
        DrawQuad(device, effect, -1.0f, -kThird, 0.3f, kRed);
        device.setDepthStencilStateProperty(MakeDepthState(true, true, CompareFunction::Greater));
        DrawQuad(device, effect, -kThird, kThird, 0.7f, kGreen);
        device.setDepthStencilStateProperty(MakeDepthState(true, true, CompareFunction::Less));
        DrawQuad(device, effect, kThird, 1.0f, 0.3f, kBlue);
        Check(Exact(ReadPixel(device, kWidth / 6, kHeight / 2), kRed) &&
              Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kGreen) &&
              Exact(ReadPixel(device, 5 * kWidth / 6, kHeight / 2), kBlue),
              "A->B->A capture: Less -> Greater -> Less");

        // ClearOptions::DepthBuffer resets every pixel in the complete active backbuffer.
        SetFullViewport(device, kWidth, kHeight);
        SetRasterizer(device);
        ClearTargetAndDepth(device, kClear, 0.1f);
        device.Clear(ClearOptions::DepthBuffer, kOtherClear, 0.9f, 0);
        device.setDepthStencilStateProperty(MakeDepthState(true, true, CompareFunction::Less));
        DrawQuad(device, effect, -1.0f, 1.0f, 0.8f, kGreen);
        Check(AllExact(ReadAll(device, kWidth, kHeight), kGreen),
              "depth-only clear resets the complete active backbuffer");

        // GFX-079 contract: compare the post-Viewport.MinDepth/MaxDepth value, not raw NDC z.
        Viewport depthViewport(0, 0, kWidth, kHeight);
        depthViewport.setMinDepthProperty(0.2f);
        depthViewport.setMaxDepthProperty(0.8f);
        device.setViewportProperty(depthViewport);
        ClearTargetAndDepth(device, kOtherClear, 0.5f);
        device.setDepthStencilStateProperty(MakeDepthState(true, true, CompareFunction::Less));
        DrawQuad(device, effect, -1.0f, 1.0f, 0.6f, kRed); // maps to 0.56: reject
        Check(Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kOtherClear),
              "Viewport depth range maps z=0.6 to 0.56 before comparison");
        DrawQuad(device, effect, -1.0f, 1.0f, 0.4f, kGreen); // maps to 0.44: pass
        Check(Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kGreen),
              "Viewport depth range maps z=0.4 to 0.44 before comparison/write");
        SetFullViewport(device, kWidth, kHeight);

        // GFX-083 contract: polygon offset is applied before the selected comparison and stored only
        // on pass. Positive constant bias pushes z=0.4 behind stored 0.5; zero bias then passes.
        ClearTargetAndDepth(device, kClear, 0.5f);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        SetRasterizer(device, kDepthBias, 0.0f);
        DrawQuad(device, effect, -1.0f, 1.0f, 0.4f, kRed);
        Check(Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kClear),
              "DepthBias is applied before depth comparison");
        SetRasterizer(device);
        DrawQuad(device, effect, -1.0f, 1.0f, 0.4f, kGreen);
        Check(Exact(ReadPixel(device, kWidth / 2, kHeight / 2), kGreen),
              "failed biased fragment did not replace stored depth");

        // Full clear and compare behavior on an offscreen depth target.
        {
            constexpr int rtWidth = 47;
            constexpr int rtHeight = 39;
            RenderTarget2D target(device, rtWidth, rtHeight, false, SurfaceFormat::Color,
                                  DepthFormat::Depth24, 0, RenderTargetUsage::PreserveContents);
            device.SetRenderTarget(&target);
            SetFullViewport(device, rtWidth, rtHeight);
            SetRasterizer(device);
            ClearTargetAndDepth(device, kClear, 0.1f);
            device.Clear(ClearOptions::DepthBuffer, kOtherClear, 0.9f, 0);
            device.setDepthStencilStateProperty(MakeDepthState(true, true, CompareFunction::Less));
            DrawQuad(device, effect, -1.0f, 1.0f, 0.8f, kCyan);
            Check(AllExact(ReadAll(device, rtWidth, rtHeight), kCyan),
                  "depth-only clear resets the complete active RenderTarget2D");
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            SetFullViewport(device, kWidth, kHeight);
        }

        // Backbuffer, target A, and target B own isolated depth buffers. Distinct comparison
        // directions/depths make accidental sharing visible across backbuffer->A->B->A->B->backbuffer.
        {
            constexpr int aWidth = 40;
            constexpr int aHeight = 32;
            constexpr int bWidth = 52;
            constexpr int bHeight = 36;
            RenderTarget2D targetA(device, aWidth, aHeight, false, SurfaceFormat::Color,
                                   DepthFormat::Depth24, 0, RenderTargetUsage::PreserveContents);
            RenderTarget2D targetB(device, bWidth, bHeight, false, SurfaceFormat::Color,
                                   DepthFormat::Depth24, 0, RenderTargetUsage::PreserveContents);

            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            SetFullViewport(device, kWidth, kHeight);
            ClearTargetAndDepth(device, kOtherClear, 0.5f);
            device.setDepthStencilStateProperty(MakeDepthState(true, true, CompareFunction::Equal));
            DrawQuad(device, effect, -1.0f, 1.0f, 0.5f, kBlue);
            const Color initialBackbuffer = ReadPixel(device, kWidth / 2, kHeight / 2);

            device.SetRenderTarget(&targetA);
            SetFullViewport(device, aWidth, aHeight);
            ClearTargetAndDepth(device, kClear, 0.6f);
            device.setDepthStencilStateProperty(MakeDepthState(true, true, CompareFunction::Less));
            DrawQuad(device, effect, -1.0f, 1.0f, 0.2f, kRed);
            const Color initialA = ReadPixel(device, aWidth / 2, aHeight / 2);

            device.SetRenderTarget(&targetB);
            SetFullViewport(device, bWidth, bHeight);
            ClearTargetAndDepth(device, kOtherClear, 0.4f);
            device.setDepthStencilStateProperty(MakeDepthState(true, true, CompareFunction::Greater));
            DrawQuad(device, effect, -1.0f, 1.0f, 0.8f, kGreen);
            const Color initialB = ReadPixel(device, bWidth / 2, bHeight / 2);
            Check(Exact(initialBackbuffer, kBlue) && Exact(initialA, kRed) && Exact(initialB, kGreen),
                  "backbuffer and two RenderTarget2D depth targets establish distinct contents");

            device.SetRenderTarget(&targetA);
            SetFullViewport(device, aWidth, aHeight);
            device.setDepthStencilStateProperty(MakeDepthState(true, true, CompareFunction::Less));
            DrawQuad(device, effect, -1.0f, 1.0f, 0.5f, kCyan); // fails against A's 0.2
            const Color restoredA = ReadPixel(device, aWidth / 2, aHeight / 2);

            device.SetRenderTarget(&targetB);
            SetFullViewport(device, bWidth, bHeight);
            device.setDepthStencilStateProperty(MakeDepthState(true, true, CompareFunction::Greater));
            DrawQuad(device, effect, -1.0f, 1.0f, 0.6f, kMagenta); // fails against B's 0.8
            const Color restoredB = ReadPixel(device, bWidth / 2, bHeight / 2);

            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            SetFullViewport(device, kWidth, kHeight);
            device.setDepthStencilStateProperty(MakeDepthState(true, true, CompareFunction::Greater));
            DrawQuad(device, effect, -1.0f, 1.0f, 0.4f, kYellow); // fails against backbuffer 0.5
            const Color restoredBackbuffer = ReadPixel(device, kWidth / 2, kHeight / 2);
            Check(Exact(restoredA, kRed) && Exact(restoredB, kGreen) &&
                  Exact(restoredBackbuffer, kBlue),
                  "A->B->A target transitions restore each target's isolated depth buffer");

            // A depth-only clear changes A only; target B and the backbuffer retain depth/color.
            device.SetRenderTarget(&targetA);
            SetFullViewport(device, aWidth, aHeight);
            device.Clear(ClearOptions::DepthBuffer, kOtherClear, 0.9f, 0);
            device.setDepthStencilStateProperty(MakeDepthState(true, true, CompareFunction::Less));
            DrawQuad(device, effect, -1.0f, 1.0f, 0.5f, kCyan);
            const Color clearedA = ReadPixel(device, aWidth / 2, aHeight / 2);

            device.SetRenderTarget(&targetB);
            SetFullViewport(device, bWidth, bHeight);
            const Color untouchedB = ReadPixel(device, bWidth / 2, bHeight / 2);
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            SetFullViewport(device, kWidth, kHeight);
            const Color untouchedBackbuffer = ReadPixel(device, kWidth / 2, kHeight / 2);
            Check(Exact(clearedA, kCyan) && Exact(untouchedB, kGreen) &&
                  Exact(untouchedBackbuffer, kBlue),
                  "active-target depth clear is isolated from other depth targets");
        }

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    SoftwareDepthStateTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(kWidth);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(kHeight);
        graphicsDeviceManager_->setPreferredDepthStencilFormatProperty(DepthFormat::Depth24Stencil8);
    }

    int getResult() const { return result_; }
};

int main()
{
    SoftwareDepthStateTest game;
    game.Run();
    return game.getResult();
}
