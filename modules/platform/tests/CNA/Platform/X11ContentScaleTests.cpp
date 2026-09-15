// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0156: the session's content scale -- per monitor where the session sets
// one, followed as it changes -- and a window's display scale that is 1, because X11 has one
// coordinate space.
//
// The parsers need no server. The rest changes the private server's RESOURCE_MANAGER and plays
// its XSETTINGS manager, so it runs only on the launcher's own server
// (CNA_X11_PRIVATE_TEST_SERVER), and puts both back.

#include <gtest/gtest.h>

#include "../../../src/X11/X11ContentScale.hpp"
#include "../../../src/X11/X11Headers.hpp"

#include "CNA/Platform/PlatformFactory.hpp"
#include "System/Environment.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace {

using namespace CNA::Platform;
using CNA::Platform::X11::ParseScreenScaleFactors;
using CNA::Platform::X11::ScaleFromXftDpi;
using CNA::Platform::X11::ScaleFromXsettings;
using CNA::Platform::X11::UsableScale;
using CNA::Platform::X11::kCurrentTime;
using CNA::Platform::X11::kNone;
using CNA::Platform::X11::kXFalse;

/// An XSETTINGS property with integer settings, in either byte order.
std::vector<unsigned char> Xsettings(const std::vector<std::pair<std::string, std::int32_t>>& integers,
                                     const bool mostSignificantFirst = false,
                                     const std::string& leadingString = {})
{
    std::vector<unsigned char> out;
    const auto put16 = [&](const std::uint32_t value) {
        if (mostSignificantFirst) { out.push_back(static_cast<unsigned char>(value >> 8)); out.push_back(static_cast<unsigned char>(value)); }
        else { out.push_back(static_cast<unsigned char>(value)); out.push_back(static_cast<unsigned char>(value >> 8)); }
    };
    const auto put32 = [&](const std::uint32_t value) {
        for (int index = 0; index < 4; ++index)
        {
            const int shift = mostSignificantFirst ? 8 * (3 - index) : 8 * index;
            out.push_back(static_cast<unsigned char>(value >> shift));
        }
    };
    const auto pad = [&]() { while (out.size() % 4 != 0) { out.push_back(0); } };
    out.push_back(mostSignificantFirst ? 1 : 0);
    out.push_back(0); out.push_back(0); out.push_back(0);
    put32(7);  // serial
    put32(static_cast<std::uint32_t>(integers.size() + (leadingString.empty() ? 0 : 1)));
    if (!leadingString.empty())
    {
        // A string setting first, to prove its length is skipped correctly.
        out.push_back(1); out.push_back(0);
        const std::string name = "Net/ThemeName";
        put16(static_cast<std::uint32_t>(name.size()));
        out.insert(out.end(), name.begin(), name.end());
        pad();
        put32(0);
        put32(static_cast<std::uint32_t>(leadingString.size()));
        out.insert(out.end(), leadingString.begin(), leadingString.end());
        pad();
    }
    for (const auto& [name, value] : integers)
    {
        out.push_back(0); out.push_back(0);
        put16(static_cast<std::uint32_t>(name.size()));
        out.insert(out.end(), name.begin(), name.end());
        pad();
        put32(0);
        put32(static_cast<std::uint32_t>(value));
    }
    return out;
}

TEST(X11ContentScaleParsing, XftDpiIsNinetySixToOne)
{
    EXPECT_EQ(ScaleFromXftDpi("Xft.dpi:\t192\n"), 2.0f);
    EXPECT_EQ(ScaleFromXftDpi("Xft.antialias:\t1\nXft.dpi:\t144\nXft.hinting:\t1\n"), 1.5f);
    EXPECT_FALSE(ScaleFromXftDpi("Xft.antialias:\t1\n"));
    EXPECT_FALSE(ScaleFromXftDpi(""));
    // A value no monitor has is a broken setting, not a scale.
    EXPECT_FALSE(ScaleFromXftDpi("Xft.dpi:\t9600\n"));
    EXPECT_FALSE(ScaleFromXftDpi("Xft.dpi:\t0\n"));
}

