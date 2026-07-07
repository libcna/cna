// SPDX-License-Identifier: MS-PL
#ifdef CNA_DEVICES

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "CNA/Devices/Detail/ITrayBackend.hpp"
#include "CNA/Devices/SystemTray.hpp"

using CNA::Devices::SystemTray;
using CNA::Devices::Detail::ITrayBackend;
using CNA::Devices::Detail::TrayEntryClickCallback;

namespace
{
    // Task DEVICES-CNA-009: the real backend (Detail::SdlTrayBackend) creates a
    // genuine, visible tray icon on the real desktop the moment Create() is called --
    // learned directly from DEVICES-CNA-008's real zenity incident, every test here
    // uses this fake instead, never the real backend. SystemTray's test-only
    // constructor overload takes this fake *before* any Create() call happens, unlike
    // a post-construction SetBackendForTesting() swap (see SystemTray.hpp's own doc
    // comment for why that ordering matters here specifically).
    class FakeTrayBackend final : public ITrayBackend
    {
    public:
        bool CreateCalled = false;
        std::string LastCreateTooltip;

        bool DestroyCalled = false;

        // Task DEVICES-CNA-009: SystemTray owns its backend directly (unlike
        // FileDialog's process-wide swappable backend), so once the owning
        // SystemTray is destroyed, this FakeTrayBackend is destroyed with it --
        // reading DestroyCalled through a dangling raw pointer afterward is a real
        // use-after-free (caught by ASan during this task's own development, not
        // hypothetical). Tests that need to observe destruction after the SystemTray
        // itself has gone out of scope must use this externally-owned flag instead
        // of the member above.
        std::shared_ptr<bool> DestroyedFlag;

        int SetTooltipCallCount = 0;
        std::string LastTooltip;

        struct Entry
        {
            std::string Label;
            bool Checkable = false;
            bool Checked = false;
            bool Enabled = true;
            TrayEntryClickCallback OnClick;
        };
        std::vector<Entry> Entries;

        void Create(const std::string& tooltip) override
        {
            CreateCalled = true;
            LastCreateTooltip = tooltip;
        }

        void Destroy() override
        {
            DestroyCalled = true;
            if (DestroyedFlag)
            {
                *DestroyedFlag = true;
            }
        }

        void SetTooltip(const std::string& tooltip) override
        {
            ++SetTooltipCallCount;
            LastTooltip = tooltip;
        }

        std::size_t AddEntry(
            const std::string& label,
            bool checkable,
            bool initiallyChecked,
            bool initiallyEnabled,
            TrayEntryClickCallback onClick) override
        {
            Entries.push_back(Entry{label, checkable, initiallyChecked, initiallyEnabled, std::move(onClick)});
            return Entries.size() - 1;
        }

        void SetEntryLabel(std::size_t index, const std::string& label) override
        {
            if (index < Entries.size())
            {
                Entries[index].Label = label;
            }
        }

        void SetEntryChecked(std::size_t index, bool checked) override
        {
            if (index < Entries.size())
            {
                Entries[index].Checked = checked;
            }
        }

        [[nodiscard]] bool GetEntryChecked(std::size_t index) const override
        {
            return index < Entries.size() && Entries[index].Checked;
        }

        void SetEntryEnabled(std::size_t index, bool enabled) override
        {
            if (index < Entries.size())
            {
                Entries[index].Enabled = enabled;
            }
        }

        [[nodiscard]] bool GetEntryEnabled(std::size_t index) const override
        {
            return index < Entries.size() && Entries[index].Enabled;
        }
    };
} // namespace

TEST(SystemTrayTests, GetIsSupportedPropertyDoesNotCrash)
{
    EXPECT_NO_THROW({ (void)SystemTray::getIsSupportedProperty(); });
}

TEST(SystemTrayTests, ConstructorCallsCreateOnTheInjectedFakeBackend)
{
    auto fakeOwned = std::make_unique<FakeTrayBackend>();
    FakeTrayBackend* fake = fakeOwned.get();

    SystemTray tray("my tooltip", std::move(fakeOwned));

    EXPECT_TRUE(fake->CreateCalled);
    EXPECT_EQ(fake->LastCreateTooltip, "my tooltip");
    EXPECT_FALSE(fake->DestroyCalled);
}

