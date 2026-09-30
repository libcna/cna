// SPDX-License-Identifier: MS-PL
// living-room-simulator's R-23: SpriteBatch drawn into a bound RenderTargetCube face drew nothing.
// XNA sets the viewport to the face (Size x Size) when a cube face is bound and SpriteBatch lays
// the sprite out in that viewport's pixels. EasyGL's FlushBatch asked for the bound target's size
// through GetCurrentRenderTarget2DSize, which answered only for a 2D or MRT binding, so a cube face
// was laid out as the back buffer -- outside the face. Clear still reached the face, since it needs
// no projection.
//
// The face is 16 px, cleared black; a red sprite covers Rectangle(0, 0, 8, 8). The face is read
// back with GetData, which reads the GPU face, so the check also pins the orientation: the red
// block must be the top-left quarter, not the bottom-left.

#include "common/PixelTestGame.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SpriteBatchRenderTargetCubeFaceTest final : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        constexpr int kFace = 16;
        auto& device = getGraphicsDeviceProperty();
        const std::vector<std::uint8_t> whitePixel = {255, 255, 255, 255};
        Texture2D white = Texture2D::CreateFromPixels(device, 1, 1, whitePixel);
        RenderTargetCube cube(device, kFace, false, SurfaceFormat::Color, DepthFormat::None);
        SpriteBatch batch(device);

        device.SetRenderTarget(&cube, CubeMapFace::PositiveZ);
        device.Clear(Color::Black);
        batch.Begin();
        batch.Draw(white, Rectangle(0, 0, kFace / 2, kFace / 2), Color::Red);
        batch.End();
        device.SetRenderTarget(nullptr);

        std::vector<Color> pixels(static_cast<std::size_t>(kFace * kFace));
        cube.GetData(CubeMapFace::PositiveZ, pixels.data(), static_cast<int>(pixels.size()));

        int wrong = 0;
        for (int y = 0; y < kFace; ++y)
        {
            for (int x = 0; x < kFace; ++x)
            {
                const bool inside = x < kFace / 2 && y < kFace / 2;
                const Color expected = inside ? Color::Red : Color::Black;
                if (pixels[static_cast<std::size_t>(y * kFace + x)] != expected) { ++wrong; }
            }
        }
        const Color topLeft = pixels[0];
        const Color bottomLeft = pixels[static_cast<std::size_t>((kFace - 1) * kFace)];
        const bool pass = wrong == 0;
        std::printf("[%s] SpriteBatch into a cube face: %d of %d pixels wrong; top-left (%d,%d,%d) "
                    "bottom-left (%d,%d,%d)\n",
                    pass ? "PASS" : "FAIL", wrong, kFace * kFace, topLeft.getRProperty(),
                    topLeft.getGProperty(), topLeft.getBProperty(), bottomLeft.getRProperty(),
                    bottomLeft.getGProperty(), bottomLeft.getBProperty());
        if (!pass) { MarkFailedEXT(); }
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<SpriteBatchRenderTargetCubeFaceTest>();
}
