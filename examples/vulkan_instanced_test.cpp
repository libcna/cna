// SPDX-License-Identifier: MS-PL
// Task 124: Vulkan integration test — DrawInstancedPrimitives with 3 instances.
//
// Renders a small colored quad three times with different per-instance world
// matrices: left (x=-0.6), centre (x=0), right (x=+0.6).  Background is green.
//
// The per-instance buffer holds 3 column-major mat4 world matrices (64 bytes each).
// The Vulkan instanced shader combines VP (from push constants) with the per-instance
// world matrix: gl_Position = vp * world * vec4(aPos, 1.0).
//
// Expected after rendering:
//   - Pixel at W/6   (NDC ≈ -0.67) → red (left instance)
//   - Pixel at W/2   (NDC ≈  0.00) → red (centre instance)
//   - Pixel at 5*W/6 (NDC ≈ +0.67) → red (right instance)
//   - Pixel at W/12  (NDC ≈ -0.83) → green (background, between left and screen edge)
//
// Exit code 0 = PASS, 1 = FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

// Compact per-vertex layout that matches the Vulkan colored3d stride=16 format.
struct GpuVPC { float x, y, z; uint8_t r, g, b, a; };
static_assert(sizeof(GpuVPC) == 16);

// Column-major mat4 (64 bytes) used as per-instance world transform.
struct InstMat4 { float m[16]; };
static_assert(sizeof(InstMat4) == 64);

// Build a column-major translation matrix.
static InstMat4 TranslateMat(float tx, float ty, float tz)
{
    InstMat4 m{};
    m.m[0]  = 1; m.m[5]  = 1; m.m[10] = 1; m.m[15] = 1;  // identity
    m.m[12] = tx; m.m[13] = ty; m.m[14] = tz;               // translation column
    return m;
}

class VulkanInstancedTest : public Game
{
    bool done_   = false;
    int  result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 255, 0, 255));   // green background
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);

        // Small red quad centered at the origin, NDC ±0.08 on each axis.
        const GpuVPC quadVerts[4] = {
            { -0.08f,  0.08f, 0.0f,  255, 0, 0, 255 },
            { -0.08f, -0.08f, 0.0f,  255, 0, 0, 255 },
            {  0.08f, -0.08f, 0.0f,  255, 0, 0, 255 },
            {  0.08f,  0.08f, 0.0f,  255, 0, 0, 255 },
        };
        VertexBuffer perVertVb(device, 4);
        perVertVb.SetDataRaw(quadVerts, 4, static_cast<int>(sizeof(GpuVPC)));

        // Index buffer for the quad (two triangles: 0-1-2, 0-2-3).
        const uint16_t indices[6] = { 0, 1, 2,  0, 2, 3 };
        IndexBuffer ib(device, 6);
        ib.SetData(indices, 6);

        // Three per-instance world matrices (left / centre / right).
        const InstMat4 instMats[3] = {
            TranslateMat(-0.6f, 0.0f, 0.0f),
            TranslateMat( 0.0f, 0.0f, 0.0f),
            TranslateMat(+0.6f, 0.0f, 0.0f),
        };
        VertexBuffer instVb(device, 3);
        instVb.SetDataRaw(instMats, 3, static_cast<int>(sizeof(InstMat4)));

        // Set up BasicEffect with Identity matrices (NDC pass-through).
        // The Vulkan instanced shader uses pc.diffuseColor for all fragment colour;
        // per-vertex colour is not read.  Set diffuse to red explicitly.
        BasicEffect fx(device);
        fx.setWorldProperty(Matrix::getIdentityProperty());
        fx.setViewProperty(Matrix::getIdentityProperty());
        fx.setProjectionProperty(Matrix::getIdentityProperty());
        fx.setDiffuseColorProperty(Vector3(1.0f, 0.0f, 0.0f));
        fx.Apply();

        // Bind per-vertex VB as currentVertexBuffer_ (needed by DrawInstancedPrimitives).
        device.SetVertexBuffer(&perVertVb);
        // Register both VBs so the renderer can find the per-instance one.
        std::vector<VertexBufferBinding> bindings = {
            VertexBufferBinding(&perVertVb, 0, 0),  // per-vertex
            VertexBufferBinding(&instVb,    0, 1),  // per-instance (instanceFrequency=1)
        };
        device.SetVertexBuffers(bindings);
        device.SetIndexBuffer(&ib);

        // Task 896 finding: this quad's winding is CCW/back-facing under CNA's real default
        // RasterizerState — needs CullNone.
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        // Draw 3 instances of the quad (2 triangles each).
        device.DrawInstancedPrimitives(PrimitiveType::TriangleList,
                                       0,   // baseVertex
                                       0,   // minVertexIndex
                                       4,   // numVertices
                                       0,   // startIndex
                                       2,   // primitiveCount (2 triangles)
                                       3);  // instanceCount

        // Read back pixels from each instance position.
        const Rectangle leftReg(  W / 6,     H / 2, 1, 1);
        const Rectangle centReg(  W / 2,     H / 2, 1, 1);
        const Rectangle rightReg( 5 * W / 6, H / 2, 1, 1);
        const Rectangle bgReg(    W / 12,    H / 2, 1, 1);
        Color leftPx(0,0,0,0), centPx(0,0,0,0), rightPx(0,0,0,0), bgPx(0,0,0,0);
        device.GetBackBufferData(&leftReg,  &leftPx,  0, 1);
        device.GetBackBufferData(&centReg,  &centPx,  0, 1);
        device.GetBackBufferData(&rightReg, &rightPx, 0, 1);
        device.GetBackBufferData(&bgReg,    &bgPx,    0, 1);

        // Instance pixels should be red; background pixel should be green.
        const bool leftOk  = (leftPx.getRProperty()  >= 200 && leftPx.getGProperty()  <= 50);
        const bool centOk  = (centPx.getRProperty()  >= 200 && centPx.getGProperty()  <= 50);
        const bool rightOk = (rightPx.getRProperty() >= 200 && rightPx.getGProperty() <= 50);
        const bool bgOk    = (bgPx.getGProperty()    >= 200 && bgPx.getRProperty()    <= 50);

        if (leftOk && centOk && rightOk && bgOk)
        {
            std::printf("[PASS] VulkanInstanced: left=(%d,%d,%d) cent=(%d,%d,%d) "
                        "right=(%d,%d,%d) bg=(%d,%d,%d)\n",
                        leftPx.getRProperty(),  leftPx.getGProperty(),  leftPx.getBProperty(),
                        centPx.getRProperty(),  centPx.getGProperty(),  centPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty(),
                        bgPx.getRProperty(),    bgPx.getGProperty(),    bgPx.getBProperty());
            result_ = 0;
        }
        else
        {
            std::printf("[FAIL] VulkanInstanced: left=(%d,%d,%d) cent=(%d,%d,%d) "
                        "right=(%d,%d,%d) bg=(%d,%d,%d)\n"
                        "       expected: left/cent/right=red, bg=green\n",
                        leftPx.getRProperty(),  leftPx.getGProperty(),  leftPx.getBProperty(),
                        centPx.getRProperty(),  centPx.getGProperty(),  centPx.getBProperty(),
                        rightPx.getRProperty(), rightPx.getGProperty(), rightPx.getBProperty(),
                        bgPx.getRProperty(),    bgPx.getGProperty(),    bgPx.getBProperty());
            result_ = 1;
        }
        Exit();
    }

public:
    int getResult() const { return result_; }
};

int main()
{
    VulkanInstancedTest game;
    game.Run();
    return game.getResult();
}
