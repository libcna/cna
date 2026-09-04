// SPDX-License-Identifier: MS-PL
// plans/plan_magnum.md MAGNUM-52: SkinnedEffect pixel test for the MAGNUM renderer.
//
// Skinning is measured by WHERE the quad lands, not by its colour: a bone palette that never
// reaches the shader, or a weight sum taken over the wrong number of pairs, moves the geometry
// somewhere else entirely. Three cases -- an identity bone (nothing moves), a real translation
// bone (everything moves by it), and a 50/50 blend of two bones whose result is only correct if
// the second weight pair is summed at all.
//
// Exit code 0 = all PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    /// The stride-52 skinned layout: position, normal, texture coordinate, four blend weights and
    /// four blend indices.
    struct SkinnedVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedVertex) == 52, "the skinned layout must stay 52 bytes");
}

class MagnumSkinnedEffectTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> deviceManager_;
    Texture2D texture_;
    bool done_ = false;
    int result_ = 0;

    Color ReadAt(GraphicsDevice& device, int x)
    {
        const Rectangle region(x, kSize / 2, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    void Check(const char* label, Color pixel, bool wantQuad)
    {
        // The quad is lit and textured red over a green background, so "is the quad here?" reduces
        // to which channel dominates -- no exact colour is needed, and none is asserted.
        const bool isQuad = pixel.getRProperty() > pixel.getGProperty()
                         && pixel.getRProperty() > 50;
        if (isQuad == wantQuad)
        {
            std::printf("[PASS] %s: got=(%d,%d,%d)\n", label,
                        pixel.getRProperty(), pixel.getGProperty(), pixel.getBProperty());
            return;
        }
        std::printf("[FAIL] %s: got=(%d,%d,%d), expected %s\n", label,
                    pixel.getRProperty(), pixel.getGProperty(), pixel.getBProperty(),
                    wantQuad ? "the quad (red-dominant)" : "background (green)");
        result_ = 1;
    }

    void DrawSkinned(GraphicsDevice& device, const std::vector<Matrix>& bones,
                     int weightsPerVertex, const SkinnedVertex (&quad)[6])
    {
        device.Clear(Color(0, 255, 0, 255));
        device.SetDepthTestEnabled(false);
        device.setBlendStateProperty(BlendState::Opaque);
        device.setRasterizerStateProperty(RasterizerState::CullNone);

        SkinnedEffect effect(device);
        effect.setTextureProperty(&texture_);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.SetBoneTransforms(bones);
        effect.setWeightsPerVertexProperty(weightsPerVertex);
        effect.EnableDefaultLighting();
        effect.Apply();

        VertexBuffer buffer(device, 6);
        buffer.SetDataRaw(quad, 6, static_cast<int>(sizeof(SkinnedVertex)));
        device.SetVertexBuffer(&buffer);
        device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        device.SetVertexBuffer(nullptr);
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        texture_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                               std::vector<uint8_t>{255, 0, 0, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();

        // Authored covering NDC x -1..0 -- the left half of the screen -- so any bone translation
        // shows up as the quad crossing the centre line.
        const SkinnedVertex singleBoneQuad[6] = {
            {-1,  1, 0,  0, 0, 1,  0, 0,  1, 0, 0, 0,  0, 0, 0, 0},
            {-1, -1, 0,  0, 0, 1,  0, 1,  1, 0, 0, 0,  0, 0, 0, 0},
            { 0, -1, 0,  0, 0, 1,  1, 1,  1, 0, 0, 0,  0, 0, 0, 0},
            {-1,  1, 0,  0, 0, 1,  0, 0,  1, 0, 0, 0,  0, 0, 0, 0},
            { 0, -1, 0,  0, 0, 1,  1, 1,  1, 0, 0, 0,  0, 0, 0, 0},
            { 0,  1, 0,  0, 0, 1,  1, 0,  1, 0, 0, 0,  0, 0, 0, 0},
        };

        // The same geometry split evenly between bone 0 and bone 1.
        const SkinnedVertex twoBoneQuad[6] = {
            {-1,  1, 0,  0, 0, 1,  0, 0,  0.5f, 0.5f, 0, 0,  0, 1, 0, 0},
            {-1, -1, 0,  0, 0, 1,  0, 1,  0.5f, 0.5f, 0, 0,  0, 1, 0, 0},
            { 0, -1, 0,  0, 0, 1,  1, 1,  0.5f, 0.5f, 0, 0,  0, 1, 0, 0},
            {-1,  1, 0,  0, 0, 1,  0, 0,  0.5f, 0.5f, 0, 0,  0, 1, 0, 0},
            { 0, -1, 0,  0, 0, 1,  1, 1,  0.5f, 0.5f, 0, 0,  0, 1, 0, 0},
            { 0,  1, 0,  0, 0, 1,  1, 0,  0.5f, 0.5f, 0, 0,  0, 1, 0, 0},
        };

        // NDC -0.75 and +0.25 in pixels. The unmoved quad covers -1..0 and the shifted one
        // -0.5..0.5, so -0.75 is inside exactly one of them and +0.25 inside the other -- a sample
        // at -0.5 would sit on the shifted quad's own edge and answer "inside" for both.
        const int leftSample = kSize / 8;
        const int rightSample = 5 * kSize / 8;

        // An identity bone must move nothing at all -- the baseline that separates "skinning does
        // nothing" from "skinning is not reaching the shader".
        DrawSkinned(device, {Matrix::getIdentityProperty()}, 1, singleBoneQuad);
        Check("identity bone: quad stays in the left half", ReadAt(device, leftSample), true);
        Check("identity bone: right half stays background", ReadAt(device, rightSample), false);

        // A real translation bone moves every vertex bound to it.
        DrawSkinned(device, {Matrix::CreateTranslation(0.5f, 0.0f, 0.0f)}, 1, singleBoneQuad);
        Check("translation bone: quad has moved off the left", ReadAt(device, leftSample), false);
        Check("translation bone: quad has arrived on the right", ReadAt(device, rightSample), true);

        // Half of an identity bone plus half of a +1.0 translation is a +0.5 translation -- the
        // same result as the case above, but only if the SECOND weight pair is summed. A shader
        // stopping after the first pair would leave the quad at half weight in the left half.
        DrawSkinned(device,
                    {Matrix::getIdentityProperty(), Matrix::CreateTranslation(1.0f, 0.0f, 0.0f)},
                    2, twoBoneQuad);
        Check("two-bone blend: quad has moved off the left", ReadAt(device, leftSample), false);
        Check("two-bone blend: quad has arrived on the right", ReadAt(device, rightSample), true);

        Exit();
    }

public:
    MagnumSkinnedEffectTest()
    {
        deviceManager_ = std::make_unique<GraphicsDeviceManager>(this);
        deviceManager_->setPreferredBackBufferWidthProperty(kSize);
        deviceManager_->setPreferredBackBufferHeightProperty(kSize);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    MagnumSkinnedEffectTest game;
    game.Run();
    return game.GetResult();
}
