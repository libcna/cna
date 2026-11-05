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

TEST(GraphicsDeviceResetOrientationRefreshTest, InvalidAppliedOrientationsRaiseOnlyResettingAndKeepAppliedParameters)
{
    GraphicsDevice d(GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef, Parameters());
    std::vector<std::string> events;
    d.DeviceResetting += [&](System::Object* sender,const System::EventArgs& args) {
        EXPECT_EQ(sender,&d); EXPECT_EQ(&args,&System::EventArgs::Empty); events.push_back("resetting");
    };
    d.DeviceReset += [&](System::Object*,const System::EventArgs&) { events.push_back("reset"); };
    // Reset IL_003d-0055 notifies first; ConvertPresentationParametersToNative IL_001d-0023
    // -> Helpers.ValidateOrientation IL_0000-0019 accepts only 0,1,2,4 (not every flag combination).
    for(int invalid : {-1,3,5,6,7,8})
    {
        auto p=Parameters(12,10);
        p.setDisplayOrientationProperty(static_cast<Microsoft::Xna::Framework::DisplayOrientation>(invalid));
        Argument<System::ArgumentException>("", "The specified DisplayOrientation is invalid.", [&] { d.Reset(p); });
        EXPECT_EQ(d.getPresentationParametersProperty().getBackBufferWidthProperty(),8);
        EXPECT_EQ(d.getPresentationParametersProperty().getDisplayOrientationProperty(),Microsoft::Xna::Framework::DisplayOrientation::Default);
    }
    EXPECT_EQ(events,(std::vector<std::string>(6,"resetting")));
    for(int valid : {0,1,2,4})
    {
        auto p=Parameters();
        p.setDisplayOrientationProperty(static_cast<Microsoft::Xna::Framework::DisplayOrientation>(valid));
        d.Reset(p);
        EXPECT_EQ(d.getPresentationParametersProperty().getDisplayOrientationProperty(),p.getDisplayOrientationProperty());
    }
    EXPECT_EQ(events.size(),14u); // Six refusals + four complete before/after pairs.
}
