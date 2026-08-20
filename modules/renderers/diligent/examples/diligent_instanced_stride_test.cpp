// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-65: real-device proof that hardware instancing respects the ACTUAL
// bound vertex/instance stream strides instead of a hardcoded slot-0 stride 16 / an
// AUTO_STRIDE-derived slot-1 stride 64.
//
// Before this task, DiligentRenderer::GetOrCreatePipeline()'s Instanced3D layout hardcoded
// slot 0's LayoutElement::Stride to the literal 16 (assuming every caller's per-vertex stream was a
// real VertexPositionColor buffer) and let LAYOUT_ELEMENT_AUTO_STRIDE derive slot 1's stride purely
// from the four float4 rows this shader declares (64), regardless of what the caller's real buffers
// actually were. Diligent pipelines are immutable: a caller with a genuinely 12-byte position-only
// vertex stream, or a padded (>64-byte) instance stream, would silently misfetch every element past
// the first -- the exact "silent-wrong-output" failure mode design decision 7 forbids.
//
// PipelineKey now carries the real (instancedVertexStride, instancedInstanceStride) pair, so a
// different real stride gets its own distinct cached pipeline built with the correct
// LayoutElement::Stride, and DrawInstancedPrimitivesEx() refuses (throws) a stride too small to
// hold what the shader reads at all, rather than attempting an impossible fetch.
//
// Check A -- a genuinely 12-byte position-only vertex stream (not VertexPositionColor) with a
//   standard 64-byte instance stream: 3 instances at distinct positions read back correctly.
// Check B -- a standard 16-byte VertexPositionColor vertex stream with a PADDED 80-byte instance
//   stream (16 bytes of trailing scratch data per instance the shader never reads): 3 instances
//   read back correctly, proving padding doesn't shift subsequent instances' matrix rows.
// Check C -- both non-standard strides at once (12-byte vertex + 80-byte instance).
// Check D -- startIndex + baseVertex combined with the 12-byte position-only vertex stream (offset
//   composition still works with a non-default stride), same discrimination technique as
//   diligent_drawoffset_test.cpp's own Check E.
// Check E -- a vertex stream stride too small to hold Position (8 bytes) is refused (throws),
//   not silently misfetched.
// Check F -- an instance stream stride too small to hold a 4x4 matrix (32 bytes) is refused.
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
#include <stdexcept>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 96;

    /// A genuinely 12-byte position-only vertex -- not VertexPositionColor's 16-byte layout.
    struct PositionOnlyVertex
    {
        float x, y, z;
    };
    static_assert(sizeof(PositionOnlyVertex) == 12, "PositionOnlyVertex must be exactly 12 bytes");

    /// Row-major 4x4 translation matrix, 16 floats: matches kInstancedVertexHlsl's per-instance
    /// stream convention (four consecutive float4 rows).
    struct InstanceMatrixRowMajor
    {
        float m[16];
    };
    static_assert(sizeof(InstanceMatrixRowMajor) == 64, "InstanceMatrixRowMajor must be 64 bytes");

    /// A padded instance stream: the real 64-byte matrix plus 16 bytes of trailing scratch data the
    /// shader never reads, matching a hypothetical caller who packs extra per-instance fields (e.g.
    /// a colour or ID) alongside the transform.
    struct PaddedInstanceMatrixRowMajor
    {
        float m[16];
        float scratch[4];
    };
    static_assert(sizeof(PaddedInstanceMatrixRowMajor) == 80,
                 "PaddedInstanceMatrixRowMajor must be exactly 80 bytes");

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

    PaddedInstanceMatrixRowMajor PaddedTranslationRowMajor(float tx, float ty, float tz)
    {
        PaddedInstanceMatrixRowMajor result{};
        result.m[0] = 1.0f;
        result.m[5] = 1.0f;
        result.m[10] = 1.0f;
        result.m[15] = 1.0f;
        result.m[12] = tx;
        result.m[13] = ty;
        result.m[14] = tz;
        // Deliberately non-zero scratch bytes: if the pipeline's slot-1 stride were still hardcoded
        // to 64 instead of this buffer's real 80, the NEXT instance's fetch would read these bytes
        // as (part of) its own matrix instead of skipping over them.
        result.scratch[0] = 999.0f;
        result.scratch[1] = 999.0f;
        result.scratch[2] = 999.0f;
        result.scratch[3] = 999.0f;
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

class DiligentInstancedStrideTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
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

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        BasicEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        effect.Apply();

        const PositionOnlyVertex quad12[4] = {
            {-0.1f, 0.1f, 0.0f}, {-0.1f, -0.1f, 0.0f}, {0.1f, -0.1f, 0.0f}, {0.1f, 0.1f, 0.0f}};
        const Color red(255, 0, 0, 255);
        const VertexPositionColor quad16[4] = {
            {Vector3(-0.1f, 0.1f, 0.0f), red},
            {Vector3(-0.1f, -0.1f, 0.0f), red},
            {Vector3(0.1f, -0.1f, 0.0f), red},
            {Vector3(0.1f, 0.1f, 0.0f), red},
        };
        const std::uint16_t indices[6] = {0, 1, 2, 0, 2, 3};

        auto readThree = [&](const char* label) {
            const int centerY = kSize / 2;
            const Color leftPixel = ReadPixel(device, kSize / 2 - kSize * 3 / 10, centerY);
            const Color centerPixel = ReadPixel(device, kSize / 2, centerY);
            const Color rightPixel = ReadPixel(device, kSize / 2 + kSize * 3 / 10, centerY);
            const Color backgroundPixel = ReadPixel(device, kSize / 2 - kSize * 45 / 100, centerY);
            std::printf("    (%s left=(%d,%d,%d) center=(%d,%d,%d) right=(%d,%d,%d) bg=(%d,%d,%d))\n",
                        label, leftPixel.getRProperty(), leftPixel.getGProperty(),
                        leftPixel.getBProperty(), centerPixel.getRProperty(),
                        centerPixel.getGProperty(), centerPixel.getBProperty(),
                        rightPixel.getRProperty(), rightPixel.getGProperty(), rightPixel.getBProperty(),
                        backgroundPixel.getRProperty(), backgroundPixel.getGProperty(),
                        backgroundPixel.getBProperty());
            return ColorNear(leftPixel, red) && ColorNear(centerPixel, red) &&
                   ColorNear(rightPixel, red) && ColorNear(backgroundPixel, Color::Lime);
        };

        // Check A: 12-byte position-only vertex stream + standard 64-byte instance stream.
        {
            VertexBuffer perVertexVb(device, 4);
            perVertexVb.SetDataRaw(quad12, 4, static_cast<int>(sizeof(PositionOnlyVertex)));
            IndexBuffer ib(device, 6);
            ib.SetData(indices, 6);
            const InstanceMatrixRowMajor instances[3] = {
                TranslationRowMajor(-0.6f, 0.0f, 0.0f), TranslationRowMajor(0.0f, 0.0f, 0.0f),
                TranslationRowMajor(0.6f, 0.0f, 0.0f)};
            VertexBuffer instanceVb(device, 3);
            instanceVb.SetDataRaw(instances, 3, static_cast<int>(sizeof(InstanceMatrixRowMajor)));

            device.Clear(Color::Lime);
            device.SetVertexBuffer(&perVertexVb);
            std::vector<VertexBufferBinding> bindings = {VertexBufferBinding(&perVertexVb, 0, 0),
                                                          VertexBufferBinding(&instanceVb, 0, 1)};
            device.SetVertexBuffers(bindings);
            device.SetIndexBuffer(&ib);
            device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 3);
            Check(readThree("A"), "12-byte position-only vertex stream + 64-byte instance stream");
        }

        // Check B: standard 16-byte VertexPositionColor vertex stream + padded 80-byte instance
        // stream.
        {
            VertexBuffer perVertexVb(device, 4);
            perVertexVb.SetData(quad16, 4);
            IndexBuffer ib(device, 6);
            ib.SetData(indices, 6);
            const PaddedInstanceMatrixRowMajor instances[3] = {
                PaddedTranslationRowMajor(-0.6f, 0.0f, 0.0f), PaddedTranslationRowMajor(0.0f, 0.0f, 0.0f),
                PaddedTranslationRowMajor(0.6f, 0.0f, 0.0f)};
            VertexBuffer instanceVb(device, 3);
            instanceVb.SetDataRaw(instances, 3, static_cast<int>(sizeof(PaddedInstanceMatrixRowMajor)));

            device.Clear(Color::Lime);
            device.SetVertexBuffer(&perVertexVb);
            std::vector<VertexBufferBinding> bindings = {VertexBufferBinding(&perVertexVb, 0, 0),
                                                          VertexBufferBinding(&instanceVb, 0, 1)};
            device.SetVertexBuffers(bindings);
            device.SetIndexBuffer(&ib);
            device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 3);
            Check(readThree("B"), "16-byte VertexPositionColor stream + PADDED 80-byte instance stream");
        }

        // Check C: both non-standard strides at once.
        {
            VertexBuffer perVertexVb(device, 4);
            perVertexVb.SetDataRaw(quad12, 4, static_cast<int>(sizeof(PositionOnlyVertex)));
            IndexBuffer ib(device, 6);
            ib.SetData(indices, 6);
            const PaddedInstanceMatrixRowMajor instances[3] = {
                PaddedTranslationRowMajor(-0.6f, 0.0f, 0.0f), PaddedTranslationRowMajor(0.0f, 0.0f, 0.0f),
                PaddedTranslationRowMajor(0.6f, 0.0f, 0.0f)};
            VertexBuffer instanceVb(device, 3);
            instanceVb.SetDataRaw(instances, 3, static_cast<int>(sizeof(PaddedInstanceMatrixRowMajor)));

            device.Clear(Color::Lime);
            device.SetVertexBuffer(&perVertexVb);
            std::vector<VertexBufferBinding> bindings = {VertexBufferBinding(&perVertexVb, 0, 0),
                                                          VertexBufferBinding(&instanceVb, 0, 1)};
            device.SetVertexBuffers(bindings);
            device.SetIndexBuffer(&ib);
            device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 3);
            Check(readThree("C"), "12-byte vertex stream + 80-byte padded instance stream combined");
        }

        // Check D: startIndex + baseVertex combined with the 12-byte position-only vertex stream.
        // 9 vertices: 0-2 = off-screen quad's first triangle equivalent is not used here directly --
        // instead mirror diligent_drawoffset_test.cpp's technique: verts 0-2 (A, left), 3-5 (off
        // screen), 6-8 (B, right) as a 9-vertex/2-triangle-per-group layout collapsed to indices.
        {
            const PositionOnlyVertex offsetVerts[9] = {
                // Group A (verts 0-2): left small triangle -- must NOT be drawn (wrong group).
                {-0.9f, 0.05f, 0.0f}, {-0.7f, 0.05f, 0.0f}, {-0.8f, -0.05f, 0.0f},
                // Group (verts 3-5): deliberately off-screen.
                {5.0f, 5.0f, 0.0f}, {5.2f, 5.0f, 0.0f}, {5.1f, 4.9f, 0.0f},
                // Group B (verts 6-8): right small triangle -- must be the one drawn.
                {0.7f, 0.05f, 0.0f}, {0.9f, 0.05f, 0.0f}, {0.8f, -0.05f, 0.0f},
            };
            VertexBuffer vb(device, 9);
            vb.SetDataRaw(offsetVerts, 9, static_cast<int>(sizeof(PositionOnlyVertex)));
            const std::uint16_t idx[6] = {0, 1, 2, 3, 4, 5};
            IndexBuffer ib(device, 6);
            ib.SetData(idx, 6);
            InstanceMatrixRowMajor identity = TranslationRowMajor(0.0f, 0.0f, 0.0f);
            VertexBuffer instanceVb(device, 1);
            instanceVb.SetDataRaw(&identity, 1, static_cast<int>(sizeof(InstanceMatrixRowMajor)));

            device.Clear(Color::Black);
            device.SetVertexBuffer(&vb);
            std::vector<VertexBufferBinding> bindings = {VertexBufferBinding(&vb, 0, 0),
                                                          VertexBufferBinding(&instanceVb, 0, 1)};
            device.SetVertexBuffers(bindings);
            device.SetIndexBuffer(&ib);
            // startIndex=3, baseVertex=3: index 0 (relative) + baseVertex 3 = vertex 3, offset by
            // startIndex 3 into the index buffer -> indices {3,4,5} + baseVertex 3 = verts {6,7,8}.
            device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 3, 0, 3, 3, 1, 1);

            const Color leftPixel = ReadPixel(device, kSize / 2 - kSize * 4 / 10, kSize / 2);
            const Color rightPixel = ReadPixel(device, kSize / 2 + kSize * 4 / 10, kSize / 2);
            std::printf("    (D left=(%d,%d,%d) right=(%d,%d,%d))\n", leftPixel.getRProperty(),
                        leftPixel.getGProperty(), leftPixel.getBProperty(), rightPixel.getRProperty(),
                        rightPixel.getGProperty(), rightPixel.getBProperty());
            Check(ColorNear(rightPixel, red) && !ColorNear(leftPixel, red),
                 "startIndex=3 + baseVertex=3 on a 12-byte vertex stream: right group drawn, not left");
        }

        // Check E: a vertex stream stride too small to hold Position (8 bytes) is refused.
        {
            bool threw = false;
            try
            {
                struct TooSmall { float x, y; };
                VertexBuffer tinyVb(device, 4);
                TooSmall tiny[4] = {};
                tinyVb.SetDataRaw(tiny, 4, static_cast<int>(sizeof(TooSmall)));
                IndexBuffer ib(device, 6);
                ib.SetData(indices, 6);
                InstanceMatrixRowMajor identity = TranslationRowMajor(0.0f, 0.0f, 0.0f);
                VertexBuffer instanceVb(device, 1);
                instanceVb.SetDataRaw(&identity, 1, static_cast<int>(sizeof(InstanceMatrixRowMajor)));

                device.SetVertexBuffer(&tinyVb);
                std::vector<VertexBufferBinding> bindings = {VertexBufferBinding(&tinyVb, 0, 0),
                                                              VertexBufferBinding(&instanceVb, 0, 1)};
                device.SetVertexBuffers(bindings);
                device.SetIndexBuffer(&ib);
                device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 1);
            }
            catch (const std::runtime_error&)
            {
                threw = true;
            }
            Check(threw, "8-byte-stride vertex stream (too small for Position) is refused, not "
                        "silently misfetched");
        }

        // Check F: an instance stream stride too small to hold a 4x4 matrix (32 bytes) is refused.
        {
            bool threw = false;
            try
            {
                struct HalfMatrix { float m[8]; };
                VertexBuffer perVertexVb(device, 4);
                perVertexVb.SetDataRaw(quad12, 4, static_cast<int>(sizeof(PositionOnlyVertex)));
                IndexBuffer ib(device, 6);
                ib.SetData(indices, 6);
                HalfMatrix half{};
                VertexBuffer instanceVb(device, 1);
                instanceVb.SetDataRaw(&half, 1, static_cast<int>(sizeof(HalfMatrix)));

                device.SetVertexBuffer(&perVertexVb);
                std::vector<VertexBufferBinding> bindings = {VertexBufferBinding(&perVertexVb, 0, 0),
                                                              VertexBufferBinding(&instanceVb, 0, 1)};
                device.SetVertexBuffers(bindings);
                device.SetIndexBuffer(&ib);
                device.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 1);
            }
            catch (const std::runtime_error&)
            {
                threw = true;
            }
            Check(threw, "32-byte-stride instance stream (too small for a 4x4 matrix) is refused, "
                        "not silently misfetched");
        }

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        result_ = passCount_ == totalCount_ ? 0 : 1;
        Exit();
    }

public:
    DiligentInstancedStrideTest()
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
        DiligentInstancedStrideTest game;
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
