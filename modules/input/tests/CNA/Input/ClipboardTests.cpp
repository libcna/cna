// SPDX-License-Identifier: MS-PL
//
// PLAT-88: CNA::Input::Clipboard reads the platform, not SDL.

#include <gtest/gtest.h>

#include "CNA/Input/Clipboard.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using CNA::Input::Clipboard;
using CNA::Platform::GetCurrentPlatform;
using CNA::Platform::PlatformSubsystem;

// A clipboard needs the video subsystem, which needs a display. On a headless box these run under
// Xvfb; without one they skip, matching the real-window MouseCursor and TextInputEXT tests. Within
// one process the platform owns the selection and returns its own cached text, so a set-then-get
// round-trip is reliable with no system clipboard manager present.
namespace
{
    class CnaInputClipboardTest : public ::testing::Test
    {
    protected:
        bool videoUp_ = false;
        std::string saved_;

        void SetUp() override
        {
            try
            {
                GetCurrentPlatform().AcquireSubsystem(PlatformSubsystem::Video);
                videoUp_ = true;
            }
            catch (const std::exception& error)
            {
                GTEST_SKIP() << "no video subsystem (no display): " << error.what();
            }

            // Video being up is necessary but not sufficient: HEADLESS has a video subsystem and
            // no clipboard at all. The round-trip's real precondition is a clipboard service, and
            // its absence is covered by its own test below rather than by a failure here.
            if (GetCurrentPlatform().GetClipboard() == nullptr)
            {
                GTEST_SKIP() << "this platform reports no clipboard";
            }
            saved_ = Clipboard::GetTextEXT();
        }

        void TearDown() override
        {
            if (videoUp_)
            {
                Clipboard::SetTextEXT(saved_);  // restore whatever was there
                GetCurrentPlatform().ReleaseSubsystem(PlatformSubsystem::Video);
            }
        }
    };
}

TEST_F(CnaInputClipboardTest, SetTextThenGetTextRoundTripsIncludingUtf8)
{
    const std::string text = "cna clipboard \xC3\xA9\xE2\x82\xAC";  // ASCII + U+00E9 + U+20AC
    Clipboard::SetTextEXT(text);

    EXPECT_EQ(Clipboard::GetTextEXT(), text);
    EXPECT_TRUE(Clipboard::HasTextEXT());
}

TEST_F(CnaInputClipboardTest, EmptyTextLeavesNoText)
{
    Clipboard::SetTextEXT("");

    EXPECT_EQ(Clipboard::GetTextEXT(), std::string());
    EXPECT_FALSE(Clipboard::HasTextEXT());
}

namespace
{
    /// A platform with no clipboard at all, which is what the Clipboard capability being false
    /// means. HEADLESS is one; a terminal will be another.
    class ClipboardlessPlatform final : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        [[nodiscard]] CNA::Platform::IPlatformClipboard* GetClipboard() override { return nullptr; }
    };
}

TEST(CnaInputClipboardWithoutAServiceTest, ReportsEmptyAndIgnoresWritesRatherThanFailing)
{
    // This branch did not exist before the migration -- SDL's clipboard functions were always
    // callable. A game implementing a text field calls all three unconditionally, so each has to
    // answer rather than throw, and the answer has to be the same one an empty clipboard gives.
    ClipboardlessPlatform platform;
    const CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);

    EXPECT_NO_THROW(Clipboard::SetTextEXT("ignored"));
    EXPECT_EQ(Clipboard::GetTextEXT(), std::string());
    EXPECT_FALSE(Clipboard::HasTextEXT());
}

// --- the primary selection (plans/plan_x11.md X11-0157) ----------------------------------------------

namespace
{
    /// An in-memory selection, to see which service the CNAEXT calls reach.
    class RecordingSelection final : public CNA::Platform::IPlatformClipboard
    {
    public:
        [[nodiscard]] bool HasText() const override { return !text.empty(); }
        [[nodiscard]] std::string GetText() const override { return text; }
        void SetText(const std::string& value) override { text = value; }
        std::string text;
    };

    /// A platform whose clipboard and primary selection are two separate recorders.
    class TwoSelectionPlatform final : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        [[nodiscard]] CNA::Platform::IPlatformClipboard* GetClipboard() override
        {
            return &clipboard;
        }
        [[nodiscard]] CNA::Platform::IPlatformClipboard* GetPrimarySelection() override
        {
            return &primary;
        }
        RecordingSelection clipboard;
        RecordingSelection primary;
    };

    /// A platform with a clipboard but no primary selection -- Windows, macOS, the web.
    class ClipboardOnlyPlatform final : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        [[nodiscard]] CNA::Platform::IPlatformClipboard* GetClipboard() override
        {
            return &clipboard;
        }
        [[nodiscard]] CNA::Platform::IPlatformClipboard* GetPrimarySelection() override
        {
            return nullptr;
        }
        RecordingSelection clipboard;
    };
}

TEST(CnaInputPrimarySelectionTest, SelectingAndPastingUseThePrimarySelectionNotTheClipboard)
{
    TwoSelectionPlatform platform;
    const CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);
    platform.clipboard.text = "copied";

    Clipboard::SetPrimarySelectionTextEXT("selected \xE2\x9C\x93");
    EXPECT_EQ(platform.primary.text, "selected \xE2\x9C\x93");
    EXPECT_EQ(platform.clipboard.text, "copied") << "selecting must not replace what was copied";
    EXPECT_EQ(Clipboard::GetPrimarySelectionTextEXT(), "selected \xE2\x9C\x93");
    EXPECT_TRUE(Clipboard::HasPrimarySelectionTextEXT());

    Clipboard::SetTextEXT("copied again");
    EXPECT_EQ(Clipboard::GetPrimarySelectionTextEXT(), "selected \xE2\x9C\x93")
        << "copying must not replace what was selected";

    Clipboard::SetPrimarySelectionTextEXT("");
    EXPECT_FALSE(Clipboard::HasPrimarySelectionTextEXT());
}

