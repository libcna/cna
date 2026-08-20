// SPDX-License-Identifier: MS-PL
// plans/plan_opengl4.md GL4-20: plain (non-render-target) TextureCube for the OpenGL4 graphics renderer
// -- CreateTextureCube previously fell through to IGraphicsRenderer's default (returns nullptr),
// so TextureCube::SetData/GetData silently no-op'd on this renderer. OpenGL4TextureCubeRenderer now
// allocates a real GL_TEXTURE_CUBE_MAP with every face x every mip level pre-allocated via
// glTexImage2D (needed so glTexSubImage2D's box writes land on defined storage) -- reusing the
// same GL_TEXTURE_CUBE_MAP_POSITIVE_X+face arithmetic OpenGL4RenderTargetCubeRenderer (GL4-15)
// already established. GetData deliberately does NOT Y-flip (unlike
// OpenGL4RenderTargetCubeRenderer::GetData, which flips because it reads back a framebuffer-origin
// render target) -- this is a plain texture, matching EasyGLTextureCubeRenderer::GetData's own
// non-flipped convention; Check C below is the real proof this direction is correct, not assumed.
//
// Methodology matches this project's own established easygl_texturecube_faces_test.cpp /
// easygl_texturecube_partial_rect_test.cpp / easygl_texturecube_mip_test.cpp family, adapted to
// PixelTestGame's newer OpenGL4-side convention.
//
// Check A -- a size=2 cube (mipMap=false) has each of the 6 faces written a distinct solid colour
//   via the whole-face SetData overload; every pixel of every face read back via the whole-face
//   GetData overload matches its own face's colour exactly -- proves faces don't alias each other.
// Check B -- a sub-rectangle SetData/GetData round-trip on a single face (via the rect overload)
//   proves x/y box offsets are honored, not just whole-face uploads, and doesn't bleed into an
//   adjacent, untouched face.
// Check C -- an asymmetric (non-square-symmetric) single-pixel pattern written into one corner of
//   a face is read back at the SAME corner it was written to (not vertically mirrored) --
//   decisive proof GetData's deliberate no-Y-flip choice is correct, not just "didn't throw".
// Check D -- a mipMap=true cube's level 1 (half size) round-trips real data on one face -- proves
//   level>0 storage genuinely exists (glTexImage2D pre-allocation per level), not just level 0.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

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

    struct FaceEntry { CubeMapFace face; const char* name; Color colour; };

    const std::array<FaceEntry, 6> kFaces = {{
        {CubeMapFace::PositiveX, "+X", Color(255,   0,   0, 255)},
        {CubeMapFace::NegativeX, "-X", Color(  0, 255,   0, 255)},
        {CubeMapFace::PositiveY, "+Y", Color(  0,   0, 255, 255)},
        {CubeMapFace::NegativeY, "-Y", Color(255, 255,   0, 255)},
        {CubeMapFace::PositiveZ, "+Z", Color(255,   0, 255, 255)},
        {CubeMapFace::NegativeZ, "-Z", Color(  0, 255, 255, 255)},
    }};
}

