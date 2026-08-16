// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Input/TextInputType.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework::Input;
using Microsoft::Xna::Framework::Rectangle;

namespace
{
    class CannedTextInput final : public CNA::Platform::IPlatformTextInput
    {
    public:
        int startCalls = 0;
        int stopCalls = 0;
        int areaCalls = 0;
        CNA::Platform::WindowId lastWindow = 0;
        CNA::Platform::TextInputType lastType = CNA::Platform::TextInputType::Default;
        CNA::Platform::TextInputArea lastArea{};
        bool active = false;
        bool screenKeyboardShown = false;
        bool failCommands = false;

        void Start(const CNA::Platform::WindowId window,
                   const CNA::Platform::TextInputType type) override
        {
            FailIfRequested();
            ++startCalls;
            lastWindow = window;
            lastType = type;
            active = true;
        }

        void Stop(const CNA::Platform::WindowId window) override
        {
            FailIfRequested();
            ++stopCalls;
            lastWindow = window;
            active = false;
        }

        [[nodiscard]] bool IsActive(const CNA::Platform::WindowId window) const override
        {
            return active && window == lastWindow;
        }

        [[nodiscard]] bool IsScreenKeyboardShown(
            const CNA::Platform::WindowId window) const override
        {
            return screenKeyboardShown && window == lastWindow;
        }

        void SetInputArea(const CNA::Platform::WindowId window,
                          const CNA::Platform::TextInputArea& area) override
        {
            FailIfRequested();
            ++areaCalls;
            lastWindow = window;
            lastArea = area;
        }

    private:
        void FailIfRequested() const
        {
            if (failCommands)
            {
                throw CNA::Platform::PlatformException("CannedTextInput", "requested failure");
            }
        }
    };

    class CannedTextInputPlatform final
        : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        bool serviceAvailable = true;

        [[nodiscard]] CNA::Platform::IPlatformTextInput* GetTextInput() override
        {
            return serviceAvailable ? &textInput : nullptr;
        }

        CannedTextInput textInput;
    };

    // TextInputEXT is a static class with global state. Reset it around every test so
    // cases do not leak subscribers or a stale window identity into one another. The canned
    // platform makes lifecycle coverage deterministic on SDL3, headless and terminal builds.
    class TextInputEXTTest : public ::testing::Test
    {
    protected:
        static constexpr std::uintptr_t Handle = 0xC0FFEEu;
        static constexpr CNA::Platform::WindowId Window = 37;

        CannedTextInputPlatform platform;
        std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> installed;

        void SetUp() override
        {
            TextInputEXT::ResetForTests();
            installed =
                std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(platform);
        }

        void TearDown() override
        {
            installed.reset();
            TextInputEXT::ResetForTests();
        }

        static void PublishWindow(const std::uintptr_t handle = Handle,
                                  const CNA::Platform::WindowId window = Window)
        {
            TextInputEXT::setWindowHandleProperty(handle);
            TextInputEXT::INTERNAL_setWindowId(window);
        }
    };
}

TEST_F(TextInputEXTTest, TextInputDispatchesEachCodeUnitToSubscriber)
{
    std::u16string captured;
    TextInputEXT::TextInput = [&captured](charcs c) { captured += c; };

    TextInputEXT::INTERNAL_OnTextInput(u'H');
    TextInputEXT::INTERNAL_OnTextInput(u'i');

    EXPECT_EQ(captured, u"Hi");
}

TEST_F(TextInputEXTTest, TextInputWithoutSubscriberIsSafe)
{
    TextInputEXT::TextInput = nullptr;
    EXPECT_NO_THROW(TextInputEXT::INTERNAL_OnTextInput(u'x'));
}

// DEC-06: TextInput/TextEditing are multicast System::MulticastAction (match FNA's event Action<...>).
TEST_F(TextInputEXTTest, TextInputIsMulticastAndDeliversToEverySubscriber)
{
    TextInputEXT::TextInput = nullptr;
    std::u16string a;
    std::u16string b;
    TextInputEXT::TextInput += [&a](charcs c) { a += c; };
    TextInputEXT::TextInput += [&b](charcs c) { b += c; };

    TextInputEXT::INTERNAL_OnTextInput(u'X');
    TextInputEXT::INTERNAL_OnTextInput(u'Y');

    EXPECT_EQ(a, u"XY");
    EXPECT_EQ(b, u"XY");
}

