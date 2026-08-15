// SPDX-License-Identifier: MS-PL
#pragma once

namespace CNA::Internal::Renderers::Fna3d::Detail
{
    /**
     * @brief CNAEXT. Runs FNA3D's pre-window driver selection and reports whether it needs OpenGL.
     *
     * FNA3D chooses its driver at runtime and reports the window flags that driver needs through
     * `FNA3D_PrepareWindowAttributes()` -- `SDL_WINDOW_OPENGL` for the OpenGL driver, none for
     * SDL_GPU or Direct3D 11. That call must happen *before* `SDL_CreateWindow`, because it also
     * primes the GL attributes the visual is chosen from, so `GraphicsDevice` asks for it while
     * assembling the window flags (the same shape LLGL, Diligent and bgfx already use for their
     * own runtime-selected APIs).
     *
     * Declared in a header that pulls in neither SDL nor FNA3D so the graphics module can call it
     * without taking a dependency on either.
     *
     * @return True when the selected FNA3D driver needs an OpenGL-capable window.
     */
    [[nodiscard]] bool PrepareWindowNeedsOpenGl();
}
