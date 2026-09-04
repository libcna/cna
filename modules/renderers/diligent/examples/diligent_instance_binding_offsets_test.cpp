// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-202: real-device proof that the Diligent renderer's instanced route consumes the two
// per-binding properties the removed GpuDrawParams::instanceVb/instanceVertexOffset/
// instanceFrequency trio could not carry, and that the geometry binding's own VertexOffset reaches
// slot 0 as well. Everything here goes through the public XNA API only.
//
// Each leg is written so that "consumed" and "ignored" produce DIFFERENT pixels, and both outcomes
// are in bounds -- a leg whose failure mode is an out-of-range fetch proves nothing about which
// value the renderer actually used.
//
//   Leg A -- the instance binding's VertexOffset. Four instance matrices at x = -0.6/-0.2/0.2/0.6,
//            bound with VertexOffset = 1, three instances drawn. Consumed: matrices 1..3, so 0.6 is
//            lit and -0.6 is background. Ignored: matrices 0..2, so -0.6 is lit and 0.6 is not.
//
//   Leg B -- the instance binding's InstanceFrequency. Four matrices, four instances, frequency 2.
//            Consumed: the matrix index advances once per two instances (0,0,1,1), so only -0.6 and
//            -0.2 are lit and 0.2/0.6 stay background. Ignored (an assumed step rate of 1): the
//            index advances per instance and all four positions are lit.
//
//   Leg C -- the geometry binding's VertexOffset. Eight vertices holding two quads (left at -0.6,
//            right at 0.6), bound with VertexOffset = 4 and drawn with one identity instance.
//            Consumed: the right quad renders. Ignored: the left one does.
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
    // 4 (leg A) + 4 (leg B) + 2 (leg C) + 2 capability assertions.
    constexpr int kChecks = 12;

    /// Row-major 4x4, four consecutive float4 rows -- this renderer's own kInstancedVertexHlsl
    /// convention, identical to diligent_instanced_test.cpp's.
    struct InstanceMatrixRowMajor
    {
        float m[16];
    };

    InstanceMatrixRowMajor TranslationRowMajor(float tx)
    {
        InstanceMatrixRowMajor result{};
        result.m[0] = 1.0f;
        result.m[5] = 1.0f;
        result.m[10] = 1.0f;
        result.m[15] = 1.0f;
        result.m[12] = tx;
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

    /// Back-buffer x for a clip-space x, at the vertical centre.
    int PixelForClipX(float clipX)
    {
        return static_cast<int>((clipX * 0.5f + 0.5f) * static_cast<float>(kSize));
    }
}

