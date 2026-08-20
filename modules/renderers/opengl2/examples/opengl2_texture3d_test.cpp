// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: proof for Texture3D (GL_TEXTURE_3D, core desktop GL since 1.2) on the native
// OpenGL 2.1 graphics renderer. Verified via SetData()/GetData() round-trips across multiple
// depth slices, since no stock effect samples a volume texture.
//
// Check A -- Texture3D construction succeeds.
// Check B -- a full-volume SetData() with a distinct solid color per depth slice reads back
//   exactly per slice via GetData() -- proves the Z dimension is real (not just W*H content
//   replicated, and not slices overwriting each other).
// Check C -- a sub-volume SetData()/GetData() round-trip (a small box strictly inside the
//   texture) reads back correctly, proving the x/y/z sub-region offset math in both directions.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::OpenGL2;

namespace
{
    bool CloseTo(int a, int b, int tol) { return std::abs(a - b) <= tol; }
    bool Matches(const Color& c, const Color& expected, int tol)
    {
        return CloseTo(c.getRProperty(), expected.getRProperty(), tol)
            && CloseTo(c.getGProperty(), expected.getGProperty(), tol)
            && CloseTo(c.getBProperty(), expected.getBProperty(), tol);
    }

    std::string ColorStr(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," + std::to_string(c.getGProperty())
             + "," + std::to_string(c.getBProperty()) + ")";
    }
}

class OpenGL2Texture3DTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    int passCount_ = 0;
    int result_ = 1;

    void Check(const std::string& label, bool ok)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        constexpr int kW = 4, kH = 4, kD = 4;

        bool threwOnConstruct = false;
        std::unique_ptr<Texture3D> tex;
        try
        {
            tex = std::make_unique<Texture3D>(dev, kW, kH, kD, false, SurfaceFormat::Color);
        }
        catch (const std::exception&) { threwOnConstruct = true; }
        Check("Texture3D construction succeeds", !threwOnConstruct);
        if (threwOnConstruct) return;

        // Check B: one distinct solid color per depth slice, full-volume SetData/GetData.
        const Color sliceColors[kD] = {
            Color(255, 0, 0, 255), Color(0, 255, 0, 255), Color(0, 0, 255, 255), Color(255, 255, 0, 255),
        };
        std::vector<Color> volume(static_cast<std::size_t>(kW) * kH * kD, Color(0, 0, 0, 255));
        for (int z = 0; z < kD; ++z)
            for (int i = 0; i < kW * kH; ++i)
                volume[static_cast<std::size_t>(z) * kW * kH + i] = sliceColors[z];
        tex->SetData(volume.data(), static_cast<int>(volume.size()));

        std::vector<Color> readBack(volume.size(), Color(0, 0, 0, 0));
        tex->GetData(readBack.data(), static_cast<int>(readBack.size()));
        bool allSlicesMatch = true;
        for (int z = 0; z < kD; ++z)
        {
            const Color got = readBack[static_cast<std::size_t>(z) * kW * kH];
            if (!Matches(got, sliceColors[z], 5))
            {
                allSlicesMatch = false;
                std::printf("  slice %d: expected %s got %s\n", z, ColorStr(sliceColors[z]).c_str(), ColorStr(got).c_str());
            }
        }
        Check("each depth slice holds its own distinct color after a full-volume round-trip", allSlicesMatch);

        // Check C: sub-volume round-trip -- a 2x2x2 box at (1,1,1) inside the 4x4x4 volume.
        const Color subColor(128, 64, 200, 255);
        std::vector<Color> subVolume(8, subColor);
        tex->SetData(0, 1, 1, 3, 3, 1, 3, subVolume.data(), 0, static_cast<int>(subVolume.size()));
        std::vector<Color> subReadBack(8, Color(0, 0, 0, 0));
        tex->GetData(0, 1, 1, 3, 3, 1, 3, subReadBack.data(), 0, static_cast<int>(subReadBack.size()));
        bool subMatches = true;
        for (const Color& c : subReadBack)
            if (!Matches(c, subColor, 5)) subMatches = false;
        Check("a sub-volume SetData()/GetData() round-trip at a non-zero (x,y,z) offset matches: got[0]=" +
              ColorStr(subReadBack[0]), subMatches);

        std::printf("=== %d/%d PASS ===\n", passCount_, 3);
        result_ = (passCount_ == 3) ? 0 : 1;
    }

    void Draw(const GameTime&) override
    {
        getGraphicsDeviceProperty().Clear(Color::Black);
        Exit();
    }

public:
    OpenGL2Texture3DTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(320);
        gdm_->setPreferredBackBufferHeightProperty(240);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    OpenGL2Texture3DTest game;
    game.Run();
    return game.getResult();
}
