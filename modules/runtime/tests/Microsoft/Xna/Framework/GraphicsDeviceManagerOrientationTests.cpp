// SPDX-License-Identifier: MS-PL
//
// CBIND-138: on a platform that rotates (Android, iOS) the device is created in an orientation the
// game supports. XNA's Default means what the preferred back buffer implies -- Portrait for a
// taller one -- so a 480x800 game is given a 480x800 back buffer, and the operating system is told
// that set rather than "no preference". Before, every game got a landscape back buffer there.
//
// The build's own platform underneath, reporting itself as Android: that name is the whole of
// GraphicsDeviceManager's rotating-platform decision, and everything else stays real.

#include <gtest/gtest.h>

#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameWindow.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "RuntimePlatformTestSupport.hpp"

#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;

namespace
{
    namespace Platform = CNA::Platform;

    class AndroidSystemInfo final : public Platform::IPlatformSystemInfo
    {
    public:
        explicit AndroidSystemInfo(Platform::IPlatformSystemInfo* inner) : inner_(inner) {}

        [[nodiscard]] std::string GetPlatformName() const override { return "Android"; }
        [[nodiscard]] int GetSystemMemoryMegabytes() const override
        {
            return inner_->GetSystemMemoryMegabytes();
        }
        [[nodiscard]] int GetLogicalCoreCount() const override { return inner_->GetLogicalCoreCount(); }
        [[nodiscard]] std::vector<Platform::PlatformLocale> GetPreferredLocales() const override
        {
            return inner_->GetPreferredLocales();
        }
        [[nodiscard]] Platform::PowerInfo GetPowerInfo() const override { return inner_->GetPowerInfo(); }
        bool OpenUrl(const std::string& url) override { return inner_->OpenUrl(url); }

    private:
        Platform::IPlatformSystemInfo* inner_;
    };

    class RotatingPlatform final : public Platform::Testing::PlatformTestDecorator
    {
    public:
        explicit RotatingPlatform(std::unique_ptr<Platform::IPlatform> inner)
            : PlatformTestDecorator(std::move(inner)),
              systemInfo_(PlatformTestDecorator::GetSystemInfo())
        {
        }

        [[nodiscard]] Platform::IPlatformSystemInfo* GetSystemInfo() override { return &systemInfo_; }

    private:
        AndroidSystemInfo systemInfo_;
    };

    class OrientationGame final : public Game
    {
    public:
        explicit OrientationGame(std::unique_ptr<Platform::IPlatform> platform)
            : Game(std::move(platform))
        {
        }
    };

    struct CreatedDevice
    {
        DisplayOrientation orientation;
        int width;
        int height;
    };

    CreatedDevice CreateDevice(DisplayOrientation supported, int width, int height)
    {
        auto game = std::make_unique<OrientationGame>(
            std::make_unique<RotatingPlatform>(Platform::PlatformFactory::Create()));
        auto manager = std::make_unique<GraphicsDeviceManager>(game.get());
        manager->setSupportedOrientationsProperty(supported);
        manager->setPreferredBackBufferWidthProperty(width);
        manager->setPreferredBackBufferHeightProperty(height);
        manager->ApplyChanges();

        const auto& parameters = game->getGraphicsDeviceProperty().getPresentationParametersProperty();
        const CreatedDevice created{parameters.getDisplayOrientationProperty(),
                                    parameters.getBackBufferWidthProperty(),
                                    parameters.getBackBufferHeightProperty()};
        manager.reset();
        game.reset();
        return created;
    }

    class GraphicsDeviceManagerOrientationTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            if (!CNA::Runtime::Testing::DefaultPlatformCanCreateWindow())
            {
                GTEST_SKIP() << "this build's platform cannot create a window here";
            }
        }
    };
}

TEST_F(GraphicsDeviceManagerOrientationTest, ADefaultPortraitGameGetsAPortraitBackBuffer)
{
    const CreatedDevice created = CreateDevice(DisplayOrientation::Default, 480, 800);
    EXPECT_EQ(created.orientation, DisplayOrientation::Portrait);
    EXPECT_EQ(created.width, 480);
    EXPECT_EQ(created.height, 800);
}

TEST_F(GraphicsDeviceManagerOrientationTest, ADefaultLandscapeGameKeepsItsLandscapeBackBuffer)
{
    const CreatedDevice created = CreateDevice(DisplayOrientation::Default, 853, 480);
    EXPECT_EQ(created.orientation, DisplayOrientation::LandscapeLeft);
    EXPECT_EQ(created.width, 853);
    EXPECT_EQ(created.height, 480);
}

TEST_F(GraphicsDeviceManagerOrientationTest, AnExplicitPortraitSetSwapsALandscapePreference)
{
    const CreatedDevice created = CreateDevice(DisplayOrientation::Portrait, 800, 480);
    EXPECT_EQ(created.orientation, DisplayOrientation::Portrait);
    EXPECT_EQ(created.width, 480);
    EXPECT_EQ(created.height, 800);
}
