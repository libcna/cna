// SPDX-License-Identifier: MS-PL
#include "GraphicsDeviceGpuPresentFixture.hpp"
namespace CNA::Testing::Gap08c {
#if defined(CNA_GAP08_X11_CAPTURE)
TEST_F(GpuPresentPixels, NativeOverrideSelectsTheOtherClientAndItsPhysicalBounds)
{
    CreateOther(); ASSERT_FALSE(HasFatalFailure());
    SeedOther(); ASSERT_FALSE(HasFatalFailure());
    DrawQuadrants();
    // x=18 exceeds own width 16 but lies in other width 24; source top-right is green.
    device->Present(Rectangle(4,0,4,4),Rectangle(18,2,4,4),foreign->NativeToken());
    EXPECT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) {
        if(!Outside(rgb,24,20,18,2,4,4,0x2468AC)) return false;
        for(int y=2;y<6;++y) for(int x=18;x<22;++x) if(rgb[y*24+x]!=0x00FF00) return false;
        return true;
    }));
    EXPECT_EQ(front->Capture(),std::vector<std::uint32_t>(256,0xFF00FF));
}
TEST_F(GpuPresentPixels, NullRectanglesFillTheWholeForeignClient)
{
    CreateOther(); ASSERT_FALSE(HasFatalFailure());
    device->Clear(Color(255,0,0,255));
    device->Present(std::nullopt,std::nullopt,foreign->NativeToken());
    EXPECT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) { return Solid(rgb,24,20,0xFF0000); }));
    EXPECT_EQ(front->Capture(),std::vector<std::uint32_t>(256,0xFF00FF));
}
TEST_F(GpuPresentPixels, AlternatingOwnAndOverrideKeepsIndependentHistories)
{
    CreateOther(); ASSERT_FALSE(HasFatalFailure());
    SeedOther(); ASSERT_FALSE(HasFatalFailure());
    device->Clear(Color(255,0,0,255));
    device->Present(std::nullopt,Rectangle(4,4,8,8),0);
    ASSERT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,16,16,4,4,8,8,0xFF00FF) && rgb[6*16+6]==0xFF0000;
    }));
    device->Clear(Color(0,255,0,255));
    device->Present(std::nullopt,Rectangle(18,2,4,4),foreign->NativeToken());
    ASSERT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,24,20,18,2,4,4,0x2468AC) && rgb[3*24+19]==0x00FF00;
    }));
    device->Clear(Color(0,0,255,255));
    device->Present(std::nullopt,Rectangle(0,0,4,4),0);
    EXPECT_TRUE(front->WaitFor(*platform,[](const auto& rgb) {
        for(int y=0;y<16;++y) for(int x=0;x<16;++x)
        {
            const auto expected=(x<4 && y<4)?0x0000FF:
                ((x>=4 && x<12 && y>=4 && y<12)?0xFF0000:0xFF00FF);
            if(rgb[y*16+x]!=expected) return false;
        }
        return true;
    }));
    EXPECT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,24,20,18,2,4,4,0x2468AC) && rgb[3*24+19]==0x00FF00;
    }));
}
TEST_F(GpuPresentPixels, OverrideResizeUsesFreshClientDimensions)
{
    CreateOther(); ASSERT_FALSE(HasFatalFailure());
    SeedOther(); ASSERT_FALSE(HasFatalFailure());
    foreign.reset(); other->SetSize(28,24); other->Sync();
    ASSERT_EQ(other->GetPixelSize().width,28); ASSERT_EQ(other->GetPixelSize().height,24);
    foreign=std::make_unique<PresentedWindow>(other->GetNativeHandle(),28,24);
    // Full seed after resize avoids inventing a resize-retention oracle.
    device->Clear(Color(36,104,172,255));
    device->Present(std::nullopt,std::nullopt,foreign->NativeToken());
    ASSERT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) { return Solid(rgb,28,24,0x2468AC); }));
    device->Clear(Color(255,255,255,255));
    device->Present(std::nullopt,Rectangle(24,20,4,4),foreign->NativeToken());
    EXPECT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,28,24,24,20,4,4,0x2468AC) && rgb[21*28+25]==0xFFFFFF;
    }));
    EXPECT_EQ(front->Capture(),std::vector<std::uint32_t>(256,0xFF00FF));
}
TEST_F(GpuPresentPixels, ExternalFirstPartialPresentRetainsExistingNativePixels)
{
    // This host is NOT created/registered by IPlatform. Its XID must be adopted.
    // Strict requested contract: no whole-window seed is allowed to hide missing initial history.
    foreign=std::make_unique<PresentedWindow>(own->GetNativeHandle(),24,20,true);
    foreign->Fill(0x2468AC);
    ASSERT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) { return Solid(rgb,24,20,0x2468AC); }));
    DrawQuadrants();
    device->Present(Rectangle(4,0,4,4),Rectangle(18,2,4,4),foreign->NativeToken());
    EXPECT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) {
        return Outside(rgb,24,20,18,2,4,4,0x2468AC) && rgb[3*24+19]==0x00FF00;
    }));
    EXPECT_EQ(front->Capture(),std::vector<std::uint32_t>(256,0xFF00FF));
    // Owner history+readback decision: native dirty updates preserve this unseeded host.
}
TEST_F(GpuPresentPixels, RendererDestructionReleasesOverrideResourcesButNeverTheHost)
{
    foreign=std::make_unique<PresentedWindow>(own->GetNativeHandle(),24,20,true);
    SeedOther(); ASSERT_FALSE(HasFatalFailure());
    device.reset();
    ASSERT_TRUE(foreign->IsAlive());
    foreign->Fill(0x13579B);
    ASSERT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) { return Solid(rgb,24,20,0x13579B); }));
    // A replacement renderer must be able to bind a native presenter to this same borrowed host.
    MakeDevice();
    ASSERT_EQ(device->GetGraphicsRendererType(),selected);
    device->Clear(Color(0,0,255,255));
    device->Present(std::nullopt,std::nullopt,foreign->NativeToken());
    EXPECT_TRUE(foreign->WaitFor(*platform,[](const auto& rgb) { return Solid(rgb,24,20,0x0000FF); }));
    device.reset(); EXPECT_TRUE(foreign->IsAlive());
}
#endif
} // namespace CNA::Testing::Gap08c
