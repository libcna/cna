// SPDX-License-Identifier: MS-PL
// Task 461: canary test proving the new shared PixelTestGame helper genuinely works end to
// end (window/device creation, one Draw(), GetBackBufferData readback, pixel comparison, exit
// code) -- not just written and assumed. Clears to solid green and checks the centre pixel.

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
        ExpectPixel("centre", Rectangle(W / 2, H / 2, 1, 1), Color(0, 255, 0, 255));
    }
};

int main()
{
    return CNA::Examples::RunPixelTest<PixelTestGameSmokeTest>();
}
