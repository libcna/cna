// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-51: real-device proof that RasterizerState.FillMode::WireFrame causes
// the Diligent renderer to rasterize only triangle edges, not interiors, mirroring
// vulkan_fill_mode_test.cpp (Task 327).
//
// Triangle geometry (input NDC, Y is flipped by the vertex shader): top (0, 0.8), BR (1,-0.8),
// BL (-1,-0.8). After the shader's Y-flip these become CW in Vulkan NDC, making them a front face
// under the default CullCounterClockwiseFace rasterizer state. At y=0 (screen centre) the
// left/right edges cross at x=-0.5 and x=+0.5, so the centre pixel at NDC (0,0) is well inside the
// triangle interior.
//
// Sub-test 1 — Solid baseline: centre must be RED (interior filled).
// Sub-test 2 — WireFrame: centre must be BLACK (interior not rasterized).
// Sub-test 3 — Reset to Solid: centre must be RED again (FillMode reverts).
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/FillMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    struct GpuVPC
    {
        float x, y, z;
        std::uint8_t r, g, b, a;
    };
    static_assert(sizeof(GpuVPC) == 16, "GpuVPC must be 16 bytes");
}

class DiligentFillModeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++passCount_;
    }

    static Color ReadCenter(GraphicsDevice& device)
    {
        const auto& viewport = device.getViewportProperty();
        Rectangle region(viewport.getWidthProperty() / 2, viewport.getHeightProperty() / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    static Color DrawAndRead(GraphicsDevice& device, VertexBuffer& vertexBuffer, BasicEffect& effect)
    {
        device.Clear(Color(0, 0, 0, 255));
        device.setBlendStateProperty(BlendState::Opaque);
        effect.Apply();
        device.SetVertexBuffer(&vertexBuffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        device.SetVertexBuffer(nullptr);
        return ReadCenter(device);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        GpuVPC verts[3] = {
            {0.0f, 0.8f, 0.f, 255, 0, 0, 255},
            {1.0f, -0.8f, 0.f, 255, 0, 0, 255},
            {-1.0f, -0.8f, 0.f, 255, 0, 0, 255},
        };
        VertexDeclaration decl(16, {
            VertexElement(0, VertexElementFormat::Vector3, VertexElementUsage::Position, 0),
            VertexElement(12, VertexElementFormat::Color, VertexElementUsage::Color, 0),
        });
        VertexBuffer vb(device, decl, 3, BufferUsage::None);
        vb.SetDataRaw(verts, 3, 16);

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;

        Color pixel(0, 0, 0, 0);
        char buf[160];

        // Sub-test 1: Solid fill (default RasterizerState) -- interior must be rasterized.
        pixel = DrawAndRead(device, vb, effect);
        std::snprintf(buf, sizeof(buf), "Solid: centre=(%d,%d,%d) expected red",
                      pixel.getRProperty(), pixel.getGProperty(), pixel.getBProperty());
        Check(pixel.getRProperty() >= 200 && pixel.getGProperty() <= 30 && pixel.getBProperty() <= 30,
              buf);

        // Sub-test 2: WireFrame -- interior must NOT be rasterized.
        {
            RasterizerState rasterizerState;
            rasterizerState.setFillModeProperty(FillMode::WireFrame);
            device.setRasterizerStateProperty(rasterizerState);
        }
        pixel = DrawAndRead(device, vb, effect);
        std::snprintf(buf, sizeof(buf),
                      "WireFrame: centre=(%d,%d,%d) expected black (interior not rasterized)",
                      pixel.getRProperty(), pixel.getGProperty(), pixel.getBProperty());
        Check(pixel.getRProperty() <= 30 && pixel.getGProperty() <= 30 && pixel.getBProperty() <= 30,
              buf);

        // Sub-test 3: Reset to Solid -- verify FillMode change takes effect.
        {
            RasterizerState rasterizerState;
            rasterizerState.setFillModeProperty(FillMode::Solid);
            device.setRasterizerStateProperty(rasterizerState);
        }
        pixel = DrawAndRead(device, vb, effect);
        std::snprintf(buf, sizeof(buf), "Solid (reset): centre=(%d,%d,%d) expected red again",
                      pixel.getRProperty(), pixel.getGProperty(), pixel.getBProperty());
        Check(pixel.getRProperty() >= 200 && pixel.getGProperty() <= 30 && pixel.getBProperty() <= 30,
              buf);

        std::printf("\nResult: %d/3 PASS\n", passCount_);
        result_ = passCount_ == 3 ? 0 : 1;
        Exit();
    }

public:
    DiligentFillModeTest()
    {
        graphicsDeviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        graphicsDeviceManager_->setPreferredBackBufferWidthProperty(320);
        graphicsDeviceManager_->setPreferredBackBufferHeightProperty(240);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    try
    {
        DiligentFillModeTest game;
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