TEST(SystemTrayTests, DestructorCallsDestroyOnTheInjectedFakeBackend)
{
    auto fakeOwned = std::make_unique<FakeTrayBackend>();
    FakeTrayBackend* fake = fakeOwned.get();

    // SystemTray owns the backend directly, so it (and this FakeTrayBackend) is
    // destroyed when `tray` goes out of scope below -- `destroyed` is a separate
    // heap allocation this test retains its own reference to, specifically so it
    // remains safe to read after that happens (see FakeTrayBackend::DestroyedFlag's
    // own doc comment for the real use-after-free this replaced).
    auto destroyed = std::make_shared<bool>(false);
    fake->DestroyedFlag = destroyed;

    {
        SystemTray tray("my tooltip", std::move(fakeOwned));
        EXPECT_FALSE(*destroyed);
    }

    EXPECT_TRUE(*destroyed);
}

TEST(SystemTrayTests, SetTooltipPropertyForwardsToBackend)
{
    auto fakeOwned = std::make_unique<FakeTrayBackend>();
    FakeTrayBackend* fake = fakeOwned.get();
    SystemTray tray("initial", std::move(fakeOwned));

    tray.setTooltipProperty("updated");

    EXPECT_EQ(fake->SetTooltipCallCount, 1);
    EXPECT_EQ(fake->LastTooltip, "updated");
}

TEST(SystemTrayTests, AddEntryForwardsParametersAndReturnsIncrementingIndices)
{
    auto fakeOwned = std::make_unique<FakeTrayBackend>();
    FakeTrayBackend* fake = fakeOwned.get();
    SystemTray tray("tooltip", std::move(fakeOwned));

    const std::size_t firstIndex = tray.AddEntry("Show", false, false, true, [] {});
    const std::size_t secondIndex = tray.AddEntry("Enabled", true, true, true, [] {});

    EXPECT_EQ(firstIndex, 0u);
    EXPECT_EQ(secondIndex, 1u);
    ASSERT_EQ(fake->Entries.size(), 2u);
    EXPECT_EQ(fake->Entries[0].Label, "Show");
    EXPECT_FALSE(fake->Entries[0].Checkable);
    EXPECT_TRUE(fake->Entries[1].Checkable);
    EXPECT_TRUE(fake->Entries[1].Checked);
}

TEST(SystemTrayTests, EntryClickCallbackFiresThroughTheBackend)
{
    auto fakeOwned = std::make_unique<FakeTrayBackend>();
    FakeTrayBackend* fake = fakeOwned.get();
    SystemTray tray("tooltip", std::move(fakeOwned));

    bool clicked = false;
    tray.AddEntry("Quit", false, false, true, [&clicked] { clicked = true; });

    ASSERT_EQ(fake->Entries.size(), 1u);
    ASSERT_TRUE(static_cast<bool>(fake->Entries[0].OnClick));
    fake->Entries[0].OnClick();

    EXPECT_TRUE(clicked);
}

TEST(SystemTrayTests, SetAndGetEntryCheckedRoundTrip)
{
    auto fakeOwned = std::make_unique<FakeTrayBackend>();
    SystemTray tray("tooltip", std::move(fakeOwned));

    const std::size_t index = tray.AddEntry("Toggle", true, false, true, [] {});
    EXPECT_FALSE(tray.GetEntryChecked(index));

    tray.SetEntryChecked(index, true);
    EXPECT_TRUE(tray.GetEntryChecked(index));
}

TEST(SystemTrayTests, SetAndGetEntryEnabledRoundTrip)
{
    auto fakeOwned = std::make_unique<FakeTrayBackend>();
    SystemTray tray("tooltip", std::move(fakeOwned));

    const std::size_t index = tray.AddEntry("Action", false, false, true, [] {});
    EXPECT_TRUE(tray.GetEntryEnabled(index));

    tray.SetEntryEnabled(index, false);
    EXPECT_FALSE(tray.GetEntryEnabled(index));
}

#endif // CNA_DEVICES
