// SPDX-License-Identifier: MS-PL

#include "X11Displays.hpp"

#include "X11Display.hpp"
#include "X11Error.hpp"
#include "X11ModeSwitch.hpp"
#include "X11Window.hpp"

#include <algorithm>
#include <cstring>

namespace CNA::Platform::X11 {

    namespace {

#if defined(CNA_X11_HAVE_XRANDR)
        /// Refresh rate in Hz from a RandR mode's dot clock and total scanline/frame extents.
        ///
        /// RandR reports the pixel clock and the blanking-inclusive totals, not a rate: the rate
        /// is clock / (hTotal * vTotal). Doubled and interlaced modes adjust the vertical total,
        /// and skipping that adjustment reports an interlaced 1080i as 30 Hz instead of 60.
        float RefreshRateFromMode(const XRRModeInfo& mode)
        {
            if (mode.hTotal == 0 || mode.vTotal == 0)
            {
                return 0.0f;
            }
            double vTotal = mode.vTotal;
            if ((mode.modeFlags & RR_DoubleScan) != 0) { vTotal *= 2.0; }
            if ((mode.modeFlags & RR_Interlace) != 0) { vTotal /= 2.0; }
            return static_cast<float>(static_cast<double>(mode.dotClock) /
                                      (static_cast<double>(mode.hTotal) * vTotal));
        }
#endif

        int OverlapArea(const WindowBounds& window, const DisplayInfo& display)
        {
            const int left = std::max(window.x, display.x);
            const int top = std::max(window.y, display.y);
            const int right = std::min(window.x + window.width, display.x + display.width);
            const int bottom = std::min(window.y + window.height, display.y + display.height);
            if (right <= left || bottom <= top)
            {
                return 0;
            }
            return (right - left) * (bottom - top);
        }

    } // namespace

    X11Displays::X11Displays(X11Connection& connection) : connection_(connection) {}

    void X11Displays::InvalidateCache()
    {
        cacheValid_ = false;
    }

    void X11Displays::EnsureCache() const
    {
        // A mode this process switched is a change the cache must reflect at once, not after the
        // event pump reaches the server's notification of it.
        if (!cacheValid_ || modeGeneration_ != connection_.GetModeSwitcher().GetGeneration())
        {
            BuildCache();
        }
    }