TEST(X11ContentScaleParsing, XsettingsPreferTheWindowScalingFactorThenXftDpi)
{
    EXPECT_EQ(ScaleFromXsettings(Xsettings({{"Gdk/WindowScalingFactor", 2}, {"Xft/DPI", 96 * 1024}})), 2.0f);
    EXPECT_EQ(ScaleFromXsettings(Xsettings({{"Xft/DPI", 144 * 1024}})), 1.5f);
    EXPECT_FALSE(ScaleFromXsettings(Xsettings({{"Net/DoubleClickTime", 400}})));
}

TEST(X11ContentScaleParsing, XsettingsAreReadInEitherByteOrderAndPastStrings)
{
    EXPECT_EQ(ScaleFromXsettings(Xsettings({{"Gdk/WindowScalingFactor", 2}}, true)), 2.0f);
    EXPECT_EQ(ScaleFromXsettings(Xsettings({{"Xft/DPI", 192 * 1024}}, false, "Adwaita")), 2.0f);
    EXPECT_EQ(ScaleFromXsettings(Xsettings({{"Xft/DPI", 192 * 1024}}, true, "Adwaita-dark")), 2.0f);
}

TEST(X11ContentScaleParsing, ATruncatedXsettingsPropertyIsReadAsFarAsItGoes)
{
    std::vector<unsigned char> property = Xsettings({{"Gdk/WindowScalingFactor", 2}});
    for (std::size_t length = 0; length < property.size(); ++length)
    {
        const std::vector<unsigned char> truncated(property.begin(), property.begin() + static_cast<std::ptrdiff_t>(length));
        EXPECT_FALSE(ScaleFromXsettings(truncated)) << "length " << length;
    }
    EXPECT_EQ(ScaleFromXsettings(property), 2.0f);
}

TEST(X11ContentScaleParsing, KdesPerScreenFactorsAreNamedOrPositional)
{
    const auto named = ParseScreenScaleFactors("eDP-1=2;HDMI-1=1;");
    EXPECT_EQ(named.named.size(), 2u);
    EXPECT_EQ(named.named.at("eDP-1"), 2.0f);
    EXPECT_EQ(named.named.at("HDMI-1"), 1.0f);
    const auto positional = ParseScreenScaleFactors("1.5;1.25");
    ASSERT_EQ(positional.positional.size(), 2u);
    EXPECT_EQ(positional.positional[0], 1.5f);
    EXPECT_EQ(positional.positional[1], 1.25f);
    // Entries that are not usable are dropped rather than guessed at.
    const auto broken = ParseScreenScaleFactors("eDP-1=abc;=2;DP-2=100;DP-3=1.75");
    EXPECT_EQ(broken.named.size(), 1u);
    EXPECT_EQ(broken.named.at("DP-3"), 1.75f);
    EXPECT_TRUE(broken.positional.empty());
}

TEST(X11ContentScaleParsing, UsableScalesAreFiniteAndSane)
{
    EXPECT_EQ(UsableScale(1.0), 1.0f);
    EXPECT_FALSE(UsableScale(0.25));
    EXPECT_FALSE(UsableScale(12.0));
    EXPECT_FALSE(UsableScale(std::nan("")));
}

// --- the live scale, on the launcher's server -------------------------------------------------------

