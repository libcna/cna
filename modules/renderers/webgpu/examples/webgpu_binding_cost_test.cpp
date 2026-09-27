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
// Check H -- WEBGPUPERF-0007, the classic families: a steady frame of lit, textured BasicEffect draws
//   and SpriteBatch sprites creates no bind group either.
// Check I -- ... and four times as many of each cost the same number of queue writes.
// Check J -- WEBGPUPERF-0005, a custom-WGSL ShaderEffect: sixteen draws, each with its own World and
//   colour in the effect's uniform block, put their own colour in their own cell.
// Check K -- a steady frame of those draws creates no bind group.
// Check L -- ... and four times as many cost the same number of queue writes.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
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
    constexpr int kChurnStart = 17;
    constexpr int kShaderEffectStart = kChurnStart + kChurnFrames;

    // A custom WGSL ShaderEffect on the 3D route, sampling a texture so its group carries the
    // sampler and the view beside the uniform block.
    const char* const kVertexWgsl = R"WGSL(
struct Uniforms {
    World: mat4x4f,
    View: mat4x4f,
    Projection: mat4x4f,
    uColor: vec3f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
struct VOut {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};
@vertex fn vs_main(
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f
) -> VOut {
    var out: VOut;
    out.position = u.Projection * u.View * u.World * vec4f(position, 1.0);
    out.uv = uv + normal.xy * 0.0;
    return out;
}
)WGSL";
    const char* const kFragmentWgsl = R"WGSL(
