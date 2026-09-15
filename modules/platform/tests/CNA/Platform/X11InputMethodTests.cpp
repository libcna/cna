// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0152: input-method composition through XIM, against a real input method.
//
// Runs only under `tools/platform/x11_test_server.sh --with-ibus`, which starts a private Xvfb and
// a private ibus with its XIM server on it -- nothing shared with a developer's own session. Keys
// are typed with xdotool, from outside the process, exactly as a keyboard would; the composition
// is a dead key -- dead_acute, then e, composing é -- which ibus's core engine composes on every
// layout, with no language engine or dictionary.

#include <gtest/gtest.h>

#include "../../../src/X11/X11Headers.hpp"
#include "../../../src/X11/X11Platform.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "System/Environment.hpp"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace {

using namespace CNA::Platform;

bool Xdotool(const std::string& arguments)
{
    return std::system(("xdotool " + arguments + " >/dev/null 2>&1").c_str()) == 0;
}

/// Sets an environment variable for one scope, restoring what was there.
class ScopedEnvironment
{
public:
    ScopedEnvironment(std::string name, const std::optional<std::string>& value)
        : name_(std::move(name)), saved_(System::Environment::GetEnvironmentVariable(name_))
    {
        System::Environment::SetEnvironmentVariable(name_, value);
    }
    ~ScopedEnvironment() { System::Environment::SetEnvironmentVariable(name_, saved_); }
    ScopedEnvironment(const ScopedEnvironment&) = delete;
    ScopedEnvironment& operator=(const ScopedEnvironment&) = delete;

private:
    std::string name_;
    std::optional<std::string> saved_;
};

class X11InputMethod : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const char* ibus = std::getenv("CNA_X11_TEST_IBUS");
        if (ibus == nullptr || std::string(ibus) != "1")
        {
            GTEST_SKIP() << "needs tools/platform/x11_test_server.sh --with-ibus";
        }
        if (std::system("command -v xdotool >/dev/null 2>&1") != 0 ||
            std::system("command -v setxkbmap >/dev/null 2>&1") != 0)
        {
            GTEST_SKIP() << "xdotool or setxkbmap is not installed";
        }
        // A layout with a real dead key. xdotool can type a keysym the keymap lacks only by
        // remapping a spare keycode for an instant, and the input method -- which resolves keys
        // through its own view of the keymap -- misses that. US International has dead_acute on
        // the apostrophe key. This server is the launcher's own (CNA_X11_PRIVATE_TEST_SERVER), so
        // changing its keymap touches no one.
        const char* privateServer = std::getenv("CNA_X11_PRIVATE_TEST_SERVER");
        if (privateServer == nullptr || std::string(privateServer) != "1" ||
            std::system("setxkbmap -layout us -variant intl >/dev/null 2>&1") != 0)
        {
            GTEST_SKIP() << "could not give the private server a layout with dead keys";
        }
        // The input method picks the new keymap up from the server's MappingNotify.
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    void TearDown() override
    {
        window_.reset();
        if (platform_ != nullptr)
        {
            platform_->ReleaseSubsystem(PlatformSubsystem::Video);
        }
        platform_.reset();
    }

    /// Creates the platform -- after the environment for it is set -- and a focused window.
    void Open()
    {
        platform_ = std::make_unique<X11::X11Platform>();
        platform_->AcquireSubsystem(PlatformSubsystem::Video);
        WindowDescription description;
        description.title = "CNA input method test";
        description.width = 320;
        description.height = 200;
        window_ = platform_->CreateWindow(description);
        window_->Show();
        window_->Sync();
        // No window manager on this server: focus is set directly, as the launcher's other
        // suites do.
        ::Display* display = XOpenDisplay(nullptr);
        ASSERT_NE(display, nullptr);
        XSetInputFocus(display, static_cast<::Window>(window_->GetWindowHandle()), RevertToParent,
                       CNA::Platform::X11::kCurrentTime);
        XSync(display, CNA::Platform::X11::kXFalse);
        XCloseDisplay(display);
        ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return window_->HasFocus(); }))
            << "the window never received focus";
        seen_.clear();
    }

    bool PumpUntil(const std::function<bool(const std::vector<PlatformEvent>&)>& done,
                   const std::chrono::milliseconds budget = std::chrono::milliseconds(3000))
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        std::vector<PlatformEvent> batch;
        while (std::chrono::steady_clock::now() < deadline)
        {
            platform_->PollEvents(batch);
            seen_.insert(seen_.end(), batch.begin(), batch.end());
            if (done(seen_))
            {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    /// Everything that arrives for a while, when the point is what does NOT arrive.
    void Drain(const std::chrono::milliseconds period = std::chrono::milliseconds(600))
    {
        (void) PumpUntil([](const std::vector<PlatformEvent>&) { return false; }, period);
    }

    [[nodiscard]] std::vector<TextEditingEvent> Editing() const
    {
        std::vector<TextEditingEvent> result;
        for (const PlatformEvent& event : seen_)
        {
            if (const auto* editing = std::get_if<TextEditingEvent>(&event))
            {
                result.push_back(*editing);
            }
        }
        return result;
    }

    [[nodiscard]] std::string Committed() const
    {
        std::string text;
        for (const PlatformEvent& event : seen_)
        {
            if (const auto* input = std::get_if<TextInputEvent>(&event))
            {
                text += input->text;
            }
        }
        return text;
    }

    std::unique_ptr<IPlatform> platform_;
    std::unique_ptr<IPlatformWindow> window_;
    std::vector<PlatformEvent> seen_;
};

TEST_F(X11InputMethod, AnApplicationThatDrawsTheCompositionReceivesIt)
{
    const ScopedEnvironment ui("CNA_IME_IMPLEMENTED_UI", std::string("composition"));
    Open();
    ASSERT_TRUE(platform_->GetCapabilities().ime)
        << "ibus offers on-the-spot composition; the backend should have negotiated it";
    platform_->GetTextInput()->Start(window_->GetId(), TextInputType::Text);

    ASSERT_TRUE(Xdotool("key dead_acute"));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        return !Editing().empty() && !Editing().back().text.empty();
    })) << "no composition arrived for the dead key";
    const TextEditingEvent composing = Editing().back();
    EXPECT_EQ(composing.window, window_->GetId());
    EXPECT_LE(composing.cursor, static_cast<int>(composing.text.size()));
    EXPECT_EQ(Committed(), "") << "nothing is committed while composing";

    ASSERT_TRUE(Xdotool("key e"));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return Committed() == "\xC3\xA9"; }))
        << "é was not committed; committed so far: '" << Committed() << "'";
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        return !Editing().empty() && Editing().back().text.empty();
    })) << "the composition never ended";

    // The composing keys were the input method's, not the game's: no key event carries the
    // input method's commit as a press of keycode 0, and nothing arrived for "no key".
    for (const PlatformEvent& event : seen_)
    {
        if (const auto* key = std::get_if<KeyEvent>(&event))
        {
            EXPECT_NE(key->scancode, Scancode::Unknown) << "a key event for no key";
        }
    }
}

