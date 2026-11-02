// SPDX-License-Identifier: MS-PL
#include "CNA/C/display.h"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include <thread>
using namespace Microsoft::Xna::Framework::Graphics;
static_assert(static_cast<unsigned>(GraphicsDeviceTypeEXT::Hardware) ==
              CNA_GRAPHICS_DEVICE_TYPE_HARDWARE_EXT);
static_assert(static_cast<unsigned>(GraphicsDeviceTypeEXT::Reference) ==
              CNA_GRAPHICS_DEVICE_TYPE_REFERENCE_EXT);
static_assert(static_cast<unsigned>(GraphicsDeviceTypeEXT::Null) == CNA_GRAPHICS_DEVICE_TYPE_NULL_EXT);
extern "C" CNA_Bool cna_test_adapter_wide(float aspect)
{
    return GraphicsAdapter::IsWideScreenAspectRatioEXT(aspect) ? CNA_TRUE : CNA_FALSE;
}
extern "C" void cna_test_adapter_preferences(CNA_Bool nullDevice, CNA_Bool referenceDevice, uint32_t *type,
                                             uint32_t *renderer)
{
    auto &adapter = GraphicsAdapter::getDefaultAdapterProperty();
    adapter.setUseNullDeviceProperty(nullDevice != CNA_FALSE);
    adapter.setUseReferenceDeviceProperty(referenceDevice != CNA_FALSE);
    *type = static_cast<uint32_t>(GraphicsAdapter::GetCurrentDeviceTypeEXT());
    auto native = GraphicsAdapter::GetRequiredRendererEXT();
    *renderer = !native                                          ? CNA_GRAPHICS_RENDERER_UNKNOWN
                : *native == CNA::GraphicsRendererType::Headless ? CNA_GRAPHICS_RENDERER_HEADLESS
                                                                 : CNA_GRAPHICS_RENDERER_SOFTWARE;
}
extern "C" int cna_test_adapter_worker(int (*test)(void))
{
    int result = 0;
    std::thread worker([&] { result = test(); });
    worker.join();
    return result;
}
extern "C" CNA_Bool cna_test_adapter_logical(int32_t *width, int32_t *height)
{
    PresentationParameters pp;
    pp.setIsFullScreenProperty(false);
    pp.setBackBufferWidthProperty(320);
    pp.setBackBufferHeightProperty(240);
    auto &adapter = GraphicsAdapter::getDefaultAdapterProperty();
    GraphicsDevice device(adapter, GraphicsProfile::Reach, pp);
    int w = 0, h = 0;
    bool ok = device.GetLogicalSizeEXT(w, h);
    *width = w;
    *height = h;
    return ok ? CNA_TRUE : CNA_FALSE;
}
