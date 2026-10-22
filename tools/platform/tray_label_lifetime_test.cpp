// SPDX-License-Identifier: MS-PL
// A dedicated executable links the real Sdl3Tray.cpp against a retaining SDL seam.
// The Unix dbusmenu implementation retains labels rather than copying them. This
// seam proves CNA's ownership deterministically without a desktop or session bus;
// it is not evidence of a real StatusNotifier host integration run.
#include "../../modules/platform/src/Sdl3/Sdl3Tray.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

struct SDL_TrayEntry {
    const char* label{};
    SDL_TrayCallback callback{};
    void* userdata{};
    bool checked{}, enabled{};
};
struct SDL_TrayMenu { std::vector<std::unique_ptr<SDL_TrayEntry>> entries; };
struct SDL_Tray { SDL_TrayMenu menu; };
namespace {
SDL_Tray* nativeTray{};
bool failInsert{};
}

extern "C" {
SDL_Tray* SDLCALL SDL_CreateTray(SDL_Surface*, const char*) {
    nativeTray = new SDL_Tray;
    return nativeTray;
}
SDL_TrayMenu* SDLCALL SDL_CreateTrayMenu(SDL_Tray* tray) { return &tray->menu; }
void SDLCALL SDL_DestroyTray(SDL_Tray* tray) { delete tray; nativeTray = nullptr; }
void SDLCALL SDL_SetTrayTooltip(SDL_Tray*, const char*) {}
SDL_TrayEntry* SDLCALL SDL_InsertTrayEntryAt(SDL_TrayMenu* menu, int, const char* label, SDL_TrayEntryFlags flags) {
    if (failInsert) { SDL_SetError("injected insertion failure"); return nullptr; }
    auto entry = std::make_unique<SDL_TrayEntry>();
    entry->label = label;
    entry->checked = (flags & SDL_TRAYENTRY_CHECKED) != 0;
    entry->enabled = (flags & SDL_TRAYENTRY_DISABLED) == 0;
    auto* result = entry.get();
    menu->entries.push_back(std::move(entry));
    return result;
}
void SDLCALL SDL_SetTrayEntryLabel(SDL_TrayEntry* entry, const char* label) { entry->label = label; }
void SDLCALL SDL_SetTrayEntryCallback(SDL_TrayEntry* entry, SDL_TrayCallback callback, void* userdata) {
    entry->callback = callback; entry->userdata = userdata;
}
void SDLCALL SDL_SetTrayEntryChecked(SDL_TrayEntry* entry, bool checked) { entry->checked = checked; }
bool SDLCALL SDL_GetTrayEntryChecked(SDL_TrayEntry* entry) { return entry->checked; }
void SDLCALL SDL_SetTrayEntryEnabled(SDL_TrayEntry* entry, bool enabled) { entry->enabled = enabled; }
bool SDLCALL SDL_GetTrayEntryEnabled(SDL_TrayEntry* entry) { return entry->enabled; }
}

TEST(Sdl3TrayLabelLifetimeTest, AddedLabelDoesNotBorrowTheCallersStorage) {
    for (const std::size_t size : {std::size_t{5}, std::size_t{256}}) {
        std::string label(size, 'x'); // Outlives the tray even in the failing pre-fix case.
        CNA::Platform::Sdl3::Sdl3Tray service;
        auto tray = service.CreateTray("test");
        EXPECT_EQ(tray->AddEntry(label, false, false, true, {}), 0u);
        const char* retained = nativeTray->menu.entries[0]->label;
        EXPECT_NE(retained, label.c_str());
        label[0] = '!';
        EXPECT_EQ(std::string(retained), std::string(size, 'x'));
    }
}

TEST(Sdl3TrayLabelLifetimeTest, ReplacedLabelDoesNotBorrowTheCallersStorage) {
    std::string original = "original", replacement(256, 'y');
    CNA::Platform::Sdl3::Sdl3Tray service;
    auto tray = service.CreateTray("test");
    tray->AddEntry(original, false, false, true, {});
    tray->SetEntryLabel(0, replacement);
    const char* retained = nativeTray->menu.entries[0]->label;
    EXPECT_NE(retained, replacement.c_str());
    replacement[0] = '!';
    EXPECT_EQ(std::string(retained), std::string(256, 'y'));
    tray->SetEntryLabel(99, "ignored");
    EXPECT_EQ(nativeTray->menu.entries.size(), 1u);
}

TEST(Sdl3TrayLabelLifetimeTest, TemporaryLabelsSurviveGrowthUpdatesAndCallbacks) {
    CNA::Platform::Sdl3::Sdl3Tray service;
    auto tray = service.CreateTray("test");
    int clicks = 0;
    std::vector<std::string> expected;
    for (int i = 0; i < 128; ++i) {
        std::string label = "Entry " + std::to_string(i);
        expected.push_back(label);
        ASSERT_EQ(tray->AddEntry(label, true, false, true, [&] { ++clicks; }), static_cast<std::size_t>(i));
        // Stop the pre-fix witness before destroying borrowed storage; failures need no UB.
        ASSERT_NE(nativeTray->menu.entries.back()->label, label.c_str());
    }
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(std::string(nativeTray->menu.entries[i]->label), expected[i]);
        tray->SetEntryLabel(i, std::string(128, 'z') + std::to_string(i));
        EXPECT_EQ(std::string(nativeTray->menu.entries[i]->label), std::string(128, 'z') + std::to_string(i));
        tray->SetEntryLabel(i, std::string("short"));
        EXPECT_STREQ(nativeTray->menu.entries[i]->label, "short");
        const auto& entry = nativeTray->menu.entries[i];
        entry->callback(entry->userdata, entry.get());
    }
    EXPECT_EQ(clicks, 128);
    tray.reset();
    EXPECT_EQ(nativeTray, nullptr);
}

TEST(Sdl3TrayLabelLifetimeTest, FailedInsertionDoesNotConsumeAnEntryIndex) {
    CNA::Platform::Sdl3::Sdl3Tray service;
    auto tray = service.CreateTray("test");
    failInsert = true;
    EXPECT_THROW(tray->AddEntry("refused", false, false, true, {}), CNA::Platform::PlatformException);
    failInsert = false;
    EXPECT_TRUE(nativeTray->menu.entries.empty());
    EXPECT_EQ(tray->AddEntry("accepted", false, false, true, {}), 0u);
    EXPECT_EQ(nativeTray->menu.entries.size(), 1u);
}
