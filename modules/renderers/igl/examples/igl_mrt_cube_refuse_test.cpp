// SPDX-License-Identifier: MS-PL
// plans/plan_igl.md IGL-22: a RenderTargetCube face cannot take part in a multi-render-target bind on
// this renderer -- `IglRenderer::SetRenderTargets()` refuses it explicitly by name
// (`IglRenderer.cpp`: "a RenderTargetCube face cannot take part in a multi-render-target bind on
// this renderer") rather than silently misbinding it or crashing, matching this project's design
// convention that unsupported combinations refuse loudly instead of degrading quietly. This is the
// one Phase C combination `igl_mrt_test.cpp`/`igl_mrt4_test.cpp` deliberately never exercise (both
// use plain `RenderTarget2D` slots only) and `igl_rendertargetcube_test.cpp` never exercises either
// (it only ever binds one cube face at a time via `GraphicsDevice::SetRenderTarget`, never a
// multi-target set via `SetRenderTargets`).
//
// A second check confirms the refusal is a clean exception, not a corrupted device: after catching
// it, an ordinary back-buffer clear and readback still works exactly as if nothing had gone wrong.
//
// Exit code 0 = all PASS, 1 = any FAIL, 77 = SKIP (no GPU/display).

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include "common/PixelTestGame.hpp"

#include <memory>
#include <stdexcept>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;
    constexpr int kRTSize = 16;
    constexpr int kCubeSize = 16;
}

class IglMrtCubeRefuseTest : public CNA::Examples::PixelTestGame
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;

protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();

        RenderTarget2D rt0(device, kRTSize, kRTSize);
        RenderTargetCube cube(device, kCubeSize, false, SurfaceFormat::Color, DepthFormat::None);

        bool threw = false;
        try
        {
            device.SetRenderTargets(
                {RenderTargetBinding(&rt0), RenderTargetBinding(&cube, CubeMapFace::PositiveX)});
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        ExpectTrue("a RenderTargetCube face in a multi-target set is refused, not silently misbound",
                  threw);

        device.SetRenderTargets({});
        device.Clear(Color(static_cast<bytecs>(20), static_cast<bytecs>(20),
                           static_cast<bytecs>(20), static_cast<bytecs>(255)));
        ExpectPixel("after catching the refusal, the device still renders normally",
                    Rectangle(kSize / 2, kSize / 2, 1, 1),
                    Color(static_cast<bytecs>(20), static_cast<bytecs>(20),
                          static_cast<bytecs>(20), static_cast<bytecs>(255)));
    }

public:
    IglMrtCubeRefuseTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<IglMrtCubeRefuseTest>();
}
