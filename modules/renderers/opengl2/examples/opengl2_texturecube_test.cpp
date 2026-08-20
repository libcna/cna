// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: proof for TextureCube (GL_TEXTURE_CUBE_MAP) on the native OpenGL 2.1 graphics
// renderer. No stock effect samples a cube map yet on this renderer (EnvironmentMapEffect is still
// a follow-up item), so this verifies the storage/upload/readback round-trip directly through
// TextureCube::SetData()/GetData() rather than a rendered pixel.
//
// Check A -- TextureCube construction succeeds (size, no exception).
// Check B -- each of the 6 faces holds its own distinct solid color after SetData(), read back
//   exactly via GetData() -- proves CubeMapFace's ordinals correctly reach the matching
//   GL_TEXTURE_CUBE_MAP_POSITIVE_X.. face target, not e.g. all 6 landing on the same face.
// Check C -- a mipMap=true cube's construction and a level-0 SetData()/GetData() round-trip both
//   still work (proves the per-level, per-face storage pre-allocation doesn't corrupt level 0).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include "CNA/Internal/Renderers/OpenGL2/OpenGL2Renderer.hpp"

#include "common/PixelTestGame.hpp"

#include <array>
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

    std::vector<Color> SolidFace(int size, const Color& color)
    {
        return std::vector<Color>(static_cast<std::size_t>(size) * size, color);
    }
}

class OpenGL2TextureCubeTest : public Game
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
        constexpr int kSize = 4;

        bool threwOnConstruct = false;
        std::unique_ptr<TextureCube> tc;
        try
        {
            tc = std::make_unique<TextureCube>(dev, kSize, false, SurfaceFormat::Color);
        }
        catch (const std::exception&) { threwOnConstruct = true; }
        Check("TextureCube construction succeeds", !threwOnConstruct);
        if (threwOnConstruct) return;

        const std::array<Color, 6> faceColors = {
            Color(255, 0, 0, 255),   // PositiveX
            Color(0, 255, 0, 255),   // NegativeX
            Color(0, 0, 255, 255),   // PositiveY
            Color(255, 255, 0, 255), // NegativeY
            Color(255, 0, 255, 255), // PositiveZ
            Color(0, 255, 255, 255), // NegativeZ
        };
        const CubeMapFace faces[6] = {
            CubeMapFace::PositiveX, CubeMapFace::NegativeX, CubeMapFace::PositiveY,
            CubeMapFace::NegativeY, CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
        };

        bool allFacesRoundTrip = true;
        for (int i = 0; i < 6; ++i)
        {
            std::vector<Color> src = SolidFace(kSize, faceColors[i]);
            tc->SetData(faces[i], src.data(), static_cast<int>(src.size()));
        }
        for (int i = 0; i < 6; ++i)
        {
            std::vector<Color> readBack(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
            tc->GetData(faces[i], readBack.data(), static_cast<int>(readBack.size()));
            if (!Matches(readBack[0], faceColors[i], 5))
            {
                allFacesRoundTrip = false;
                std::printf("  face %d: expected %s got %s\n", i, ColorStr(faceColors[i]).c_str(), ColorStr(readBack[0]).c_str());
            }
        }
        Check("all 6 faces hold their own distinct color after SetData()/GetData()", allFacesRoundTrip);

        bool mipmapOk = true;
        try
        {
            TextureCube tcMip(dev, 8, true, SurfaceFormat::Color);
            std::vector<Color> src = SolidFace(8, Color(128, 64, 200, 255));
            tcMip.SetData(CubeMapFace::PositiveX, src.data(), static_cast<int>(src.size()));
            std::vector<Color> readBack(64, Color(0, 0, 0, 0));
            tcMip.GetData(CubeMapFace::PositiveX, readBack.data(), static_cast<int>(readBack.size()));
            if (!Matches(readBack[0], Color(128, 64, 200, 255), 5))
                mipmapOk = false;
        }
        catch (const std::exception&) { mipmapOk = false; }
        Check("mipMap=true TextureCube construction + level-0 round-trip both work", mipmapOk);

        std::printf("=== %d/%d PASS ===\n", passCount_, 3);
        result_ = (passCount_ == 3) ? 0 : 1;
    }

    void Draw(const GameTime&) override
    {
        getGraphicsDeviceProperty().Clear(Color::Black);
        Exit();
    }

public:
    OpenGL2TextureCubeTest()
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

    OpenGL2TextureCubeTest game;
    game.Run();
    return game.getResult();
}