class DiligentInstanceBindingOffsetsTest : public Game
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

    static void PrepareState(GraphicsDevice& device)
    {
        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        // Task 896: this quad's winding is back-facing under the real default RasterizerState.
        device.setRasterizerStateProperty(RasterizerState::CullNone);
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        const Color red(255, 0, 0, 255);
        // Half-width 0.12 keeps the four probe columns (0.4 apart) cleanly disjoint.
        const VertexPositionColor quad[4] = {
            {Vector3(-0.12f, 0.12f, 0.0f), red},
            {Vector3(-0.12f, -0.12f, 0.0f), red},
            {Vector3(0.12f, -0.12f, 0.0f), red},
            {Vector3(0.12f, 0.12f, 0.0f), red},
        };
        VertexBuffer perVertexVb(device, 4);
        perVertexVb.SetData(quad, 4);

        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};
        IndexBuffer ib(device, 6);
        ib.SetData(indices, 6);

        const InstanceMatrixRowMajor instances[4] = {
            TranslationRowMajor(-0.6f),
            TranslationRowMajor(-0.2f),
            TranslationRowMajor(0.2f),
            TranslationRowMajor(0.6f),
        };
        VertexBuffer instanceVb(device, 4);
        instanceVb.SetDataRaw(instances, 4, static_cast<int>(sizeof(InstanceMatrixRowMajor)));

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        effect.Apply();

        const int centerY = kSize / 2;
        const int columnX[4] = {PixelForClipX(-0.6f), PixelForClipX(-0.2f),
                                PixelForClipX(0.2f), PixelForClipX(0.6f)};

        // ---- Leg A: the instance binding's own VertexOffset ----
        PrepareState(device);
        device.SetVertexBuffer(&perVertexVb);
        {
            std::vector<VertexBufferBinding> bindings = {
                VertexBufferBinding(&perVertexVb, 0, 0),
                VertexBufferBinding(&instanceVb, 1, 1),   // VertexOffset = 1, frequency = 1
            };
            device.SetVertexBuffers(bindings);
            device.SetIndexBuffer(&ib);
            device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 3);
        }
        const Color a[4] = {
            ReadPixel(device, columnX[0], centerY), ReadPixel(device, columnX[1], centerY),
            ReadPixel(device, columnX[2], centerY), ReadPixel(device, columnX[3], centerY)};
        std::printf("    legA: (%d,%d,%d) (%d,%d,%d) (%d,%d,%d) (%d,%d,%d)\n",
                    a[0].getRProperty(), a[0].getGProperty(), a[0].getBProperty(),
                    a[1].getRProperty(), a[1].getGProperty(), a[1].getBProperty(),
                    a[2].getRProperty(), a[2].getGProperty(), a[2].getBProperty(),
                    a[3].getRProperty(), a[3].getGProperty(), a[3].getBProperty());
        Check(ColorNear(a[0], Color::Lime),
              "instance VertexOffset=1: matrix 0's column is NOT drawn");
        Check(ColorNear(a[1], red), "instance VertexOffset=1: matrix 1's column is drawn");
        Check(ColorNear(a[2], red), "instance VertexOffset=1: matrix 2's column is drawn");
        Check(ColorNear(a[3], red), "instance VertexOffset=1: matrix 3's column is drawn");

        // ---- Leg B: the instance binding's own InstanceFrequency ----
        PrepareState(device);
        device.SetVertexBuffer(&perVertexVb);
        {
            std::vector<VertexBufferBinding> bindings = {
                VertexBufferBinding(&perVertexVb, 0, 0),
                VertexBufferBinding(&instanceVb, 0, 2),   // VertexOffset = 0, frequency = 2
            };
            device.SetVertexBuffers(bindings);
            device.SetIndexBuffer(&ib);
            device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 4);
        }
        const Color b[4] = {
            ReadPixel(device, columnX[0], centerY), ReadPixel(device, columnX[1], centerY),
            ReadPixel(device, columnX[2], centerY), ReadPixel(device, columnX[3], centerY)};
        std::printf("    legB: (%d,%d,%d) (%d,%d,%d) (%d,%d,%d) (%d,%d,%d)\n",
                    b[0].getRProperty(), b[0].getGProperty(), b[0].getBProperty(),
                    b[1].getRProperty(), b[1].getGProperty(), b[1].getBProperty(),
                    b[2].getRProperty(), b[2].getGProperty(), b[2].getBProperty(),
                    b[3].getRProperty(), b[3].getGProperty(), b[3].getBProperty());
        Check(ColorNear(b[0], red), "InstanceFrequency=2: matrix 0 drives instances 0 and 1");
        Check(ColorNear(b[1], red), "InstanceFrequency=2: matrix 1 drives instances 2 and 3");
        Check(ColorNear(b[2], Color::Lime),
              "InstanceFrequency=2: matrix 2 is never reached by 4 instances");
        Check(ColorNear(b[3], Color::Lime),
              "InstanceFrequency=2: matrix 3 is never reached by 4 instances");

        // ---- Leg C: the geometry binding's own VertexOffset ----
        const VertexPositionColor twoQuads[8] = {
            {Vector3(-0.72f, 0.12f, 0.0f), red},   // quad 0, centred at -0.6
            {Vector3(-0.72f, -0.12f, 0.0f), red},
            {Vector3(-0.48f, -0.12f, 0.0f), red},
            {Vector3(-0.48f, 0.12f, 0.0f), red},
            {Vector3(0.48f, 0.12f, 0.0f), red},    // quad 1, centred at 0.6
            {Vector3(0.48f, -0.12f, 0.0f), red},
            {Vector3(0.72f, -0.12f, 0.0f), red},
            {Vector3(0.72f, 0.12f, 0.0f), red},
        };
        VertexBuffer twoQuadVb(device, 8);
        twoQuadVb.SetData(twoQuads, 8);
        const InstanceMatrixRowMajor identity[1] = {TranslationRowMajor(0.0f)};
        VertexBuffer identityInstanceVb(device, 1);
        identityInstanceVb.SetDataRaw(identity, 1, static_cast<int>(sizeof(InstanceMatrixRowMajor)));

        PrepareState(device);
        device.SetVertexBuffer(&twoQuadVb);
        {
            std::vector<VertexBufferBinding> bindings = {
                VertexBufferBinding(&twoQuadVb, 4, 0),      // VertexOffset = 4 -> quad 1
                VertexBufferBinding(&identityInstanceVb, 0, 1),
            };
            device.SetVertexBuffers(bindings);
            device.SetIndexBuffer(&ib);
            device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 1);
        }
        const Color cLeft = ReadPixel(device, columnX[0], centerY);
        const Color cRight = ReadPixel(device, columnX[3], centerY);
        std::printf("    legC: left=(%d,%d,%d) right=(%d,%d,%d)\n",
                    cLeft.getRProperty(), cLeft.getGProperty(), cLeft.getBProperty(),
                    cRight.getRProperty(), cRight.getGProperty(), cRight.getBProperty());
        Check(ColorNear(cRight, red), "geometry VertexOffset=4: the second quad is drawn");
        Check(ColorNear(cLeft, Color::Lime), "geometry VertexOffset=4: the first quad is NOT drawn");

        // ---- The capability boundary this renderer declares, asserted rather than assumed ----
        Check(!device.SupportsCapability(CNA::GraphicsCapability::MultiStreamVertexInput),
              "MultiStreamVertexInput is reported false");
        Check(device.SupportsCapability(CNA::GraphicsCapability::Instancing),
              "Instancing is reported true");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    DiligentInstanceBindingOffsetsTest()
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
        DiligentInstanceBindingOffsetsTest game;
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