class OpenGL4TextureCubeTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& dev = getGraphicsDeviceProperty();

        // --- Check A: per-face round-trip on a size=2 cube ---
        {
            constexpr int kSize = 2;
            TextureCube tex(dev, kSize, /*mipMap=*/false, SurfaceFormat::Color);

            for (const auto& fe : kFaces)
            {
                std::vector<Color> px(kSize * kSize, fe.colour);
                tex.SetData(fe.face, px.data(), 0, kSize * kSize);
            }

            bool allOk = true;
            for (const auto& fe : kFaces)
            {
                std::vector<Color> rb(kSize * kSize, Color(0, 0, 0, 255));
                tex.GetData(fe.face, rb.data(), 0, kSize * kSize);
                for (const auto& px : rb)
                    if (!ColourEq(px, fe.colour)) allOk = false;
            }
            Check(allOk, "Check A: size=2 TextureCube per-face SetData/GetData round-trip (6 faces, no cross-face aliasing)");
        }

        // --- Check B: sub-rectangle box on a single face, doesn't bleed into a neighbor ---
        {
            constexpr int kSize = 4;
            TextureCube tex(dev, kSize, /*mipMap=*/false, SurfaceFormat::Color);

            const Color bg(10, 10, 10, 255);
            const Color fg(200, 50, 150, 255);
            std::vector<Color> fullFace(kSize * kSize, bg);
            tex.SetData(CubeMapFace::PositiveX, fullFace.data(), 0, kSize * kSize);
            tex.SetData(CubeMapFace::NegativeX, fullFace.data(), 0, kSize * kSize);

            const Rectangle box(1, 1, 2, 2);
            std::vector<Color> boxPx(4, fg);
            tex.SetData(CubeMapFace::PositiveX, 0, &box, boxPx.data(), 0, 4);

            std::vector<Color> insideBox(4, Color(0, 0, 0, 255));
            tex.GetData(CubeMapFace::PositiveX, 0, &box, insideBox.data(), 0, 4);
            bool insideOk = true;
            for (const auto& px : insideBox) if (!ColourEq(px, fg)) insideOk = false;

            const Rectangle corner(0, 0, 1, 1);
            std::vector<Color> outsideBox(1, Color(0, 0, 0, 255));
            tex.GetData(CubeMapFace::PositiveX, 0, &corner, outsideBox.data(), 0, 1);
            const bool outsideOk = ColourEq(outsideBox[0], bg);

            // The identical box region on the untouched -X face must still be background.
            std::vector<Color> otherFace(4, Color(0, 0, 0, 255));
            tex.GetData(CubeMapFace::NegativeX, 0, &box, otherFace.data(), 0, 4);
            bool otherFaceOk = true;
            for (const auto& px : otherFace) if (!ColourEq(px, bg)) otherFaceOk = false;

            Check(insideOk && outsideOk && otherFaceOk,
                  "Check B: a 2x2 sub-rectangle SetData/GetData round-trip on +X honors x/y offsets (doesn't bleed outside its box or into the -X face)");
        }

        // --- Check C: no-Y-flip proof via an asymmetric single-pixel corner pattern ---
        {
            constexpr int kSize = 2;
            TextureCube tex(dev, kSize, /*mipMap=*/false, SurfaceFormat::Color);

            const Color bg(0, 0, 0, 255);
            const Color marker(255, 128, 0, 255);
            std::vector<Color> fullFace(kSize * kSize, bg);
            tex.SetData(CubeMapFace::PositiveZ, fullFace.data(), 0, kSize * kSize);

            // Mark only the top-left texel (0,0) -- if GetData flipped Y, reading (0,0) back
            // would instead see background (the marker would appear at (0, kSize-1)).
            const Rectangle topLeft(0, 0, 1, 1);
            std::vector<Color> markerPx(1, marker);
            tex.SetData(CubeMapFace::PositiveZ, 0, &topLeft, markerPx.data(), 0, 1);

            std::vector<Color> readTopLeft(1, Color(0, 0, 0, 255));
            tex.GetData(CubeMapFace::PositiveZ, 0, &topLeft, readTopLeft.data(), 0, 1);
            const bool topLeftHasMarker = ColourEq(readTopLeft[0], marker);

            const Rectangle bottomLeft(0, kSize - 1, 1, 1);
            std::vector<Color> readBottomLeft(1, Color(0, 0, 0, 255));
            tex.GetData(CubeMapFace::PositiveZ, 0, &bottomLeft, readBottomLeft.data(), 0, 1);
            const bool bottomLeftStillBg = ColourEq(readBottomLeft[0], bg);

            Check(topLeftHasMarker && bottomLeftStillBg,
                  "Check C: GetData reads back the SAME (0,0) texel it was written to, not vertically mirrored (no-Y-flip confirmed for a plain, non-render-target TextureCube)");
        }

        // --- Check D: mip level 1 storage genuinely exists and round-trips ---
        {
            constexpr int kSize = 8;
            TextureCube tex(dev, kSize, /*mipMap=*/true, SurfaceFormat::Color);

            // Level 0: 8x8. Level 1: 4x4.
            const Color level1Colour(30, 200, 220, 255);
            std::vector<Color> level1Px(4 * 4, level1Colour);
            const Rectangle level1Rect(0, 0, 4, 4);
            tex.SetData(CubeMapFace::PositiveY, 1, &level1Rect, level1Px.data(), 0, static_cast<int>(level1Px.size()));

            std::vector<Color> rb(4 * 4, Color(0, 0, 0, 255));
            tex.GetData(CubeMapFace::PositiveY, 1, &level1Rect, rb.data(), 0, static_cast<int>(rb.size()));
            bool ok = true;
            for (const auto& px : rb) if (!ColourEq(px, level1Colour)) ok = false;

            Check(ok, "Check D: mipMap=true TextureCube level 1 (4x4) storage genuinely exists and round-trips real data");
        }
    }

public:
    OpenGL4TextureCubeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL4TextureCubeTest>();
}
