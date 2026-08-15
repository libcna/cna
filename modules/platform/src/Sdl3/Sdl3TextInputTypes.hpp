// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformTextInput.hpp"

#include <SDL3/SDL.h>

namespace CNA::Platform::Sdl3 {

    /** @brief Maps CNA's portable text-purpose hint to SDL3's corresponding value. */
    [[nodiscard]] SDL_TextInputType ToSdlTextInputType(TextInputType type);

} // namespace CNA::Platform::Sdl3
