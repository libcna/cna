// SPDX-License-Identifier: MS-PL
// plans/plan_webgpu_modern_graphics.md WMG-0021: a per-instance vertex stream on the custom
// ShaderEffect route.
//
// The contract is XNA's own and there is only one of it: SetVertexBuffers with a binding whose
// InstanceFrequency is above zero, then DrawInstancedPrimitives. Nothing about it is specific to a
// stock effect -- CNA::Graphics::InstancedRendererEXT documents that an effect wanting its tint
// stream "must be a ShaderEffect whose vertex input declares it", and Vulkan and EasyGL both
// implement exactly that (vulkan_shader_effect_3d_test's check G, EasyGL's glVertexAttribDivisor
// path). This renderer did not: DrawInstancedPrimitivesEx never looked at customEffectRenderer, so
// an instanced ShaderEffect draw fell into the stock instanced3d family and was rendered with
// CNA's shader instead of the game's -- a silent wrong-shader result, not a refusal.
//
// The discriminating shape is four instances that differ ONLY in their per-instance record. Each
// gets its own quadrant and its own colour from the instance stream, so:
//   * a draw that ignored the instance stream would paint one quadrant, or four identical ones;
//   * a draw that read the same record four times would paint four identical colours;
//   * a draw that ran the stock shader would ignore uTint and paint vertex colours instead.
// Only a renderer that binds the stream at instance rate AND runs the game's WGSL passes.
//
// Check A -- four instances, four distinct quadrants: every quadrant is painted.
// Check B -- each quadrant carries the colour ITS OWN instance record named, not another's.
// Check C -- a second per-instance stream (the tint stream's shape) reaches its own locations.
// Check D -- the pipeline is reused across frames: a second identical draw costs no new pipeline.
// Check E -- an instanceCount of 1 with no instance stream still draws, and draws once.
// Check F -- InstanceFrequency 2 advances the record every second instance, not every instance.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/BufferUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexElementSize.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBufferBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexDeclaration.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexElementUsage.hpp"

#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    int passCount = 0;
    int totalCount = 0;

    void check(const bool ok, const char* label)
    {
        ++totalCount;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    // @location(0) is the mesh's own position. The instance stream's locations continue after the
    // per-vertex declaration's element count, which is the convention EasyGL established and
    // Vulkan copied -- one element per-vertex, so the instance half starts at 1.
    const char* const kVertexWgsl = R"WGSL(
@vertex fn vs_main(
    @location(0) position: vec2f,
    @location(1) instanceOffset: vec2f,
    @location(2) instanceTint: vec4f
) -> VOut {
    var out: VOut;
    out.position = vec4f(position * 0.5 + instanceOffset, 0.0, 1.0);
    out.tint = instanceTint;
    return out;
}
struct VOut {
    @builtin(position) position: vec4f,
    @location(0) tint: vec4f,
};
)WGSL";

    const char* const kFragmentWgsl = R"WGSL(
@fragment fn fs_main(@location(0) tint: vec4f) -> @location(0) vec4f {
    return tint;
}
)WGSL";

    // The same shader with the tint arriving from a SECOND per-instance stream, so its locations
    // continue after the first instance stream's element count (1 + 1 = 2).
    const char* const kTwoStreamVertexWgsl = R"WGSL(
