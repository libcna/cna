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

TEST(GraphicsDeviceResetArgumentRefreshTest, NullExtensionAdapterRetainsIdentityAndDisposedDeviceRejectsEveryOverload)
{
    GraphicsDevice d(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, Parameters());
    GraphicsAdapter* const adapter = &d.getAdapterProperty();
    // CNAEXT Reset pointer overload documents nullptr as retain-current. XNA's nullable managed
    // argument is refused at Reset IL_002a-003c, but CNA's XNA overload is a non-null C++ reference.
    d.Reset(Parameters(12,10), static_cast<GraphicsAdapter*>(nullptr));
    EXPECT_EQ(&d.getAdapterProperty(), adapter);
    EXPECT_EQ(d.getPresentationParametersProperty().getBackBufferWidthProperty(), 12);
    EXPECT_EQ(d.getViewportProperty().getHeightProperty(), 10);
    d.Dispose();
    // Refresh Reset core IL_0000-0010: disposal is the first refusal on every mapped overload.
    EXPECT_THROW(d.Reset(), System::ObjectDisposedException);
    EXPECT_THROW(d.Reset(Parameters()), System::ObjectDisposedException);
    EXPECT_THROW(d.Reset(Parameters(), *adapter), System::ObjectDisposedException);
    EXPECT_THROW(d.Reset(Parameters(), static_cast<GraphicsAdapter*>(nullptr)), System::ObjectDisposedException);
}