TEST_F(TextInputEXTTest, TextEditingIsMulticastAndDeliversToEverySubscriber)
{
    TextInputEXT::TextEditing = nullptr;
    int calls1 = 0;
    int calls2 = 0;
    TextInputEXT::TextEditing += [&calls1](const std::string&, int, int) { ++calls1; };
    TextInputEXT::TextEditing += [&calls2](const std::string&, int, int) { ++calls2; };

    TextInputEXT::INTERNAL_OnTextEditing("draft", 0, 5);

    EXPECT_EQ(calls1, 1);
    EXPECT_EQ(calls2, 1);
}

TEST_F(TextInputEXTTest, TextEditingDispatchesTextStartAndLength)
{
    std::string text;
    int start = -1;
    int length = -1;
    TextInputEXT::TextEditing = [&](const std::string& t, int s, int l)
    {
        text = t;
        start = s;
        length = l;
    };

    TextInputEXT::INTERNAL_OnTextEditing("draft", 1, 3);

    EXPECT_EQ(text, "draft");
    EXPECT_EQ(start, 1);
    EXPECT_EQ(length, 3);
}

TEST_F(TextInputEXTTest, TextEditingEmptyCompositionFiresWithEmptyString)
{
    bool called = false;
    std::string text = "unset";
    int start = -1;
    int length = -1;
    TextInputEXT::TextEditing = [&](const std::string& t, int s, int l)
    {
        called = true;
        text = t;
        start = s;
        length = l;
    };

    // FNA passes null for an empty composition; CNA maps that to an empty string.
    TextInputEXT::INTERNAL_OnTextEditing(std::string(), 0, 0);

    EXPECT_TRUE(called);
    EXPECT_TRUE(text.empty());
    EXPECT_EQ(start, 0);
    EXPECT_EQ(length, 0);
}

TEST_F(TextInputEXTTest, TextEditingWithoutSubscriberIsSafe)
{
    TextInputEXT::TextEditing = nullptr;
    EXPECT_NO_THROW(TextInputEXT::INTERNAL_OnTextEditing("x", 0, 1));
}

// CNAEXT/EXT (input_noxna.md N-014): SDL3-new IME candidate list, no FNA counterpart to compare
// against. Dispatches the candidate strings, the pre-selected index, and the layout orientation.
TEST_F(TextInputEXTTest, TextEditingCandidatesDispatchesListSelectedAndHorizontal)
{
    std::vector<std::string> candidates;
    int selected = -2;
    bool horizontal = true;
    TextInputEXT::TextEditingCandidatesEXT = [&](const std::vector<std::string>& c, int s, bool h)
    {
        candidates = c;
        selected = s;
        horizontal = h;
    };

    TextInputEXT::INTERNAL_OnTextEditingCandidates({"a", "b", "c"}, 1, false);

    ASSERT_EQ(candidates.size(), std::size_t{3});
    EXPECT_EQ(candidates[0], "a");
    EXPECT_EQ(candidates[1], "b");
    EXPECT_EQ(candidates[2], "c");
    EXPECT_EQ(selected, 1);
    EXPECT_FALSE(horizontal);
}

TEST_F(TextInputEXTTest, TextEditingCandidatesIsMulticastAndDeliversToEverySubscriber)
{
    int calls1 = 0;
    int calls2 = 0;
    TextInputEXT::TextEditingCandidatesEXT += [&calls1](const std::vector<std::string>&, int, bool) { ++calls1; };
    TextInputEXT::TextEditingCandidatesEXT += [&calls2](const std::vector<std::string>&, int, bool) { ++calls2; };

    TextInputEXT::INTERNAL_OnTextEditingCandidates({"x"}, -1, true);

    EXPECT_EQ(calls1, 1);
    EXPECT_EQ(calls2, 1);
}

TEST_F(TextInputEXTTest, TextEditingCandidatesWithoutSubscriberIsSafe)
{
    EXPECT_NO_THROW(TextInputEXT::INTERNAL_OnTextEditingCandidates({}, -1, false));
}

// TextInputEXT::ResetForTests documents that it resets callbacks and window identity — verify the
// callback lists are actually cleared, not just the handle/id pair.
TEST_F(TextInputEXTTest, ResetForTestsClearsAllSubscriberLists)
{
    bool textInputCalled = false;
    bool textEditingCalled = false;
    bool textEditingCandidatesCalled = false;
    TextInputEXT::TextInput = [&](charcs) { textInputCalled = true; };
    TextInputEXT::TextEditing = [&](const std::string&, int, int) { textEditingCalled = true; };
    TextInputEXT::TextEditingCandidatesEXT = [&](const std::vector<std::string>&, int, bool) { textEditingCandidatesCalled = true; };

    TextInputEXT::ResetForTests();

    TextInputEXT::INTERNAL_OnTextInput(u'x');
    TextInputEXT::INTERNAL_OnTextEditing("draft", 0, 5);
    TextInputEXT::INTERNAL_OnTextEditingCandidates({"a"}, 0, false);

    EXPECT_FALSE(textInputCalled);
    EXPECT_FALSE(textEditingCalled);
    EXPECT_FALSE(textEditingCandidatesCalled);
}

