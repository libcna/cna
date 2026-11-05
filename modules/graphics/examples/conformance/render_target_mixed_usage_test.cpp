// SPDX-License-Identifier: MS-PL
#include "RenderTargetMixedUsageFixture.hpp"
#include "ConformanceSupport.hpp"
#include "common/PixelTestGame.hpp"
#include "CNA/GraphicsCapability.hpp"
#include <cstdio>
#include <memory>

class MixedUsageGame final : public Microsoft::Xna::Framework::Game
{
    std::unique_ptr<Microsoft::Xna::Framework::GraphicsDeviceManager> manager_;
    bool done_=false;
    int result_=0;
protected:
    void Draw(const Microsoft::Xna::Framework::GameTime&) override
    {
        if(done_) return;
        done_=true;
        auto& device=getGraphicsDeviceProperty();
        if(!device.SupportsCapability(CNA::GraphicsCapability::MultipleRenderTargets))
        { result_=77; std::puts("[SKIP] renderer does not support MRT"); Exit(); return; }
        auto check=[&](bool ok,const std::string& label) {
            if(!ok) { std::printf("[FAIL] %s\n",label.c_str()); result_=1; }
        };
        CNA::Tests::MixedUsage::FourSlots(device,check);
        CNA::Tests::MixedUsage::CubeFaces(device,check);
        CNA::Tests::MixedUsage::PublicClearAndEqualBinding(device,check);
        CNA::Tests::MixedUsage::DepthStencilOwnership(device,check);
        CNA::Tests::MixedUsage::CubeDepthStencilOwnership(device,check);
        CNA::Tests::MixedUsage::MaskedState(device,check);
        // Optional MSAA leg: qualification requires the REAL applied count, not a raw request.
        using namespace Microsoft::Xna::Framework::Graphics;
        RenderTarget2D probe(device,4,4,false,SurfaceFormat::Color,DepthFormat::None,4,
                             RenderTargetUsage::PreserveContents);
        RenderTargetCube cubeProbe(device,4,false,SurfaceFormat::Color,DepthFormat::None,4,
                                   RenderTargetUsage::PreserveContents);
        if(probe.getMultiSampleCountProperty()==4 && cubeProbe.getMultiSampleCountProperty()==4)
        {
            CNA::Tests::MixedUsage::FourSlots(device,check,4);
            CNA::Tests::MixedUsage::CubeFaces(device,check,4);
        }
        else std::puts("[SKIP] MSAA leg requires both target kinds to apply four samples");
        if(result_==0) std::puts("[PASS] mixed MRT usage, cube faces, depth/stencil and public clear");
        Exit();
    }
public:
    MixedUsageGame()
    {
        manager_=std::make_unique<Microsoft::Xna::Framework::GraphicsDeviceManager>(this);
        manager_->setGraphicsProfileProperty(Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef);
        manager_->setPreferredBackBufferWidthProperty(64);
        manager_->setPreferredBackBufferHeightProperty(64);
    }
    int Result() const { return result_; }
};
int main()
{
    if(!CNA::Examples::Conformance::ProbePixelOutputAvailable("render_target_mixed_usage")) return 77;
    if(!CNA::Examples::ProbeGpuDisplayAvailable()) return 77;
    try { MixedUsageGame game; game.Run(); return game.Result(); }
    catch(const std::exception& e) { std::fprintf(stderr,"[FAIL] %s\n",e.what()); return 1; }
}
