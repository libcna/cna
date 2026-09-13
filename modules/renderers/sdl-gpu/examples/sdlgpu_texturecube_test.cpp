// SPDX-License-Identifier: MS-PL
// plans/plan_sdlgpu.md SDLGPU-51: plain, non-render-target TextureCube proof for the SDL_GPU graphics
// renderer -- a single SDL_GPU_TEXTURETYPE_CUBE texture, SAMPLER usage only (never a render
// target), real per-face SetData/GetData via a transfer-buffer copy pass. Matches the byte-exact
// round-trip bar SDLGPU-40 (Texture3D) already met, and re-checks that the cycle=true orphan-write
// bug found there (SDL_UploadToGPUTexture silently swapping in a fresh GPU resource on each call,
// abandoning earlier writes) does not reappear for per-face cube uploads.
//
// Check A -- each of the 6 faces uploaded with a distinct solid color reads back byte-exact,
//   AND face 0 is re-verified last (after all other 5 faces were written) -- genuinely proves
//   multiple sequential per-face SetData() calls accumulate onto the same resource rather than
//   the earlier faces' writes being silently orphaned.
// Check B -- mipMap: authored level 1 on one face survives a later level-0 upload to another
//   face. SDL's mip generator operates on the whole cube and must not overwrite levels authored
//   on the other five faces.
// Check C -- mipMap: explicit authored data written directly to level 1 of a face is not
//   clobbered by the level-0 auto-generation that already ran, and that face's level 0 remains
//   intact afterward too.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SdlGpu;

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

    const CubeMapFace kFaces[6] = {
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };

    const Color kFaceColors[6] = {
        Color::Red, Color::Green, Color::Blue, Color::Orange, Color::Magenta, Color::Cyan,
    };
}

class SdlGpuTextureCubeTest : public Game
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

        // Check A: each face uploaded with a distinct solid color, byte-exact round-trip;
        // face 0 re-verified last to prove earlier writes were not orphaned by later ones.
        {
            TextureCube tex(dev, 4, false, SurfaceFormat::Color);
            const int pixelsPerFace = 4 * 4;
            for (int f = 0; f < 6; ++f)
            {
                const auto solid = SolidColors(pixelsPerFace, kFaceColors[f]);
                tex.SetData(kFaces[f], solid.data(), pixelsPerFace);
            }

            bool allFacesExact = true;
            for (int f = 0; f < 6; ++f)
            {
                std::vector<Color> got(pixelsPerFace, Color(0, 0, 0, 0));
                tex.GetData(kFaces[f], got.data(), pixelsPerFace);
                if (!AllExact(got, kFaceColors[f])) allFacesExact = false;
            }
            Check(allFacesExact, "each of the 6 faces reads back its own byte-exact solid color");

            std::vector<Color> gotFace0Again(pixelsPerFace, Color(0, 0, 0, 0));
            tex.GetData(kFaces[0], gotFace0Again.data(), pixelsPerFace);
            Check(AllExact(gotFace0Again, kFaceColors[0]),
                  "face 0 remains intact after all 5 other faces were written afterward");
        }

        // Check B: authored +X mip 1 must survive a later full -X level-0 upload. Before
        // SDLGPU-70 every full level-0 write regenerated the WHOLE cube chain, so the second write
        // replaced +X's authored mip from its own level 0.
        {
            TextureCube tex(dev, 8, true, SurfaceFormat::Color);
            const auto plusX = SolidColors(8 * 8, Color::Red);
            tex.SetData(CubeMapFace::PositiveX, 0, nullptr, plusX.data(), 0,
                        static_cast<int>(plusX.size()));
            const auto authored = SolidColors(4 * 4, Color::White);
            tex.SetData(CubeMapFace::PositiveX, 1, nullptr, authored.data(), 0,
                        static_cast<int>(authored.size()));
            const auto minusX = SolidColors(8 * 8, Color::Green);
            tex.SetData(CubeMapFace::NegativeX, 0, nullptr, minusX.data(), 0,
                        static_cast<int>(minusX.size()));

            std::vector<Color> gotMip(4 * 4, Color(0, 0, 0, 0));
            tex.GetData(CubeMapFace::PositiveX, 1, nullptr, gotMip.data(), 0, static_cast<int>(gotMip.size()));
            Check(AllExact(gotMip, Color::White),
                  "authored +X mip survives a later full -X level-0 upload");
        }

        // Check C: mipMap "authored mip data" case -- explicit data written directly to level 1
        // of a face stays isolated from its level 0, and level 0 remains intact after this later,
        // separate SetData call.
        {
            TextureCube tex(dev, 8, true, SurfaceFormat::Color);
            const auto full = SolidColors(8 * 8, Color::Orange);
            tex.SetData(CubeMapFace::PositiveY, 0, nullptr, full.data(), 0, static_cast<int>(full.size()));

            const auto authoredMip = SolidColors(4 * 4, Color::Magenta);
            tex.SetData(CubeMapFace::PositiveY, 1, nullptr, authoredMip.data(), 0, static_cast<int>(authoredMip.size()));

            std::vector<Color> gotMip(4 * 4, Color(0, 0, 0, 0));
            tex.GetData(CubeMapFace::PositiveY, 1, nullptr, gotMip.data(), 0, static_cast<int>(gotMip.size()));
            Check(AllExact(gotMip, Color::Magenta),
                  "mipMap authored data: explicit level-1 SetData remains exact");

            std::vector<Color> gotLevel0(8 * 8, Color(0, 0, 0, 0));
            tex.GetData(CubeMapFace::PositiveY, 0, nullptr, gotLevel0.data(), 0, static_cast<int>(gotLevel0.size()));
            Check(AllExact(gotLevel0, Color::Orange), "face level 0 remains intact after a later, separate level-1 SetData call");
        }

        std::printf("=== %d/5 PASS ===\n", passCount);
        result_ = (passCount == 5) ? 0 : 1;
        Exit();
    }

public:
    SdlGpuTextureCubeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        // GraphicsDeviceManager.SynchronizeWithVerticalRetrace defaults to true (the XNA
        // default); this test's virtual/headless display has no real vblank signal, so leaving
        // VSync on makes every frame wait roughly a second, blowing past this test's frame
        // budget. Disabling it here -- before Game::DoInitialize()'s CreateDevice() call reads
        // it -- is the correct, property-level way to request Immediate presentation.
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuTextureCubeTest game;
    game.Run();
    return game.getResult();
}
