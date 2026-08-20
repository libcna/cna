// SPDX-License-Identifier: MS-PL
// plans/plan_magnum.md MAGNUM-57: vertex-array cache correctness for the MAGNUM renderer.
//
// The renderer caches a built vertex array against the binding it was built for, and then applies
// primitive, element count, instance count, base vertex and index offset as per-draw setters on
// top of it. That is only sound if those five genuinely are per-draw: a draw reusing a cached
// array must still land exactly where its own parameters say, not where the draw that populated
// the cache did.
//
// Two quads live in one buffer -- red on the left, green on the right -- and the three draws below
// all share one binding, so the second and third necessarily run against a cached array. Each
// selects its quad a different way: by index offset, by base vertex, and back to the first again.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    /// Which quad a sample landed on, judged by the dominant channel over a black clear.
    enum class Quad { None, Left, Right };
}

class MagnumMeshCacheTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> deviceManager_;
    std::unique_ptr<BasicEffect> effect_;
    bool done_ = false;
    int result_ = 0;

    Quad QuadAt(GraphicsDevice& device, int x)
    {
        const Rectangle region(x, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        if (pixel.getRProperty() > 128 && pixel.getGProperty() < 128) return Quad::Left;
        if (pixel.getGProperty() > 128 && pixel.getRProperty() < 128) return Quad::Right;
        return Quad::None;
    }

    static const char* Name(Quad quad)
    {
        switch (quad)
        {
            case Quad::Left:  return "the left (red) quad";
            case Quad::Right: return "the right (green) quad";
            default:          return "nothing";
        }
    }

    void Check(const char* label, Quad got, Quad want)
    {
        if (got == want)
        {
            std::printf("[PASS] %s: %s\n", label, Name(got));
            return;
        }
        std::printf("[FAIL] %s: got %s, expected %s\n", label, Name(got), Name(want));
        result_ = 1;
    }

    void DrawRange(GraphicsDevice& device, VertexBuffer& vertices, IndexBuffer& indices,
                   int baseVertex, int minVertexIndex, int startIndex)
    {
        device.Clear(Color(0, 0, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        effect_->Apply();
        device.SetVertexBuffer(&vertices);
        device.SetIndexBuffer(&indices);
        // Six vertices per quad; minVertexIndex is the lowest DECODED index relative to the base
        // vertex, so it moves with the index range rather than with the base.
        device.DrawIndexedPrimitives(PrimitiveType::TriangleList, baseVertex, minVertexIndex, 6,
                                     startIndex, 2);
        device.SetIndexBuffer(nullptr);
        device.SetVertexBuffer(nullptr);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& device = getGraphicsDeviceProperty();
        effect_ = std::make_unique<BasicEffect>(device);
        effect_->VertexColorEnabled = true;
        effect_->setLightingEnabledProperty(false);
        effect_->setTextureEnabledProperty(false);
        effect_->setWorldProperty(Matrix::getIdentityProperty());
        effect_->setViewProperty(Matrix::getIdentityProperty());
        effect_->setProjectionProperty(Matrix::getIdentityProperty());
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        const Color red(255, 0, 0, 255);
        const Color green(0, 255, 0, 255);
        // Vertices 0..5 are the left quad, 6..11 the right one.
        const std::array<VertexPositionColor, 12> vertices = {
            VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), red),
            VertexPositionColor(Vector3(-1.0f,  1.0f, 0.0f), red),
            VertexPositionColor(Vector3( 0.0f,  1.0f, 0.0f), red),
            VertexPositionColor(Vector3(-1.0f, -1.0f, 0.0f), red),
            VertexPositionColor(Vector3( 0.0f,  1.0f, 0.0f), red),
            VertexPositionColor(Vector3( 0.0f, -1.0f, 0.0f), red),
            VertexPositionColor(Vector3( 0.0f, -1.0f, 0.0f), green),
            VertexPositionColor(Vector3( 0.0f,  1.0f, 0.0f), green),
            VertexPositionColor(Vector3( 1.0f,  1.0f, 0.0f), green),
            VertexPositionColor(Vector3( 0.0f, -1.0f, 0.0f), green),
            VertexPositionColor(Vector3( 1.0f,  1.0f, 0.0f), green),
            VertexPositionColor(Vector3( 1.0f, -1.0f, 0.0f), green),
        };
        VertexBuffer vertexBuffer(device, VertexPositionColor::getVertexDeclarationStatic(),
                                  static_cast<int>(vertices.size()), BufferUsage::None);
        vertexBuffer.SetData(vertices.data(), 0, static_cast<int>(vertices.size()));

        std::array<uint16_t, 12> indices{};
        for (uint16_t i = 0; i < indices.size(); ++i)
            indices[i] = i;
        IndexBuffer indexBuffer(device, IndexElementSize::SixteenBits,
                                static_cast<int>(indices.size()), BufferUsage::None);
        indexBuffer.SetData(indices.data(), static_cast<int>(indices.size()));

        const int leftSample = kSize / 4;
        const int rightSample = 3 * kSize / 4;

        // Populates the cache.
        DrawRange(device, vertexBuffer, indexBuffer, 0, 0, 0);
        Check("first draw, left half", QuadAt(device, leftSample), Quad::Left);
        Check("first draw, right half", QuadAt(device, rightSample), Quad::None);

        // Same binding, different index offset: must select the second quad off the cached array.
        DrawRange(device, vertexBuffer, indexBuffer, 0, 6, 6);
        Check("index offset moves the range, left half", QuadAt(device, leftSample), Quad::None);
        Check("index offset moves the range, right half", QuadAt(device, rightSample), Quad::Right);

        // Same binding again, this time reaching the second quad through the base vertex. Getting
        // the index offset's units wrong, or baking either term into the cached binding, lands this
        // somewhere else entirely.
        DrawRange(device, vertexBuffer, indexBuffer, 6, 0, 0);
        Check("base vertex moves the range, left half", QuadAt(device, leftSample), Quad::None);
        Check("base vertex moves the range, right half", QuadAt(device, rightSample), Quad::Right);

        // And back: a cached array must not have kept either of the previous draws' parameters.
        DrawRange(device, vertexBuffer, indexBuffer, 0, 0, 0);
        Check("returning to the first range, left half", QuadAt(device, leftSample), Quad::Left);
        Check("returning to the first range, right half", QuadAt(device, rightSample), Quad::None);

        Exit();
    }

public:
    MagnumMeshCacheTest()
    {
        deviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        deviceManager_->setPreferredBackBufferWidthProperty(kSize);
        deviceManager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    MagnumMeshCacheTest game;
    game.Run();
    return game.GetResult();
}
