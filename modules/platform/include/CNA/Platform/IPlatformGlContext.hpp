// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

#include <string>

namespace CNA::Platform {

    /** @brief Which OpenGL profile a context is created for. */
    enum class GlProfile
    {
        /** @brief Desktop OpenGL core profile: no deprecated fixed-function entry points. */
        Core,
        /** @brief Desktop OpenGL compatibility profile: fixed-function retained. */
        Compatibility,
        /** @brief OpenGL ES. */
        Es
    };

    /**
     * @brief The attributes an OpenGL context is created with.
     *
     * Every field corresponds to an attribute CNA's GL renderer families actually request today;
     * the set was taken from their measured `SDL_GL_*` usage rather than from the full attribute
     * list a GL loader could expose. A dropped attribute here is not a compile error — it is a
     * silent rendering difference — which is why the set is grounded rather than guessed.
     */
    struct GlContextDescription
    {
        /** @brief Requested major version. */
        int majorVersion = 3;
        /** @brief Requested minor version. */
        int minorVersion = 3;
        /** @brief Requested profile. */
        GlProfile profile = GlProfile::Core;

        /** @brief Requested depth buffer size in bits; zero for none. */
        int depthBits = 24;
        /** @brief Requested stencil buffer size in bits; zero for none. */
        int stencilBits = 8;

        /** @brief Number of multisample buffers; zero disables multisampling. */
        int multisampleBuffers = 0;
        /** @brief Samples per multisample buffer; ignored when @ref multisampleBuffers is zero. */
        int multisampleSamples = 0;

        /** @brief Whether to request a double-buffered visual. */
        bool doubleBuffer = true;
    };

    /** @brief An opaque handle to a created OpenGL context. Null means "no context". */
    using GlContextHandle = void*;

    /**
     * @brief The OpenGL context binding active on the calling thread.
     *
     * OpenGL context ownership is thread-affine. This snapshot lets framework code temporarily
     * bind another device and then restore the caller's original binding without naming the
     * native window toolkit.
     */
    struct GlContextBinding
    {
        /** @brief The stable id of the window associated with the context, or zero when unbound. */
        WindowId window = 0;
        /** @brief The current context, or null when the calling thread has no current context. */
        GlContextHandle context = nullptr;
    };

    /** @brief C-compatible OpenGL entry-point loader accepted by GL helper libraries. */
    using GlProcAddressLoader = void* (*)(const char* name);

    /**
     * @brief Creates and manages OpenGL contexts for windows.
     *
     * Renderer families use this narrow service instead of importing a native window toolkit.
     * EasyGL is the first migrated family; the same contract is intentionally reusable by the
     * remaining context-backed renderers rather than duplicated as per-renderer plumbing.
     *
     * Gated by the `OpenGlContext` capability.
     */
    class IPlatformGlContext
    {
    public:
        /** @brief Destroys the service. Any contexts it still owns are destroyed with it. */
        virtual ~IPlatformGlContext() = default;

        /**
         * @brief Creates an OpenGL context for a window.
         *
         * The window must have been created with `WindowRenderIntent::OpenGl`: the native
         * attribute has to be set at window-creation time and cannot be applied afterwards.
         *
         * @param window The stable id of the platform window to create the context for.
         * @param description The requested attributes.
         * @return A non-null context handle.
         * @throws PlatformNotSupportedException If the platform reports no `OpenGlContext` capability.
         * @throws PlatformException If context creation failed.
         */
        [[nodiscard]] virtual GlContextHandle CreateContext(WindowId window,
                                                           const GlContextDescription& description) = 0;

        /**
         * @brief Destroys a context previously created by this service.
         *
         * @param context The context to destroy. Passing null is a no-op.
         */
        virtual void DestroyContext(GlContextHandle context) = 0;

        /**
         * @brief Makes a context current on the calling thread.
         *
         * @param window The stable id of the platform window to bind the context to.
         * @param context The context to make current, or null to unbind.
         * @throws PlatformException If the context could not be made current.
         */
        virtual void MakeCurrent(WindowId window, GlContextHandle context) = 0;

        /**
         * @brief Gets the OpenGL context binding active on the calling thread.
         *
         * @return The current window/context pair, or an empty binding when no context is current.
         */
        [[nodiscard]] virtual GlContextBinding GetCurrentBinding() const = 0;

        /**
         * @brief Presents the back buffer of a window's current context.
         *
         * @param window The stable id of the platform window to swap.
         */
        virtual void SwapBuffers(WindowId window) = 0;

        /**
         * @brief Sets the swap interval for the current context.
         *
         * @param interval 0 for no synchronisation, 1 for vsync, -1 for adaptive vsync.
         * @return True if the interval was applied; false if the platform declined it, in which
         * case rendering continues at whatever interval it already had.
         */
        virtual bool SetSwapInterval(int interval) = 0;

        /**
         * @brief Resolves an OpenGL entry point by name.
         *
         * @param name The entry point to resolve.
         * @return The function pointer, or null when the entry point is unavailable.
         */
        [[nodiscard]] virtual void* GetProcAddress(const std::string& name) const = 0;

        /**
         * @brief Gets the same resolver as a C-compatible callback.
         *
         * GL helper libraries bootstrap by accepting a plain function pointer rather than a
         * service object. Keeping that adapter at the platform edge prevents a renderer from
         * naming or linking the native window toolkit merely to initialise its GL dispatch table.
         *
         * @return A non-null loader callback valid for the platform service's lifetime.
         */
        [[nodiscard]] virtual GlProcAddressLoader GetProcAddressLoader() const = 0;

        /**
         * @brief Gets the attributes the created context actually has.
         *
         * A driver may grant more than was requested (a higher version, more samples) or less.
         * A renderer that assumes it got exactly what it asked for is a renderer that breaks on
         * an unfamiliar driver, so the granted values are queryable rather than assumed.
         *
         * @param context The context to query.
         * @return The attributes the context was actually created with.
         */
        [[nodiscard]] virtual GlContextDescription GetContextAttributes(GlContextHandle context) const = 0;
    };

    /**
     * @brief Returns the stable, human-readable name of an OpenGL profile.
     *
     * @param profile The profile to name.
     * @return A stable name such as `"Core"`.
     */
    [[nodiscard]] const std::string& ToString(GlProfile profile);

} // namespace CNA::Platform
