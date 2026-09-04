// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-45: real-device proof that the Diligent renderer honors non-zero
// XNA DrawPrimitives(vertexStart) / DrawIndexedPrimitives(baseVertex, startIndex) offsets, rather
// than silently drawing from the start of the bound buffer regardless of what the caller asked
// for. This is the Diligent counterpart of directx9_drawoffset_test.cpp, adapted to the public XNA API
// (BasicEffect/GraphicsDevice) and to the vertex formats this renderer actually supports (stride 16
// Colored3D; no PbrEffect and no stride-56 SkinnedVertexColor here, both unimplemented on this
// renderer).
//
// Discrimination is POSITION-based (shader-independent): two disjoint triangles are laid out in the
// vertex buffer so that the LEFT triangle covers only the LEFT probe pixel and the RIGHT triangle
// covers only the RIGHT probe pixel. Each check requests the offset that should select the RIGHT
// triangle. Post-fix the RIGHT probe is painted and the LEFT probe stays at the clear colour;
// silently drawing from offset 0 instead would paint the LEFT probe and leave the RIGHT one clear --
// the check fails.
//
// Check A -- DrawPrimitives, non-zero vertexStart.
// Check B -- DrawIndexedPrimitives, non-zero startIndex.
// Check C -- DrawIndexedPrimitives, non-zero baseVertex.
// Check D -- DrawIndexedPrimitives, non-zero startIndex AND baseVertex combined. The middle vertex
//   range is deliberately off-screen so only applying BOTH offsets lands on the RIGHT triangle.
// Check E -- DrawInstancedPrimitivesEx, non-zero startIndex AND baseVertex combined, on the
//   per-vertex stream, with a single identity-transform instance -- the per-instance stream's own
//   offset (always 0 here) must not be confused with baseVertex/startIndex on the per-vertex stream.
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
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    // LEFT triangle occupies NDC x<=0 (apex at the origin, base far to the left), so it covers the
    // LEFT probe and never the RIGHT one. The RIGHT triangle mirrors it (x>=0).
    const VertexPositionColor kLeftRight[6] = {
        {Vector3(-3.0f, -3.0f, 0.0f), Color::Red}, {Vector3(-3.0f, 3.0f, 0.0f), Color::Red},
        {Vector3(0.0f, 0.0f, 0.0f), Color::Red}, // LEFT
        {Vector3(3.0f, -3.0f, 0.0f), Color::Red}, {Vector3(3.0f, 3.0f, 0.0f), Color::Red},
        {Vector3(0.0f, 0.0f, 0.0f), Color::Red}, // RIGHT
    };
    // 0-2 LEFT, 3-5 OFF-SCREEN (clipped away), 6-8 RIGHT.
    const VertexPositionColor kLeftOffRight[9] = {
        {Vector3(-3.0f, -3.0f, 0.0f), Color::Red}, {Vector3(-3.0f, 3.0f, 0.0f), Color::Red},
        {Vector3(0.0f, 0.0f, 0.0f), Color::Red}, // LEFT
        {Vector3(5.0f, 5.0f, 0.0f), Color::Red}, {Vector3(6.0f, 5.0f, 0.0f), Color::Red},
        {Vector3(5.0f, 6.0f, 0.0f), Color::Red}, // OFF-SCREEN
        {Vector3(3.0f, -3.0f, 0.0f), Color::Red}, {Vector3(3.0f, 3.0f, 0.0f), Color::Red},
        {Vector3(0.0f, 0.0f, 0.0f), Color::Red}, // RIGHT
    };

    // Small per-instance triangles: 0-2 = A (left), 3-5 off-screen, 6-8 = B (right). The Instanced3D
    // pipeline's slot-0 layout element hardcodes a 16-byte stride (VertexPositionColor-shaped, only
    // Position read), so this stream must use that same type even though the colour is unused.
    const VertexPositionColor kInstLeftOffRight[9] = {
        {Vector3(-0.6f, -0.1f, 0.0f), Color::Red}, {Vector3(-0.4f, -0.1f, 0.0f), Color::Red},
        {Vector3(-0.6f, 0.1f, 0.0f), Color::Red}, // A (left)
        {Vector3(5.0f, 5.0f, 0.0f), Color::Red}, {Vector3(6.0f, 5.0f, 0.0f), Color::Red},
        {Vector3(5.0f, 6.0f, 0.0f), Color::Red}, // off-screen
        {Vector3(0.4f, -0.1f, 0.0f), Color::Red}, {Vector3(0.6f, -0.1f, 0.0f), Color::Red},
        {Vector3(0.4f, 0.1f, 0.0f), Color::Red}, // B (right)
    };
    struct InstanceRow
    {
        float m[16];
    };
    const InstanceRow kIdentityInstance = {
        {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1},
    };

    constexpr int kLeftX = 12, kRightX = 52, kProbeY = 32;
    constexpr int kInstLeftX = 14, kInstRightX = 46, kInstProbeY = 34;

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

    bool IsClearBlue(const Color& c)
    {
        return c.getRProperty() == 0 && c.getGProperty() == 0 && c.getBProperty() == 255 &&
               c.getAProperty() == 255;
    }

    // "The intended (RIGHT) probe was painted AND the zero-offset (LEFT) probe was NOT" together
    // prove the geometry landed where the requested offset dictates, not at the offset-0 range.
    bool OffsetHonored(const Color& rightProbe, const Color& leftProbe)
    {
        return !IsClearBlue(rightProbe) && IsClearBlue(leftProbe);
    }
}

class DiligentDrawOffsetTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    bool done_ = false;
    int result_ = 1;

    void CommonState(GraphicsDevice& device)
    {
        device.Clear(Color(0, 0, 255, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
    }

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
        effect.Apply();

        // Check A: DrawPrimitives, non-zero vertexStart.
        {
            VertexBuffer vb(device, 6);
            vb.SetData(kLeftRight, 6);
            CommonState(device);
            device.SetVertexBuffer(&vb);
            device.DrawPrimitives(PrimitiveType::TriangleList, 3, 1);
            check(OffsetHonored(ReadPixel(device, kRightX, kProbeY), ReadPixel(device, kLeftX, kProbeY)),
                  "DrawPrimitives vertexStart=3: RIGHT triangle drawn, not vertices 0..2");
        }

        // Check B: DrawIndexedPrimitives, non-zero startIndex.
        {
            VertexBuffer vb(device, 6);
            vb.SetData(kLeftRight, 6);
            const std::uint16_t idx[6] = {0, 1, 2, 3, 4, 5};
            IndexBuffer ib(device, 6);
            ib.SetData(idx, 6);
            CommonState(device);
            device.SetVertexBuffer(&vb);
            device.SetIndexBuffer(&ib);
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 0, 0, 6, 3, 1);
            check(OffsetHonored(ReadPixel(device, kRightX, kProbeY), ReadPixel(device, kLeftX, kProbeY)),
                  "DrawIndexedPrimitives startIndex=3: reads IB from index 3, RIGHT drawn");
        }

        // Check C: DrawIndexedPrimitives, non-zero baseVertex.
        {
            VertexBuffer vb(device, 6);
            vb.SetData(kLeftRight, 6);
            const std::uint16_t idx[3] = {0, 1, 2};
            IndexBuffer ib(device, 3);
            ib.SetData(idx, 3);
            CommonState(device);
            device.SetVertexBuffer(&vb);
            device.SetIndexBuffer(&ib);
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 0, 1);
            check(OffsetHonored(ReadPixel(device, kRightX, kProbeY), ReadPixel(device, kLeftX, kProbeY)),
                  "DrawIndexedPrimitives baseVertex=3: index+baseVertex -> RIGHT drawn");
        }

        // Check D: DrawIndexedPrimitives, startIndex AND baseVertex combined. Only applying BOTH
        // lands on the RIGHT triangle (verts 6..8); the middle range (verts 3..5) is off-screen.
        {
            VertexBuffer vb(device, 9);
            vb.SetData(kLeftOffRight, 9);
            const std::uint16_t idx[6] = {0, 1, 2, 3, 4, 5};
            IndexBuffer ib(device, 6);
            ib.SetData(idx, 6);
            CommonState(device);
            device.SetVertexBuffer(&vb);
            device.SetIndexBuffer(&ib);
            device.DrawIndexedPrimitives(PrimitiveType::TriangleList, 3, 0, 6, 3, 1);
            check(OffsetHonored(ReadPixel(device, kRightX, kProbeY), ReadPixel(device, kLeftX, kProbeY)),
                  "DrawIndexedPrimitives startIndex=3 + baseVertex=3: BOTH offsets -> RIGHT drawn");
        }

        // Check E: DrawInstancedPrimitivesEx, startIndex AND baseVertex combined on the per-vertex
        // stream. One identity-transform instance; the per-instance stream offset must not be
        // confused with baseVertex/startIndex on the per-vertex stream.
        {
            VertexBuffer vb(device, 9);
            vb.SetData(kInstLeftOffRight, 9);
            const std::uint16_t idx[6] = {0, 1, 2, 3, 4, 5};
            IndexBuffer ib(device, 6);
            ib.SetData(idx, 6);
            VertexBuffer instanceVb(device, 1);
            instanceVb.SetDataRaw(&kIdentityInstance, 1, static_cast<int>(sizeof(InstanceRow)));

            BasicEffect instEffect(device);
            instEffect.setWorldProperty(I);
            instEffect.setViewProperty(I);
            instEffect.setProjectionProperty(I);
            instEffect.setDiffuseColorProperty(Vector3(0.0f, 1.0f, 0.0f));
            instEffect.Apply();

            CommonState(device);
            device.SetVertexBuffer(&vb);
            std::vector<VertexBufferBinding> bindings = {
                VertexBufferBinding(&vb, 0, 0),
                VertexBufferBinding(&instanceVb, 0, 1),
            };
            device.SetVertexBuffers(bindings);
            device.SetIndexBuffer(&ib);
            device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 3, 1, 1);
            check(OffsetHonored(ReadPixel(device, kInstRightX, kInstProbeY),
                                ReadPixel(device, kInstLeftX, kInstProbeY)),
                  "DrawInstancedPrimitivesEx startIndex=3 + baseVertex=3: instance B drawn");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        result_ = passCount == totalCount ? 0 : 1;
        Exit();
    }

public:
    DiligentDrawOffsetTest()
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
        DiligentDrawOffsetTest game;
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
