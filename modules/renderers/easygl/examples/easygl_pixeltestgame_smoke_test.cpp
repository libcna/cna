// SPDX-License-Identifier: MS-PL
// Task 461/462: canary test proving the shared PixelTestGame helper genuinely works end to
// end (window/device creation, one Draw(), GetBackBufferData readback, pixel comparison, exit
// code) -- not just written and assumed. Clears to solid green and checks the centre pixel
// both with an exact match (Task 461) and a tolerance-based match (Task 462).

#include "common/PixelTestGame.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

class PixelTestGameSmokeTest : public CNA::Examples::PixelTestGame
{
protected:
    void RunTest() override
    {
        auto& device = getGraphicsDeviceProperty();
        const auto& vp = device.getViewportProperty();
        const int W = vp.getWidthProperty();
        const int H = vp.getHeightProperty();

        device.Clear(Color(0, 255, 0, 255));
        ExpectPixel("centre-exact", Rectangle(W / 2, H / 2, 1, 1), Color(0, 255, 0, 255));
        // Deliberately off by 10 on R and B -- passes only because of the tolerance=20 below;
        // proves the tolerance path is genuinely exercised, not just present but unused.
        ExpectPixel("centre-tolerance", Rectangle(W / 2, H / 2, 1, 1),
                    Color(10, 255, 10, 255), /*tolerance=*/20);
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<PixelTestGameSmokeTest>();
}
