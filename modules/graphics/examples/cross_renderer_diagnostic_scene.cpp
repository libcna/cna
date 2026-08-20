// SPDX-License-Identifier: MS-PL
// plans/plan_software.md SOFTWARE-61/84: cross-renderer diagnostic tool. Renders one simple, fully
// unlit (VertexColorEnabled, no lighting) scene and dumps the resulting 64x64 RGBA8 backbuffer
// to a raw file -- deliberately renderer-agnostic (no #ifdef, no renderer-specific include), built
// once per renderer (see CMakeLists.txt's SOFTWARE and EASYGL sections) so the SAME source
// produces one dump per renderer, compared afterwards by cross_renderer_diagnostic_compare.
//
// This is not a self-contained CTest: CNA_GRAPHICS_RENDERER is a compile-time choice, so a single
// build/test run only ever has one renderer linked in. Comparing two renderers' output needs two
// separate builds' dumps, plus the comparator -- see docs/software-renderer.md's
// "Cross-renderer diagnostic" section for the exact 3-command invocation.
//
// CullNone is set explicitly: this triangle's screen-space winding happens to be
// counter-clockwise-as-displayed, which the real default RasterizerState.CullCounterClockwise
// would cull on any renderer that implements real culling (SOFTWARE-81) -- not the point of this
// diagnostic, so it's turned off deliberately, same mitigation already used by
// software_rasterizer_test.cpp/software_effects_test.cpp.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
}

class CrossRendererDiagnosticScene : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::string outputPath_;
    int result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        auto& dev = getGraphicsDeviceProperty();
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.Clear(Color::Black, 1.0f);

        // Tri-color triangle (unlit, VertexColorEnabled only) -- the same easy-to-reason-about
        // geometry as software_rasterizer_test.cpp's Check B, reused here so a known-good
        // reference already exists for this exact shape.
        const Color pureGreen(0, 255, 0, 255);
        const VertexPositionColor verts[3] = {
            {Vector3(0.0f, 0.9f, 0.5f), Color::Red},
            {Vector3(-0.9f, -0.9f, 0.5f), pureGreen},
            {Vector3(0.9f, -0.9f, 0.5f), Color::Blue},
        };
        VertexBuffer vb(dev, 3);
        vb.SetData(verts, 3);
        BasicEffect fx(dev);
        fx.VertexColorEnabled = true;
        fx.Apply();
        dev.SetVertexBuffer(&vb);
        dev.DrawPrimitives(PrimitiveType::TriangleList, 0, 1);
        dev.SetVertexBuffer(nullptr);

        std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
        const Rectangle region(0, 0, kSize, kSize);
        dev.GetBackBufferData(&region, pixels.data(), 0, static_cast<int>(pixels.size()));

        std::FILE* f = std::fopen(outputPath_.c_str(), "wb");
        if (f == nullptr)
        {
            std::fprintf(stderr, "cross_renderer_diagnostic_scene: failed to open '%s' for writing\n",
                        outputPath_.c_str());
            Exit();
            return;
        }
        // Written byte-for-byte as R,G,B,A per pixel (Color::getRProperty()..getAProperty(),
        // not Color's own in-memory packed layout) so the dump format is independent of Color's
        // internal representation.
        std::vector<std::uint8_t> rgba(pixels.size() * 4u);
        for (std::size_t i = 0; i < pixels.size(); ++i)
        {
            rgba[i * 4 + 0] = pixels[i].getRProperty();
            rgba[i * 4 + 1] = pixels[i].getGProperty();
            rgba[i * 4 + 2] = pixels[i].getBProperty();
            rgba[i * 4 + 3] = pixels[i].getAProperty();
        }
        std::fwrite(rgba.data(), 1, rgba.size(), f);
        std::fclose(f);
        std::printf("wrote %zu bytes to %s\n", rgba.size(), outputPath_.c_str());
        result_ = 0;
        Exit();
    }

public:
    explicit CrossRendererDiagnosticScene(std::string outputPath) : outputPath_(std::move(outputPath))
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main(int argc, char** argv)
{
    const std::string outputPath = argc > 1 ? argv[1] : "diagnostic_dump.rgba";
    CrossRendererDiagnosticScene game(outputPath);
    game.Run();
    return game.getResult();
}
