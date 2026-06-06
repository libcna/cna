// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "Microsoft/Xna/Framework/Input/MouseCursor.hpp"

namespace Microsoft::Xna::Framework::Input
{
    MouseCursor MouseCursor::MakeSystem(SDL_SystemCursor id)
    {
        SDL_Cursor* c = SDL_CreateSystemCursor(id);
        return MouseCursor(c, /*owning=*/true);
    }

    // Static instances — created lazily on first program use via static initialisation.
    MouseCursor MouseCursor::Arrow      = MouseCursor::MakeSystem(SDL_SYSTEM_CURSOR_DEFAULT);
    MouseCursor MouseCursor::Crosshair  = MouseCursor::MakeSystem(SDL_SYSTEM_CURSOR_CROSSHAIR);
    MouseCursor MouseCursor::Hand       = MouseCursor::MakeSystem(SDL_SYSTEM_CURSOR_POINTER);
    MouseCursor MouseCursor::IBeam      = MouseCursor::MakeSystem(SDL_SYSTEM_CURSOR_TEXT);
    MouseCursor MouseCursor::No         = MouseCursor::MakeSystem(SDL_SYSTEM_CURSOR_NOT_ALLOWED);
    MouseCursor MouseCursor::SizeAll    = MouseCursor::MakeSystem(SDL_SYSTEM_CURSOR_MOVE);
    MouseCursor MouseCursor::SizeNESW   = MouseCursor::MakeSystem(SDL_SYSTEM_CURSOR_NESW_RESIZE);
    MouseCursor MouseCursor::SizeNS     = MouseCursor::MakeSystem(SDL_SYSTEM_CURSOR_NS_RESIZE);
    MouseCursor MouseCursor::SizeNWSE   = MouseCursor::MakeSystem(SDL_SYSTEM_CURSOR_NWSE_RESIZE);
    MouseCursor MouseCursor::SizeWE     = MouseCursor::MakeSystem(SDL_SYSTEM_CURSOR_EW_RESIZE);
    MouseCursor MouseCursor::Wait       = MouseCursor::MakeSystem(SDL_SYSTEM_CURSOR_WAIT);
    MouseCursor MouseCursor::WaitCursor = MouseCursor::MakeSystem(SDL_SYSTEM_CURSOR_PROGRESS);

    MouseCursor::MouseCursor()
        : sdlCursor_(SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT))
        , owning_(true)
    {
    }

    MouseCursor::MouseCursor(SDL_Cursor* sdlCursor, bool owning)
        : sdlCursor_(sdlCursor)
        , owning_(owning)
    {
    }

    MouseCursor::~MouseCursor()
    {
        if (owning_ && sdlCursor_ != nullptr)
        {
            SDL_DestroyCursor(sdlCursor_);
            sdlCursor_ = nullptr;
        }
    }
}