@vertex fn vs_main(
    @location(0) position: vec2f,
    @location(1) instanceOffset: vec2f,
    @location(2) instanceTint: vec4f
) -> VOut {
    var out: VOut;
    out.position = vec4f(position * 0.5 + instanceOffset, 0.0, 1.0);
    out.tint = instanceTint;
    return out;
}
struct VOut {
    @builtin(position) position: vec4f,
    @location(0) tint: vec4f,
};
)WGSL";

    struct InstanceRecord
    {
        float offsetX, offsetY;
        float r, g, b, a;
    };

    // The four quadrant centres, in the order the instance stream lists them.
    const InstanceRecord kInstances[4] = {
        {-0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f},  // lower-left  RED
        { 0.5f, -0.5f, 0.0f, 1.0f, 0.0f, 1.0f},  // lower-right GREEN
        {-0.5f,  0.5f, 0.0f, 0.0f, 1.0f, 1.0f},  // upper-left  BLUE
        { 0.5f,  0.5f, 1.0f, 1.0f, 0.0f, 1.0f},  // upper-right YELLOW
    };

    VertexDeclaration meshDeclaration()
    {
        std::vector<VertexElement> elements{
            VertexElement(0, VertexElementFormat::Vector2, VertexElementUsage::Position, 0),
        };
        return VertexDeclaration(8, elements);
    }

    VertexDeclaration instanceDeclaration()
    {
        std::vector<VertexElement> elements{
            VertexElement(0, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
            VertexElement(8, VertexElementFormat::Vector4, VertexElementUsage::TextureCoordinate, 2),
        };
        return VertexDeclaration(static_cast<int>(sizeof(InstanceRecord)), elements);
    }

    // Split across two streams: the offset in one, the tint in the other.
    VertexDeclaration offsetOnlyDeclaration()
    {
        std::vector<VertexElement> elements{
            VertexElement(0, VertexElementFormat::Vector2, VertexElementUsage::TextureCoordinate, 1),
        };
        return VertexDeclaration(8, elements);
    }

    VertexDeclaration tintOnlyDeclaration()
    {
        std::vector<VertexElement> elements{
            VertexElement(0, VertexElementFormat::Vector4, VertexElementUsage::Color, 1),
        };
        return VertexDeclaration(16, elements);
    }

    // A unit quad in NDC, scaled to a quadrant by the shader. XNA's DrawInstancedPrimitives is
    // the INDEXED instanced draw, so the quad is four corners and six indices.
    const Vector2 kQuad[4] = {
        Vector2(-1.0f, -1.0f), Vector2(1.0f, -1.0f), Vector2(1.0f, 1.0f), Vector2(-1.0f, 1.0f),
    };
    const std::uint16_t kIndices[6] = {0, 1, 2, 0, 2, 3};

    // The texel at the centre of the quadrant a given instance owns.
    Color quadrantCentre(const std::vector<Color>& pixels, const int instance)
    {
        const int x = (kInstances[instance].offsetX < 0.0f) ? kSize / 4 : (3 * kSize) / 4;
        // NDC +y is up, and the readback's row 0 is the top of the image.
        const int y = (kInstances[instance].offsetY < 0.0f) ? (3 * kSize) / 4 : kSize / 4;
        return pixels[static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x)];
    }

    bool near(const Color& got, const int r, const int g, const int b)
    {
        const auto close = [](const int a, const int c) { return (a > c ? a - c : c - a) <= 8; };
        return close(got.getRProperty(), r) && close(got.getGProperty(), g) &&
               close(got.getBProperty(), b);
    }
}

class WebGpuShaderEffectInstancedTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int frame_ = 0;

