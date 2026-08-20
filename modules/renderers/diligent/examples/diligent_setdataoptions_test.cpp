// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-44: real-device proof that the Diligent renderer's vertex/index buffers
// genuinely re-upload data through SetDataOptions::Discard and SetDataOptions::NoOverwrite, rather
// than silently ignoring the hint (the shared IGraphicsRenderer default) or, worse, uploading once
// and leaving stale GPU content behind.
//
// Check A -- DynamicVertexBuffer::SetData(..., Discard) then a second SetData(..., NoOverwrite)
//   with different geometry: the second draw must read back the SECOND upload's colour, proving
//   NoOverwrite genuinely reaches the GPU buffer rather than being a no-op after the first Discard.
// Check B -- DynamicIndexBuffer::SetData(..., Discard) selecting the LEFT triangle out of a fixed
//   vertex buffer, then SetData(..., NoOverwrite) selecting the RIGHT one: the second draw must
//   paint the RIGHT probe and leave the LEFT one at the clear colour.
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
#include "Microsoft/Xna/Framework/Graphics/DynamicIndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DynamicVertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SetDataOptions.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    int passCount = 0;
    int totalCount = 0;

    void check(bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++passCount;
    }

    Color ReadPixel(GraphicsDevice& device, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    bool ColorNear(Color a, Color b, int tolerance = 12)
    {
        return std::abs(a.getRProperty() - b.getRProperty()) <= tolerance &&
               std::abs(a.getGProperty() - b.getGProperty()) <= tolerance &&
               std::abs(a.getBProperty() - b.getBProperty()) <= tolerance;
    }

    bool IsClearBlue(const Color& c)
    {
        return c.getRProperty() == 0 && c.getGProperty() == 0 && c.getBProperty() == 255 &&
               c.getAProperty() == 255;
    }

    constexpr int kLeftX = 12, kRightX = 52, kProbeY = 32;
}

class DiligentSetDataOptionsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    bool done_ = false;
    int result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const Matrix I = Matrix::getIdentityProperty();

        BasicEffect effect(device);
        effect.setWorldProperty(I);
        effect.setViewProperty(I);
        effect.setProjectionProperty(I);
        effect.VertexColorEnabled = true;
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));

        // Check A: Discard then NoOverwrite into the same DynamicVertexBuffer, different colours.
        {
            const VertexPositionColor discardQuad[6] = {
                {Vector3(-1.0f, 1.0f, 0.0f), Color::Red},  {Vector3(-1.0f, -1.0f, 0.0f), Color::Red},
                {Vector3(1.0f, -1.0f, 0.0f), Color::Red},  {Vector3(-1.0f, 1.0f, 0.0f), Color::Red},
                {Vector3(1.0f, -1.0f, 0.0f), Color::Red},  {Vector3(1.0f, 1.0f, 0.0f), Color::Red},
            };
            const VertexPositionColor noOverwriteQuad[6] = {
                {Vector3(-1.0f, 1.0f, 0.0f), Color::Lime}, {Vector3(-1.0f, -1.0f, 0.0f), Color::Lime},
                {Vector3(1.0f, -1.0f, 0.0f), Color::Lime}, {Vector3(-1.0f, 1.0f, 0.0f), Color::Lime},
                {Vector3(1.0f, -1.0f, 0.0f), Color::Lime}, {Vector3(1.0f, 1.0f, 0.0f), Color::Lime},
            };

            DynamicVertexBuffer vb(device, VertexPositionColor::getVertexDeclarationStatic(), 6,
                                   BufferUsage::None);
            vb.SetData(discardQuad, 0, 6, SetDataOptions::Discard);

            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setBlendStateProperty(BlendState::Opaque);
            device.Clear(Color(0, 0, 255, 255));
            effect.Apply();
            device.SetVertexBuffer(&vb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            check(ColorNear(ReadPixel(device, kSize / 2, kSize / 2), Color::Red),
                  "DynamicVertexBuffer SetData(Discard): first upload renders");

            vb.SetData(noOverwriteQuad, 0, 6, SetDataOptions::NoOverwrite);
            device.Clear(Color(0, 0, 255, 255));
            effect.Apply();
            device.SetVertexBuffer(&vb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            check(ColorNear(ReadPixel(device, kSize / 2, kSize / 2), Color::Lime),
                  "DynamicVertexBuffer SetData(NoOverwrite): second upload actually reaches the GPU");
        }

        // Check B: Discard then NoOverwrite into the same DynamicIndexBuffer, selecting a different
        // triangle out of a fixed LEFT/RIGHT vertex buffer each time.
        {
            const VertexPositionColor leftRight[6] = {
                {Vector3(-3.0f, -3.0f, 0.0f), Color::Red}, {Vector3(-3.0f, 3.0f, 0.0f), Color::Red},
                {Vector3(0.0f, 0.0f, 0.0f), Color::Red}, // LEFT: 0,1,2
                {Vector3(3.0f, -3.0f, 0.0f), Color::Red}, {Vector3(3.0f, 3.0f, 0.0f), Color::Red},
                {Vector3(0.0f, 0.0f, 0.0f), Color::Red}, // RIGHT: 3,4,5
            };
            VertexBuffer vb(device, VertexPositionColor::getVertexDeclarationStatic(), 6,
                            BufferUsage::None);
            vb.SetData(leftRight, 0, 6);

            const std::uint16_t leftIdx[3] = {0, 1, 2};
            const std::uint16_t rightIdx[3] = {3, 4, 5};
            DynamicIndexBuffer ib(device, IndexElementSize::SixteenBits, 3, BufferUsage::None);
            ib.SetData(leftIdx, 0, 3, SetDataOptions::Discard);

            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setBlendStateProperty(BlendState::Opaque);
            device.Clear(Color(0, 0, 255, 255));
            effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect.Apply();
            device.SetVertexBuffer(&vb);
            device.SetIndexBuffer(&ib);
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 0, 1);
            check(!IsClearBlue(ReadPixel(device, kLeftX, kProbeY)) &&
                      IsClearBlue(ReadPixel(device, kRightX, kProbeY)),
                  "DynamicIndexBuffer SetData(Discard): first upload (LEFT indices) renders LEFT");

            ib.SetData(rightIdx, 0, 3, SetDataOptions::NoOverwrite);
            device.Clear(Color(0, 0, 255, 255));
            effect.Apply();
            device.SetVertexBuffer(&vb);
            device.SetIndexBuffer(&ib);
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 0, 1);
            check(IsClearBlue(ReadPixel(device, kLeftX, kProbeY)) &&
                      !IsClearBlue(ReadPixel(device, kRightX, kProbeY)),
                  "DynamicIndexBuffer SetData(NoOverwrite): second upload (RIGHT indices) actually "
                  "reaches the GPU");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        result_ = passCount == totalCount ? 0 : 1;
        Exit();
    }

public:
    DiligentSetDataOptionsTest()
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
        DiligentSetDataOptionsTest game;
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
