#pragma once

namespace CNA::Internal::Renderers
{
    /**
     * @brief Owns a renderer context on the calling thread for one bounded operation.
     *
     * Native GL renderers use this internal lease to serialize a complete content decode against
     * frame rendering while moving their context between threads. Other renderer families return
     * no lease because their APIs either support concurrent resource creation directly or own a
     * different synchronization boundary.
     *
     * Complete in its own header because `GraphicsResource` holds one in a `std::unique_ptr`
     * inside a template: libc++'s C++23 `unique_ptr` is constexpr, so its deleter is instantiated
     * where the template is, and a forward declaration fails there.
     */
    class IRendererThreadContextLease
    {
    public:
        /** @brief Releases the calling thread's renderer context ownership. */
        virtual ~IRendererThreadContextLease() = default;
    };

    /** @brief Selects how an outer renderer-thread context lease handles its own prior binding. */
    enum class RendererThreadContextLeaseRelease : int
    {
        /** @brief Restore the binding that was current before the bounded operation. */
        RestorePreviousBinding,
        /** @brief Release this renderer's own prior binding so another thread can acquire it. */
        ReleaseRendererBinding
    };
}
