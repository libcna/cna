// SPDX-License-Identifier: MS-PL
// plan_igl.md IGL-21/IGL-55: RenderTargetCube -- one shared cube image, six per-face
// framebuffers (FNA's own shape, per design decision documented in plan_igl.md).
//
// Scene: bind face PositiveX and clear it magenta with one draw; bind face PositiveY and clear it
// cyan with another. A working per-face binding leaves each face's own readback showing only its
// own colour, not the other's -- proving SetRenderTarget(cube, face) genuinely targets the right
// framebuffer rather than aliasing all six faces onto one image. A final check confirms releasing
// the cube target restores the back buffer, untouched by anything the cube draws did.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include "common/PixelTestGame.hpp"

#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kCubeSize = 16;
}

class IglRenderTargetCubeTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        device.Clear(Color(static_cast<bytecs>(20), static_cast<bytecs>(20),
                           static_cast<bytecs>(20), static_cast<bytecs>(255)));

        RenderTargetCube cube(device, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None);

        device.SetRenderTarget(&cube, CubeMapFace::PositiveX);
        device.Clear(Color(static_cast<bytecs>(255), static_cast<bytecs>(0),
                           static_cast<bytecs>(255), static_cast<bytecs>(255)));

        device.SetRenderTarget(&cube, CubeMapFace::PositiveY);
        device.Clear(Color(static_cast<bytecs>(0), static_cast<bytecs>(255),
                           static_cast<bytecs>(255), static_cast<bytecs>(255)));

        device.SetRenderTarget(nullptr);

        const Rectangle onePixel(0, 0, 1, 1);
        Color positiveX(0, 0, 0, 0);
        cube.GetData(CubeMapFace::PositiveX, 0, &onePixel, &positiveX, 0, 1);
        Color positiveY(0, 0, 0, 0);
        cube.GetData(CubeMapFace::PositiveY, 0, &onePixel, &positiveY, 0, 1);

        ExpectTrue("face PositiveX kept its own magenta clear, not PositiveY's cyan",
                  positiveX.getRProperty() > 200 && positiveX.getBProperty() > 200 &&
                      positiveX.getGProperty() < 40);
        ExpectTrue("face PositiveY kept its own cyan clear, not PositiveX's magenta",
                  positiveY.getGProperty() > 200 && positiveY.getBProperty() > 200 &&
                      positiveY.getRProperty() < 40);

        device.Clear(Color(static_cast<bytecs>(20), static_cast<bytecs>(20),
                           static_cast<bytecs>(20), static_cast<bytecs>(255)));
        ExpectPixel("releasing the cube target restores the back buffer, unaffected by either face",
                    Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(20), static_cast<bytecs>(20),
                          static_cast<bytecs>(20), static_cast<bytecs>(255)));
    }

public:
    IglRenderTargetCubeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglRenderTargetCubeTest>();
}