TEST(CnaInputPrimarySelectionTest, WithoutOneItReadsAsEmptyAndIgnoresWrites)
{
    ClipboardOnlyPlatform platform;
    const CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);

    EXPECT_NO_THROW(Clipboard::SetPrimarySelectionTextEXT("ignored"));
    EXPECT_EQ(Clipboard::GetPrimarySelectionTextEXT(), std::string());
    EXPECT_FALSE(Clipboard::HasPrimarySelectionTextEXT());
    EXPECT_TRUE(platform.clipboard.text.empty()) << "and it never falls back to the clipboard";
}

// --- formats other than text (plans/plan_x11.md X11-0158) --------------------------------------------

namespace
{
    /// A clipboard that keeps whatever formats it is given.
    class FormatClipboard final : public CNA::Platform::IPlatformClipboard
    {
    public:
        [[nodiscard]] bool HasText() const override { return !GetText().empty(); }
        [[nodiscard]] std::string GetText() const override
        {
            for (const auto& offer : offers)
            {
                if (offer.mimeType == "text/plain;charset=utf-8")
                {
                    return std::string(offer.data.begin(), offer.data.end());
                }
            }
            return {};
        }
        void SetText(const std::string& value) override
        {
            offers = {{"text/plain;charset=utf-8", std::vector<std::uint8_t>(value.begin(), value.end())}};
        }
        [[nodiscard]] std::vector<std::string> GetMimeTypes() const override
        {
            std::vector<std::string> types;
            for (const auto& offer : offers) { types.push_back(offer.mimeType); }
            return types;
        }
        [[nodiscard]] bool HasData(const std::string& mimeType) const override
        {
            return !GetData(mimeType).empty();
        }
        [[nodiscard]] std::vector<std::uint8_t> GetData(const std::string& mimeType) const override
        {
            for (const auto& offer : offers)
            {
                if (offer.mimeType == mimeType) { return offer.data; }
            }
            return {};
        }
        void SetData(const std::vector<CNA::Platform::ClipboardOffer>& value) override { offers = value; }
        std::vector<CNA::Platform::ClipboardOffer> offers;
    };

    class FormatClipboardPlatform final : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        [[nodiscard]] CNA::Platform::IPlatformClipboard* GetClipboard() override { return &clipboard; }
        FormatClipboard clipboard;
    };
}

TEST(CnaInputClipboardDataTest, FormatsReachTheClipboardInTheirOrderAndComeBackExactly)
{
    FormatClipboardPlatform platform;
    const CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);

    const std::vector<std::uint8_t> png = {0x89, 'P', 'N', 'G', 0x00, 0xFF, 0x0D, 0x0A};
    const std::string html = "<b>bold</b>";
    const std::string plain = "bold";
    EXPECT_TRUE(Clipboard::SetDataEXT({{"text/html", std::vector<std::uint8_t>(html.begin(), html.end())},
                                       {"text/plain;charset=utf-8",
                                        std::vector<std::uint8_t>(plain.begin(), plain.end())}}));
    EXPECT_EQ(Clipboard::GetMimeTypesEXT(),
              (std::vector<std::string>{"text/html", "text/plain;charset=utf-8"}));
    EXPECT_TRUE(Clipboard::HasDataEXT("text/html"));
    EXPECT_FALSE(Clipboard::HasDataEXT("image/png"));
    EXPECT_EQ(Clipboard::GetTextEXT(), plain) << "the plain text beside it is the text";

    EXPECT_TRUE(Clipboard::SetDataEXT("image/png", png));
    EXPECT_EQ(Clipboard::GetDataEXT("image/png"), png);
    EXPECT_TRUE(Clipboard::GetDataEXT("text/html").empty()) << "a new copy replaces every format";
    EXPECT_FALSE(Clipboard::HasTextEXT());
}

TEST(CnaInputClipboardDataTest, ATextOnlyClipboardReadsAsNoFormatsAndSaysItDidNotTakeOne)
{
    ClipboardOnlyPlatform platform;  // its clipboard implements text alone
    const CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);
    platform.clipboard.text = "kept";

    EXPECT_FALSE(Clipboard::SetDataEXT("image/png", {0x89, 'P', 'N', 'G'}));
    EXPECT_TRUE(Clipboard::GetMimeTypesEXT().empty());
    EXPECT_FALSE(Clipboard::HasDataEXT("image/png"));
    EXPECT_TRUE(Clipboard::GetDataEXT("image/png").empty());
    EXPECT_EQ(platform.clipboard.text, "kept");
}

TEST(CnaInputClipboardDataTest, WithoutAClipboardNothingIsTaken)
{
    ClipboardlessPlatform platform;
    const CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);
    EXPECT_FALSE(Clipboard::SetDataEXT("image/png", {1, 2, 3}));
    EXPECT_TRUE(Clipboard::GetMimeTypesEXT().empty());
    EXPECT_TRUE(Clipboard::GetDataEXT("image/png").empty());
}
