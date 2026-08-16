#pragma once

// plan_runtimerenderer.md design decision 2: everything GraphicsDevice needs to know about a
// renderer BEFORE an IGraphicsRenderer instance exists lives here, in one value type per renderer
// family, instead of in #ifdef chains inside GraphicsDevice.cpp.
//
// The distinction that motivates this file: IGraphicsRenderer is already a wide virtual interface,
// so everything AFTER construction is already runtime-polymorphic. But the SDL window flags, the
// decision whether a window is needed at all, whether SDL's video subsystem must be initialized,
// and the GL attributes that must be fixed before the window exists are all consumed BEFORE any
// renderer object can exist. A virtual method cannot serve them; a static descriptor can.
//
// Four renderers (BGFX, LLGL, FNA3D, DILIGENT) already made these decisions at runtime before this
// file existed -- they were simply reached through #ifdef instead of through a function pointer.
// This generalizes their existing mechanism rather than inventing a new one.

#include "CNA/GraphicsRendererType.hpp"

#include <cstdint>
#include <memory>
#include <string_view>

namespace CNA::Internal::Renderers
{
    class IGraphicsRenderer;
    struct GraphicsRendererCreateArgs;

    /**
     * @brief Coarse classification of the window surface a renderer needs.
     *
     * Deliberately coarser than the exact window-flag bitmask GraphicsRendererDescriptor::
     * prepareWindowFlags() returns: this is the granularity at which two renderers can be compared
     * for window compatibility. A windowing backend refuses to create one window carrying both an
     * OpenGL and a Vulkan intent, so a fallback crossing from one kind to another cannot reuse the
     * existing window and must recreate it (plan_runtimerenderer.md design decision 8).
     */
    enum class RendererWindowKind
    {
        /** @brief No window at all (HEADLESS, SOFTWARE, STUB, PORTABLEGL). */
        None,

        /** @brief An ordinary window with no graphics-API intent (SDL_RENDERER, the D3D family). */
        Plain,

        /** @brief A window created with an OpenGL render intent. */
        OpenGL,

        /** @brief A window created with a Vulkan render intent. */
        Vulkan,

        /** @brief A window created with a Metal render intent. */
        Metal
    };

    /**
     * @brief Whether a window created for @p existing can be reused by @p candidate.
     *
     * RendererWindowKind::None is compatible with everything in the sense that it needs no window;
     * a renderer that wants a window can never adopt a window that was never created, and one that
     * wants none never looks at the window it was handed. Two identical kinds are always
     * compatible. Every other crossing (OpenGL vs Vulkan, OpenGL vs Metal, Plain vs OpenGL, ...)
     * requires the window to be destroyed and recreated.
     *
     * @param existing  Window kind the current window was created for.
     * @param candidate Window kind the renderer being considered needs.
     * @return true when @p candidate can run against a window created for @p existing.
     */
    [[nodiscard]] constexpr bool AreWindowKindsCompatible(
        RendererWindowKind existing, RendererWindowKind candidate) noexcept
    {
        if (candidate == RendererWindowKind::None)
            return true;
        if (existing == RendererWindowKind::None)
            return false;
        return existing == candidate;
    }

    /**
     * @brief The presentation state a pre-window hook is allowed to read.
     *
     * Deliberately not PresentationParameters itself: a renderer descriptor must not include the
     * Microsoft::Xna::Framework::Graphics headers, so the few fields a pre-window hook genuinely
     * needs are copied into this renderer-agnostic struct instead. The int-ordinal convention for
     * the format fields matches GraphicsRendererCreateArgs::backBufferFormat.
     */
    struct RendererPreWindowRequest
    {
        /** @brief Requested back-buffer width in pixels; 0 means "unset". */
        int backBufferWidth = 0;

        /** @brief Requested back-buffer height in pixels; 0 means "unset". */
        int backBufferHeight = 0;

        /** @brief Requested MSAA sample count; 1 (or 0) means no multisampling. */
        int multiSampleCount = 1;

        /** @brief Requested Microsoft::Xna::Framework::Graphics::SurfaceFormat ordinal. */
        int backBufferFormat = 0;

        /** @brief Requested Microsoft::Xna::Framework::Graphics::DepthFormat ordinal. */
        int depthStencilFormat = 0;

        /** @brief Whether the game requested exclusive fullscreen. */
        bool isFullScreen = false;
    };

