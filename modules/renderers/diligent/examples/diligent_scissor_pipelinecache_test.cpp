// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-58: real-device proof that toggling RasterizerState.ScissorTestEnable
// is not silently swallowed by the pipeline cache.
//
// Diligent pipeline state objects are immutable: RasterizerDesc.ScissorEnable is baked in at
// creation time, not a per-draw dynamic state. DiligentRenderer::PipelineKey is the only
// thing GetOrCreatePipeline() consults to decide "reuse an existing PSO" vs. "create a new one".
// Before this task, ScissorEnable was read live off a separate `scissorEnabled_` member at pipeline
// *build* time but was absent from PipelineKey itself -- so once a pipeline was cached for one
// ShaderVariant/topology/blend/depth/stencil/format/sample-count combination, EVERY later draw with
// that exact same combination reused it verbatim, regardless of what ApplyRasterizerState() had
// most recently set ScissorEnable to. Concretely: draw once with scissor off (caches a PSO with
// ScissorEnable=false), then draw again with scissor on and an unchanged everything-else -- the
// second draw would silently render unclipped, because the cached PSO from the first draw is
// reused untouched.
//
// Each of SpriteBatch, an indexed 3D draw and a hardware-instanced draw hold every other pipeline
// axis (variant, topology, blend, depth/stencil, target format, sample count) fixed across a whole
// sequence, so the only thing that could make GetOrCreatePipeline() return a stale PSO is a missing
// scissorEnable key field. Two sequences per path, matching the task's own acceptance criteria:
//   - off -> on -> off (catches "enabling scissor after an off-draw is ignored")
//   - on -> off -> on  (catches "disabling scissor after an on-draw is ignored", the mirror image)
// Sampling point: the scissor rectangle always covers only the right half of the canvas. The left
// sample point is BLACK (the clear colour) whenever scissor is genuinely enabled, and the draw
// colour whenever it is genuinely disabled -- that is the one pixel whose expected value flips with
// the bug. The right sample point stays the draw colour throughout (inside the scissor rect either
// way), which mainly proves the draw actually executed rather than failing silently.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 128;
    constexpr int kLeftX = kSize / 4;
    constexpr int kRightX = 3 * kSize / 4;
    constexpr int kCenterY = kSize / 2;

    bool ColorNear(Color a, Color b, int tolerance = 20)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tolerance &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tolerance &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tolerance;
    }

    Color ReadPixel(GraphicsDevice& device, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    Vector3 ToUnitRgb(Color c)
    {
        return Vector3(c.getRProperty() / 255.0f, c.getGProperty() / 255.0f,
                       c.getBProperty() / 255.0f);
    }

    /// Row-major identity-translation 4x4, matching kInstancedVertexHlsl's per-instance stream
    /// convention (four consecutive float4 rows).
    struct InstanceMatrixRowMajor
    {
        float m[16];
    };

    InstanceMatrixRowMajor IdentityRowMajor()
    {
        InstanceMatrixRowMajor result{};
        result.m[0] = 1.0f;
        result.m[5] = 1.0f;
        result.m[10] = 1.0f;
        result.m[15] = 1.0f;
        return result;
    }
}

class DiligentScissorPipelineCacheTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    SpriteBatch* spriteBatch_ = nullptr;
    Texture2D whiteTexture_;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const std::string& label)
    {
        ++totalCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok)
            ++passCount_;
    }

    using DrawFn = std::function<void(GraphicsDevice&, Color, bool)>;

    void RunSequence(GraphicsDevice& device, const char* pathLabel, const char* seqLabel,
                      const std::vector<bool>& scissorStates, const std::vector<Color>& drawColors,
                      const DrawFn& drawFn)
    {
        for (std::size_t i = 0; i < scissorStates.size(); ++i)
        {
            device.Clear(Color::Black);
            drawFn(device, drawColors[i], scissorStates[i]);

            const Color left = ReadPixel(device, kLeftX, kCenterY);
            const Color right = ReadPixel(device, kRightX, kCenterY);
            const Color expectedLeft = scissorStates[i] ? Color::Black : drawColors[i];

            char buf[224];
            std::snprintf(buf, sizeof(buf),
                          "%s %s step %zu (scissor=%s): left=(%d,%d,%d) expected %s",
                          pathLabel, seqLabel, i, scissorStates[i] ? "on" : "off",
                          left.getRProperty(), left.getGProperty(), left.getBProperty(),
                          scissorStates[i] ? "black (clipped)" : "draw colour (not clipped)");
            Check(ColorNear(left, expectedLeft), buf);

            std::snprintf(buf, sizeof(buf),
                          "%s %s step %zu (scissor=%s): right=(%d,%d,%d) expected draw colour",
                          pathLabel, seqLabel, i, scissorStates[i] ? "on" : "off",
                          right.getRProperty(), right.getGProperty(), right.getBProperty());
            Check(ColorNear(right, drawColors[i]), buf);
        }
    }

    void RunPath(GraphicsDevice& device, const char* pathLabel, const DrawFn& drawFn)
    {
        const std::vector<Color> threeColors = {Color::Red, Color::Lime, Color::Blue};
        RunSequence(device, pathLabel, "off->on->off", {false, true, false}, threeColors, drawFn);
        RunSequence(device, pathLabel, "on->off->on", {true, false, true}, threeColors, drawFn);
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = new SpriteBatch(getGraphicsDeviceProperty());
        whiteTexture_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                    std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        device.setScissorRectangleProperty(Rectangle(kSize / 2, 0, kSize - kSize / 2, kSize));

        // Path 1: SpriteBatch. Begin() always assigns the RasterizerState it is handed (or the
        // CullCounterClockwise default), so the scissor toggle is threaded through Begin()'s own
        // parameter rather than a separate setRasterizerStateProperty() call.
        RunPath(device, "SpriteBatch", [this](GraphicsDevice& dev, Color color, bool scissorOn)
        {
            RasterizerState rs;
            rs.setCullModeProperty(CullMode::None);
            rs.setScissorTestEnableProperty(scissorOn);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr,
                                &rs);
            spriteBatch_->Draw(whiteTexture_, Rectangle(0, 0, kSize, kSize), color);
            spriteBatch_->End();
        });

        // Path 2: indexed 3D draw (Colored3D variant, BasicEffect without vertex colour so the
        // draw colour comes from DiffuseColor and doesn't require re-uploading the vertex buffer
        // per step).
        VertexPositionColor quadVerts[4] = {
            {Vector3(-1.0f, 1.0f, 0.0f), Color::White},
            {Vector3(-1.0f, -1.0f, 0.0f), Color::White},
            {Vector3(1.0f, -1.0f, 0.0f), Color::White},
            {Vector3(1.0f, 1.0f, 0.0f), Color::White},
        };
        VertexBuffer quadVb(device, 4);
        quadVb.SetData(quadVerts, 4);
        const std::uint16_t quadIndices[6] = {0, 1, 2, 0, 2, 3};
        IndexBuffer quadIb(device, 6);
        quadIb.SetData(quadIndices, 6);

        BasicEffect indexed3DEffect(device);
        indexed3DEffect.setWorldProperty(Matrix::getIdentityProperty());
        indexed3DEffect.setViewProperty(Matrix::getIdentityProperty());
        indexed3DEffect.setProjectionProperty(Matrix::getIdentityProperty());

        RunPath(device, "Indexed3D", [&](GraphicsDevice& dev, Color color, bool scissorOn)
        {
            RasterizerState rs;
            rs.setCullModeProperty(CullMode::None);
            rs.setScissorTestEnableProperty(scissorOn);
            dev.setRasterizerStateProperty(rs);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.SetDepthTestEnabled(false);

            indexed3DEffect.setDiffuseColorProperty(ToUnitRgb(color));
            indexed3DEffect.Apply();
            dev.SetVertexBuffer(&quadVb);
            dev.SetIndexBuffer(&quadIb);
            dev.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2);
        });

        // Path 3: hardware-instanced draw (Instanced3D variant), a single identity-transform
        // instance over the same full-screen quad geometry. Slot 0's PSO layout element hardcodes a
        // 16-byte stride (DILIGENT-43's own note: a real VertexPositionColor stream, colour unused
        // by the shader) rather than deriving it from the bound buffer, so this must upload a real
        // VertexPositionColor buffer via the typed SetData() overload -- not a raw 12-byte Vector3
        // buffer -- to avoid corrupting every vertex fetch after the first (same gotcha
        // diligent_instanced_test.cpp already documents).
        VertexPositionColor posOnlyQuad[4] = {
            {Vector3(-1.0f, 1.0f, 0.0f), Color::White}, {Vector3(-1.0f, -1.0f, 0.0f), Color::White},
            {Vector3(1.0f, -1.0f, 0.0f), Color::White}, {Vector3(1.0f, 1.0f, 0.0f), Color::White},
        };
        VertexBuffer perVertexVb(device, 4);
        perVertexVb.SetData(posOnlyQuad, 4);
        const std::uint16_t instIndices[6] = {0, 1, 2, 0, 2, 3};
        IndexBuffer instIb(device, 6);
        instIb.SetData(instIndices, 6);
        InstanceMatrixRowMajor identityInstance = IdentityRowMajor();
        VertexBuffer instanceVb(device, 1);
        instanceVb.SetDataRaw(&identityInstance, 1, static_cast<int>(sizeof(InstanceMatrixRowMajor)));

        BasicEffect instancedEffect(device);
        instancedEffect.setWorldProperty(Matrix::getIdentityProperty());
        instancedEffect.setViewProperty(Matrix::getIdentityProperty());
        instancedEffect.setProjectionProperty(Matrix::getIdentityProperty());

        RunPath(device, "Instanced", [&](GraphicsDevice& dev, Color color, bool scissorOn)
        {
            RasterizerState rs;
            rs.setCullModeProperty(CullMode::None);
            rs.setScissorTestEnableProperty(scissorOn);
            dev.setRasterizerStateProperty(rs);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.SetDepthTestEnabled(false);

            instancedEffect.setDiffuseColorProperty(ToUnitRgb(color));
            instancedEffect.Apply();
            dev.SetVertexBuffer(&perVertexVb);
            std::vector<VertexBufferBinding> bindings = {
                VertexBufferBinding(&perVertexVb, 0, 0),
                VertexBufferBinding(&instanceVb, 0, 1),
            };
            dev.SetVertexBuffers(bindings);
            dev.SetIndexBuffer(&instIb);
            dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 1);
        });

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = passCount_ == totalCount_ ? 0 : 1;
        Exit();
    }

public:
    DiligentScissorPipelineCacheTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(kSize);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentScissorPipelineCacheTest game;
        game.Run();
        return game.GetResult();
    }
    catch (const std::exception& error)
    {
        const std::string message = error.what();
        const bool noDevice = message.find("no device type could be created") != std::string::npos ||
                              message.find("unsupported SDL video driver") != std::string::npos ||
                              message.find("SDL_Init") != std::string::npos ||
                              message.find("live SDL window") != std::string::npos;
        if (noDevice)
        {
            std::printf("[SKIP] CNA Diligent smoke: no usable device (%s)\n", error.what());
            return 77;
        }
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        return 1;
    }
}
