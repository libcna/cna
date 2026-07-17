#pragma once

namespace CNA
{
    /**
     * @brief Controls what happens when code calls a graphics operation the active backend
     *        doesn't support (e.g. any 3D draw/state call on the 2D-only SDL_Renderer backend).
     *
     * Only applies to "fire and forget" state/draw calls with no meaningful return value
     * (clears, state toggles, draws) -- resource-creation calls (CreateVertexBuffer and
     * similar) always throw regardless of this setting, since silently handing back an unusable
     * null-backed resource is a worse failure mode than an immediate, honest exception.
     */
    enum class UnsupportedGraphicsCallBehavior
    {
        /** @brief Throw std::runtime_error (the default -- matches CNA's established behavior). */
        Throw,

        /** @brief Silently do nothing instead of throwing. */
        Ignore
    };
} // CNA