    /**
     * @brief A family's request for framebuffer attributes that must be fixed before the window
     *        is created.
     *
     * Mirrors CNA::Platform::OpenGlFramebufferDescription, but deliberately does not include a
     * sample count: the requested MSAA level is presentation state, not a family constant, so
     * GraphicsDevice supplies it from PresentationParameters when wantsMultiSample is set.
     *
     * A default-constructed value means "this family has no pre-window request".
     */
    struct RendererGlFramebufferRequest
    {
        /** @brief Requested depth-buffer bits; 0 means "do not request a depth buffer". */
        int depthBits = 0;

        /** @brief Requested stencil-buffer bits; 0 means "do not request a stencil buffer". */
        int stencilBits = 0;

        /** @brief Whether a double-buffered visual is required. */
        bool doubleBuffered = false;

        /**
         * @brief Whether the visual must carry the multisample buffer implied by
         *        PresentationParameters::MultiSampleCount.
         */
        bool wantsMultiSample = false;
    };

    /**
     * @brief A renderer's answer about a GraphicsAdapter-level query, or a deferral.
     *
     * plan_runtimerenderer.md design decision 9, with a correction the implementation forced:
     * GraphicsAdapter's profile/format queries are ADAPTER-level and run before any GraphicsDevice
     * exists, so they cannot be routed through an IGraphicsRenderer virtual the way the texture and
     * render-target queries are. They go through these static descriptor hooks instead -- which is
     * the right shape anyway, since the whole point of an adapter query is to ask before committing
     * to a device.
     */
    struct RendererAdapterQueries
    {
        /**
         * @brief Whether the renderer can honestly enforce a GraphicsProfile floor.
         *
         * Null means "no renderer-specific answer": GraphicsAdapter keeps its honest `return true`,
         * which is what 45 of the 46 renderers had. Only D3D9 has a real D3DCAPS9 to consult, and
         * this project refuses to substitute a hardcoded table pretending to be a capability query.
         */
        bool (*isProfileSupported)(int graphicsProfile) = nullptr;

        /**
         * @brief Whether a surface format is valid for a profile AND supported by the real device.
         *
         * Null means "no renderer-specific answer"; GraphicsAdapter applies its own format rule.
         */
        bool (*isRenderTargetFormatSupported)(int graphicsProfile, int surfaceFormat) = nullptr;

        /** @brief Back-buffer counterpart of isRenderTargetFormatSupported. */
        bool (*isBackBufferFormatSupported)(int graphicsProfile, int surfaceFormat) = nullptr;

        /**
         * @brief Clamps a requested MSAA count to what the device supports for a format.
         *
         * Null means "no renderer-specific answer"; GraphicsAdapter reports 0, as before.
         */
        int (*clampMultiSampleCount)(int surfaceFormat, int requestedMultiSampleCount) = nullptr;
    };

    /**
     * @brief The pre-construction contract of one renderer family.
     *
     * One instance exists per renderer family, returned by that family's own GetDescriptor().
     * GraphicsDevice consults it before creating a window and before creating the renderer itself;
     * GraphicsRendererRegistry holds the set of descriptors compiled into this build.
     *
     * Every function pointer is required to be non-null. A family with nothing to do for a given
     * hook supplies a trivial implementation rather than leaving the pointer null, so no call site
     * needs a null check.
     */
    struct GraphicsRendererDescriptor
    {
        /** @brief Public identity this descriptor serves. */
        GraphicsRendererType type = GraphicsRendererType::Stub;

        /**
         * @brief Public renderer name, matching the CNA_GRAPHICS_RENDERER value exactly.
         *
         * Points at static storage (a string literal), so it stays valid for the lifetime of the
         * program. Equal to CNA::getGraphicsRendererName(type).
         */
        std::string_view name;

        /** @brief Coarse window classification, used for fallback compatibility decisions. */
        RendererWindowKind windowKind = RendererWindowKind::None;

        /**
         * @brief Whether this renderer needs a real window at all.
         *
         * false for HEADLESS, SOFTWARE, STUB and PORTABLEGL -- GraphicsRendererCreateArgs::window
         * stays nullptr and no SDL window is ever created for them.
         */
        bool needsWindow = false;

