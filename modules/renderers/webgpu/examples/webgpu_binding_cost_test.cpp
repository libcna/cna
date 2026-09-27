// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu_perf.md WEBGPUPERF-0001..0004: what a frame of PbrEffect draws costs wgpu.
//
// Every PbrEffect draw used to create four bind groups and write five uniform blocks with a queue
// write each, and wgpu then spent as long releasing those objects at the next submit as it had
// creating them: a street frame of 1 212 draws took 95 ms here where Vulkan took 30. The blocks now
// go into one uniform arena per flush, bound at dynamic offsets, and the texture, shadow and IBL
// groups come from a cache. This test pins both halves -- that it still draws right, and that it
// stays cheap.
//
// Check A -- sixteen draws, each with its own world matrix and base colour, put their own colour in
//   their own cell. The per-draw blocks reach the draw that owns them through the arena's offsets;
//   a wrong offset hands a draw its neighbour's colour, or every draw the last one's.
// Check B -- a steady frame of sixteen draws creates no bind group at all.
// Check C -- nor does a steady frame of sixty-four.
// Check D -- the frame's queue writes do not grow with its draw count: sixty-four draws cost the
//   same number of writes as sixteen.
// Check E -- a texture created and destroyed every frame is the one that frame draws. A cached group
//   must never survive into a later frame whose new texture happens to reuse the old one's address.
// Check F -- ... and the cache does not grow with the number of frames that did that.
// Check G -- zero uncaptured WebGPU errors across the run.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPURenderer.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::Renderers::WebGPU::WebGPURenderer;

namespace
{
    constexpr int kSize = 64;
    constexpr int kGrid = 4;
    constexpr int kChurnFrames = 24;

    // VertexPositionNormalTangentTexture's 48-byte layout, as webgpu_pbr3d_test.cpp uses it.
    struct PbrGpuVertex
    {
        float px, py, pz;
        float nx, ny, nz;
        float tx, ty, tz, tw;
        float u, v;
    };
    static_assert(sizeof(PbrGpuVertex) == 48, "PBR vertex must be 48 bytes");

    // Channels of exactly 0 or 1 survive the effect's sRGB encode as exactly 0 or 255.
    const std::array<Vector3, 8> kPalette{
        Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1), Vector3(1, 1, 0),
        Vector3(1, 0, 1), Vector3(0, 1, 1), Vector3(1, 1, 1), Vector3(0, 0, 0)};

    Vector3 CellColour(int cell) { return kPalette[static_cast<std::size_t>((cell * 3) % 8)]; }

    bool Matches(const Color& pixel, const Vector3& expected, int tolerance = 24)
    {
        const auto near = [tolerance](int value, float target) {
            const int wanted = target > 0.5f ? 255 : 0;
            return value >= wanted - tolerance && value <= wanted + tolerance;
        };
        return near(pixel.getRProperty(), expected.X) && near(pixel.getGProperty(), expected.Y) &&
               near(pixel.getBProperty(), expected.Z);
    }

    struct Counters
    {
        std::size_t bindGroups = 0;
        std::size_t queueWrites = 0;
    };
}

class WebGpuBindingCostTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<VertexBuffer> quad_;
    std::unique_ptr<PbrEffect> effect_;
    Texture2D whiteA_;
    Texture2D whiteB_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;
    Counters at16Start_, at16End_, at64Start_, at64End_;
    std::size_t cacheAfterFirstChurn_ = 0;
    int churnMismatches_ = 0;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    WebGPURenderer& Renderer()
    {
        return static_cast<WebGPURenderer&>(getGraphicsDeviceProperty().GetRenderer());
    }

    Counters Snapshot()
    {
        return Counters{Renderer().GetBindGroupCreateCountEXT(), Renderer().GetQueueWriteCountEXT()};
    }

    void PrepareEffect(Texture2D* texture)
    {
        effect_->setViewProperty(Matrix::getIdentityProperty());
        effect_->setProjectionProperty(Matrix::getIdentityProperty());
        effect_->setTextureProperty(texture);
        effect_->setRoughnessFactorProperty(1.0f);
        effect_->setMetallicFactorProperty(0.0f);
        effect_->setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect_->DirectionalLight0.setEnabledProperty(false);
        effect_->DirectionalLight1.setEnabledProperty(false);
        effect_->DirectionalLight2.setEnabledProperty(false);
    }

    // One draw per cell, `repeats` times over, each with its own world matrix and colour and the
    // two textures alternating -- sixteen or sixty-four draws with sixteen distinct uniform sets.
    void DrawGrid(GraphicsDevice& dev, int repeats)
    {
        dev.SetVertexBuffer(quad_.get());
        for (int r = 0; r < repeats; ++r)
            for (int cell = 0; cell < kGrid * kGrid; ++cell)
            {
                const float x = -0.75f + 0.5f * static_cast<float>(cell % kGrid);
                const float y = 0.75f - 0.5f * static_cast<float>(cell / kGrid);
                PrepareEffect(cell % 2 == 0 ? &whiteA_ : &whiteB_);
                effect_->setWorldProperty(Matrix::CreateScale(0.24f) *
                                          Matrix::CreateTranslation(x, y, 0.0f));
                effect_->setDiffuseColorProperty(CellColour(cell));
                effect_->Apply();
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            }
        dev.SetVertexBuffer(nullptr);
    }

    Color Read(GraphicsDevice& dev, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color pixel(0, 0, 0, 0);
        dev.GetBackBufferData(&region, &pixel, 0, 1);
        return pixel;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteA_ = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
        whiteB_ = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
        const PbrGpuVertex tl{-1, 1, 0.5f, 0, 0, -1, 1, 0, 0, 1, 0, 0};
        const PbrGpuVertex bl{-1, -1, 0.5f, 0, 0, -1, 1, 0, 0, 1, 0, 1};
        const PbrGpuVertex br{1, -1, 0.5f, 0, 0, -1, 1, 0, 0, 1, 1, 1};
        const PbrGpuVertex tr{1, 1, 0.5f, 0, 0, -1, 1, 0, 0, 1, 1, 0};
        const std::vector<PbrGpuVertex> verts{tl, bl, br, tl, br, tr};
        quad_ = std::make_unique<VertexBuffer>(dev, static_cast<int>(verts.size()));
        quad_->SetDataRaw(verts.data(), static_cast<int>(verts.size()),
                          static_cast<int>(sizeof(PbrGpuVertex)));
        effect_ = std::make_unique<PbrEffect>(dev);
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.Clear(Color(64, 64, 64, 255));

        // Frames 0-3 draw sixteen, 4-7 sixty-four. A counter read at the start of a frame includes
        // the previous frame's flush, so frame 3 -> 4 measures a steady frame of sixteen and
        // frame 7 -> 8 a steady frame of sixty-four.
        if (frame_ == 3) at16Start_ = Snapshot();
        if (frame_ == 4) at16End_ = Snapshot();
        if (frame_ == 7) at64Start_ = Snapshot();
        if (frame_ == 8) at64End_ = Snapshot();

        if (frame_ < 4)
        {
            DrawGrid(dev, 1);
        }
        else if (frame_ < 8)
        {
            DrawGrid(dev, 4);
        }
        else if (frame_ == 8)
        {
            DrawGrid(dev, 1);
            int wrong = 0;
            for (int cell = 0; cell < kGrid * kGrid; ++cell)
            {
                const int px = kSize / (2 * kGrid) + (kSize / kGrid) * (cell % kGrid);
                const int py = kSize / (2 * kGrid) + (kSize / kGrid) * (cell / kGrid);
                const Color pixel = Read(dev, px, py);
                if (!Matches(pixel, CellColour(cell)))
                {
                    ++wrong;
                    std::printf("    cell %d at (%d,%d) is (%d,%d,%d)\n", cell, px, py,
                                pixel.getRProperty(), pixel.getGProperty(), pixel.getBProperty());
                }
            }
            check(wrong == 0, "A: sixteen draws each show their own colour (" +
                                  std::to_string(wrong) + " wrong)");
        }
        else
        {
            // Check E: a fresh texture every frame, red and green in turn, destroyed at the end of
            // Draw while its queued draw still holds it.
            const int churn = frame_ - 9;
            const bool red = churn % 2 == 0;
            Texture2D texture = Texture2D::CreateFromPixels(
                dev, 1, 1,
                std::vector<std::uint8_t>{static_cast<std::uint8_t>(red ? 255 : 0),
                                          static_cast<std::uint8_t>(red ? 0 : 255), 0, 255});
            PrepareEffect(&texture);
            effect_->setWorldProperty(Matrix::getIdentityProperty());
            effect_->setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
            effect_->Apply();
            dev.SetVertexBuffer(quad_.get());
            dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            dev.SetVertexBuffer(nullptr);
            const Color pixel = Read(dev, kSize / 2, kSize / 2);
            if (!Matches(pixel, red ? Vector3(1, 0, 0) : Vector3(0, 1, 0)))
                ++churnMismatches_;
            if (churn == 2)
                cacheAfterFirstChurn_ = Renderer().GetBindingCacheSizeEXT();

            if (churn == kChurnFrames - 1)
            {
                const std::size_t groups16 = at16End_.bindGroups - at16Start_.bindGroups;
                const std::size_t groups64 = at64End_.bindGroups - at64Start_.bindGroups;
                const std::size_t writes16 = at16End_.queueWrites - at16Start_.queueWrites;
                const std::size_t writes64 = at64End_.queueWrites - at64Start_.queueWrites;
                std::printf("    per frame: 16 draws -> %zu groups, %zu writes; 64 draws -> %zu "
                            "groups, %zu writes\n", groups16, writes16, groups64, writes64);
                check(groups16 == 0, "B: a steady frame of 16 draws creates no bind group (" +
                                         std::to_string(groups16) + ")");
                check(groups64 == 0, "C: a steady frame of 64 draws creates no bind group (" +
                                         std::to_string(groups64) + ")");
                check(writes64 == writes16, "D: 64 draws cost the queue writes 16 do (" +
                                                std::to_string(writes64) + " vs " +
                                                std::to_string(writes16) + ")");
                check(churnMismatches_ == 0,
                      "E: every frame draws its own new texture (" +
                          std::to_string(churnMismatches_) + " frames wrong)");
                const std::size_t cacheEnd = Renderer().GetBindingCacheSizeEXT();
                check(cacheEnd <= cacheAfterFirstChurn_ + 2,
                      "F: the binding cache does not grow with dead textures (" +
                          std::to_string(cacheAfterFirstChurn_) + " -> " +
                          std::to_string(cacheEnd) + ")");
                const std::size_t errors = Renderer().GetUncapturedErrorCountEXT();
                check(errors == 0, "G: zero uncaptured WebGPU errors (" + std::to_string(errors) + ")");
                std::printf("=== %d/7 PASS ===\n", passCount_);
                result_ = passCount_ == 7 ? 0 : 1;
                Exit();
            }
        }
        ++frame_;
    }

public:
    WebGpuBindingCostTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // GetBackBufferData is HiDef-only (SOFTWARE-213).
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    WebGpuBindingCostTest game;
    game.Run();
    return game.getResult();
}
