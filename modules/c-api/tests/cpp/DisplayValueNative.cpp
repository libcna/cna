// SPDX-License-Identifier: MS-PL
#include "CNA/C/runtime_graphics_manager.h"
#include "Microsoft/Xna/Framework/Graphics/DisplayMode.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceInformation.hpp"
#include <cstring>
#include <thread>
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
extern "C" uint64_t cna_test_display_native(const CNA_DisplayMode *value, int32_t *hash, CNA_Rectangle *area,
                                            char *text)
{
    DisplayMode native(value->width, value->height, static_cast<SurfaceFormat>(value->format));
    *hash = native.GetHashCode();
    auto bounds = native.getTitleSafeAreaProperty();
    *area = {bounds.X, bounds.Y, bounds.Width, bounds.Height};
    auto str = native.ToString();
    std::memcpy(text, str.data(), str.size());
    return str.size();
}
static GraphicsDeviceInformation native_info(const CNA_GraphicsDeviceInformation &value, uint64_t window)
{
    GraphicsDeviceInformation native;
    native.setGraphicsProfileProperty(static_cast<GraphicsProfile>(value.graphics_profile));
    const auto &p = value.presentation_parameters;
    auto &n = native.getPresentationParametersProperty();
    n.setBackBufferWidthProperty(p.back_buffer_width);
    n.setBackBufferHeightProperty(p.back_buffer_height);
    n.setBackBufferFormatProperty(static_cast<SurfaceFormat>(p.back_buffer_format));
    n.setDepthStencilFormatProperty(static_cast<DepthFormat>(p.depth_stencil_format));
    n.setMultiSampleCountProperty(p.multi_sample_count);
    n.setDisplayOrientationProperty(static_cast<DisplayOrientation>(p.display_orientation));
    n.setPresentationIntervalProperty(static_cast<PresentInterval>(p.presentation_interval));
    n.setRenderTargetUsageProperty(static_cast<RenderTargetUsage>(p.render_target_usage));
    n.setDeviceWindowHandleProperty(static_cast<std::uintptr_t>(window));
    n.setIsFullScreenProperty(p.is_full_screen != 0);
    n.setHeadlessEXTProperty(p.headless_ext != 0);
    return native;
}
extern "C" int32_t cna_test_information_hash(const CNA_GraphicsDeviceInformation *value, uint64_t window)
{
    uint32_t adapter = value->adapter_index < 0 ? 0U : static_cast<uint32_t>(value->adapter_index) + 1U;
    return static_cast<int32_t>(static_cast<uint32_t>(native_info(*value, window).GetHashCode()) ^ adapter);
}
extern "C" CNA_Bool cna_test_information_equal(const CNA_GraphicsDeviceInformation *left, uint64_t lw,
                                               const CNA_GraphicsDeviceInformation *right, uint64_t rw)
{
    auto a = native_info(*left, lw), b = native_info(*right, rw);
    bool adapter =
        left->adapter_index == right->adapter_index || (left->adapter_index < 0 && right->adapter_index < 0);
    return adapter && a.Equals(&b) ? CNA_TRUE : CNA_FALSE;
}
extern "C" int cna_test_display_worker(int (*test)(void))
{
    int result = 0;
    std::thread worker([&] { result = test(); });
    worker.join();
    return result;
}