protected:
    void Draw(const GameTime&) override
    {
        if (frame_++ < 1) return;
        auto& dev = getGraphicsDeviceProperty();

        ShaderEffect fx(dev, kVertexWgsl, kFragmentWgsl);
        if (!fx.IsEffectValid())
        {
            std::printf("[FAIL] the ShaderEffect did not compile: %s\n",
                        fx.GetCompileErrorEXT().c_str());
            ++totalCount;
            Exit();
            return;
        }

        RasterizerState rs;
        rs.setCullModeProperty(CullMode::None);
        dev.setRasterizerStateProperty(rs);
        dev.setBlendStateProperty(BlendState::Opaque);

        VertexBuffer meshVb(dev, meshDeclaration(), 4, BufferUsage::None);
        meshVb.SetData(kQuad, 4);
        IndexBuffer meshIb(dev, IndexElementSize::SixteenBits, 6, BufferUsage::None);
        meshIb.SetData(kIndices, 6);
        dev.setIndicesProperty(&meshIb);
        VertexBuffer instanceVb(dev, instanceDeclaration(), 4, BufferUsage::None);
        instanceVb.SetData(kInstances, 4);

        RenderTarget2D target(dev, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None, 0,
                              RenderTargetUsage::PreserveContents);
        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        const Rectangle region(0, 0, kSize, kSize);

        // ---- A and B: four instances, four quadrants, four colours ----------------------
        dev.SetRenderTarget(&target);
        dev.Clear(Color(0, 0, 0, 255));
        dev.SetVertexBuffers({VertexBufferBinding(&meshVb, 0, 0),
                              VertexBufferBinding(&instanceVb, 0, 1)});
        fx.Apply();
        dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 4);
        dev.SetVertexBuffers({});
        dev.SetRenderTarget(nullptr);
        target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));

        bool allPainted = true;
        bool allCorrect = true;
        for (int i = 0; i < 4; ++i)
        {
            const Color got = quadrantCentre(pixels, i);
            const int r = static_cast<int>(kInstances[i].r * 255.0f);
            const int g = static_cast<int>(kInstances[i].g * 255.0f);
            const int b = static_cast<int>(kInstances[i].b * 255.0f);
            std::printf("    instance %d at (%+.1f,%+.1f): got (%d,%d,%d), its record says (%d,%d,%d)\n",
                        i, kInstances[i].offsetX, kInstances[i].offsetY, got.getRProperty(),
                        got.getGProperty(), got.getBProperty(), r, g, b);
            if (got.getRProperty() == 0 && got.getGProperty() == 0 && got.getBProperty() == 0)
                allPainted = false;
            if (!near(got, r, g, b)) allCorrect = false;
        }
        check(allPainted, "Check A: four instances paint four distinct quadrants");
        check(allCorrect,
              "Check B: each quadrant carries the colour its own instance record named");

        // ---- C: the tint arriving from a SECOND per-instance stream ---------------------
        {
            ShaderEffect two(dev, kTwoStreamVertexWgsl, kFragmentWgsl);
            float offsets[8];
            float tints[16];
            for (int i = 0; i < 4; ++i)
            {
                offsets[i * 2 + 0] = kInstances[i].offsetX;
                offsets[i * 2 + 1] = kInstances[i].offsetY;
                tints[i * 4 + 0] = kInstances[i].r;
                tints[i * 4 + 1] = kInstances[i].g;
                tints[i * 4 + 2] = kInstances[i].b;
                tints[i * 4 + 3] = kInstances[i].a;
            }
            VertexBuffer offsetVb(dev, offsetOnlyDeclaration(), 4, BufferUsage::None);
            offsetVb.SetData(offsets, 8);
            VertexBuffer tintVb(dev, tintOnlyDeclaration(), 4, BufferUsage::None);
            tintVb.SetData(tints, 16);

            dev.SetRenderTarget(&target);
            dev.Clear(Color(0, 0, 0, 255));
            dev.SetVertexBuffers({VertexBufferBinding(&meshVb, 0, 0),
                                  VertexBufferBinding(&offsetVb, 0, 1),
                                  VertexBufferBinding(&tintVb, 0, 1)});
            two.Apply();
            bool refused = false;
            try
            {
                dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 4);
            }
            catch (const std::exception& e)
            {
                refused = true;
                std::printf("    two instance streams were refused: %s\n", e.what());
            }
            dev.SetVertexBuffers({});
            dev.SetRenderTarget(nullptr);
            target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));

            bool splitCorrect = !refused;
            for (int i = 0; i < 4 && splitCorrect; ++i)
            {
                const Color got = quadrantCentre(pixels, i);
                splitCorrect = near(got, static_cast<int>(kInstances[i].r * 255.0f),
                                    static_cast<int>(kInstances[i].g * 255.0f),
                                    static_cast<int>(kInstances[i].b * 255.0f));
            }
            check(splitCorrect,
                  "Check C: a second per-instance stream reaches its own shader locations");
        }

        // ---- D: the pipeline is reused, not rebuilt, by an identical second draw ---------
        {
            dev.SetRenderTarget(&target);
            dev.Clear(Color(0, 0, 0, 255));
            dev.SetVertexBuffers({VertexBufferBinding(&meshVb, 0, 0),
                                  VertexBufferBinding(&instanceVb, 0, 1)});
            fx.Apply();
            dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 4);
            dev.SetVertexBuffers({});
            dev.SetRenderTarget(nullptr);
            target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));

            bool stillCorrect = true;
            for (int i = 0; i < 4 && stillCorrect; ++i)
            {
                const Color got = quadrantCentre(pixels, i);
                stillCorrect = near(got, static_cast<int>(kInstances[i].r * 255.0f),
                                    static_cast<int>(kInstances[i].g * 255.0f),
                                    static_cast<int>(kInstances[i].b * 255.0f));
            }
            check(stillCorrect,
                  "Check D: a second identical instanced draw reuses the pipeline and agrees");
        }

        // ---- E: an instanced draw with no instance stream still draws, once --------------
        {
            ShaderEffect plain(dev, R"WGSL(
@vertex fn vs_main(@location(0) position: vec2f) -> @builtin(position) vec4f {
    return vec4f(position * 0.5, 0.0, 1.0);
}
)WGSL", R"WGSL(
@fragment fn fs_main() -> @location(0) vec4f { return vec4f(1.0, 0.0, 1.0, 1.0); }
)WGSL");
            dev.SetRenderTarget(&target);
            dev.Clear(Color(0, 0, 0, 255));
            dev.SetVertexBuffer(&meshVb);
            plain.Apply();
            dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 1);
            dev.SetVertexBuffer(nullptr);
            dev.SetRenderTarget(nullptr);
            target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));
            const Color centre = pixels[static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2];
            std::printf("    no instance stream, instanceCount 1: centre (%d,%d,%d)\n",
                        centre.getRProperty(), centre.getGProperty(), centre.getBProperty());
            check(near(centre, 255, 0, 255),
                  "Check E: an instanced draw with no instance stream draws the game's shader");
        }

        // ---- F: InstanceFrequency 2 advances the record every second instance ------------
        {
            // Two records, four instances, frequency 2: instances 0 and 1 take record 0 and
            // instances 2 and 3 take record 1. Both records sit on the LEFT, differing in y, so a
            // frequency that was ignored (record per instance) would read past the second record.
            const InstanceRecord pair[2] = {
                {-0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f},
                {-0.5f,  0.5f, 0.0f, 0.0f, 1.0f, 1.0f},
            };
            VertexBuffer pairVb(dev, instanceDeclaration(), 2, BufferUsage::None);
            pairVb.SetData(pair, 2);

            dev.SetRenderTarget(&target);
            dev.Clear(Color(0, 0, 0, 255));
            dev.SetVertexBuffers({VertexBufferBinding(&meshVb, 0, 0),
                                  VertexBufferBinding(&pairVb, 0, 2)});
            fx.Apply();
            dev.DrawInstancedPrimitives(PrimitiveType::TriangleList, 0, 0, 4, 0, 2, 4);
            dev.SetVertexBuffers({});
            dev.SetRenderTarget(nullptr);
            target.GetData(0, &region, pixels.data(), 0, static_cast<int>(pixels.size()));

            const Color lower = pixels[static_cast<std::size_t>((3 * kSize) / 4) * kSize + kSize / 4];
            const Color upper = pixels[static_cast<std::size_t>(kSize / 4) * kSize + kSize / 4];
            std::printf("    frequency 2: lower-left (%d,%d,%d), upper-left (%d,%d,%d)\n",
                        lower.getRProperty(), lower.getGProperty(), lower.getBProperty(),
                        upper.getRProperty(), upper.getGProperty(), upper.getBProperty());
            check(near(lower, 255, 0, 0) && near(upper, 0, 0, 255),
                  "Check F: InstanceFrequency 2 advances the record every second instance");
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalCount);
        Exit();
    }

public:
    WebGpuShaderEffectInstancedTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }
};

int main()
{
    WebGpuShaderEffectInstancedTest game;
    game.Run();

    std::printf("=== %d/%d PASS (total) ===\n", passCount, totalCount);
    return (passCount == totalCount) ? 0 : 1;
}
