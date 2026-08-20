// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: real Texture3D mip-level SetData/GetData round-trip proof -- reuses
// examples/easygl_texture3d_mip_test.cpp's own scene, mip-level colouring, and expected values
// verbatim. Unlike Texture2D's own mip test (opengl2_texture_mip_filter_test.cpp's sibling,
// easygl_texture2d_mip_test.cpp, which is pure CPU-shadow readback), Texture3D::GetData() calls
// renderer_->GetData() directly -- a real glGetTexImage GPU readback -- so this genuinely exercises
// Texture3DRenderer's per-level glTexImage3D storage allocation, not just the CPU-side shadow.
//
// Until this task, Texture3DRenderer's constructor only ever allocated level-0 GL storage
// (glTexImage3D called once, level=0) regardless of the mipMap flag, and never clamped
// GL_TEXTURE_MAX_LEVEL -- SetData(level > 0, ...) wrote via glTexSubImage3D into storage that was
// never allocated, which is undefined/a GL error, not a working upload. Fixed by allocating every
// level's storage up front (mirroring TextureCubeRenderer's own established per-level loop),
// confirmed against FNA3D_Driver_OpenGL.c's real OPENGL_CreateTexture3D: depth halves per level
// (SDL_max(depth >> i, 1)) exactly like width/height, even though Texture3D.cpp's own
// CalculateMipLevels(width, height) deliberately excludes depth from the LEVEL COUNT formula.
//
// A 4x4x4 Texture3D with mipMap=true has 3 mip levels: 4x4x4 (mip 0), 2x2x2 (mip 1), 1x1x1 (mip 2).
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    const Color kRed   (255,   0,   0, 255);
    const Color kWhite (255, 255, 255, 255);
    const Color kOrange(255, 128,   0, 255);
    const Color kGray  (128, 128, 128, 255);

    bool colourEq(Color a, Color b)
    {
        return a.getRProperty() == b.getRProperty()
            && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty();
    }
}

class OpenGL2Texture3DMipTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 0;

    void check(bool ok, const char* label, Color got, Color want)
    {
        std::printf("[%s] %s: want=(%d,%d,%d) got=(%d,%d,%d)\n",
            ok ? "PASS" : "FAIL", label,
            want.getRProperty(), want.getGProperty(), want.getBProperty(),
            got.getRProperty(),  got.getGProperty(),  got.getBProperty());
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();

        Texture3D vol(dev, 4, 4, 4, /*mipMap=*/true, SurfaceFormat::Color);

        std::vector<Color> red64  (64, kRed);
        std::vector<Color> white8 ( 8, kWhite);
        std::vector<Color> orange1( 1, kOrange);

        vol.SetData(0, 0, 0, 4, 4, 0, 4, red64.data(),   0, 64);
        vol.SetData(1, 0, 0, 2, 2, 0, 2, white8.data(),  0,  8);
        vol.SetData(2, 0, 0, 1, 1, 0, 1, orange1.data(), 0,  1);

        {
            std::vector<Color> rb(64, kGray);
            vol.GetData(0, 0, 0, 4, 4, 0, 4, rb.data(), 0, 64);
            for (int i = 0; i < 64; ++i)
            {
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "mip0 texel %d", i);
                check(colourEq(rb[i], kRed), lbl, rb[i], kRed);
            }
        }
        {
            std::vector<Color> rb(8, kGray);
            vol.GetData(1, 0, 0, 2, 2, 0, 2, rb.data(), 0, 8);
            for (int i = 0; i < 8; ++i)
            {
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "mip1 texel %d", i);
                check(colourEq(rb[i], kWhite), lbl, rb[i], kWhite);
            }
        }
        {
            std::vector<Color> rb(1, kGray);
            vol.GetData(2, 0, 0, 1, 1, 0, 1, rb.data(), 0, 1);
            check(colourEq(rb[0], kOrange), "mip2 texel 0", rb[0], kOrange);
        }

        Exit();
    }

    void Draw(const GameTime&) override {}

public:
    OpenGL2Texture3DMipTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(1);
        gdm_->setPreferredBackBufferHeightProperty(1);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2Texture3DMipTest game;
    game.Run();
    return game.getResult();
}
