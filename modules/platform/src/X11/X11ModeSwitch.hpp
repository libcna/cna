// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/IPlatformSystemServices.hpp"
#include "CNA/Platform/IPlatformWindow.hpp"
#include "X11Headers.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CNA::Platform::X11 {

    class X11Connection;
    class X11ModeGuardian;

    /** @brief One display mode a monitor can be switched to. */
    struct X11ModeCandidate
    {
        /** @brief The RandR mode XID. */
        std::uint64_t id = 0;
        /** @brief The width the monitor shows, its rotation applied. */
        int width = 0;
        /** @brief The height the monitor shows, its rotation applied. */
        int height = 0;
        /** @brief The refresh rate in Hz. */
        float refreshRate = 0.0f;
    };

    /**
     * @brief Picks the mode exclusive fullscreen uses for a requested size.
     *
     * SDL's rule, so that a game gets the mode here it would get under the SDL3 backend: the
     * smallest mode at least as wide and as tall as the request, preferring the aspect ratio
     * closest to the request's; and among modes of that size, the refresh rate closest to the
     * desktop's, ties going to the higher rate. (That last clause is what SDL's comments say it
     * does. Its code as written keeps the first mode of a size it meets, which is the highest
     * rate whatever the desktop runs at.)
     *
     * @param modes The monitor's modes, in any order.
     * @param width The requested width.
     * @param height The requested height.
     * @param desktopRefreshRate The refresh rate the desktop runs at.
     * @return The mode, or nothing when no mode is large enough.
     */
    [[nodiscard]] std::optional<X11ModeCandidate> ChooseExclusiveMode(
        std::vector<X11ModeCandidate> modes, int width, int height, float desktopRefreshRate);

    /** @brief The mode a monitor was switched to. */
    struct X11AppliedMode
    {
        /** @brief The width the monitor shows. */
        int width = 0;
        /** @brief The height the monitor shows. */
        int height = 0;
        /** @brief The refresh rate in Hz. */
        float refreshRate = 0.0f;
    };

    /**
     * @brief Changes monitors' display modes for exclusive fullscreen, and always gives them back.
     *
     * One per connection. A window asks for the monitor under it to be switched to the mode that
     * suits a size; the switcher remembers what that monitor's CRTC was before the first switch,
     * and a release -- leaving exclusive fullscreen, hiding or destroying the window, losing focus,
     * the connection closing -- puts it back exactly. A process that dies without releasing is
     * covered by an @ref X11ModeGuardian, started with the first switch.
     *
     * Both directions leave alone a CRTC that someone else has set in the meantime: a user who
     * changed the resolution while the game ran made a decision a restore must not overrule.
     *
     * The screen is resized around each CRTC change as `xrandr` would -- to the bounding box of
     * the lit CRTCs -- so on one monitor the pointer stays on the visible area, and on several the
     * other monitors keep their place (see @ref PlanScreenSizes).
     */
    class X11ModeSwitcher
    {
    public:
        /**
         * @brief Creates the switcher for one connection. Nothing is asked of the server yet.
         *
         * @param connection The connection whose monitors are switched.
         */
        explicit X11ModeSwitcher(X11Connection& connection);

        /** @brief Restores every mode still switched. */
        ~X11ModeSwitcher();

        X11ModeSwitcher(const X11ModeSwitcher&) = delete;
        X11ModeSwitcher& operator=(const X11ModeSwitcher&) = delete;

        /**
         * @brief Works out the mode a size would get, without switching anything.
         *
         * @param owner Who is asking; a monitor it already switched is the one looked at.
         * @param area Where the window is, in root coordinates; picks the monitor.
         * @param width The requested width.
         * @param height The requested height.
         * @param whyNot Receives the reason when there is no mode.
         * @return The mode, or nothing when there is none for this size.
         */
        [[nodiscard]] std::optional<X11AppliedMode> Resolve(const void* owner,
                                                            const WindowBounds& area, int width,
                                                            int height, std::string& whyNot) const;

        /**
         * @brief Switches the monitor under @p area to the mode for a size.
         *
         * @param owner Who holds the switch until @ref Release.
         * @param area Where the window is, in root coordinates.
         * @param width The requested width.
         * @param height The requested height.
         * @param whyNot Receives the reason when there is no mode.
         * @return The mode now in effect, or nothing -- with nothing changed -- when there is none.
         * @throws PlatformException When the server refused a mode it listed.
         */
        std::optional<X11AppliedMode> Apply(const void* owner, const WindowBounds& area, int width,
                                            int height, std::string& whyNot);

        /**
         * @brief Gives back the mode @p owner switched, if it switched one.
         *
         * @param owner Whose switch to undo.
         */
        void Release(const void* owner);

        /**
         * @brief Gets whether @p owner holds a switch.
         *
         * @param owner Who to ask about.
         * @return True while its mode is in effect.
         */
        [[nodiscard]] bool IsApplied(const void* owner) const;

        /**
         * @brief Gets the mode a CRTC had before an application switched it.
         *
         * @param crtc The CRTC.
         * @param mode Receives the mode; untouched on false.
         * @return True when this switcher has the CRTC switched.
         */
        [[nodiscard]] bool TryGetOriginalMode(XID crtc, DisplayMode& mode) const;

        /** @brief Restores every mode still switched. */
        void ReleaseAll();

        /**
         * @brief Gets a counter that moves whenever this switcher changes a mode.
         *
         * Lets a cache of the display configuration notice a change this process made before its
         * own event pump has reached the server's notification of it.
         *
         * @return The counter.
         */
        [[nodiscard]] std::uint64_t GetGeneration() const { return generation_; }

    private:
        struct Record;

        Record* FindByOwner(const void* owner) const;
        Record* FindByCrtc(XID crtc) const;
        void Restore(Record& record);
        void Erase(const Record* record);
        [[nodiscard]] std::unique_ptr<X11ModeGuardian> StartGuardian(const Record& record,
                                                                     XID appliedMode) const;

        X11Connection& connection_;
        std::vector<std::unique_ptr<Record>> records_;
        std::uint64_t generation_ = 0;
    };

} // namespace CNA::Platform::X11