    void X11Displays::BuildCache() const
    {
        cache_.clear();
        cacheValid_ = true;
        modeGeneration_ = connection_.GetModeSwitcher().GetGeneration();

        Display* display = connection_.GetDisplay();
        const int screen = connection_.GetScreen();

#if defined(CNA_X11_HAVE_XRANDR)
        if (connection_.HasRandr())
        {
            X11ErrorTrap trap(display);

            // RandR 1.5 monitors are the right unit: the server already merged the CRTCs that
            // drive one physical panel, and it knows which one the user marked primary. The
            // `active_only` argument is True because an unlit output is not a display a game can
            // put a window on.
            int monitorCount = 0;
            XRRMonitorInfo* monitors = XRRGetMonitors(display, connection_.GetRoot(), True,
                                                      &monitorCount);
            if (monitors != nullptr && monitorCount > 0)
            {
                XRRScreenResources* resources =
                    XRRGetScreenResourcesCurrent(display, connection_.GetRoot());
                for (int index = 0; index < monitorCount; ++index)
                {
                    const XRRMonitorInfo& monitor = monitors[index];
                    CachedDisplay entry;
                    entry.info.id = static_cast<std::uint32_t>(cache_.size() + 1);
                    entry.info.x = monitor.x;
                    entry.info.y = monitor.y;
                    entry.info.width = monitor.width;
                    entry.info.height = monitor.height;
                    entry.info.contentScale = connection_.GetDisplayScale();
                    entry.info.desktopMode.width = monitor.width;
                    entry.info.desktopMode.height = monitor.height;

                    if (char* name = XGetAtomName(display, monitor.name))
                    {
                        entry.info.name = name;
                        XFree(name);
                    }

                    // The refresh rate is a property of the CRTC driving the monitor, not of the
                    // monitor rectangle, so it has to be looked up through the first output.
                    if (resources != nullptr && monitor.noutput > 0)
                    {
                        entry.output = monitor.outputs[0];
                        if (XRROutputInfo* outputInfo =
                                XRRGetOutputInfo(display, resources, monitor.outputs[0]))
                        {
                            entry.crtc = outputInfo->crtc;
                            if (outputInfo->crtc != 0)
                            {
                                if (XRRCrtcInfo* crtcInfo =
                                        XRRGetCrtcInfo(display, resources, outputInfo->crtc))
                                {
                                    for (int modeIndex = 0; modeIndex < resources->nmode;
                                         ++modeIndex)
                                    {
                                        if (resources->modes[modeIndex].id == crtcInfo->mode)
                                        {
                                            entry.info.desktopMode.refreshRate =
                                                RefreshRateFromMode(resources->modes[modeIndex]);
                                            break;
                                        }
                                    }
                                    XRRFreeCrtcInfo(crtcInfo);
                                }
                            }
                            XRRFreeOutputInfo(outputInfo);
                        }
                    }

                    // The primary monitor goes first, because GraphicsAdapter treats the first
                    // display as the default adapter and a game launched with no explicit
                    // display should land on the user's main screen.
                    if (monitor.primary != 0 && !cache_.empty())
                    {
                        cache_.insert(cache_.begin(), entry);
                        for (std::size_t fix = 0; fix < cache_.size(); ++fix)
                        {
                            cache_[fix].info.id = static_cast<std::uint32_t>(fix + 1);
                        }
                    }
                    else
                    {
                        cache_.push_back(entry);
                    }
                }
                if (resources != nullptr) { XRRFreeScreenResources(resources); }
                XRRFreeMonitors(monitors);
            }
            else if (monitors != nullptr)
            {
                XRRFreeMonitors(monitors);
            }

            // RandR 1.2 fallback: enumerate lit CRTCs directly. Reached on a server that has
            // RandR but not 1.5, and on one where XRRGetMonitors reported nothing.
            if (cache_.empty())
            {
                if (XRRScreenResources* resources =
                        XRRGetScreenResourcesCurrent(display, connection_.GetRoot()))
                {
                    for (int index = 0; index < resources->ncrtc; ++index)
                    {
                        XRRCrtcInfo* crtcInfo =
                            XRRGetCrtcInfo(display, resources, resources->crtcs[index]);
                        if (crtcInfo == nullptr)
                        {
                            continue;
                        }
                        if (crtcInfo->mode != 0 && crtcInfo->width > 0 && crtcInfo->height > 0)
                        {
                            CachedDisplay entry;
                            entry.info.id = static_cast<std::uint32_t>(cache_.size() + 1);
                            entry.info.x = crtcInfo->x;
                            entry.info.y = crtcInfo->y;
                            entry.info.width = static_cast<int>(crtcInfo->width);
                            entry.info.height = static_cast<int>(crtcInfo->height);
                            entry.info.contentScale = connection_.GetDisplayScale();
                            entry.info.desktopMode.width = entry.info.width;
                            entry.info.desktopMode.height = entry.info.height;
                            entry.crtc = resources->crtcs[index];
                            if (crtcInfo->noutput > 0)
                            {
                                entry.output = crtcInfo->outputs[0];
                                if (XRROutputInfo* outputInfo =
                                        XRRGetOutputInfo(display, resources, crtcInfo->outputs[0]))
                                {
                                    entry.info.name = outputInfo->name != nullptr
                                                          ? std::string(outputInfo->name)
                                                          : std::string();
                                    XRRFreeOutputInfo(outputInfo);
                                }
                            }
                            for (int modeIndex = 0; modeIndex < resources->nmode; ++modeIndex)
                            {
                                if (resources->modes[modeIndex].id == crtcInfo->mode)
                                {
                                    entry.info.desktopMode.refreshRate =
                                        RefreshRateFromMode(resources->modes[modeIndex]);
                                    break;
                                }
                            }
                            cache_.push_back(entry);
                        }
                        XRRFreeCrtcInfo(crtcInfo);
                    }
                    XRRFreeScreenResources(resources);
                }
            }
            trap.Sync();
        }
#endif

        if (cache_.empty())
        {
            // No RandR, or RandR reported nothing usable. The screen itself is then genuinely the
            // only display there is, and reporting it is accurate rather than a placeholder.
            CachedDisplay entry;
            entry.info.id = 1;
            entry.info.name = "Screen " + std::to_string(screen);
            entry.info.x = 0;
            entry.info.y = 0;
            entry.info.width = DisplayWidth(display, screen);
            entry.info.height = DisplayHeight(display, screen);
            entry.info.contentScale = connection_.GetDisplayScale();
            entry.info.desktopMode.width = entry.info.width;
            entry.info.desktopMode.height = entry.info.height;
            cache_.push_back(entry);
        }

        const X11ModeSwitcher& switcher = connection_.GetModeSwitcher();
        for (CachedDisplay& entry : cache_)
        {
            if (entry.info.name.empty())
            {
                entry.info.name = "Display " + std::to_string(entry.info.id);
            }
            // While exclusive fullscreen holds a mode of its own on a monitor, the monitor's
            // geometry and current mode are that mode's -- it is what the screen shows -- but the
            // DESKTOP mode is still the one the desktop will get back, which is what the contract
            // means by it.
            entry.currentMode = entry.info.desktopMode;
            DisplayMode original;
            if (entry.crtc != 0 && switcher.TryGetOriginalMode(entry.crtc, original))
            {
                entry.info.desktopMode = original;
            }
        }
    }

    std::vector<DisplayInfo> X11Displays::GetDisplays() const
    {
        EnsureCache();
        std::vector<DisplayInfo> displays;
        displays.reserve(cache_.size());
        for (const CachedDisplay& entry : cache_)
        {
            displays.push_back(entry.info);
        }
        return displays;
    }