        /**
         * @brief Whether SDL's video subsystem must be initialized for this renderer.
         *
         * false for the same four window-free renderers, which is what lets them run in CI
         * containers with no display server present -- not merely without a visible window.
         */
        bool needsVideoSubsystem = false;

        // MERGE (plan_platform.md PLAT-8 x plan_runtimerenderer.md P2): this used to be
        // `std::uint32_t (*prepareWindowFlags)()`, returning a raw windowing-library flag bitmask,
        // which put that library in all 15 renderer descriptors and broke next's decoupling ratchet
        // (19 files, 94 references over a budget of 0).
        //
        // Nothing was lost by dropping it: every descriptor's hook only mapped windowKind onto those
        // flags, which GraphicsDevice can do once, behind the platform contract. The single
        // exception was Metal asking for a high-density backing, and that is data, so it is data
        // here.
        bool wantsHighDpi = false;

        /**
         * @brief Framebuffer attributes that must be fixed before the window exists.
         *
         * MERGE (plan_platform.md PLAT-8 x plan_runtimerenderer.md P2): this replaces
         * `void (*applyPreWindowAttributes)(const RendererPreWindowRequest&)`, whose every
         * implementation called into the windowing library directly. The platform contract already
         * carries these as data on WindowDescription::openGlFramebuffer, so a hook bought nothing
         * and cost every descriptor a windowing-library dependency.
         *
         * Only families driving a desktop GLX visual need this. GLX fixes the window's visual --
         * and therefore its depth/stencil/multisample bits -- at window-creation time, so a
         * renderer constructor asking afterwards is too late and silently gets a 0-bit stencil
         * buffer, making every DepthStencilState::StencilEnable a permanent no-op. Families on EGL
         * (the whole EasyGL set) choose their config at context-creation time and leave this zeroed.
         *
         * All-zero means "no pre-window request", which is the correct default.
         */
        RendererGlFramebufferRequest glFramebuffer{};

        /**
         * @brief Whether this family is handed a platform surface presenter.
         *
         * MERGE (plan_platform.md PLAT-8 x plan_runtimerenderer.md design decision 2): next chose
         * the platform services a renderer receives with `#if defined(CNA_RENDERER_<X>)` chains in
         * GraphicsDevice::createRenderer(). In a multi-renderer binary those chains answer for
         * whichever macros are defined rather than for the family being constructed, so the
         * question moves here, where it is answered per family.
         *
         * True for the CPU-raster families that present through the platform (SKIA, BLEND2D).
         */
        bool needsSurfacePresenter = false;

        /**
         * @brief Whether this family is handed the platform's OpenGL context service.
         *
         * Context-backed renderers receive only that narrow service plus the surface snapshot;
         * they never resolve a native window or reach through IPlatform themselves.
         */
        bool needsGlContext = false;

        /** @brief Whether this family is handed the platform's Vulkan surface service. */
        bool needsVulkanSurface = false;

        /**
         * @brief Cheap, side-effect-free probe of whether this renderer can run here.
         *
         * Answers "is the loader present, is a device enumerable" -- never "does creating a device
         * succeed", which is what create() itself answers. Must not create a window, a device, or
         * any other resource, and must not throw.
         *
         * A renderer with no meaningful probe returns true. Returning true here is not a promise
         * that create() will succeed; it only means there is no cheap way to know otherwise.
         *
         * @return true when this renderer is worth attempting on this machine.
         */
        bool (*isAvailable)() = nullptr;

        /**
         * @brief Creates the renderer instance.
         *
         * The family's own CreateGraphicsRenderer, reached through a pointer so that several
         * families can be linked into one binary (plan_runtimerenderer.md design decision 4).
         *
         * @param args Construction arguments, already populated by GraphicsDevice.
         * @return The new renderer; never nullptr on success. Throws on failure.
         */
        std::unique_ptr<IGraphicsRenderer> (*create)(const GraphicsRendererCreateArgs& args) = nullptr;

        /**
         * @brief Adapter-level queries this renderer can answer before a device exists.
         *
         * Unlike the hooks above, the members of this struct MAY be null -- null means "this
         * renderer has no renderer-specific answer, use the framework's own rule". That is the
         * honest state for 45 of the 46 renderers, and the distinction matters: a renderer forced
         * to restate the framework rule would eventually restate it wrongly.
         */
        RendererAdapterQueries adapterQueries{};
    };

}
