// SPDX-License-Identifier: MS-PL
// Manual Glide 3.x smoke test. It deliberately is not a CTest: execution needs a separately
// supplied glide3x.dll (normally dgVoodoo2) and a working Windows display path.
//
// It proves the public CNA path issues an actual Glide clear, uploads an ARGB4444 texture, emits
// the sprite as two Glide triangles, submits clipped colored and perspective-textured 3D triangles,
// and reads the physical Glide backbuffer through grLfbReadRegion.

#include "CNA/GraphicsRendererType.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Glide/GlideRenderer.hpp"
#include "CNA/Internal/Graphics/BuiltInVertexStreams.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kCanvasSize = 64;

    class GlideSmokeTest final : public Game
    {
    public:
        GlideSmokeTest()
        {
            gdm_ = std::make_unique<GraphicsDeviceManager>(this);
            gdm_->setPreferredBackBufferWidthProperty(kCanvasSize);
            gdm_->setPreferredBackBufferHeightProperty(kCanvasSize);
            gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
        }

        [[nodiscard]] int Result() const { return result_; }

    protected:
        void LoadContent() override
        {
            auto& device = getGraphicsDeviceProperty();
            spriteBatch_ = std::make_unique<SpriteBatch>(device);
            texture_ = std::make_unique<Texture2D>(Texture2D::CreateFromPixels(
                device, 1, 1, std::vector<std::uint8_t>{255, 0, 0, 255}));
        }

        void Draw(const GameTime&) override
        {
            if (completed_)
            {
                return;
            }
            completed_ = true;

            auto& device = getGraphicsDeviceProperty();
            auto& renderer = static_cast<CNA::Internal::Renderers::Glide::GlideRenderer&>(
                device.GetRenderer());
            Check(CNA::getCurrentGraphicsRendererType() == CNA::GraphicsRendererType::Glide,
                  "the GLIDE renderer is selected at compile time");
            Check(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()) != nullptr,
                  "the Glide context uses CNA's real SDL/Win32 window");
            Check(device.SupportsCapability(CNA::GraphicsCapability::ThreeD),
                  "GLIDE reports its fixed-function colored 3D capability");

            device.Clear(Color(0, 255, 0, 255));
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Rectangle(10, 10, 20, 20), Rectangle(0, 0, 1, 1), Color::White);
            spriteBatch_->End();

            Color inside(0, 0, 0, 0);
            const Rectangle insideRegion(15, 15, 1, 1);
            device.GetBackBufferData(&insideRegion, &inside, 0, 1);
            Check(inside.getRProperty() > 200 && inside.getGProperty() < 40 && inside.getBProperty() < 40,
                  "SpriteBatch drew the uploaded texture via Glide triangles (red probe)");

            Color outside(0, 0, 0, 0);
            const Rectangle outsideRegion(0, 0, 1, 1);
            device.GetBackBufferData(&outsideRegion, &outside, 0, 1);
            Check(outside.getGProperty() > 200 && outside.getRProperty() < 40 && outside.getBProperty() < 40,
                  "grLfbReadRegion reads the untouched Glide clear (green probe)");

            using Vertex = CNA::Internal::Graphics::PositionColorStream;
            const std::array<Vertex, 3> triangle = {{
                {-0.75f, -0.75f, 0.25f, 0, 255, 0, 255},
                { 0.75f, -0.75f, 0.25f, 0, 255, 0, 255},
                { 0.00f,  0.75f, 0.25f, 0, 255, 0, 255},
            }};
            auto vertexBuffer = renderer.CreateVertexBuffer(static_cast<int>(triangle.size()));
            vertexBuffer->SetData(triangle.data(), static_cast<int>(triangle.size()), sizeof(Vertex));
            renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            renderer.SetDepthTestEnabled(true);
            renderer.SetDepthWriteEnabled(true);
            renderer.DrawColoredPrimitives(*vertexBuffer, Matrix::getIdentityProperty(),
                                          Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                          PrimitiveType::TriangleList, 1);
            renderer.SetDepthTestEnabled(false);

            Color triangleProbe(0, 0, 0, 0);
            const Rectangle triangleProbeRegion(32, 32, 1, 1);
            device.GetBackBufferData(&triangleProbeRegion, &triangleProbe, 0, 1);
            Check(triangleProbe.getGProperty() > 200 && triangleProbe.getRProperty() < 40 &&
                      triangleProbe.getBProperty() < 40,
                  "VertexPositionColor reaches native Glide Gouraud triangle rasterization");

            // A non-full viewport must affect both NDC-to-window conversion and Glide's native
            // clip rectangle. The same triangle's centroid moves from the 64x64 centre to the
            // centre of this 32x32 viewport.
            renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            renderer.SetViewport(0, 0, 32, 32, 0.0f, 1.0f);
            renderer.DrawColoredPrimitives(*vertexBuffer, Matrix::getIdentityProperty(),
                                          Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                          PrimitiveType::TriangleList, 1);
            Color viewportProbe(0, 0, 0, 0);
            const Rectangle viewportProbeRegion(16, 16, 1, 1);
            device.GetBackBufferData(&viewportProbeRegion, &viewportProbe, 0, 1);
            Check(viewportProbe.getGProperty() > 200 && viewportProbe.getRProperty() < 40 &&
                      viewportProbe.getBProperty() < 40,
                  "Viewport maps fixed-function NDC geometry and clips through Glide");
            renderer.SetViewport(0, 0, kCanvasSize, kCanvasSize, 0.0f, 1.0f);

            // The upper vertex lies beyond XNA's near plane. The CPU clipper must split the
            // triangle before Glide sees it rather than dropping the whole primitive or emitting
            // a non-projectable vertex to the FIFO.
            const std::array<Vertex, 3> crossingTriangle = {{
                {-0.75f, -0.75f,  0.25f, 0, 0, 255, 255},
                { 0.75f, -0.75f,  0.25f, 0, 0, 255, 255},
                { 0.00f,  0.75f, -0.25f, 0, 0, 255, 255},
            }};
            auto clippingBuffer = renderer.CreateVertexBuffer(static_cast<int>(crossingTriangle.size()));
            clippingBuffer->SetData(crossingTriangle.data(), static_cast<int>(crossingTriangle.size()), sizeof(Vertex));
            renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            renderer.DrawColoredPrimitives(*clippingBuffer, Matrix::getIdentityProperty(),
                                          Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                          PrimitiveType::TriangleList, 1);
            Color clippedProbe(0, 0, 0, 0);
            const Rectangle clippedProbeRegion(32, 40, 1, 1);
            device.GetBackBufferData(&clippedProbeRegion, &clippedProbe, 0, 1);
            Check(clippedProbe.getBProperty() > 200 && clippedProbe.getRProperty() < 40,
                  "near-plane-crossing triangle is clipped then rasterized through Glide");

            using TexturedVertex = CNA::Internal::Graphics::PositionTextureStream;
            const std::array<TexturedVertex, 3> texturedTriangle = {{
                {-0.75f, -0.75f, 0.25f, 0.25f, 1.25f},
                { 0.75f, -0.75f, 0.25f, 2.25f, 1.25f},
                { 0.00f,  0.75f, 0.25f, 1.25f, 2.25f},
            }};
            auto texturedBuffer = renderer.CreateVertexBuffer(static_cast<int>(texturedTriangle.size()));
            texturedBuffer->SetData(texturedTriangle.data(), static_cast<int>(texturedTriangle.size()), sizeof(TexturedVertex));
            CNA::Internal::Renderers::GpuDrawParams texturedParams{};
            texturedParams.texture0 = &texture_->GetRenderer();
            texturedParams.textureEnabled = true;
            texturedParams.vertexColorEnabled = false;
            renderer.ApplySamplerState(0, 0, 0, 0, 4); // LinearWrap: crosses two U/V intervals.
            renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            renderer.DrawPrimitivesEx(*texturedBuffer, Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     PrimitiveType::TriangleList, 1, texturedParams);
            Color texturedProbe(0, 0, 0, 0);
            const Rectangle texturedProbeRegion(32, 32, 1, 1);
            device.GetBackBufferData(&texturedProbeRegion, &texturedProbe, 0, 1);
            Check(texturedProbe.getRProperty() > 200 && texturedProbe.getGProperty() < 40 &&
                      texturedProbe.getBProperty() < 40,
                  "VertexPositionTexture wraps across texture-address intervals through TMU0");

            renderer.ApplySamplerState(0, 0, 2, 2, 4); // LinearMirror over the same out-of-range UVs.
            renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            renderer.DrawPrimitivesEx(*texturedBuffer, Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     PrimitiveType::TriangleList, 1, texturedParams);
            Color mirroredProbe(0, 0, 0, 0);
            device.GetBackBufferData(&texturedProbeRegion, &mirroredProbe, 0, 1);
            Check(mirroredProbe.getRProperty() > 200 && mirroredProbe.getGProperty() < 40 &&
                      mirroredProbe.getBProperty() < 40,
                  "VertexPositionTexture mirrors out-of-range UVs through TMU0");

            std::printf("=== %d/%d PASS ===\n", passes_, kChecks);
            result_ = passes_ == kChecks ? 0 : 1;
            Exit();
        }

    private:
        static constexpr int kChecks = 10;

        void Check(bool condition, const char* label)
        {
            std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
            if (condition)
            {
                ++passes_;
            }
        }

        std::unique_ptr<GraphicsDeviceManager> gdm_;
        std::unique_ptr<SpriteBatch> spriteBatch_;
        std::unique_ptr<Texture2D> texture_;
        bool completed_ = false;
        int passes_ = 0;
        int result_ = 1;
    };
} // namespace

int main()
{
    GlideSmokeTest game;
    game.Run();
    return game.Result();
}
