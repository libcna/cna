// SPDX-License-Identifier: MS-PL

#include "Sdl3TextInputTypes.hpp"

namespace CNA::Platform::Sdl3 {

    SDL_TextInputType ToSdlTextInputType(const TextInputType type)
    {
        switch (type)
        {
            case TextInputType::Default:
            case TextInputType::Text:                  return SDL_TEXTINPUT_TYPE_TEXT;
            case TextInputType::TextName:              return SDL_TEXTINPUT_TYPE_TEXT_NAME;
            case TextInputType::TextEmail:             return SDL_TEXTINPUT_TYPE_TEXT_EMAIL;
            case TextInputType::TextUsername:          return SDL_TEXTINPUT_TYPE_TEXT_USERNAME;
            case TextInputType::TextPasswordHidden:    return SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN;
            case TextInputType::TextPasswordVisible:   return SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_VISIBLE;
            case TextInputType::Number:                return SDL_TEXTINPUT_TYPE_NUMBER;
            case TextInputType::NumberPasswordHidden:  return SDL_TEXTINPUT_TYPE_NUMBER_PASSWORD_HIDDEN;
            case TextInputType::NumberPasswordVisible: return SDL_TEXTINPUT_TYPE_NUMBER_PASSWORD_VISIBLE;
        }
        return SDL_TEXTINPUT_TYPE_TEXT;
    }

} // namespace CNA::Platform::Sdl3
