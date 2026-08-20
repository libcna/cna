// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-43: real-device proof that hardware instancing on the Diligent
// renderer renders every instance at its own per-instance world position from a single
// DrawInstancedPrimitives call, through the public XNA API only.
//
// A small quad is drawn 3 times via one DrawInstancedPrimitives call, each instance carrying its
// own translation (left / centre / right) in a per-instance vertex buffer bound with
// VertexBufferBinding's instanceFrequency. If the renderer genuinely consumed per-instance data
// (rather than, say, silently drawing only the first instance 3 times, or ignoring the instance
// transform and drawing every instance on top of each other), each of the three positions reads
// back the quad's colour and the untouched background between them stays the clear colour.
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
#include <cstdlib>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 96;
    constexpr int kChecks = 4;

    /// Row-major 4x4 translation matrix, 16 floats: matches this renderer's own kInstancedVertexHlsl
    /// convention (four consecutive float4 rows -- translation lives in the last row, M41-M43,
    /// exactly like Matrix::CreateTranslation and every other CNA-Diligent shader's g_World).
    struct InstanceMatrixRowMajor
    {
        float m[16];
    };

    InstanceMatrixRowMajor TranslationRowMajor(float tx, float ty, float tz)
    {
        InstanceMatrixRowMajor result{};
        result.m[0] = 1.0f;
        result.m[5] = 1.0f;
        result.m[10] = 1.0f;
        result.m[15] = 1.0f;
        result.m[12] = tx;
        result.m[13] = ty;
        result.m[14] = tz;
        return result;
    }

    bool ColorNear(Color a, Color b, int tolerance = 30)
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
}

class DiligentInstancedTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    bool done_ = false;
    int passCount_ = 0;
    int result_ = 1;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok)
            ++passCount_;
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        // Task 896's own finding, reused verbatim by every CNA-Diligent instancing/3D test in this
        // family: this quad's winding is back-facing under the real default RasterizerState.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        const Color red(255, 0, 0, 255);
        const VertexPositionColor quadVertices[4] = {
            {Vector3(-0.1f, 0.1f, 0.0f), red},
            {Vector3(-0.1f, -0.1f, 0.0f), red},
            {Vector3(0.1f, -0.1f, 0.0f), red},
            {Vector3(0.1f, 0.1f, 0.0f), red},
        };
        // A plain sizeof(VertexPositionColor) is not the GPU stream's byte layout: Color inherits a
        // polymorphic IPackedVector base, so the C++ struct carries a vtable pointer SetDataRaw
        // would otherwise copy verbatim. The typed SetData() overload packs into the real 16-byte
        // (float3 + uint32 colour) stream this shader's slot-0 layout element expects.
        VertexBuffer perVertexVb(device, 4);
        perVertexVb.SetData(quadVertices, 4);

        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        IndexBuffer ib(device, 6);
        ib.SetData(indices, 6);

        const InstanceMatrixRowMajor instances[3] = {
            TranslationRowMajor(-0.6f, 0.0f, 0.0f),
            TranslationRowMajor(0.0f, 0.0f, 0.0f),
            TranslationRowMajor(0.6f, 0.0f, 0.0f),
        };
        VertexBuffer instanceVb(device, 3);
        instanceVb.SetDataRaw(instances, 3, static_cast<int>(sizeof(InstanceMatrixRowMajor)));

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        effect.Apply();

        device.SetVertexBuffer(&perVertexVb);
        std::vector<VertexBufferBinding> bindings = {
            VertexBufferBinding(&perVertexVb, 0, 0),
            VertexBufferBinding(&instanceVb, 0, 1),
        };
        device.SetVertexBuffers(bindings);
        device.SetIndexBuffer(&ib);

        device.DrawInstancedPrimitives(PrimitiveType::TriangleList,
                                       0,  // baseVertex
                                       0,  // minVertexIndex
                                       4,  // numVertices
                                       0,  // startIndex
                                       2,  // primitiveCount
                                       3); // instanceCount

        const int centerY = kSize / 2;
        const Color leftPixel = ReadPixel(device, kSize / 2 - kSize * 3 / 10, centerY);
        const Color centerPixel = ReadPixel(device, kSize / 2, centerY);
        const Color rightPixel = ReadPixel(device, kSize / 2 + kSize * 3 / 10, centerY);
        const Color backgroundPixel = ReadPixel(device, kSize / 2 - kSize * 45 / 100, centerY);

        std::printf("    (left=(%d,%d,%d) center=(%d,%d,%d) right=(%d,%d,%d) bg=(%d,%d,%d))\n",
                    leftPixel.getRProperty(), leftPixel.getGProperty(), leftPixel.getBProperty(),
                    centerPixel.getRProperty(), centerPixel.getGProperty(), centerPixel.getBProperty(),
                    rightPixel.getRProperty(), rightPixel.getGProperty(), rightPixel.getBProperty(),
                    backgroundPixel.getRProperty(), backgroundPixel.getGProperty(),
                    backgroundPixel.getBProperty());

        Check(ColorNear(leftPixel, red), "instance 0's translated position reads back red");
        Check(ColorNear(centerPixel, red), "instance 1's translated position reads back red");
        Check(ColorNear(rightPixel, red), "instance 2's translated position reads back red");
        Check(ColorNear(backgroundPixel, Color::Lime),
              "the untouched background between instances stays the clear colour");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    DiligentInstancedTest()
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
        DiligentInstancedTest game;
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
