// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

union SDL_Event;

namespace CNA::Platform::Sdl3 {

    /**
     * @brief Translates one SDL event into CNA's own event vocabulary.
     *
     * Kept as a free function rather than a method so it can be exercised directly against
     * synthetic events: SDL's own `SDL_PushEvent` lets the tests drive every branch without a
     * user, a device or a display server.
     *
     * Not every SDL event has a CNA counterpart — SDL emits many CNA has no use for — and
     * silently dropping those is correct rather than a gap. The return value distinguishes
     * "translated" from "deliberately ignored" so `PollEvents` never appends a default-
     * constructed event for something it did not understand.
     *
     * @param source The SDL event to translate.
     * @param destination Receives the translated event; untouched when this returns false.
     * @return True if @p source maps onto a CNA event.
     */
    [[nodiscard]] bool MapSdlEvent(const SDL_Event& source, PlatformEvent& destination);

} // namespace CNA::Platform::Sdl3