class X11ContentScaleLive : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const char* privateServer = std::getenv("CNA_X11_PRIVATE_TEST_SERVER");
        if (privateServer == nullptr || std::string(privateServer) != "1")
        {
            GTEST_SKIP() << "changes the server's resources; needs tools/platform/x11_test_server.sh";
        }
        observer_ = XOpenDisplay(nullptr);
        ASSERT_NE(observer_, nullptr);
        root_ = DefaultRootWindow(observer_);
        resourceManager_ = XInternAtom(observer_, "RESOURCE_MANAGER", kXFalse);
        // What the server had, to put back.
        Atom type = kNone;
        int format = 0;
        unsigned long count = 0;
        unsigned long remaining = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(observer_, root_, resourceManager_, 0, 1 << 20, kXFalse, XA_STRING,
                               &type, &format, &count, &remaining, &data) == Success &&
            data != nullptr)
        {
            savedResources_ = std::string(reinterpret_cast<char*>(data), count);
        }
        if (data != nullptr) { XFree(data); }
        savedQt_ = System::Environment::GetEnvironmentVariable("QT_SCREEN_SCALE_FACTORS");
        savedGdk_ = System::Environment::GetEnvironmentVariable("GDK_SCALE");
        System::Environment::SetEnvironmentVariable("QT_SCREEN_SCALE_FACTORS", std::nullopt);
        System::Environment::SetEnvironmentVariable("GDK_SCALE", std::nullopt);
    }

    void TearDown() override
    {
        window_.reset();
        if (platform_ != nullptr)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
        if (observer_ != nullptr)
        {
            if (settingsWindow_ != kNone)
            {
                XDestroyWindow(observer_, settingsWindow_);
            }
            if (savedResources_)
            {
                SetResources(*savedResources_);
            }
            else
            {
                XDeleteProperty(observer_, root_, resourceManager_);
                XSync(observer_, kXFalse);
            }
            XCloseDisplay(observer_);
        }
        System::Environment::SetEnvironmentVariable("QT_SCREEN_SCALE_FACTORS", savedQt_);
        System::Environment::SetEnvironmentVariable("GDK_SCALE", savedGdk_);
    }

    void SetResources(const std::string& text) const
    {
        XChangeProperty(observer_, root_, resourceManager_, XA_STRING, 8, PropModeReplace,
                        reinterpret_cast<const unsigned char*>(text.data()),
                        static_cast<int>(text.size()));
        XSync(observer_, kXFalse);
    }

    void ClearResources() const
    {
        XDeleteProperty(observer_, root_, resourceManager_);
        XSync(observer_, kXFalse);
    }

    /// Becomes the screen's XSETTINGS manager, as gsd-xsettings or xsettingsd would.
    void BecomeSettingsManager(const std::vector<unsigned char>& settings)
    {
        settingsWindow_ = XCreateSimpleWindow(observer_, root_, 0, 0, 1, 1, 0, 0, 0);
        const Atom property = XInternAtom(observer_, "_XSETTINGS_SETTINGS", kXFalse);
        XChangeProperty(observer_, settingsWindow_, property, property, 8, PropModeReplace,
                        settings.data(), static_cast<int>(settings.size()));
        const Atom selection = XInternAtom(observer_, "_XSETTINGS_S0", kXFalse);
        XSetSelectionOwner(observer_, selection, settingsWindow_, kCurrentTime);
        XEvent announce{};
        announce.xclient.type = ClientMessage;
        announce.xclient.window = root_;
        announce.xclient.message_type = XInternAtom(observer_, "MANAGER", kXFalse);
        announce.xclient.format = 32;
        announce.xclient.data.l[0] = static_cast<long>(kCurrentTime);
        announce.xclient.data.l[1] = static_cast<long>(selection);
        announce.xclient.data.l[2] = static_cast<long>(settingsWindow_);
        XSendEvent(observer_, root_, kXFalse, StructureNotifyMask, &announce);
        XSync(observer_, kXFalse);
    }

    void PublishSettings(const std::vector<unsigned char>& settings) const
    {
        const Atom property = XInternAtom(observer_, "_XSETTINGS_SETTINGS", kXFalse);
        XChangeProperty(observer_, settingsWindow_, property, property, 8, PropModeReplace,
                        settings.data(), static_cast<int>(settings.size()));
        XSync(observer_, kXFalse);
    }

    void Open()
    {
        platform_ = PlatformFactory::Create("X11");
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
        WindowDescription description;
        description.title = "CNA content scale";
        description.width = 333;
        description.height = 222;
        window_ = platform_->CreateWindow(description);
        window_->Sync();
    }

    [[nodiscard]] float FirstDisplayScale() const
    {
        const std::vector<DisplayInfo> displays = platform_->GetDisplays()->GetDisplays();
        return displays.empty() ? 0.0f : displays.front().contentScale;
    }

    bool PumpUntil(const std::function<bool(const std::vector<PlatformEvent>&)>& done,
                   const std::chrono::milliseconds budget = std::chrono::milliseconds(2000))
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        std::vector<PlatformEvent> batch;
        while (std::chrono::steady_clock::now() < deadline)
        {
            platform_->PollEvents(batch);
            seen_.insert(seen_.end(), batch.begin(), batch.end());
            if (done(seen_)) { return true; }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

    [[nodiscard]] bool SawScaleChange() const
    {
        const WindowId id = window_->GetId();
        return std::any_of(seen_.begin(), seen_.end(), [id](const PlatformEvent& event) {
            const auto* window = std::get_if<WindowEvent>(&event);
            return window != nullptr && window->window == id &&
                   window->kind == WindowEventKind::DisplayScaleChanged;
        });
    }

    ::Display* observer_ = nullptr;
    ::Window root_ = kNone;
    Atom resourceManager_ = kNone;
    ::Window settingsWindow_ = kNone;
    std::optional<std::string> savedResources_;
    std::optional<std::string> savedQt_;
    std::optional<std::string> savedGdk_;
    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
    std::vector<PlatformEvent> seen_;
};

