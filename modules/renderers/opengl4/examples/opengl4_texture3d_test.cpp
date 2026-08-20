// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-20: plain (non-render-target) Texture3D for the OpenGL4 graphics renderer --
// CreateTexture3D previously fell through to IGraphicsRenderer's default (returns nullptr), so
// Texture3D::SetData/GetData silently no-op'd on this renderer. OpenGL4Texture3DRenderer now
// allocates a real GL_TEXTURE_3D with every mip level pre-allocated via gl4_glTexImage3D (needed
// so gl4_glTexSubImage3D's box writes land on defined storage), and reads back per Z-slice via a
// temporary FBO + gl4_glFramebufferTextureLayer + glReadPixels (desktop GL has glGetTexImage as
// an alternative, but the FBO approach matches this renderer's own established
// OpenGL4RenderTargetCubeRenderer::GetData per-face convention and supports true sub-rectangle
// reads, which glGetTexImage cannot do).
//
// Methodology matches this project's own established easygl_texture3d_slices_test.cpp /
// easygl_texture3d_partial_box_test.cpp / easygl_texture3d_mip_test.cpp family, adapted to
// PixelTestGame's newer OpenGL4-side convention.
//
// Check A-D -- a 2x2x4 volume (mipMap=false) has each Z-slice written a distinct solid colour
//   (Red/Green/Blue/Yellow) via the 9-arg SetData/GetData box overload; every pixel of every
//   slice read back matches its own slice's colour exactly -- proves slices don't alias.
// Check E -- a sub-rectangle SetData/GetData round-trip within a single slice (not the whole
//   face) proves x/y/z box offsets are honored, not just whole-level uploads.
// Check F -- a mipMap=true volume's level 1 (half every dimension) round-trips real data --
//   proves level>0 storage genuinely exists (glTexImage3D pre-allocation) and not just level 0.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    bool ColourEq(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty() &&
               a.getGProperty() == b.getGProperty() &&
               a.getBProperty() == b.getBProperty();
    }

    struct SliceEntry { int z; const char* name; Color colour; };

    const std::array<SliceEntry, 4> kSlices = {{
        {0, "z=0", Color(255, 0, 0, 255)},
        {1, "z=1", Color(0, 255, 0, 255)},
        {2, "z=2", Color(0, 0, 255, 255)},
        {3, "z=3", Color(255, 255, 0, 255)},
    }};
}

class OpenGL4Texture3DTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();

        // --- Checks A-D: per-slice round-trip on a 2x2x4 volume ---
        {
            constexpr int kW = 2, kH = 2, kD = 4;
            Texture3D tex(dev, kW, kH, kD, /*mipMap=*/false, SurfaceFormat::Color);

            for (const auto& se : kSlices)
            {
                std::vector<Color> px(kW * kH, se.colour);
                tex.SetData(0, 0, 0, kW, kH, se.z, se.z + 1, px.data(), 0, kW * kH);
            }

            bool allOk = true;
            for (const auto& se : kSlices)
            {
                std::vector<Color> rb(kW * kH, Color(0, 0, 0, 255));
                tex.GetData(0, 0, 0, kW, kH, se.z, se.z + 1, rb.data(), 0, kW * kH);
                for (const auto& px : rb)
                    if (!ColourEq(px, se.colour)) allOk = false;
            }
            Check(allOk, "Checks A-D: 2x2x4 Texture3D per-slice SetData/GetData round-trip (4 slices, no cross-slice aliasing)");
        }

        // --- Check E: sub-rectangle box within a single slice ---
        {
            constexpr int kW = 4, kH = 4, kD = 2;
            Texture3D tex(dev, kW, kH, kD, /*mipMap=*/false, SurfaceFormat::Color);

            const Color bg(10, 10, 10, 255);
            const Color fg(200, 50, 150, 255);
            std::vector<Color> full(kW * kH * kD, bg);
            tex.SetData(full.data(), 0, static_cast<int>(full.size()));

            // Write a 2x2 box at (1,1) within slice z=0 only.
            std::vector<Color> box(4, fg);
            tex.SetData(0, 1, 1, 3, 3, 0, 1, box.data(), 0, 4);

            std::vector<Color> insideBox(4, Color(0, 0, 0, 255));
            tex.GetData(0, 1, 1, 3, 3, 0, 1, insideBox.data(), 0, 4);
            bool insideOk = true;
            for (const auto& px : insideBox) if (!ColourEq(px, fg)) insideOk = false;

            // A pixel just outside the box (0,0) in the same slice must still be background.
            std::vector<Color> outsideBox(1, Color(0, 0, 0, 255));
            tex.GetData(0, 0, 0, 1, 1, 0, 1, outsideBox.data(), 0, 1);
            const bool outsideOk = ColourEq(outsideBox[0], bg);

            // The same box region in slice z=1 (never written) must also still be background.
            std::vector<Color> otherSlice(4, Color(0, 0, 0, 255));
            tex.GetData(0, 1, 1, 3, 3, 1, 2, otherSlice.data(), 0, 4);
            bool otherSliceOk = true;
            for (const auto& px : otherSlice) if (!ColourEq(px, bg)) otherSliceOk = false;

            Check(insideOk && outsideOk && otherSliceOk,
                  "Check E: a 2x2x1 sub-box SetData/GetData round-trip honors x/y/z offsets (doesn't bleed outside its box or into other slices)");
        }

        // --- Check F: mip level 1 storage genuinely exists and round-trips ---
        {
            constexpr int kW = 8, kH = 8, kD = 4;
            Texture3D tex(dev, kW, kH, kD, /*mipMap=*/true, SurfaceFormat::Color);

            // Level 0: 8x8x4. Level 1: 4x4x2.
            const Color level1Colour(30, 200, 220, 255);
            std::vector<Color> level1Px(4 * 4 * 2, level1Colour);
            tex.SetData(1, 0, 0, 4, 4, 0, 2, level1Px.data(), 0, static_cast<int>(level1Px.size()));

            std::vector<Color> rb(4 * 4 * 2, Color(0, 0, 0, 255));
            tex.GetData(1, 0, 0, 4, 4, 0, 2, rb.data(), 0, static_cast<int>(rb.size()));
            bool ok = true;
            for (const auto& px : rb) if (!ColourEq(px, level1Colour)) ok = false;

            Check(ok, "Check F: mipMap=true Texture3D level 1 (4x4x2) storage genuinely exists and round-trips real data");
        }
    }

public:
    OpenGL4Texture3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4Texture3DTest>();
}