    bool X11Displays::TryGetDisplayForWindow(const IPlatformWindow& window,
                                             DisplayInfo& display) const
    {
        const auto* x11Window = dynamic_cast<const X11Window*>(&window);
        if (x11Window == nullptr)
        {
            // A window from another platform implementation. Refusing is the contract's answer;
            // guessing from its bounds would silently pair an SDL3 window with an X11 display.
            return false;
        }

        EnsureCache();
        const WindowBounds bounds = x11Window->GetClientBounds();

        // Largest overlap, not "contains the top-left corner": a window straddling two monitors
        // belongs to the one showing most of it, which is also where a window manager would put
        // it if asked to maximise.
        const CachedDisplay* best = nullptr;
        int bestArea = 0;
        for (const CachedDisplay& entry : cache_)
        {
            const int area = OverlapArea(bounds, entry.info);
            if (area > bestArea)
            {
                bestArea = area;
                best = &entry;
            }
        }
        if (best == nullptr && !cache_.empty())
        {
            // Entirely off-screen -- an unmapped window at a negative position, typically. The
            // first display is the honest fallback for "which display would it appear on".
            best = &cache_.front();
        }
        if (best == nullptr)
        {
            return false;
        }
        display = best->info;
        return true;
    }

    bool X11Displays::TryGetSafeAreaForWindow(const IPlatformWindow& window,
                                              WindowBounds& safeArea) const
    {
        (void) window;
        (void) safeArea;
        return false;
    }

    std::vector<DisplayMode> X11Displays::GetDisplayModes(const std::uint32_t displayId) const
    {
        EnsureCache();
        std::vector<DisplayMode> modes;

#if defined(CNA_X11_HAVE_XRANDR)
        if (!connection_.HasRandr())
        {
            return modes;
        }
        const auto found = std::find_if(cache_.begin(), cache_.end(),
                                        [displayId](const CachedDisplay& entry) {
                                            return entry.info.id == displayId;
                                        });
        if (found == cache_.end() || found->output == 0)
        {
            return modes;
        }

        Display* display = connection_.GetDisplay();
        X11ErrorTrap trap(display);
        XRRScreenResources* resources = XRRGetScreenResourcesCurrent(display, connection_.GetRoot());
        if (resources == nullptr)
        {
            return modes;
        }
        if (XRROutputInfo* outputInfo = XRRGetOutputInfo(display, resources, found->output))
        {
            for (int index = 0; index < outputInfo->nmode; ++index)
            {
                for (int modeIndex = 0; modeIndex < resources->nmode; ++modeIndex)
                {
                    const XRRModeInfo& mode = resources->modes[modeIndex];
                    if (mode.id != outputInfo->modes[index])
                    {
                        continue;
                    }
                    DisplayMode entry;
                    entry.width = static_cast<int>(mode.width);
                    entry.height = static_cast<int>(mode.height);
                    entry.refreshRate = RefreshRateFromMode(mode);
                    modes.push_back(entry);
                    break;
                }
            }
            XRRFreeOutputInfo(outputInfo);
        }
        XRRFreeScreenResources(resources);
        trap.Sync();
#else
        (void) displayId;
#endif
        return modes;
    }

    bool X11Displays::TryGetCurrentDisplayMode(const std::uint32_t displayId,
                                               DisplayMode& mode) const
    {
        EnsureCache();
        const auto found = std::find_if(cache_.begin(), cache_.end(),
                                        [displayId](const CachedDisplay& entry) {
                                            return entry.info.id == displayId;
                                        });
        if (found == cache_.end())
        {
            return false;
        }
        mode = found->currentMode;
        return true;
    }

    bool X11Displays::IsScreenSaverEnabled() const
    {
        int timeout = 0;
        int interval = 0;
        int preferBlanking = 0;
        int allowExposures = 0;
        XGetScreenSaver(connection_.GetDisplay(), &timeout, &interval, &preferBlanking,
                        &allowExposures);
        // Timeout 0 means the server's screen saver is disabled. This describes the X server's
        // own saver, not a desktop environment's separate screen locker, which no client can
        // observe -- which is why this is a query about the server and says so.
        return timeout != 0;
    }

    void X11Displays::SetScreenSaverEnabled(const bool enabled)
    {
        Display* display = connection_.GetDisplay();
        int timeout = 0;
        int interval = 0;
        int preferBlanking = 0;
        int allowExposures = 0;
        XGetScreenSaver(display, &timeout, &interval, &preferBlanking, &allowExposures);

        if (!enabled)
        {
            if (savedScreenSaverTimeout_ < 0)
            {
                // Remembered so the user's own timeout comes back, rather than a hardcoded
                // default that would silently change their session settings.
                savedScreenSaverTimeout_ = timeout;
            }
            XSetScreenSaver(display, 0, interval, preferBlanking, allowExposures);
        }
        else
        {
            const int restored = savedScreenSaverTimeout_ >= 0 ? savedScreenSaverTimeout_ : timeout;
            XSetScreenSaver(display, restored, interval, preferBlanking, allowExposures);
            savedScreenSaverTimeout_ = -1;
        }
        XFlush(display);
    }

} // namespace CNA::Platform::X11