TEST_F(X11ContentScaleLive, AtXftDpi192TheWindowStaysOnePixelPerUnitAndTheDisplaySaysTwo)
{
    // D-16: the session's scale was reported as the window's display scale, so a renderer
    // divided the drawable by it and multiplied input by it.
    SetResources("Xft.dpi:\t192\n");
    Open();
    const WindowBounds bounds = window_->GetClientBounds();
    const WindowSize pixels = window_->GetPixelSize();
    const float scale = window_->GetDisplayScale();
    EXPECT_EQ(scale, 1.0f);
    // The contract's own relation: pixels are the logical size times the display scale.
    EXPECT_EQ(pixels.width, static_cast<int>(std::lround(bounds.width * scale)));
    EXPECT_EQ(pixels.height, static_cast<int>(std::lround(bounds.height * scale)));
    EXPECT_FALSE(platform_->GetCapabilities().highDpi);
    EXPECT_EQ(FirstDisplayScale(), 2.0f) << "the session's scale belongs to the display";
}

TEST_F(X11ContentScaleLive, AChangedXftDpiIsFollowedAndAnnounced)
{
    SetResources("Xft.dpi:\t96\n");
    Open();
    EXPECT_EQ(FirstDisplayScale(), 1.0f);
    SetResources("Xft.dpi:\t144\n");
    EXPECT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return SawScaleChange(); }))
        << "no DisplayScaleChanged for a new Xft.dpi";
    EXPECT_EQ(FirstDisplayScale(), 1.5f);
    EXPECT_EQ(window_->GetDisplayScale(), 1.0f);
}

TEST_F(X11ContentScaleLive, AnXsettingsManagerIsReadAndFollowed)
{
    // No Xft.dpi: the XSETTINGS manager is next, as under SDL3.
    ClearResources();
    Open();
    EXPECT_EQ(FirstDisplayScale(), 1.0f);
    BecomeSettingsManager(Xsettings({{"Gdk/WindowScalingFactor", 2}}));
    EXPECT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return SawScaleChange(); }))
        << "a settings manager announcing itself was not read";
    EXPECT_EQ(FirstDisplayScale(), 2.0f);

    seen_.clear();
    PublishSettings(Xsettings({{"Gdk/WindowScalingFactor", 1}, {"Xft/DPI", 120 * 1024}}));
    // Gdk/WindowScalingFactor 1 is a usable scale and wins over Xft/DPI, as in SDL3.
    EXPECT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return SawScaleChange(); }));
    EXPECT_EQ(FirstDisplayScale(), 1.0f);

    seen_.clear();
    XDestroyWindow(observer_, settingsWindow_);
    settingsWindow_ = kNone;
    XSync(observer_, kXFalse);
    SetResources("Xft.dpi:\t120\n");
    EXPECT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return SawScaleChange(); }));
    EXPECT_EQ(FirstDisplayScale(), 1.25f);
}

TEST_F(X11ContentScaleLive, KdesPerScreenFactorGivesTheMonitorItsOwnScale)
{
    // The launcher's Xvfb has one output, called "screen".
    SetResources("Xft.dpi:\t96\n");
    System::Environment::SetEnvironmentVariable("QT_SCREEN_SCALE_FACTORS", std::string("screen=1.75;"));
    Open();
    EXPECT_EQ(FirstDisplayScale(), 1.75f);
    System::Environment::SetEnvironmentVariable("QT_SCREEN_SCALE_FACTORS", std::string("1.5"));
    window_.reset();
    platform_->ReleaseSubsystem(PlatformSubsystem::Video);
    platform_.reset();
    Open();
    EXPECT_EQ(FirstDisplayScale(), 1.5f) << "a positional list applies in display order";
}

TEST_F(X11ContentScaleLive, GdkScaleIsTheLastResort)
{
    ClearResources();
    System::Environment::SetEnvironmentVariable("GDK_SCALE", std::string("2"));
    Open();
    EXPECT_EQ(FirstDisplayScale(), 2.0f);
}

} // namespace