TEST_F(TextInputEXTTest, WindowHandlePropertyRoundTrips)
{
    EXPECT_EQ(TextInputEXT::getWindowHandleProperty(), std::uintptr_t{0});

    const std::uintptr_t handle = 0xDEADBEEFu;
    TextInputEXT::setWindowHandleProperty(handle);
    EXPECT_EQ(TextInputEXT::getWindowHandleProperty(), handle);

    TextInputEXT::setWindowHandleProperty(0);
    EXPECT_EQ(TextInputEXT::getWindowHandleProperty(), std::uintptr_t{0});
}

// P8-005(c): the framework never leaves stale window identity behind. ResetForTests clears both
// halves, so a later raw handle cannot accidentally inherit the previous platform window id.
TEST_F(TextInputEXTTest, ResetForTestsClearsWindowHandleSoLaterCallsAreNullGuarded)
{
    PublishWindow();
    TextInputEXT::ResetForTests();
    EXPECT_EQ(TextInputEXT::getWindowHandleProperty(), std::uintptr_t{0});

    // Re-publishing only a raw handle must not retain the old id or reach the service.
    TextInputEXT::setWindowHandleProperty(Handle);
    EXPECT_NO_THROW(TextInputEXT::StartTextInput());
    EXPECT_NO_THROW(TextInputEXT::StopTextInput());
    EXPECT_FALSE(TextInputEXT::IsTextInputActive());
    EXPECT_EQ(platform.textInput.startCalls, 0);
    EXPECT_EQ(platform.textInput.stopCalls, 0);
}

TEST_F(TextInputEXTTest, IsTextInputActiveIsFalseWithoutWindow)
{
    // No published window id -> false without touching the platform service.
    EXPECT_FALSE(TextInputEXT::IsTextInputActive());
}

TEST_F(TextInputEXTTest, IsScreenKeyboardShownIsFalseWithoutWindow)
{
    EXPECT_FALSE(TextInputEXT::IsScreenKeyboardShown());
    EXPECT_FALSE(TextInputEXT::IsScreenKeyboardShown(0));
}

TEST_F(TextInputEXTTest, StartStopAndSetRectangleWithoutWindowAreSafeNoOps)
{
    // With no window identity every call is null-guarded, so none reach the service.
    EXPECT_NO_THROW(TextInputEXT::StartTextInput());
    EXPECT_NO_THROW(TextInputEXT::StopTextInput());
    EXPECT_NO_THROW(TextInputEXT::SetInputRectangle(Rectangle(0, 0, 10, 10)));
}

// N-014b: StartTextInputWithTypeEXT shares StartTextInput's null-window guard, for every hint value.
TEST_F(TextInputEXTTest, StartTextInputWithTypeWithoutWindowIsSafeNoOpForEveryType)
{
    using CNA::Input::TextInputTypeEXT;
    for (const TextInputTypeEXT type : {
             TextInputTypeEXT::Text, TextInputTypeEXT::TextName, TextInputTypeEXT::TextEmail,
             TextInputTypeEXT::TextUsername, TextInputTypeEXT::TextPasswordHidden,
             TextInputTypeEXT::TextPasswordVisible, TextInputTypeEXT::Number,
             TextInputTypeEXT::NumberPasswordHidden, TextInputTypeEXT::NumberPasswordVisible })
    {
        EXPECT_NO_THROW(TextInputEXT::StartTextInputWithTypeEXT(type));
    }
    EXPECT_FALSE(TextInputEXT::IsTextInputActive());
}

// P7-009(c): SetInputRectangle must not crash on zero-size or negative rectangle values. With no
// window the null guard makes every call a safe no-op.
TEST_F(TextInputEXTTest, SetInputRectangleWithZeroOrNegativeValuesIsSafe)
{
    EXPECT_NO_THROW(TextInputEXT::SetInputRectangle(Rectangle(0, 0, 0, 0)));
    EXPECT_NO_THROW(TextInputEXT::SetInputRectangle(Rectangle(-5, -5, 0, 0)));
    EXPECT_NO_THROW(TextInputEXT::SetInputRectangle(Rectangle(-10, -20, -30, -40)));
}

