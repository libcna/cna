// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#if defined(CNA_RENDERER_DIRECTX9) || defined(CNA_RENDERER_PRESENT_DIRECTX9)
#include "CNA/Internal/Renderers/DirectX9/DirectX9Renderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
namespace {
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::DirectX9;
using Microsoft::WRL::ComPtr;
class Gap08bDirectX9PresentRegion : public testing::Test
{
protected:
    void SetUp() override
    {
        device=std::make_unique<GraphicsDevice>(GraphicsAdapter::getDefaultAdapterProperty(),
            GraphicsProfile::HiDef,PresentationParameters::CreateWindowedEXT(64,48));
        renderer=dynamic_cast<DirectX9Renderer*>(&device->GetRenderer());
        if (!renderer) GTEST_SKIP() << "requires selected DIRECTX9";
    }
    IGraphicsRenderer::RendererPresentRegionEXT Request()
    {
        IGraphicsRenderer::RendererPresentRegionEXT r;
        r.hasSourceRectangle=true; r.sourceX=8; r.sourceY=6; r.sourceWidth=16; r.sourceHeight=12;
        r.hasDestinationRectangle=true; r.destinationX=16; r.destinationY=8;
        r.destinationWidth=32; r.destinationHeight=24; r.targetWidth=64; r.targetHeight=48;
        return r;
    }
    std::unique_ptr<GraphicsDevice> device;
    DirectX9Renderer* renderer=nullptr;
};
TEST_F(Gap08bDirectX9PresentRegion, NativeViewportScissorAndTargetSurviveRegionPresentation)
{
    auto* native=renderer->GetDeviceEXT();
    D3DVIEWPORT9 viewport{2,3,8,6,0.25f,0.75f},after{};
    RECT scissor{1,2,5,7},clip{};
    ASSERT_TRUE(SUCCEEDED(native->SetViewport(&viewport)));
    ASSERT_TRUE(SUCCEEDED(native->SetScissorRect(&scissor)));
    ASSERT_TRUE(SUCCEEDED(native->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE)));
    ASSERT_TRUE(SUCCEEDED(native->SetRenderState(D3DRS_COLORWRITEENABLE,D3DCOLORWRITEENABLE_RED)));
    ComPtr<IDirect3DSurface9> before,target;
    ASSERT_TRUE(SUCCEEDED(native->GetRenderTarget(0,before.GetAddressOf())));
    ASSERT_TRUE(renderer->PresentRegionEXT(Request()));
    // Refresh Present core IL_0086-0098 delegates without modifying drawing state.
    ASSERT_TRUE(SUCCEEDED(native->GetViewport(&after)));
    EXPECT_EQ(after.X,2u); EXPECT_EQ(after.Y,3u); EXPECT_EQ(after.Width,8u); EXPECT_EQ(after.Height,6u);
    EXPECT_EQ(after.MinZ,0.25f); EXPECT_EQ(after.MaxZ,0.75f);
    ASSERT_TRUE(SUCCEEDED(native->GetScissorRect(&clip)));
    EXPECT_EQ(clip.left,1); EXPECT_EQ(clip.top,2); EXPECT_EQ(clip.right,5); EXPECT_EQ(clip.bottom,7);
    DWORD enabled=0,mask=0;
    ASSERT_TRUE(SUCCEEDED(native->GetRenderState(D3DRS_SCISSORTESTENABLE,&enabled)));
    ASSERT_TRUE(SUCCEEDED(native->GetRenderState(D3DRS_COLORWRITEENABLE,&mask)));
    EXPECT_EQ(enabled,TRUE); EXPECT_EQ(mask,DWORD(D3DCOLORWRITEENABLE_RED));
    ASSERT_TRUE(SUCCEEDED(native->GetRenderTarget(0,target.GetAddressOf())));
    EXPECT_EQ(target.Get(),before.Get());
}
TEST_F(Gap08bDirectX9PresentRegion, PresentationResourcesDoNotPreventResetAndTheNextRegion)
{
    ASSERT_TRUE(renderer->PresentRegionEXT(Request()));
    // D3D9 Reset requires all additional chains/default-pool surfaces released first.
    // A fresh Clear/Present after reset proves real device usability, not just no exception.
    renderer->DebugSimulateContextLoss(); renderer->DebugRestoreContext();
    ASSERT_EQ(renderer->GetDeviceEXT()->TestCooperativeLevel(),D3D_OK);
    renderer->Clear(0,1,0,1);
    ASSERT_TRUE(renderer->PresentRegionEXT(Request()));
    EXPECT_EQ(renderer->GetDeviceEXT()->TestCooperativeLevel(),D3D_OK);
}
} // namespace
#endif
