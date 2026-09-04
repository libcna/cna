// SPDX-License-Identifier: MS-PL
//
// plans/plan_diligent.md DILIGENT-35: real-device proof for SkinnedEffect on the Diligent renderer,
// through the public XNA API only.
//
// The scene is a quad covering the LEFT half of the screen, every vertex bound to bone 0.
//
// Check A -- with an identity bone palette the quad stays on the left: the left half is textured
//   and the right half keeps the background. This is the "skinning did not corrupt anything" case.
// Check B -- with bone 0 set to a translation of +1 along X, the very same vertex buffer moves to
//   the RIGHT half. Only a shader that actually reads the bone palette can do this, and A alone
//   would pass on a renderer that ignores bones entirely.
// Check C -- the background on the far left is exposed by that move, so B is a real translation
//   rather than the quad having grown to cover everything.
// Check D -- WeightsPerVertex = 1 means only the first weight/index pair is summed: a second pair
//   pointing at a bone with a huge translation must be ignored. FNA's Skin() is defined that way,
//   and a shader that always sums four pairs would fling the quad off screen and fail this.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = no usable device.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"

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
    constexpr int kSize = 64;
    constexpr int kChecks = 4;

    /// The stride-52 skinned layout: position, normal, UV, four weights, four bone indices.
    struct SkinnedVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float u, v;
        float w0, w1, w2, w3;
        std::uint8_t i0, i1, i2, i3;
    };
    static_assert(sizeof(SkinnedVertex) == 52, "the skinned vertex layout must be 52 bytes");

    Color ReadPixel(GraphicsDevice& device, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        device.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

    bool IsRedish(Color c) { return c.getRProperty() > 100 && c.getGProperty() < 90; }
    bool IsBackground(Color c) { return c.getRProperty() < 60 && c.getBProperty() > 100; }
}

class DiligentSkinnedTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> graphicsDeviceManager_;
    Texture2D redTexture_;
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
    void LoadContent() override
    {
        redTexture_ = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 1, 1,
                                                  std::vector<std::uint8_t>{255, 0, 0, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setBlendStateProperty(BlendState::Opaque);

        // Left half of clip space, every vertex fully weighted to bone 0. The second weight/index
        // pair points at bone 1 with weight 0, which check D turns into a trap.
        const SkinnedVertex vertices[6] = {
            {-1, 1, 0.5f, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0},
            {-1, -1, 0.5f, 0, 0, 1, 0, 1, 1, 0, 0, 0, 0, 1, 0, 0},
            {0, -1, 0.5f, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0},
            {-1, 1, 0.5f, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0},
            {0, -1, 0.5f, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 0, 0},
            {0, 1, 0.5f, 0, 0, 1, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0},
        };
        VertexBuffer vertexBuffer(device, 6);
        vertexBuffer.SetDataRaw(vertices, 6, static_cast<int>(sizeof(SkinnedVertex)));

        SkinnedEffect effect(device);
        effect.setTextureProperty(&redTexture_);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::getIdentityProperty());
        effect.setProjectionProperty(Matrix::getIdentityProperty());
        effect.setWeightsPerVertexProperty(1);
        effect.EnableDefaultLighting();

        std::vector<Matrix> bones(72, Matrix::getIdentityProperty());

        const auto drawWith = [&](const std::vector<Matrix>& palette) {
            effect.SetBoneTransforms(palette);
            device.Clear(Color::Blue);
            effect.Apply();
            device.SetVertexBuffer(&vertexBuffer);
            device.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
        };

        drawWith(bones);
        const bool leftTextured = IsRedish(ReadPixel(device, kSize / 4, kSize / 2));
        const bool rightClear = IsBackground(ReadPixel(device, 3 * kSize / 4, kSize / 2));
        Check(leftTextured && rightClear,
              "identity bones leave the quad on the left half");

        bones[0] = Matrix::CreateTranslation(1.0f, 0.0f, 0.0f);
        drawWith(bones);
        Check(IsRedish(ReadPixel(device, 3 * kSize / 4, kSize / 2)),
              "a translated bone 0 moves the same vertex buffer to the right half");
        Check(IsBackground(ReadPixel(device, kSize / 8, kSize / 2)),
              "...and uncovers the background it left behind (a move, not a stretch)");

        // Bone 1 would throw the quad far off screen, but WeightsPerVertex is 1 and its weight is
        // 0, so FNA's Skin() never reaches it.
        bones[1] = Matrix::CreateTranslation(100.0f, 0.0f, 0.0f);
        drawWith(bones);
        Check(IsRedish(ReadPixel(device, 3 * kSize / 4, kSize / 2)),
              "WeightsPerVertex = 1 ignores the second weight/index pair entirely");

        std::printf("=== %d/%d PASS ===\n", passCount_, kChecks);
        result_ = passCount_ == kChecks ? 0 : 1;
        Exit();
    }

public:
    DiligentSkinnedTest()
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
        DiligentSkinnedTest game;
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
