// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "RuntimePlatformTestSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    class PresentationSelectionProbe : public GraphicsDeviceManager
    {
    public:
        GraphicsDeviceInformation Select() { return FindBestDevice(true); }
    };
}

TEST(GraphicsDeviceManagerPresentationTest, DefaultDepth24ReachesTheSelectedParameters)
{
    // Refresh Game.dll GraphicsDeviceManager::.ctor IL_0007-0009 assigns ordinal2
    // (Depth24). A bare PresentationParameters instead defaults to None.
    EXPECT_EQ(PresentationParameters().getDepthStencilFormatProperty(), DepthFormat::None);
    PresentationSelectionProbe manager;
    EXPECT_EQ(manager.getPreferredDepthStencilFormatProperty(), DepthFormat::Depth24);
    EXPECT_EQ(manager.Select().getPresentationParametersProperty().getDepthStencilFormatProperty(),
              DepthFormat::Depth24);
    for (const auto depth : {DepthFormat::None, DepthFormat::Depth16, DepthFormat::Depth24Stencil8})
    {
        manager.setPreferredDepthStencilFormatProperty(depth);
        EXPECT_EQ(manager.Select().getPresentationParametersProperty().getDepthStencilFormatProperty(), depth);
    }
}

TEST(GraphicsDeviceManagerPresentationTest, DefaultDepth24ReachesCreationAndExplicitPreferencesReachReset)
{
    if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
        GTEST_SKIP() << "Selected platform cannot create the ordinary hidden Game window";
    Game game;
    GraphicsDeviceManager manager(&game);
    auto& device = game.getGraphicsDeviceProperty();

    // Do not let Game's eager Depth24 device conceal a dropped manager default.
    auto none = device.getPresentationParametersProperty().Clone();
    none.setDepthStencilFormatProperty(DepthFormat::None);
    device.Reset(none);
    ASSERT_EQ(device.getPresentationParametersProperty().getDepthStencilFormatProperty(), DepthFormat::None);

    // Refresh .ctor IL_0007-0009; CreateDevice forwards the selected parameters.
    DepthFormat prepared = DepthFormat::None;
    manager.PreparingDeviceSettings += [&](System::Object*, const PreparingDeviceSettingsEventArgs& args)
    {
        prepared = args.getGraphicsDeviceInformationProperty().getPresentationParametersProperty()
            .getDepthStencilFormatProperty();
    };
    manager.CreateDevice();
    EXPECT_EQ(prepared, DepthFormat::Depth24);
    EXPECT_EQ(device.getPresentationParametersProperty().getDepthStencilFormatProperty(), DepthFormat::Depth24);

    for (const auto depth : {DepthFormat::Depth16, DepthFormat::None, DepthFormat::Depth24Stencil8,
                             DepthFormat::Depth24})
    {
        SCOPED_TRACE(static_cast<int>(depth));
        manager.setPreferredDepthStencilFormatProperty(depth);
        manager.ApplyChanges();
        EXPECT_EQ(prepared, depth);
        EXPECT_EQ(device.getPresentationParametersProperty().getDepthStencilFormatProperty(), depth);
    }
}
