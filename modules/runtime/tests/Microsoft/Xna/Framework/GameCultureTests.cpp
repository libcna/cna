// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformFactory.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "System/Globalization/CultureInfo.hpp"

#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using Microsoft::Xna::Framework::Game;
using System::Globalization::CultureInfo;

namespace
{
    class PreferredLocaleSystemInfo final : public CNA::Platform::IPlatformSystemInfo
    {
    public:
        PreferredLocaleSystemInfo(CNA::Platform::IPlatformSystemInfo& inner,
                                  std::vector<CNA::Platform::PlatformLocale> locales)
            : inner_(inner), locales_(std::move(locales))
        {
        }

        [[nodiscard]] std::string GetPlatformName() const override
        {
            return inner_.GetPlatformName();
        }

        [[nodiscard]] int GetSystemMemoryMegabytes() const override
        {
            return inner_.GetSystemMemoryMegabytes();
        }

        [[nodiscard]] int GetLogicalCoreCount() const override
        {
            return inner_.GetLogicalCoreCount();
        }

        [[nodiscard]] std::vector<CNA::Platform::PlatformLocale> GetPreferredLocales() const override
        {
            return locales_;
        }

        [[nodiscard]] CNA::Platform::PowerInfo GetPowerInfo() const override
        {
            return inner_.GetPowerInfo();
        }

        bool OpenUrl(const std::string& url) override { return inner_.OpenUrl(url); }

    private:
        CNA::Platform::IPlatformSystemInfo& inner_;
        std::vector<CNA::Platform::PlatformLocale> locales_;
    };

    class PreferredLocalePlatform final : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        PreferredLocalePlatform(std::unique_ptr<CNA::Platform::IPlatform> inner,
                                std::vector<CNA::Platform::PlatformLocale> locales)
            : PlatformTestDecorator(std::move(inner)),
              systemInfo_(*GetInner().GetSystemInfo(), std::move(locales))
        {
        }

        [[nodiscard]] CNA::Platform::IPlatformSystemInfo* GetSystemInfo() override
        {
            return &systemInfo_;
        }

    private:
        PreferredLocaleSystemInfo systemInfo_;
    };

    class QuietGame final : public Game
    {
    public:
        explicit QuietGame(std::unique_ptr<CNA::Platform::IPlatform> platform)
            : Game(std::move(platform))
        {
        }
    };

    class CultureDefaultsGuard final
    {
    public:
        CultureDefaultsGuard()
            : culture_(CultureInfo::getDefaultThreadCurrentCultureProperty()),
              uiCulture_(CultureInfo::getDefaultThreadCurrentUICultureProperty())
        {
        }

        ~CultureDefaultsGuard()
        {
            CultureInfo::setDefaultThreadCurrentCultureProperty(culture_);
            CultureInfo::setDefaultThreadCurrentUICultureProperty(uiCulture_);
        }

    private:
        std::optional<CultureInfo> culture_;
        std::optional<CultureInfo> uiCulture_;
    };

    struct ObservedCultures
    {
        std::string culture;
        std::string uiCulture;
    };

    ObservedCultures ConstructGameWithLocale(const CNA::Platform::PlatformLocale& locale)
    {
        ObservedCultures observed;
        std::thread worker([&]
        {
            auto platform = std::make_unique<PreferredLocalePlatform>(
                CNA::Platform::PlatformFactory::Create(),
                std::vector<CNA::Platform::PlatformLocale>{locale});
            QuietGame game(std::move(platform));
            observed.culture = CultureInfo::getCurrentCultureProperty().getNameProperty();
            observed.uiCulture = CultureInfo::getCurrentUICultureProperty().getNameProperty();
        });
        worker.join();
        return observed;
    }
}

TEST(GameCultureTest, PreferredPlatformLocaleBecomesTheProcessFallback)
{
    CultureDefaultsGuard restore;
    CultureInfo::setDefaultThreadCurrentCultureProperty(std::nullopt);
    CultureInfo::setDefaultThreadCurrentUICultureProperty(std::nullopt);

    const ObservedCultures observed = ConstructGameWithLocale({"fr", "FR"});

    EXPECT_EQ(observed.culture, "fr-FR");
    EXPECT_EQ(observed.uiCulture, "fr-FR");
    ASSERT_TRUE(CultureInfo::getDefaultThreadCurrentCultureProperty().has_value());
    ASSERT_TRUE(CultureInfo::getDefaultThreadCurrentUICultureProperty().has_value());
    EXPECT_EQ(CultureInfo::getDefaultThreadCurrentCultureProperty()->getNameProperty(), "fr-FR");
    EXPECT_EQ(CultureInfo::getDefaultThreadCurrentUICultureProperty()->getNameProperty(), "fr-FR");
}

TEST(GameCultureTest, ExplicitProcessDefaultsTakePrecedenceOverThePlatformLocale)
{
    CultureDefaultsGuard restore;
    CultureInfo::setDefaultThreadCurrentCultureProperty(CultureInfo("ja-JP"));
    CultureInfo::setDefaultThreadCurrentUICultureProperty(CultureInfo("ko-KR"));

    const ObservedCultures observed = ConstructGameWithLocale({"fr", "FR"});

    EXPECT_EQ(observed.culture, "ja-JP");
    EXPECT_EQ(observed.uiCulture, "ko-KR");
    EXPECT_EQ(CultureInfo::getDefaultThreadCurrentCultureProperty()->getNameProperty(), "ja-JP");
    EXPECT_EQ(CultureInfo::getDefaultThreadCurrentUICultureProperty()->getNameProperty(), "ko-KR");
}
