// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include "CNA/Devices/Clipboard.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"

using CNA::Devices::Clipboard;

namespace
{
    // Injected service: these tests never depend on a desktop session's clipboard contents.
    class ClipboardPlatform final : public CNA::Platform::Testing::PlatformTestDecorator,
                                    public CNA::Platform::IPlatformClipboard
    {
    public:
        ClipboardPlatform() : PlatformTestDecorator(CNA::Platform::PlatformFactory::Create("Headless")) {}
        std::string text;
        bool available = true;
        bool rejectWrite = false;
        int writeCalls = 0;
        CNA::Platform::PlatformCapabilities GetCapabilities() const override
        {
            auto capabilities = PlatformTestDecorator::GetCapabilities();
            capabilities.clipboard = available;
            return capabilities;
        }
        CNA::Platform::IPlatformClipboard* GetClipboard() override
        {
            return available ? this : nullptr;
        }
        bool HasText() const override { return !text.empty(); }
        std::string GetText() const override { return text; }
        void SetText(const std::string& value) override
        {
            ++writeCalls;
            if (rejectWrite)
                throw CNA::Platform::PlatformException("SetText", "scripted clipboard failure");
            text = value;
        }
    };

    class ClipboardTests : public ::testing::Test
    {
    protected:
        ClipboardPlatform platform;
        CNA::Platform::Testing::ScopedCurrentPlatform current{platform};
    };
}

TEST_F(ClipboardTests, GetHasTextPropertyDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)Clipboard::getHasTextProperty(); });
    EXPECT_FALSE(Clipboard::getHasTextProperty());
}

TEST_F(ClipboardTests, GetTextPropertyDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)Clipboard::getTextProperty(); });
    EXPECT_TRUE(Clipboard::getTextProperty().empty());
}

TEST_F(ClipboardTests, SetTextPropertyDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)Clipboard::setTextProperty("CNA::Devices::Clipboard test"); });
    // Oracle: Clipboard.hpp forwards UTF-8 text and reports a successful write.
    EXPECT_EQ(platform.text, "CNA::Devices::Clipboard test");
    EXPECT_EQ(platform.writeCalls, 1);
}

// Oracle: Clipboard.hpp: a service accepting a write must report success and return its text.
TEST_F(ClipboardTests, SetThenGetIsConsistentWhetherOrNotAClipboardIsAvailable)
{
    const std::string testText = "CNA::Devices::Clipboard round-trip test";
    const bool setSucceeded = Clipboard::setTextProperty(testText);
    ASSERT_TRUE(setSucceeded);

    if (setSucceeded)
    {
        EXPECT_EQ(Clipboard::getTextProperty(), testText);
        EXPECT_TRUE(Clipboard::getHasTextProperty());
    }
    else
    {
        EXPECT_NO_THROW({ (void)Clipboard::getTextProperty(); });
        EXPECT_NO_THROW({ (void)Clipboard::getHasTextProperty(); });
    }
}

TEST_F(ClipboardTests, EmptyTextIsAValidInput)
{
    platform.text = "replace this";
    EXPECT_NO_THROW({ (void)Clipboard::setTextProperty(""); });
    EXPECT_TRUE(platform.text.empty());
    EXPECT_FALSE(Clipboard::getHasTextProperty());
    EXPECT_TRUE(Clipboard::getTextProperty().empty());
}

TEST_F(ClipboardTests, MissingServiceReturnsFalseAndEmptyWithoutWriting)
{
    // Oracle: Clipboard.hpp documents false/empty when no clipboard is available.
    platform.available = false;
    platform.text = "untouched";
    EXPECT_FALSE(Clipboard::setTextProperty("new text"));
    EXPECT_FALSE(Clipboard::getHasTextProperty());
    EXPECT_TRUE(Clipboard::getTextProperty().empty());
    EXPECT_EQ(platform.writeCalls, 0);
    EXPECT_EQ(platform.text, "untouched");
}

TEST_F(ClipboardTests, RejectedWriteReturnsFalseAndTheNextWriteCanSucceed)
{
    // Oracle: Clipboard.hpp promises false on write failure, true on success.
    platform.text = "old text";
    platform.rejectWrite = true;
    EXPECT_FALSE(Clipboard::setTextProperty("refused"));
    EXPECT_EQ(platform.text, "old text");
    EXPECT_EQ(platform.writeCalls, 1);
    platform.rejectWrite = false;
    const std::string utf8 = "clipboard \xC5\xBE\xF0\x9F\x98\x80";
    EXPECT_TRUE(Clipboard::setTextProperty(utf8));
    EXPECT_EQ(platform.writeCalls, 2);
    EXPECT_EQ(platform.text, utf8);
    EXPECT_EQ(Clipboard::getTextProperty(), utf8);
    EXPECT_TRUE(Clipboard::getHasTextProperty());
}

#endif // CNA_DEVICES