TEST_F(TextInputEXTTest, PlainStartStopAndActiveStateUseThePlatformService)
{
    PublishWindow();
    TextInputEXT::StartTextInput();
    EXPECT_EQ(platform.textInput.startCalls, 1);
    EXPECT_EQ(platform.textInput.lastWindow, Window);
    EXPECT_EQ(platform.textInput.lastType, CNA::Platform::TextInputType::Default);
    EXPECT_TRUE(TextInputEXT::IsTextInputActive());

    TextInputEXT::StopTextInput();
    EXPECT_EQ(platform.textInput.stopCalls, 1);
    EXPECT_EQ(platform.textInput.lastWindow, Window);
    EXPECT_FALSE(TextInputEXT::IsTextInputActive());
}

TEST_F(TextInputEXTTest, ScreenKeyboardQueriesOnlyThePublishedWindowPair)
{
    PublishWindow();
    platform.textInput.lastWindow = Window;
    platform.textInput.screenKeyboardShown = true;

    EXPECT_TRUE(TextInputEXT::IsScreenKeyboardShown());
    EXPECT_TRUE(TextInputEXT::IsScreenKeyboardShown(Handle));
    EXPECT_FALSE(TextInputEXT::IsScreenKeyboardShown(Handle + 1));
    EXPECT_FALSE(TextInputEXT::IsScreenKeyboardShown(0));
}

TEST_F(TextInputEXTTest, SetRectangleForwardsEveryCoordinateAndFnasZeroCursorOffset)
{
    PublishWindow();
    TextInputEXT::SetInputRectangle(Rectangle(-10, 20, 300, 40));

    ASSERT_EQ(platform.textInput.areaCalls, 1);
    EXPECT_EQ(platform.textInput.lastWindow, Window);
    EXPECT_EQ(platform.textInput.lastArea.x, -10);
    EXPECT_EQ(platform.textInput.lastArea.y, 20);
    EXPECT_EQ(platform.textInput.lastArea.width, 300);
    EXPECT_EQ(platform.textInput.lastArea.height, 40);
    EXPECT_EQ(platform.textInput.lastArea.cursorOffset, 0);
}

TEST_F(TextInputEXTTest, EveryExtensionTypeMapsToThePortableContract)
{
    using CNA::Input::TextInputTypeEXT;
    using CNA::Platform::TextInputType;
    const std::array mappings{
        std::pair{TextInputTypeEXT::Text, TextInputType::Text},
        std::pair{TextInputTypeEXT::TextName, TextInputType::TextName},
        std::pair{TextInputTypeEXT::TextEmail, TextInputType::TextEmail},
        std::pair{TextInputTypeEXT::TextUsername, TextInputType::TextUsername},
        std::pair{TextInputTypeEXT::TextPasswordHidden, TextInputType::TextPasswordHidden},
        std::pair{TextInputTypeEXT::TextPasswordVisible, TextInputType::TextPasswordVisible},
        std::pair{TextInputTypeEXT::Number, TextInputType::Number},
        std::pair{TextInputTypeEXT::NumberPasswordHidden, TextInputType::NumberPasswordHidden},
        std::pair{TextInputTypeEXT::NumberPasswordVisible, TextInputType::NumberPasswordVisible},
    };

    PublishWindow();
    for (const auto& [extension, expected] : mappings)
    {
        TextInputEXT::StartTextInputWithTypeEXT(extension);
        EXPECT_EQ(platform.textInput.lastWindow, Window);
        EXPECT_EQ(platform.textInput.lastType, expected);
    }
    EXPECT_EQ(platform.textInput.startCalls, static_cast<int>(mappings.size()));
}

TEST_F(TextInputEXTTest, MissingServiceMakesEveryLifecycleCallSafe)
{
    PublishWindow();
    platform.serviceAvailable = false;

    EXPECT_NO_THROW(TextInputEXT::StartTextInput());
    EXPECT_NO_THROW(TextInputEXT::StartTextInputWithTypeEXT(
        CNA::Input::TextInputTypeEXT::TextEmail));
    EXPECT_NO_THROW(TextInputEXT::StopTextInput());
    EXPECT_NO_THROW(TextInputEXT::SetInputRectangle(Rectangle(1, 2, 3, 4)));
    EXPECT_FALSE(TextInputEXT::IsTextInputActive());
    EXPECT_FALSE(TextInputEXT::IsScreenKeyboardShown());
}

TEST_F(TextInputEXTTest, VoidExtensionMethodsPreserveTheirNoThrowFailureContract)
{
    PublishWindow();
    platform.textInput.failCommands = true;

    EXPECT_NO_THROW(TextInputEXT::StartTextInput());
    EXPECT_NO_THROW(TextInputEXT::StartTextInputWithTypeEXT(
        CNA::Input::TextInputTypeEXT::Number));
    EXPECT_NO_THROW(TextInputEXT::StopTextInput());
    EXPECT_NO_THROW(TextInputEXT::SetInputRectangle(Rectangle(1, 2, 3, 4)));
}
