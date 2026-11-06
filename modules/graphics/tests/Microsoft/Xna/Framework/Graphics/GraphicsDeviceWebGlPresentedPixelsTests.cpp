// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#include "CNA/GraphicsRendererSelection.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "System/NotSupportedException.hpp"
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>
namespace {
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
// Browser canvas snapshot through drawImage, not CNA's backbuffer or WebGL readPixels.
// Run inside the same JS event turn as Present; preserveDrawingBuffer is not assumed.
std::vector<std::uint32_t> CaptureCanvas()
{
    std::vector<std::uint32_t> pixels(64*48);
    const int ok=MAIN_THREAD_EM_ASM_INT({
        const source=Module['canvas'];
        if (!source || source.width!==64 || source.height!==48) return 0;
        const snapshot=document.createElement('canvas'); snapshot.width=64; snapshot.height=48;
        const ctx=snapshot.getContext('2d');
        if (!ctx) return 0;
        ctx.drawImage(source,0,0);
        const bytes=ctx.getImageData(0,0,64,48).data;
        for(let i=0;i<64*48;++i)
            HEAPU32[($0>>>2)+i]=(bytes[4*i]<<16)|(bytes[4*i+1]<<8)|bytes[4*i+2];
        return 1;
    },pixels.data());
    if (!ok) throw std::runtime_error("browser canvas snapshot unavailable or wrong physical size");
    return pixels;
}
TEST(Gap08bWebGlPresentedPixels, SourceAndPartialDestinationReachTheBrowserCanvas)
{
    if (CNA::GraphicsRendererSelection::GetSelected()!=CNA::GraphicsRendererType::WebGL2)
        GTEST_SKIP() << "requires selected WEBGL2 in the browser, not node";
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),GraphicsProfile::HiDef,
                          PresentationParameters::CreateWindowedEXT(64,48));
    device.Clear(Color(255,0,255,255)); device.Present();
    ASSERT_EQ(CaptureCanvas(),std::vector<std::uint32_t>(64*48,0xFF00FF));
    device.Clear(Color(0,255,0,255));
    // Selected source is green; original destination changes exactly 32*24 pixels.
    device.Present(Rectangle(16,12,32,24),Rectangle(16,8,32,24),0);
    const auto rgb=CaptureCanvas();
    for(int y=0;y<48;++y) for(int x=0;x<64;++x)
        EXPECT_EQ(rgb[y*64+x],x>=16&&x<48&&y>=8&&y<32 ? 0x00FF00u : 0xFF00FFu)
            << x << ',' << y;
}
TEST(Gap08bWebGlPresentedPixels, NativeWindowOverrideIsRefusedWithoutChangingTheCanvas)
{
    if (CNA::GraphicsRendererSelection::GetSelected()!=CNA::GraphicsRendererType::WebGL2)
        GTEST_SKIP() << "requires selected WEBGL2 in the browser";
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),GraphicsProfile::HiDef,
                          PresentationParameters::CreateWindowedEXT(64,48));
    device.Clear(Color(255,0,255,255)); device.Present();
    ASSERT_EQ(CaptureCanvas(),std::vector<std::uint32_t>(64*48,0xFF00FF));
    device.Clear(Color(255,0,0,255));
    // G08a unsupported-native-platform policy: arbitrary IntPtr is not a browser canvas selector.
    EXPECT_THROW(device.Present(std::nullopt,std::nullopt,0x1234),System::NotSupportedException);
    EXPECT_EQ(CaptureCanvas(),std::vector<std::uint32_t>(64*48,0xFF00FF));
}
TEST(Gap08bWebGlPresentedPixels, SourceOnlySelectsTheWhiteQuadrantOfTheCanvasBackbuffer)
{
    if (CNA::GraphicsRendererSelection::GetSelected()!=CNA::GraphicsRendererType::WebGL2)
        GTEST_SKIP() << "requires selected WEBGL2 in the browser";
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(),GraphicsProfile::HiDef,
                          PresentationParameters::CreateWindowedEXT(64,48));
    Texture2D texture(device,2,2);
    const Color colors[]{Color(255,0,0,255),Color(0,255,0,255),
                         Color(0,0,255,255),Color(255,255,255,255)};
    texture.SetData(colors,0,4);
    SpriteBatch batch(device);
    batch.Begin(SpriteSortMode::Immediate,&BlendState::Opaque,&SamplerState::PointClamp,
                nullptr,nullptr);
    batch.Draw(texture,Rectangle(0,0,64,48),Color::White); batch.End();
    // Refresh source-presence IL_0014-0045; selected bottom-right quarter is literal W.
    device.Present(Rectangle(32,24,32,24),std::nullopt,0);
    EXPECT_EQ(CaptureCanvas(),std::vector<std::uint32_t>(64*48,0xFFFFFF));
}
} // namespace
#endif