TEST_F(X11InputMethod, ByDefaultTheInputMethodDrawsItsOwnCompositionAndOnlyTheCommitArrives)
{
    // What an XNA game gets: it draws no composition, so the input method draws it, and the game
    // sees the committed text alone.
    const ScopedEnvironment ui("CNA_IME_IMPLEMENTED_UI", std::nullopt);
    Open();
    EXPECT_FALSE(platform_->GetCapabilities().ime);
    platform_->GetTextInput()->Start(window_->GetId(), TextInputType::Text);

    ASSERT_TRUE(Xdotool("key dead_acute e"));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return Committed() == "\xC3\xA9"; }))
        << "é was not committed; committed so far: '" << Committed() << "'";
    Drain(std::chrono::milliseconds(300));
    EXPECT_TRUE(Editing().empty()) << "composition reached an application that did not ask for it";
}

TEST_F(X11InputMethod, OutsideTextEntryTheInputMethodTakesNoKeys)
{
    // With text input stopped the keys are the game's: the input method must not be composing
    // them. Before X11-0152 the input context had focus whenever its window did, so an IME in a
    // composing mode swallowed keys a game was using as keys.
    const ScopedEnvironment ui("CNA_IME_IMPLEMENTED_UI", std::string("composition"));
    Open();
    ASSERT_FALSE(platform_->GetTextInput()->IsActive(window_->GetId()));

    ASSERT_TRUE(Xdotool("key dead_acute e"));
    bool sawE = false;
    ASSERT_TRUE(PumpUntil([&sawE](const std::vector<PlatformEvent>& events) {
        for (const PlatformEvent& event : events)
        {
            if (const auto* key = std::get_if<KeyEvent>(&event);
                key != nullptr && key->pressed && key->scancode == Scancode::E)
            {
                sawE = true;
            }
        }
        return sawE;
    })) << "the e was taken by the input method instead of arriving as a key; keys seen: " << [this] {
        std::string keys;
        for (const PlatformEvent& event : seen_)
        {
            if (const auto* key = std::get_if<KeyEvent>(&event))
            {
                keys += std::string(key->pressed ? "+" : "-") + std::to_string(static_cast<int>(key->scancode)) + " ";
            }
        }
        return keys;
    }();
    Drain(std::chrono::milliseconds(300));
    EXPECT_TRUE(Editing().empty());
    EXPECT_EQ(Committed(), "");
}

TEST_F(X11InputMethod, StoppingTextInputAbandonsACompositionInProgress)
{
    const ScopedEnvironment ui("CNA_IME_IMPLEMENTED_UI", std::string("composition"));
    Open();
    platform_->GetTextInput()->Start(window_->GetId(), TextInputType::Text);
    ASSERT_TRUE(Xdotool("key dead_acute"));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        return !Editing().empty() && !Editing().back().text.empty();
    }));

    platform_->GetTextInput()->Stop(window_->GetId());
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) {
        return Editing().back().text.empty();
    })) << "the application was left with half a composition";

    // And nothing of it survives into the next composition: an e typed after text input starts
    // again is an e, not the é the abandoned dead key would have made of it.
    platform_->GetTextInput()->Start(window_->GetId(), TextInputType::Text);
    ASSERT_TRUE(Xdotool("key e"));
    ASSERT_TRUE(PumpUntil([this](const std::vector<PlatformEvent>&) { return !Committed().empty(); }));
    EXPECT_EQ(Committed(), "e");
}

} // namespace
