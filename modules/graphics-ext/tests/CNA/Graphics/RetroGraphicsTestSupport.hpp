// SPDX-License-Identifier: MS-PL
#pragma once
#ifdef CNA_CNAEXT
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
namespace CnaTest::RetroGraphics {
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
class HiDefDevice : public Microsoft::Xna::Framework::Graphics::GraphicsDevice {
public:
    HiDefDevice() : GraphicsDevice(
        Microsoft::Xna::Framework::Graphics::GraphicsAdapter::getDefaultAdapterProperty(),
        Microsoft::Xna::Framework::Graphics::GraphicsProfile::HiDef,
        Microsoft::Xna::Framework::Graphics::PresentationParameters()) {}
};
inline bool CanBindRenderTargets(GraphicsDevice& device) {
    try {
        Microsoft::Xna::Framework::Graphics::RenderTarget2D target(device, 1, 1);
        device.SetRenderTarget(&target); device.SetRenderTarget(nullptr); return true;
    } catch (...) { try { device.SetRenderTarget(nullptr); } catch (...) {} return false; }
}
inline bool CanReadRenderTargets(GraphicsDevice& device) {
    try {
        Microsoft::Xna::Framework::Graphics::RenderTarget2D target(device, 1, 1);
        device.SetRenderTarget(&target); device.Clear(Microsoft::Xna::Framework::Color::Black);
        device.SetRenderTarget(nullptr);
        auto pixel = Microsoft::Xna::Framework::Color::White;
        target.GetData(&pixel, 1); return true;
    } catch (...) { return false; }
}
}
#define CNA_SKIP_WITHOUT_RENDER_TARGETS(device) do { \
    if (!::CnaTest::RetroGraphics::CanBindRenderTargets(device)) \
        GTEST_SKIP() << "renderer cannot bind render targets"; \
} while (false)
#define CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(device) do { \
    if (!::CnaTest::RetroGraphics::CanReadRenderTargets(device)) \
        GTEST_SKIP() << "renderer cannot read render targets"; \
} while (false)
#endif // CNA_CNAEXT