struct Uniforms {
    World: mat4x4f,
    View: mat4x4f,
    Projection: mat4x4f,
    uColor: vec3f,
};
@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var tex: texture_2d<f32>;
@fragment fn fs_main(@location(0) uv: vec2f) -> @location(0) vec4f {
    return vec4f(textureSample(tex, texSampler, uv).rgb * u.uColor, 1.0);
}
)WGSL";
    const char* const kUniformNames[] = {"World", "View", "Projection", "uColor"};
    const int kUniformOffsets[] = {0, 64, 128, 192};
    constexpr int kUniformBlockSize = 208;

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
    std::unique_ptr<VertexBuffer> basicQuad_;
    std::unique_ptr<PbrEffect> effect_;
    std::unique_ptr<BasicEffect> basic_;
    std::unique_ptr<SpriteBatch> sprites_;
    std::unique_ptr<ShaderEffect> shaderEffect_;
    Texture2D whiteA_;
    Texture2D whiteB_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;
    Counters at16Start_, at16End_, at64Start_, at64End_;
    Counters basicStart_, basicEnd_, basic4Start_, basic4End_;
    Counters shaderStart_, shaderEnd_, shader4Start_, shader4End_;
    std::size_t cacheAfterChurn_ = 0;
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

    // The classic families: `repeats` x 16 lit, textured BasicEffect draws, then `repeats` x 64
    // sprites alternating between two textures, drawn one by one (Deferred).
    void DrawClassic(GraphicsDevice& dev, int repeats)
    {
        dev.SetVertexBuffer(basicQuad_.get());
        for (int r = 0; r < repeats; ++r)
            for (int cell = 0; cell < kGrid * kGrid; ++cell)
            {
                const float x = -0.75f + 0.5f * static_cast<float>(cell % kGrid);
                const float y = 0.75f - 0.5f * static_cast<float>(cell / kGrid);
                basic_->setWorldProperty(Matrix::CreateScale(0.24f) *
                                         Matrix::CreateTranslation(x, y, 0.0f));
                basic_->setTextureProperty(cell % 2 == 0 ? &whiteA_ : &whiteB_);
                basic_->setDiffuseColorProperty(CellColour(cell));
                basic_->Apply();
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            }
        dev.SetVertexBuffer(nullptr);

        sprites_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend);
        for (int i = 0; i < 64 * repeats; ++i)
            sprites_->Draw(i % 2 == 0 ? whiteA_ : whiteB_,
                           Vector2(static_cast<float>(i % 60), static_cast<float>((i / 60) % 60)),
                           CellColour(i % 16) == Vector3(0, 0, 0) ? Color::Gray : Color::White);
        sprites_->End();
    }

    // `repeats` x 16 draws of the custom WGSL effect, each cell with its own World and colour.
    void DrawShaderEffectGrid(GraphicsDevice& dev, int repeats)
    {
        dev.SetVertexBuffer(basicQuad_.get());
        for (int r = 0; r < repeats; ++r)
            for (int cell = 0; cell < kGrid * kGrid; ++cell)
            {
                const float x = -0.75f + 0.5f * static_cast<float>(cell % kGrid);
                const float y = 0.75f - 0.5f * static_cast<float>(cell / kGrid);
                shaderEffect_->setWorldProperty(Matrix::CreateScale(0.24f) *
                                                Matrix::CreateTranslation(x, y, 0.0f));
                shaderEffect_->setViewProperty(Matrix::getIdentityProperty());
                shaderEffect_->setProjectionProperty(Matrix::getIdentityProperty());
                shaderEffect_->Apply();
                shaderEffect_->SetTexture(0, cell % 2 == 0 ? whiteA_ : whiteB_);
                const Vector3 colour = CellColour(cell);
                shaderEffect_->SetUniformVec3("uColor", colour.X, colour.Y, colour.Z);
                dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 2);
            }
        dev.SetVertexBuffer(nullptr);
    }

    int CountWrongCells(GraphicsDevice& dev)
    {
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
        return wrong;
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

        basicQuad_ = std::make_unique<VertexBuffer>(
            dev, VertexPositionNormalTexture::getVertexDeclarationStatic(), 6, BufferUsage::None);
        const Vector3 n(0.0f, 0.0f, -1.0f);
        const VertexPositionNormalTexture basicVerts[6] = {
            {Vector3(-1.0f, 1.0f, 0.5f), n, Vector2(0.0f, 0.0f)},
            {Vector3(-1.0f, -1.0f, 0.5f), n, Vector2(0.0f, 1.0f)},
            {Vector3(1.0f, -1.0f, 0.5f), n, Vector2(1.0f, 1.0f)},
            {Vector3(-1.0f, 1.0f, 0.5f), n, Vector2(0.0f, 0.0f)},
            {Vector3(1.0f, -1.0f, 0.5f), n, Vector2(1.0f, 1.0f)},
            {Vector3(1.0f, 1.0f, 0.5f), n, Vector2(1.0f, 0.0f)},
        };
        basicQuad_->SetData(basicVerts, 0, 6);
        basic_ = std::make_unique<BasicEffect>(dev);
        basic_->setTextureEnabledProperty(true);
        basic_->setLightingEnabledProperty(true);
        basic_->DirectionalLight0.setEnabledProperty(true);
        basic_->DirectionalLight0.setDirectionProperty(Vector3(0.0f, 0.0f, 1.0f));
        basic_->DirectionalLight0.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        sprites_ = std::make_unique<SpriteBatch>(dev);
        shaderEffect_ = std::make_unique<ShaderEffect>(dev, kVertexWgsl, kFragmentWgsl);
        if (shaderEffect_->IsEffectValid())
            shaderEffect_->DeclareUniformBlockEXT(kUniformBlockSize, kUniformNames, kUniformOffsets, 4);
        else
            std::printf("    the ShaderEffect did not compile: %s\n",
                        shaderEffect_->GetCompileErrorEXT().c_str());
    }

    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.setDepthStencilStateProperty(DepthStencilState::None);
        dev.Clear(Color(64, 64, 64, 255));

        // Frames 0-3 draw sixteen PBR draws, 4-7 sixty-four, 8-11 the classic families once and
        // 12-15 four times over. A counter read at the start of a frame includes the previous
        // frame's flush, so the last frame of each phase is measured from its start to the next.
        if (frame_ == 3) at16Start_ = Snapshot();
        if (frame_ == 4) at16End_ = Snapshot();
        if (frame_ == 7) at64Start_ = Snapshot();
        if (frame_ == 8) at64End_ = Snapshot();
        if (frame_ == 11) basicStart_ = Snapshot();
        if (frame_ == 12) basicEnd_ = Snapshot();
        if (frame_ == 15) basic4Start_ = Snapshot();
        if (frame_ == 16) basic4End_ = Snapshot();
        if (frame_ == kShaderEffectStart + 3) shaderStart_ = Snapshot();
        if (frame_ == kShaderEffectStart + 4) shaderEnd_ = Snapshot();
        if (frame_ == kShaderEffectStart + 7) shader4Start_ = Snapshot();
        if (frame_ == kShaderEffectStart + 8) shader4End_ = Snapshot();

        if (frame_ < 4)
        {
            DrawGrid(dev, 1);
        }
        else if (frame_ < 8)
        {
            DrawGrid(dev, 4);
        }
        else if (frame_ < 12)
        {
            DrawClassic(dev, 1);
        }
        else if (frame_ < 16)
        {
            DrawClassic(dev, 4);
        }
        else if (frame_ == 16)
        {
            DrawGrid(dev, 1);
            const int wrong = CountWrongCells(dev);
            check(wrong == 0, "A: sixteen draws each show their own colour (" +
                                  std::to_string(wrong) + " wrong)");
        }
        else if (frame_ < kShaderEffectStart)
        {
            // Check E: a fresh texture every frame, red and green in turn, destroyed at the end of
            // Draw while its queued draw still holds it.
            const int churn = frame_ - kChurnStart;
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
                cacheAfterChurn_ = Renderer().GetBindingCacheSizeEXT();
        }
        else if (frame_ < kShaderEffectStart + 4)
        {
            DrawShaderEffectGrid(dev, 1);
        }
        else if (frame_ < kShaderEffectStart + 8)
        {
            DrawShaderEffectGrid(dev, 4);
        }
        else
        {
            DrawShaderEffectGrid(dev, 1);
            const int wrongShader = CountWrongCells(dev);
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
                const std::size_t cacheEnd = cacheAfterChurn_;
                check(cacheEnd <= cacheAfterFirstChurn_ + 2,
                      "F: the binding cache does not grow with dead textures (" +
                          std::to_string(cacheAfterFirstChurn_) + " -> " +
                          std::to_string(cacheEnd) + ")");
                const std::size_t errors = Renderer().GetUncapturedErrorCountEXT();
                check(errors == 0, "G: zero uncaptured WebGPU errors (" + std::to_string(errors) + ")");
                const std::size_t groupsBasic = basicEnd_.bindGroups - basicStart_.bindGroups;
                const std::size_t writesBasic = basicEnd_.queueWrites - basicStart_.queueWrites;
                const std::size_t groupsBasic4 = basic4End_.bindGroups - basic4Start_.bindGroups;
                const std::size_t writesBasic4 = basic4End_.queueWrites - basic4Start_.queueWrites;
                std::printf("    per frame: 16 BasicEffect + 64 sprites -> %zu groups, %zu writes; "
                            "x4 -> %zu groups, %zu writes\n", groupsBasic, writesBasic, groupsBasic4,
                            writesBasic4);
                check(groupsBasic == 0 && groupsBasic4 == 0,
                      "H: a steady frame of BasicEffect draws and sprites creates no bind group (" +
                          std::to_string(groupsBasic) + ", " + std::to_string(groupsBasic4) + ")");
                check(writesBasic4 == writesBasic,
                      "I: four times the classic draws cost the queue writes one does (" +
                          std::to_string(writesBasic4) + " vs " + std::to_string(writesBasic) + ")");
                check(wrongShader == 0, "J: sixteen ShaderEffect draws each show their own colour (" +
                                            std::to_string(wrongShader) + " wrong)");
                const std::size_t groupsShader = shaderEnd_.bindGroups - shaderStart_.bindGroups;
                const std::size_t writesShader = shaderEnd_.queueWrites - shaderStart_.queueWrites;
                const std::size_t groupsShader4 = shader4End_.bindGroups - shader4Start_.bindGroups;
                const std::size_t writesShader4 =
                    shader4End_.queueWrites - shader4Start_.queueWrites;
                std::printf("    per frame: 16 ShaderEffect draws -> %zu groups, %zu writes; x4 -> "
                            "%zu groups, %zu writes\n", groupsShader, writesShader, groupsShader4,
                            writesShader4);
                check(groupsShader == 0 && groupsShader4 == 0,
                      "K: a steady frame of ShaderEffect draws creates no bind group (" +
                          std::to_string(groupsShader) + ", " + std::to_string(groupsShader4) + ")");
                check(writesShader4 == writesShader,
                      "L: four times the ShaderEffect draws cost the queue writes one does (" +
                          std::to_string(writesShader4) + " vs " + std::to_string(writesShader) +
                          ")");
                std::printf("=== %d/12 PASS ===\n", passCount_);
                result_ = passCount_ == 12 ? 0 : 1;
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
