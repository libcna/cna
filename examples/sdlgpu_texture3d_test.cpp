// SPDX-License-Identifier: MS-PL
// plan_sdlgpu.md SDLGPU-40/SDLGPU-41: Texture3D + mipmaps proof for the SDL_GPU graphics backend --
// a single SDL_GPU_TEXTURETYPE_3D texture, SAMPLER usage only (never a render target), real
// SetData/GetData via a transfer-buffer copy pass. Matches the byte-exact round-trip bar D3D12
// already met (DX-122): a deliberately off-center sub-volume upload with a distinct color per Z
// slice, round-tripped through SetData()+GetData() with EXACT byte equality.
//
// Check A -- a 2x2x2 sub-volume uploaded at offset (1,1,0) within a 4x4x2 texture, with a
//   different solid color per Z slice (red at z=0, green at z=1), reads back byte-exact --
//   genuinely exercises the X/Y/Z offset math and per-slice pitch, not just a trivial (0,0,0) case.
// Check B -- a full-volume upload/readback round-trips byte-exact.
// Check C -- mipMap: a uniform-color full level-0 upload auto-generates level 1 (SDLGPU-41's
//   "generated case"), which reads back the same color (a uniform source downsamples to itself).
// Check D -- mipMap: explicit authored data written directly to level 1 (SDLGPU-41's "authored
//   mip data" case) is NOT clobbered by the level-0 auto-generation that already ran -- reads back
//   exactly what was explicitly uploaded to that level, AND level 0 itself still reads back its
//   own original color afterward -- genuinely proves multiple sequential SetData() calls on the
//   same texture accumulate (this is what caught a real bug: SDL_UploadToGPUTexture's cycle=true
//   silently orphaned an earlier partial write onto an abandoned GPU resource once a second write
//   followed it -- fixed by passing cycle=false, since Texture3D content is built up via multiple
//   independent sub-volume/per-level SetData calls, unlike a single full-texture replace).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include "CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::SdlGpu;

namespace
{
    int passCount = 0;

    void Check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        if (ok) ++passCount;
    }

    std::vector<Color> SolidColors(int count, const Color& c)
    {
        return std::vector<Color>(static_cast<std::size_t>(count), c);
    }

    bool AllExact(const std::vector<Color>& got, const Color& expected)
    {
        for (const Color& c : got)
            if (c.getRProperty() != expected.getRProperty() || c.getGProperty() != expected.getGProperty()
                || c.getBProperty() != expected.getBProperty() || c.getAProperty() != expected.getAProperty())
                return false;
        return true;
    }
}

class SdlGpuTexture3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        auto& dev = getGraphicsDeviceProperty();

        // Check A: byte-exact, off-center sub-volume round-trip with a distinct color per Z slice.
        {
            Texture3D tex(dev, 4, 4, 2, false, SurfaceFormat::Color);
            const auto redSlice = SolidColors(4, Color::Red);     // z=0, 2x2
            const auto greenSlice = SolidColors(4, Color::Green); // z=1, 2x2
            tex.SetData(0, /*left*/1, /*top*/1, /*right*/3, /*bottom*/3, /*front*/0, /*back*/1, redSlice.data(), 0, 4);
            tex.SetData(0, /*left*/1, /*top*/1, /*right*/3, /*bottom*/3, /*front*/1, /*back*/2, greenSlice.data(), 0, 4);

            std::vector<Color> gotRed(4, Color(0, 0, 0, 0));
            tex.GetData(0, 1, 1, 3, 3, 0, 1, gotRed.data(), 0, 4);
            std::vector<Color> gotGreen(4, Color(0, 0, 0, 0));
            tex.GetData(0, 1, 1, 3, 3, 1, 2, gotGreen.data(), 0, 4);

            Check(AllExact(gotRed, Color::Red), "off-center 2x2 sub-volume at z=0 reads back byte-exact red");
            Check(AllExact(gotGreen, Color::Green), "off-center 2x2 sub-volume at z=1 reads back byte-exact green");
        }

        // Check B: full-volume upload/readback round-trip.
        {
            Texture3D tex(dev, 4, 4, 2, false, SurfaceFormat::Color);
            const auto full = SolidColors(4 * 4 * 2, Color::Blue);
            tex.SetData(full.data(), static_cast<int>(full.size()));
            std::vector<Color> got(full.size(), Color(0, 0, 0, 0));
            tex.GetData(got.data(), static_cast<int>(got.size()));
            Check(AllExact(got, Color::Blue), "full-volume upload/readback round-trips byte-exact");
        }

        // Check C: mipMap "generated case" -- a full level-0 upload of a uniform color
        // auto-generates level 1, which reads back the same color.
        {
            Texture3D tex(dev, 8, 8, 2, true, SurfaceFormat::Color);
            const auto full = SolidColors(8 * 8 * 2, Color::Orange);
            tex.SetData(0, 0, 0, 8, 8, 0, 2, full.data(), 0, static_cast<int>(full.size()));

            std::vector<Color> gotMip(4 * 4 * 1, Color(0, 0, 0, 0));
            tex.GetData(1, 0, 0, 4, 4, 0, 1, gotMip.data(), 0, static_cast<int>(gotMip.size()));
            Check(AllExact(gotMip, Color::Orange), "mipMap generated case: level 1 of a uniform-color upload reads back the same color");
        }

        // Check D: mipMap "authored mip data" case -- explicit data written directly to level 1
        // is not clobbered by the level-0 auto-generation that already ran for this texture.
        {
            Texture3D tex(dev, 8, 8, 2, true, SurfaceFormat::Color);
            const auto full = SolidColors(8 * 8 * 2, Color::Orange);
            tex.SetData(0, 0, 0, 8, 8, 0, 2, full.data(), 0, static_cast<int>(full.size()));

            const auto authoredMip = SolidColors(4 * 4 * 1, Color::Magenta);
            tex.SetData(1, 0, 0, 4, 4, 0, 1, authoredMip.data(), 0, static_cast<int>(authoredMip.size()));

            std::vector<Color> gotMip(4 * 4 * 1, Color(0, 0, 0, 0));
            tex.GetData(1, 0, 0, 4, 4, 0, 1, gotMip.data(), 0, static_cast<int>(gotMip.size()));
            Check(AllExact(gotMip, Color::Magenta), "mipMap authored data: explicit level-1 SetData wins over auto-generation");

            // Genuinely proves cumulative (not cycled-away) writes: level 0 must still read back
            // its original color after the later, separate level-1 SetData call.
            std::vector<Color> gotLevel0(8 * 8 * 2, Color(0, 0, 0, 0));
            tex.GetData(0, 0, 0, 8, 8, 0, 2, gotLevel0.data(), 0, static_cast<int>(gotLevel0.size()));
            Check(AllExact(gotLevel0, Color::Orange), "level 0 remains intact after a later, separate level-1 SetData call");
        }

        std::printf("=== %d/6 PASS ===\n", passCount);
        result_ = (passCount == 6) ? 0 : 1;
        Exit();
    }

public:
    SdlGpuTexture3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        static_cast<SdlGpuGraphicsBackend&>(getGraphicsDeviceProperty().GetBackend()).SetSwapInterval(0);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuTexture3DTest game;
    game.Run();
    return game.getResult();
}
