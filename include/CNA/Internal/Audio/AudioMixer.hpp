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
    void DestroyMixer();
}
#endif
