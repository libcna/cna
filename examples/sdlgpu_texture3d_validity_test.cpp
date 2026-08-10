// SPDX-License-Identifier: MS-PL
// REMED-GFX-099 minimal regression: creating a mipmapped public Texture3D must not make SDL_GPU's
// Vulkan renderer create an invalid image view. The original defect occurs during construction,
// before any SetData/GetData call: an 8x8x2, four-level SDL_GPU_TEXTURETYPE_3D texture created
// with COLOR_TARGET usage makes SDL create 2D depth-plane views for every base-depth plane at
// every mip. Plane 1 is out of range once mip depth shrinks to 1.
//
// Pixel/data checks cannot detect a construction-time validation defect, so CMake makes every
// GFX-099 image-view/blit VUID and every validation error/warning test-fatal. This executable
// deliberately performs only construction; the larger data-contract regression remains in
// sdlgpu_texture3d_test.cpp.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class SdlGpuTexture3DValidityTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& device = getGraphicsDeviceProperty();
        Texture3D texture(device, 8, 8, 2, true, SurfaceFormat::Color);
        const bool propertiesExact =
            texture.getWidthProperty() == 8 &&
            texture.getHeightProperty() == 8 &&
            texture.getDepthProperty() == 2 &&
            texture.getLevelCountProperty() == 4 &&
            texture.getFormatProperty() == SurfaceFormat::Color;

        std::printf("[%s] create-only 8x8x2 Texture3D has four legal mip levels\n",
                    propertiesExact ? "PASS" : "FAIL");
        std::printf("=== %d/1 PASS ===\n", propertiesExact ? 1 : 0);
        result_ = propertiesExact ? 0 : 1;
        Exit();
    }

public:
    SdlGpuTexture3DValidityTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int Result() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuTexture3DValidityTest game;
    game.Run();
    return game.Result();
}
