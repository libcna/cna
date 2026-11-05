// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <typeinfo>
#include <vector>
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    PresentationParameters Parameters(int width = 8, int height = 6)
    {
        auto p = PresentationParameters::CreateWindowedEXT(800, 480);
        p.setBackBufferWidthProperty(width);
        p.setBackBufferHeightProperty(height);
        p.setBackBufferFormatProperty(SurfaceFormat::Color);
        p.setMultiSampleCountProperty(0);
        return p;
    }
    template<class E, class F>
    void Argument(const char* parameter, const char* body, F action)
    {
        try { action(); ADD_FAILURE() << "accepted invalid call"; }
        catch (const E& e)
        {
            EXPECT_EQ(typeid(e), typeid(E));
            EXPECT_EQ(e.getParamNameProperty(), parameter);
            std::string text = e.getMessageProperty();
            auto end = text.find(" (Parameter ");
            if (end == std::string::npos) end = text.find("\nParameter name:");
            EXPECT_EQ(text.substr(0, end), body);
        }
    }
    template<class E, class F>
    void Refusal(const char* body, F action)
    {
        try { action(); ADD_FAILURE() << "accepted invalid call"; }
        catch (const E& e)
        {
            EXPECT_EQ(typeid(e), typeid(E));
            EXPECT_EQ(std::string(e.what()), body);
        }
    }
}

TEST(GraphicsDevicePresentationCopyRefreshTest, ConstructionAndResetCopyTheSourceButGetterRemainsMutable)
{
    auto source = Parameters();
    source.setPresentationIntervalProperty(PresentInterval::Immediate);
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, source);
    source.setBackBufferWidthProperty(12);
    source.setPresentationIntervalProperty(PresentInterval::Two);
    // Refresh constructor uses native-to-managed caches; Reset IL_0274-0287 clones twice.
    EXPECT_EQ(device.getPresentationParametersProperty().getBackBufferWidthProperty(), 8);
    EXPECT_EQ(device.getPresentationParametersProperty().getPresentationIntervalProperty(), PresentInterval::Immediate);
    device.Reset(source, GraphicsAdapter::getDefaultAdapterProperty());
    source.setBackBufferWidthProperty(16);
    EXPECT_EQ(device.getPresentationParametersProperty().getBackBufferWidthProperty(), 12);
    EXPECT_EQ(device.getPresentationParametersProperty().getPresentationIntervalProperty(), PresentInterval::Two);
    auto& visible = device.getPresentationParametersProperty();
    visible.setBackBufferWidthProperty(20);
    const GraphicsDevice& readOnly = device;
    EXPECT_EQ(&visible, &readOnly.getPresentationParametersProperty());
    EXPECT_EQ(readOnly.getPresentationParametersProperty().getBackBufferWidthProperty(), 20);
    // Public cached changes do not alter the actual viewport.
    EXPECT_EQ(device.getViewportProperty().getWidthProperty(), 12);
}

TEST(GraphicsDevicePresentationCopyRefreshTest, ParameterlessResetIgnoresPublicEditsButExplicitResetAppliesThem)
{
    GraphicsDevice device(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, Parameters());
    auto& visible = device.getPresentationParametersProperty();
    visible.setBackBufferWidthProperty(12);
    visible.setBackBufferHeightProperty(10);
    // Refresh Reset() IL_0000-0012 uses pInternalCachedParams, not get_PresentationParameters.
    device.Reset();
    EXPECT_EQ(device.getPresentationParametersProperty().getBackBufferWidthProperty(), 8);
    EXPECT_EQ(device.getPresentationParametersProperty().getBackBufferHeightProperty(), 6);
    EXPECT_EQ(device.getViewportProperty().getWidthProperty(), 8);
    device.getPresentationParametersProperty().setBackBufferWidthProperty(12);
    device.getPresentationParametersProperty().setBackBufferHeightProperty(10);
    device.Reset(device.getPresentationParametersProperty());
    EXPECT_EQ(device.getViewportProperty().getWidthProperty(), 12);
    EXPECT_EQ(device.getViewportProperty().getHeightProperty(), 10);
    // The new applied snapshot survives a later public edit too.
    device.getPresentationParametersProperty().setBackBufferWidthProperty(16);
    device.Reset();
    EXPECT_EQ(device.getPresentationParametersProperty().getBackBufferWidthProperty(), 12);
    EXPECT_EQ(device.getViewportProperty().getWidthProperty(), 12);
}
