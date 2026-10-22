// SPDX-License-Identifier: MS-PL
// Real SDL/D-Bus counterpart of tray_label_lifetime_test.cpp. Run only through
// run_gpu_tests_private.sh and tray_dbus_check.py inside a private session bus.
#include "../../modules/platform/src/Sdl3/Sdl3Tray.hpp"
#include <SDL3/SDL.h>
#include <chrono>
#include <cstdio>
#include <exception>
#include <string>

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    int result = 1;
    try {
        CNA::Platform::Sdl3::Sdl3Tray service;
        auto tray = service.CreateTray("CNA private tray regression");
        bool clicked = false, done = false;
        tray->AddEntry(std::string("Say hello"), false, false, true, [&] { clicked = true; });
        tray->AddEntry(std::string(256, 'n'), true, false, true, [&] { done = true; });
        tray->SetEntryLabel(1, std::string("Night mode"));
        for (int i = 0; i < 40; ++i)
            tray->AddEntry("Entry " + std::to_string(i), false, false, true, {});
        // Reuse caller allocations before the real host reads the menu.
        std::string churn(256, '!');
        std::puts("READY"); std::fflush(stdout);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!done && std::chrono::steady_clock::now() < deadline) {
            SDL_Event event;
            SDL_WaitEventTimeout(&event, 10);
            SDL_UpdateTrays();
            if (clicked) {
                clicked = false;
                tray->SetEntryLabel(1, std::string("Updated night mode"));
                std::puts("UPDATED"); std::fflush(stdout);
            }
        }
        result = done ? 0 : 1;
        tray.reset();
    } catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); }
    SDL_Quit();
    return result;
}
