// SPDX-License-Identifier: MS-PL
// plans/plan_opengl2.md: canary test proving PixelTestGame::CompareGoldenImage() genuinely works end
// to end on OpenGL2 (live readback, checked-in reference PNG load via Texture2D::FromStream,
// per-pixel tolerance compare) -- reuses examples/easygl_goldenimage_smoke_test.cpp's own
// trivial solid-blue scene verbatim, tolerance=0 (exact match expected).

#include "common/PixelTestGame.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class OpenGL2GoldenImageSmokeTest : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color(0, 0, 255, 255));
        CompareGoldenImage("solid-blue-8x8-vs-easygl", Rectangle(0, 0, 8, 8),
                            "examples/golden/easygl_goldenimage_smoke_test.png",
                            /*tolerance=*/0);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<OpenGL2GoldenImageSmokeTest>();
}
