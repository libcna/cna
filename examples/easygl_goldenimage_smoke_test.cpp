// SPDX-License-Identifier: MS-PL
// Task 463: canary test proving PixelTestGame::CompareGoldenImage() genuinely works end to
// end (live readback, checked-in reference PNG load via Texture2D::FromStream, per-pixel
// tolerance compare) -- not just written and assumed. Clears to solid blue and compares an
// 8x8 region against examples/golden/easygl_goldenimage_smoke_test.png.

#include "common/PixelTestGame.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class GoldenImageSmokeTest : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        device.Clear(Color(0, 0, 255, 255));
        CompareGoldenImage("solid-blue-8x8", Rectangle(0, 0, 8, 8),
                            "examples/golden/easygl_goldenimage_smoke_test.png",
                            /*tolerance=*/0);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<GoldenImageSmokeTest>();
}
