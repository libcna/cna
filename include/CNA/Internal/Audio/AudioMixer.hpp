// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef SOUND_ENABLED
#include <SDL3/SDL.h>
#include <SDL3_mixer/SDL_mixer.h>

namespace CNA::Internal::Audio
{
    /// Returns the shared SDL3_mixer device, creating it on first call.
    MIX_Mixer* GetMixer();

    /// Destroys the shared SDL3_mixer device. Call at program exit only.
    ///
    /// P9-AUDIT-003: nothing in this codebase calls this today -- the MIX_Init()/MIX_Quit()
    /// refcount and the SDL audio device are never explicitly torn down during normal program
    /// lifetime, only reclaimed by the OS on process exit. Not dangerous in practice, but a
    /// future caller wiring real shutdown (e.g. into Game's dispose path) should know this isn't
    /// already hooked up anywhere.
    void DestroyMixer();
}
#endif
