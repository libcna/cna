// SPDX-License-Identifier: MS-PL

#include "X11ModeSwitch.hpp"

#include "CNA/Platform/PlatformException.hpp"
#include "X11Display.hpp"
#include "X11Error.hpp"
#include "X11ModeGuardian.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace CNA::Platform::X11 {

    std::optional<X11ModeCandidate> ChooseExclusiveMode(std::vector<X11ModeCandidate> modes,
                                                        const int width, const int height,
                                                        const float desktopRefreshRate)
    {
        // SDL's own order: largest first, and the higher rate first within a size.
        std::stable_sort(modes.begin(), modes.end(),
                         [](const X11ModeCandidate& a, const X11ModeCandidate& b) {
                             if (a.width != b.width) { return a.width > b.width; }
                             if (a.height != b.height) { return a.height > b.height; }
                             return static_cast<int>(a.refreshRate * 100.0f) >
                                    static_cast<int>(b.refreshRate * 100.0f);
                         });

        const float aspect = height > 0 ? static_cast<float>(width) / static_cast<float>(height)
                                        : 1.0f;
        const X11ModeCandidate* closest = nullptr;
        for (const X11ModeCandidate& mode : modes)
        {
            if (mode.width <= 0 || mode.height <= 0)
            {
                continue;
            }
            if (width > mode.width)
            {
                // Sorted by width: every mode from here on is narrower still.
                break;
            }
            if (height > mode.height)
            {
                // Wide enough but not tall enough -- a different aspect ratio. A closer mode may
                // still follow.
                continue;
            }
            if (closest != nullptr)
            {
                const float modeAspect =
                    static_cast<float>(mode.width) / static_cast<float>(mode.height);
                const float closestAspect =
                    static_cast<float>(closest->width) / static_cast<float>(closest->height);
                if (std::fabs(aspect - closestAspect) < std::fabs(aspect - modeAspect))
                {
                    continue;
                }
                if (mode.width == closest->width && mode.height == closest->height &&
                    std::fabs(closest->refreshRate - desktopRefreshRate) <=
                        std::fabs(mode.refreshRate - desktopRefreshRate))
                {
                    continue;
                }
            }
            closest = &mode;
        }
        if (closest == nullptr)
        {
            return std::nullopt;
        }
        return *closest;
    }

    /// One CRTC this switcher has changed, and what it was before.
    struct X11ModeSwitcher::Record
    {
        const void* owner = nullptr;
        XID crtc = 0;
        XID originalMode = 0;
        int originalX = 0;
        int originalY = 0;
        /// The CRTC's extent in its original mode, rotation included -- which is also the size
        /// the monitor showed, and so the desktop mode reported while it is switched.
        int originalWidth = 0;
        int originalHeight = 0;
        unsigned short originalRotation = 1;
        float originalRefreshRate = 0.0f;
        std::vector<XID> originalOutputs;
        int screenWidth = 0;
        int screenHeight = 0;
        std::uint32_t screenWidthMm = 0;
        std::uint32_t screenHeightMm = 0;
        XID appliedMode = 0;
        std::unique_ptr<X11ModeGuardian> guardian;
    };

#if defined(CNA_X11_HAVE_XRANDR)
    namespace {

        struct ResourcesDeleter
        {
            void operator()(XRRScreenResources* resources) const
            {
                if (resources != nullptr) { XRRFreeScreenResources(resources); }
            }
        };
        using Resources = std::unique_ptr<XRRScreenResources, ResourcesDeleter>;

        struct CrtcInfoDeleter
        {
            void operator()(XRRCrtcInfo* info) const
            {
                if (info != nullptr) { XRRFreeCrtcInfo(info); }
            }
        };
        using CrtcInfo = std::unique_ptr<XRRCrtcInfo, CrtcInfoDeleter>;

        struct OutputInfoDeleter
        {
            void operator()(XRROutputInfo* info) const
            {
                if (info != nullptr) { XRRFreeOutputInfo(info); }
            }
        };
        using OutputInfo = std::unique_ptr<XRROutputInfo, OutputInfoDeleter>;

        /// Holds the server for the lifetime of one read-modify-write of the configuration, so
        /// no other client -- a desktop's display daemon, typically -- changes it in between.
        class ServerGrab
        {
        public:
            explicit ServerGrab(Display* display) : display_(display) { XGrabServer(display_); }
            ~ServerGrab()
            {
                XUngrabServer(display_);
                XFlush(display_);
            }
            ServerGrab(const ServerGrab&) = delete;
            ServerGrab& operator=(const ServerGrab&) = delete;

        private:
            Display* display_;
        };

        bool IsRotated(const Rotation rotation)
        {
            return (rotation & (RR_Rotate_90 | RR_Rotate_270)) != 0;
        }

        float RefreshRateOf(const XRRModeInfo& mode)
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

        const XRRModeInfo* FindMode(const XRRScreenResources& resources, const RRMode id)
        {
            for (int index = 0; index < resources.nmode; ++index)
            {
                if (resources.modes[index].id == id)
                {
                    return &resources.modes[index];
                }
            }
            return nullptr;
        }

        int Overlap(const WindowBounds& area, const XRRCrtcInfo& crtc)
        {
            const int left = std::max(area.x, crtc.x);
            const int top = std::max(area.y, crtc.y);
            const int right = std::min(area.x + area.width, crtc.x + static_cast<int>(crtc.width));
            const int bottom =
                std::min(area.y + area.height, crtc.y + static_cast<int>(crtc.height));
            return right > left && bottom > top ? (right - left) * (bottom - top) : 0;
        }

        /// Everything a switch or a restore reads, in one pass.
        struct Snapshot
        {
            struct Crtc
            {
                RRCrtc id = 0;
                CrtcInfo info;
            };

            Resources resources;
            std::vector<Crtc> crtcs;
            int rootWidth = 0;
            int rootHeight = 0;
            int minimumWidth = 0;
            int minimumHeight = 0;
            int maximumWidth = 0;
            int maximumHeight = 0;

            [[nodiscard]] const Crtc* Find(const RRCrtc id) const
            {
                for (const Crtc& crtc : crtcs)
                {
                    if (crtc.id == id) { return &crtc; }
                }
                return nullptr;
            }
        };

        Snapshot TakeSnapshot(Display* display, const ::Window root)
        {
            Snapshot snapshot;
            // ...Current: the server's configuration as it stands, without asking the hardware
            // to re-probe every output, which on a real GPU takes a visible fraction of a second.
            snapshot.resources.reset(XRRGetScreenResourcesCurrent(display, root));
            if (snapshot.resources != nullptr)
            {
                for (int index = 0; index < snapshot.resources->ncrtc; ++index)
                {
                    Snapshot::Crtc crtc;
                    crtc.id = snapshot.resources->crtcs[index];
                    crtc.info.reset(XRRGetCrtcInfo(display, snapshot.resources.get(), crtc.id));
                    if (crtc.info != nullptr)
                    {
                        snapshot.crtcs.push_back(std::move(crtc));
                    }
                }
            }
            // The root's own geometry, not DisplayWidth(): Xlib's cached screen size only moves
            // when the application's event pump reaches the RRScreenChangeNotify, which after a
            // change made a moment ago it has not.
            ::Window rootReturn = kNone;
            int x = 0;
            int y = 0;
            unsigned int width = 0;
            unsigned int height = 0;
            unsigned int border = 0;
            unsigned int depth = 0;
            if (XGetGeometry(display, root, &rootReturn, &x, &y, &width, &height, &border, &depth) !=
                0)
            {
                snapshot.rootWidth = static_cast<int>(width);
                snapshot.rootHeight = static_cast<int>(height);
            }
            XRRGetScreenSizeRange(display, root, &snapshot.minimumWidth, &snapshot.minimumHeight,
                                  &snapshot.maximumWidth, &snapshot.maximumHeight);
            return snapshot;
        }

        /// True when the CRTC scales or otherwise transforms its image, as `xrandr --scale` and
        /// fractional desktop scaling do. A mode on such a CRTC does not show at its own size, so
        /// choosing one "to fit" a window would be choosing blind.
        bool IsTransformed(Display* display, const RRCrtc crtc)
        {
            // RandR 1.3. A 1.2 server answers BadRequest, caught here and read as "no transform"
            // -- which is what a 1.2 server's CRTCs genuinely have.
            X11ErrorTrap trap(display);
            XRRCrtcTransformAttributes* attributes = nullptr;
            bool transformed = false;
            if (XRRGetCrtcTransform(display, crtc, &attributes) != 0 && attributes != nullptr)
            {
                constexpr XFixed kOne = 1 << 16;
                for (int row = 0; row < 3; ++row)
                {
                    for (int column = 0; column < 3; ++column)
                    {
                        const XFixed expected = row == column ? kOne : 0;
                        if (attributes->currentTransform.matrix[row][column] != expected)
                        {
                            transformed = true;
                        }
                    }
                }
            }
            if (attributes != nullptr)
            {
                XFree(attributes);
            }
            trap.Sync();
            return transformed;
        }

        /// The modes every output on a CRTC can show, as the monitor shows them.
        std::vector<X11ModeCandidate> CandidatesFor(Display* display, const Snapshot& snapshot,
                                                    const XRRCrtcInfo& crtc)
        {
            std::vector<X11ModeCandidate> candidates;
            std::vector<RRMode> shared;
            for (int index = 0; index < crtc.noutput; ++index)
            {
                const OutputInfo output(
                    XRRGetOutputInfo(display, snapshot.resources.get(), crtc.outputs[index]));
                if (output == nullptr)
                {
                    return candidates;
                }
                std::vector<RRMode> modes(output->modes, output->modes + output->nmode);
                if (index == 0)
                {
                    shared = std::move(modes);
                }
                else
                {
                    // A CRTC driving two cloned outputs can only use a mode both of them accept.
                    std::erase_if(shared, [&modes](const RRMode mode) {
                        return std::find(modes.begin(), modes.end(), mode) == modes.end();
                    });
                }
            }
            const bool rotated = IsRotated(crtc.rotation);
            for (const RRMode id : shared)
            {
                const XRRModeInfo* mode = FindMode(*snapshot.resources, id);
                if (mode == nullptr)
                {
                    continue;
                }
                X11ModeCandidate candidate;
                candidate.id = id;
                candidate.width = static_cast<int>(rotated ? mode->height : mode->width);
                candidate.height = static_cast<int>(rotated ? mode->width : mode->height);
                candidate.refreshRate = RefreshRateOf(*mode);
                candidates.push_back(candidate);
            }
            return candidates;
        }

        struct Choice
        {
            const Snapshot::Crtc* crtc = nullptr;
            X11ModeCandidate mode;
        };

        std::optional<Choice> Choose(Display* display, const ::Window root,
                                     const Snapshot& snapshot, const RRCrtc ownersCrtc,
                                     const float originalRefreshRate, const WindowBounds& area,
                                     const int width, const int height, std::string& whyNot)
        {
            if (snapshot.resources == nullptr)
            {
                whyNot = "the X server did not describe its monitors";
                return std::nullopt;
            }

            // The monitor the owner already switched, when there is one: its window is there,
            // and it is the one whose mode is being changed again.
            const Snapshot::Crtc* chosen = ownersCrtc != 0 ? snapshot.Find(ownersCrtc) : nullptr;
            if (chosen == nullptr)
            {
                int best = 0;
                for (const Snapshot::Crtc& crtc : snapshot.crtcs)
                {
                    if (crtc.info->mode == 0) { continue; }
                    const int overlap = Overlap(area, *crtc.info);
                    if (overlap > best)
                    {
                        best = overlap;
                        chosen = &crtc;
                    }
                }
            }
            if (chosen == nullptr)
            {
                // Not on any monitor yet -- an unmapped window placed off-screen. The primary
                // monitor is where a window manager would put it.
                const RROutput primary = XRRGetOutputPrimary(display, root);
                for (const Snapshot::Crtc& crtc : snapshot.crtcs)
                {
                    if (crtc.info->mode == 0) { continue; }
                    const bool drivesPrimary =
                        std::find(crtc.info->outputs, crtc.info->outputs + crtc.info->noutput,
                                  primary) != crtc.info->outputs + crtc.info->noutput;
                    if (chosen == nullptr || drivesPrimary)
                    {
                        chosen = &crtc;
                    }
                    if (drivesPrimary) { break; }
                }
            }
            if (chosen == nullptr || chosen->info->mode == 0 || chosen->info->noutput == 0)
            {
                whyNot = "no monitor is lit";
                return std::nullopt;
            }
            if (IsTransformed(display, chosen->id))
            {
                whyNot = "the monitor is scaled or transformed, so its modes do not show at their "
                         "own size";
                return std::nullopt;
            }

            float desktopRefreshRate = originalRefreshRate;
            if (desktopRefreshRate <= 0.0f)
            {
                if (const XRRModeInfo* current = FindMode(*snapshot.resources, chosen->info->mode))
                {
                    desktopRefreshRate = RefreshRateOf(*current);
                }
            }
            const std::optional<X11ModeCandidate> mode = ChooseExclusiveMode(
                CandidatesFor(display, snapshot, *chosen->info), width, height, desktopRefreshRate);
            if (!mode)
            {
                whyNot = "no display mode is " + std::to_string(width) + "x" +
                         std::to_string(height) + " or larger";
                return std::nullopt;
            }
            return Choice{chosen, *mode};
        }

        /// Changes one CRTC, resizing the screen around it (PlanScreenSizes). False, with the
        /// screen put back, when the server refuses the CRTC configuration.
        bool ChangeCrtc(Display* display, const ::Window root, const Snapshot& snapshot,
                        const RRCrtc crtc, const RRMode mode, const int x, const int y,
                        const Rotation rotation, RROutput* outputs, const int outputCount,
                        const int extentWidth, const int extentHeight, const int preferredWidth,
                        const int preferredHeight, const int referenceWidth,
                        const int referenceHeight, const std::uint32_t referenceWidthMm,
                        const std::uint32_t referenceHeightMm)
        {
            std::vector<X11Rect> others;
            for (const Snapshot::Crtc& other : snapshot.crtcs)
            {
                if (other.id == crtc || other.info->mode == 0) { continue; }
                others.push_back(X11Rect{other.info->x, other.info->y,
                                         static_cast<int>(other.info->width),
                                         static_cast<int>(other.info->height)});
            }
            const X11ScreenPlan plan = PlanScreenSizes(
                snapshot.rootWidth, snapshot.rootHeight, others.data(), others.size(),
                X11Rect{x, y, extentWidth, extentHeight}, preferredWidth, preferredHeight,
                snapshot.minimumWidth, snapshot.minimumHeight, snapshot.maximumWidth,
                snapshot.maximumHeight);

            const auto setSize = [&](const int width, const int height) {
                XRRSetScreenSize(display, root, width, height,
                                 static_cast<int>(MillimetresFor(width, referenceWidth,
                                                                 referenceWidthMm)),
                                 static_cast<int>(MillimetresFor(height, referenceHeight,
                                                                 referenceHeightMm)));
            };

            const bool grows =
                plan.growWidth != snapshot.rootWidth || plan.growHeight != snapshot.rootHeight;
            if (grows)
            {
                setSize(plan.growWidth, plan.growHeight);
            }
            const XStatus status =
                XRRSetCrtcConfig(display, snapshot.resources.get(), crtc, kCurrentTime, x, y, mode,
                                 rotation, outputs, outputCount);
            if (status != RRSetConfigSuccess)
            {
                if (grows)
                {
                    setSize(snapshot.rootWidth, snapshot.rootHeight);
                }
                return false;
            }
            if (plan.finalWidth != plan.growWidth || plan.finalHeight != plan.growHeight)
            {
                setSize(plan.finalWidth, plan.finalHeight);
            }
            return true;
        }

        /// The authorisation the application's own connection would have used, looked up the
        /// way libxcb looks it up, as a ready connection-setup request.
        void EncodeSetupFor(X11ModeRestorePlan& plan, const std::string& displayName)
        {
            const char* authName = nullptr;
            std::size_t authNameLength = 0;
            const unsigned char* authData = nullptr;
            std::size_t authDataLength = 0;

#if defined(CNA_X11_HAVE_XAU)
            unsigned short family = FamilyLocal;
            const char* address = nullptr;
            std::size_t addressLength = 0;
            const auto* peer = reinterpret_cast<const sockaddr*>(&plan.address);
            if (peer->sa_family == AF_INET6)
            {
                const auto* in6 = reinterpret_cast<const sockaddr_in6*>(peer);
                address = reinterpret_cast<const char*>(&in6->sin6_addr);
                addressLength = sizeof(in6->sin6_addr);
                if (IN6_IS_ADDR_V4MAPPED(&in6->sin6_addr))
                {
                    address += 12;
                    addressLength = 4;
                    in_addr_t v4 = 0;
                    std::memcpy(&v4, address, sizeof(v4));
                    if (v4 != htonl(INADDR_LOOPBACK)) { family = FamilyInternet; }
                }
                else if (!IN6_IS_ADDR_LOOPBACK(&in6->sin6_addr))
                {
                    family = FamilyInternet6;
                }
            }
            else if (peer->sa_family == AF_INET)
            {
                const auto* in = reinterpret_cast<const sockaddr_in*>(peer);
                address = reinterpret_cast<const char*>(&in->sin_addr);
                addressLength = sizeof(in->sin_addr);
                if (in->sin_addr.s_addr != htonl(INADDR_LOOPBACK)) { family = FamilyInternet; }
            }
            char host[256] = {};
            if (family == FamilyLocal)
            {
                if (gethostname(host, sizeof(host) - 1) != 0) { host[0] = '\0'; }
                address = host;
                addressLength = std::strlen(host);
            }

            // The display number: after the last ':', up to the screen's '.'.
            std::string number;
            const std::size_t colon = displayName.rfind(':');
            if (colon != std::string::npos)
            {
                for (std::size_t index = colon + 1;
                     index < displayName.size() && displayName[index] >= '0' &&
                     displayName[index] <= '9';
                     ++index)
                {
                    number += displayName[index];
                }
            }

            char cookieName[] = "MIT-MAGIC-COOKIE-1";
            char* names[] = {cookieName};
            const int nameLengths[] = {static_cast<int>(sizeof(cookieName) - 1)};
            Xauth* auth = XauGetBestAuthByAddr(
                family, static_cast<unsigned short>(addressLength), address,
                static_cast<unsigned short>(number.size()), number.c_str(), 1, names, nameLengths);
            if (auth != nullptr)
            {
                authName = auth->name;
                authNameLength = auth->name_length;
                authData = reinterpret_cast<const unsigned char*>(auth->data);
                authDataLength = auth->data_length;
            }
            plan.setupLength = EncodeConnectionSetup(plan.setup, sizeof(plan.setup), authName,
                                                     authNameLength, authData, authDataLength);
            if (auth != nullptr)
            {
                XauDisposeAuth(auth);
            }
            return;
#else
            (void) displayName;
            // Without libXau the cookie cannot be looked up; a server that does not ask for one
            // (the local-user access of a desktop session, a private Xvfb) is still reachable.
            plan.setupLength = EncodeConnectionSetup(plan.setup, sizeof(plan.setup), authName,
                                                     authNameLength, authData, authDataLength);
#endif
        }

    } // namespace
#endif

    X11ModeSwitcher::X11ModeSwitcher(X11Connection& connection) : connection_(connection) {}

    X11ModeSwitcher::~X11ModeSwitcher()
    {
        ReleaseAll();
    }

    X11ModeSwitcher::Record* X11ModeSwitcher::FindByOwner(const void* owner) const
    {
        for (const auto& record : records_)
        {
            if (record->owner == owner) { return record.get(); }
        }
        return nullptr;
    }

    X11ModeSwitcher::Record* X11ModeSwitcher::FindByCrtc(const XID crtc) const
    {
        for (const auto& record : records_)
        {
            if (record->crtc == crtc) { return record.get(); }
        }
        return nullptr;
    }

    void X11ModeSwitcher::Erase(const Record* record)
    {
        // The guardian goes with the record, and its destructor disarms it.
        std::erase_if(records_, [record](const std::unique_ptr<Record>& entry) {
            return entry.get() == record;
        });
    }

    bool X11ModeSwitcher::IsApplied(const void* owner) const
    {
        return FindByOwner(owner) != nullptr;
    }

    bool X11ModeSwitcher::TryGetOriginalMode(const XID crtc, DisplayMode& mode) const
    {
        const Record* record = FindByCrtc(crtc);
        if (record == nullptr)
        {
            return false;
        }
        mode.width = record->originalWidth;
        mode.height = record->originalHeight;
        mode.refreshRate = record->originalRefreshRate;
        return true;
    }

    std::optional<X11AppliedMode> X11ModeSwitcher::Resolve(const void* owner,
                                                           const WindowBounds& area,
                                                           const int width, const int height,
                                                           std::string& whyNot) const
    {
#if defined(CNA_X11_HAVE_XRANDR)
        if (!connection_.HasRandr())
        {
            whyNot = "the X server has no XRandR 1.2, so its display modes cannot be changed";
            return std::nullopt;
        }
        Display* display = connection_.GetDisplay();
        const ::Window root = connection_.GetRoot();
        X11ErrorTrap trap(display);
        const Snapshot snapshot = TakeSnapshot(display, root);
        const Record* record = FindByOwner(owner);
        const std::optional<Choice> choice =
            Choose(display, root, snapshot, record != nullptr ? record->crtc : 0,
                   record != nullptr ? record->originalRefreshRate : 0.0f, area, width, height,
                   whyNot);
        trap.Sync();
        if (!choice)
        {
            return std::nullopt;
        }
        return X11AppliedMode{choice->mode.width, choice->mode.height, choice->mode.refreshRate};
#else
        (void) owner;
        (void) area;
        (void) width;
        (void) height;
        whyNot = "this build has no XRandR, so display modes cannot be changed";
        return std::nullopt;
#endif
    }

    std::optional<X11AppliedMode> X11ModeSwitcher::Apply(const void* owner,
                                                         const WindowBounds& area,
                                                         const int width, const int height,
                                                         std::string& whyNot)
    {
#if defined(CNA_X11_HAVE_XRANDR)
        if (!connection_.HasRandr())
        {
            whyNot = "the X server has no XRandR 1.2, so its display modes cannot be changed";
            return std::nullopt;
        }
        Display* display = connection_.GetDisplay();
        const ::Window root = connection_.GetRoot();
        X11ErrorTrap trap(display);

        std::optional<X11AppliedMode> applied;
        bool refused = false;
        std::string refusal;
        {
            const ServerGrab grab(display);
            const Snapshot snapshot = TakeSnapshot(display, root);

            Record* owned = FindByOwner(owner);
            if (owned != nullptr && snapshot.Find(owned->crtc) == nullptr)
            {
                // Its monitor is gone -- unplugged mid-game. There is nothing left to restore.
                Erase(owned);
                owned = nullptr;
            }
            // Choose() keeps an owner on the monitor it already switched, so `owned` -- when there
            // is one -- is the record for the chosen CRTC.
            const std::optional<Choice> choice =
                Choose(display, root, snapshot, owned != nullptr ? owned->crtc : 0,
                       owned != nullptr ? owned->originalRefreshRate : 0.0f, area, width, height,
                       whyNot);
            if (!choice)
            {
                return std::nullopt;
            }
            const XRRCrtcInfo& info = *choice->crtc->info;

            // Another window's switch of the same monitor is taken over rather than stacked on:
            // the original mode it recorded is still the one to go back to.
            Record* record = owned != nullptr ? owned : FindByCrtc(choice->crtc->id);
            bool created = false;
            if (record == nullptr)
            {
                auto fresh = std::make_unique<Record>();
                fresh->crtc = choice->crtc->id;
                fresh->originalMode = info.mode;
                fresh->originalX = info.x;
                fresh->originalY = info.y;
                fresh->originalWidth = static_cast<int>(info.width);
                fresh->originalHeight = static_cast<int>(info.height);
                fresh->originalRotation = info.rotation;
                fresh->originalOutputs.assign(info.outputs, info.outputs + info.noutput);
                if (const XRRModeInfo* current = FindMode(*snapshot.resources, info.mode))
                {
                    fresh->originalRefreshRate = RefreshRateOf(*current);
                }
                fresh->screenWidth = snapshot.rootWidth;
                fresh->screenHeight = snapshot.rootHeight;
                // What is kept is the density, and Xlib's cached millimetres describe the size it
                // cached alongside them -- which may be a moment older than the root's.
                const int screen = connection_.GetScreen();
                fresh->screenWidthMm = MillimetresFor(
                    snapshot.rootWidth, DisplayWidth(display, screen),
                    static_cast<std::uint32_t>(std::max(0, DisplayWidthMM(display, screen))));
                fresh->screenHeightMm = MillimetresFor(
                    snapshot.rootHeight, DisplayHeight(display, screen),
                    static_cast<std::uint32_t>(std::max(0, DisplayHeightMM(display, screen))));
                fresh->appliedMode = info.mode;
                record = fresh.get();
                records_.push_back(std::move(fresh));
                created = true;
            }
            record->owner = owner;

            const auto modeId = static_cast<RRMode>(choice->mode.id);
            // Armed before the switch, told the mode it is about to be in: a process that dies
            // between the two leaves a CRTC the guardian finds not in that mode, and leaves alone.
            if (modeId != record->originalMode)
            {
                if (record->guardian == nullptr)
                {
                    record->guardian = StartGuardian(*record, modeId);
                }
                else
                {
                    record->guardian->UpdateAppliedMode(static_cast<std::uint32_t>(modeId));
                }
            }

            if (info.mode != modeId)
            {
                std::vector<RROutput> outputs(info.outputs, info.outputs + info.noutput);
                if (!ChangeCrtc(display, root, snapshot, choice->crtc->id, modeId, info.x, info.y,
                                info.rotation, outputs.data(), static_cast<int>(outputs.size()),
                                choice->mode.width, choice->mode.height, 0, 0,
                                record->screenWidth, record->screenHeight, record->screenWidthMm,
                                record->screenHeightMm))
                {
                    refused = true;
                    refusal = "the X server refused the display mode " +
                              std::to_string(choice->mode.width) + "x" +
                              std::to_string(choice->mode.height);
                    if (created)
                    {
                        Erase(record);
                    }
                    else if (record->guardian != nullptr)
                    {
                        record->guardian->UpdateAppliedMode(
                            static_cast<std::uint32_t>(record->appliedMode));
                    }
                }
            }
            if (!refused)
            {
                if (record->appliedMode != modeId) { ++generation_; }
                record->appliedMode = modeId;
                applied = X11AppliedMode{choice->mode.width, choice->mode.height,
                                         choice->mode.refreshRate};
            }
        }
        trap.Sync();
        if (refused)
        {
            throw PlatformException("X11Window::SetFullscreenMode(ExclusiveFullscreen)", refusal);
        }
        return applied;
#else
        (void) owner;
        (void) area;
        (void) width;
        (void) height;
        whyNot = "this build has no XRandR, so display modes cannot be changed";
        return std::nullopt;
#endif
    }

    void X11ModeSwitcher::Restore(Record& record)
    {
#if defined(CNA_X11_HAVE_XRANDR)
        Display* display = connection_.GetDisplay();
        const ::Window root = connection_.GetRoot();
        X11ErrorTrap trap(display);
        bool failed = false;
        {
            const ServerGrab grab(display);
            const Snapshot snapshot = TakeSnapshot(display, root);
            const Snapshot::Crtc* crtc = snapshot.Find(record.crtc);
            if (crtc == nullptr || crtc->info->mode != record.appliedMode)
            {
                // Unplugged, or set by someone else since: not the application's to put back.
                return;
            }
            if (record.appliedMode == record.originalMode && crtc->info->x == record.originalX &&
                crtc->info->y == record.originalY)
            {
                return;
            }
            std::vector<RROutput> outputs(record.originalOutputs.begin(),
                                          record.originalOutputs.end());
            bool restored = ChangeCrtc(
                display, root, snapshot, record.crtc, record.originalMode, record.originalX,
                record.originalY, record.originalRotation, outputs.data(),
                static_cast<int>(outputs.size()), record.originalWidth, record.originalHeight,
                record.screenWidth, record.screenHeight, record.screenWidth, record.screenHeight,
                record.screenWidthMm, record.screenHeightMm);
            if (!restored)
            {
                // An output the CRTC drove before may be gone; the ones it drives now still work.
                outputs.assign(crtc->info->outputs, crtc->info->outputs + crtc->info->noutput);
                restored = ChangeCrtc(
                    display, root, snapshot, record.crtc, record.originalMode, record.originalX,
                    record.originalY, record.originalRotation, outputs.data(),
                    static_cast<int>(outputs.size()), record.originalWidth, record.originalHeight,
                    record.screenWidth, record.screenHeight, record.screenWidth,
                    record.screenHeight, record.screenWidthMm, record.screenHeightMm);
            }
            failed = !restored;
            ++generation_;
        }
        if (!trap.Sync() || failed)
        {
            // Not an exception: this runs from destructors and focus changes, and the caller can
            // do nothing more than this can. The guardian cannot either -- the server said no.
            std::fprintf(stderr,
                         "CNA X11: could not restore the display mode after exclusive fullscreen%s%s\n",
                         trap.HasError() ? ": " : "", trap.HasError() ? trap.Describe().c_str() : "");
            std::fflush(stderr);
        }
#else
        (void) record;
#endif
    }

    void X11ModeSwitcher::Release(const void* owner)
    {
        Record* record = FindByOwner(owner);
        if (record == nullptr)
        {
            return;
        }
        Restore(*record);
        Erase(record);
    }

    void X11ModeSwitcher::ReleaseAll()
    {
        while (!records_.empty())
        {
            Restore(*records_.back());
            records_.pop_back();
        }
    }

    std::unique_ptr<X11ModeGuardian> X11ModeSwitcher::StartGuardian(const Record& record,
                                                                    const XID appliedMode) const
    {
#if defined(CNA_X11_HAVE_XRANDR)
        // Under Xwayland a mode change is emulated for this client alone and ends with its
        // connection; there is nothing to put back after it dies.
        if (connection_.IsXwayland())
        {
            return nullptr;
        }
        Display* display = connection_.GetDisplay();

        X11ModeRestorePlan plan;
        socklen_t length = sizeof(plan.address);
        if (getpeername(ConnectionNumber(display), reinterpret_cast<sockaddr*>(&plan.address),
                        &length) != 0 ||
            length <= static_cast<socklen_t>(sizeof(sa_family_t)))
        {
            // An unnamed peer -- a connection handed over as a bare descriptor. There is no
            // address to reconnect to.
            return nullptr;
        }
        plan.addressLength = length;
        EncodeSetupFor(plan, connection_.GetDisplayName());
        if (plan.setupLength == 0)
        {
            return nullptr;
        }

        int opcode = 0;
        int event = 0;
        int error = 0;
        if (XQueryExtension(display, "RANDR", &opcode, &event, &error) == 0)
        {
            return nullptr;
        }
        plan.randrOpcode = static_cast<std::uint8_t>(opcode);
        plan.root = static_cast<std::uint32_t>(connection_.GetRoot());
        plan.crtc = static_cast<std::uint32_t>(record.crtc);
        plan.appliedMode = static_cast<std::uint32_t>(appliedMode);
        plan.originalMode = static_cast<std::uint32_t>(record.originalMode);
        plan.originalX = static_cast<std::int16_t>(record.originalX);
        plan.originalY = static_cast<std::int16_t>(record.originalY);
        plan.originalWidth = static_cast<std::uint16_t>(record.originalWidth);
        plan.originalHeight = static_cast<std::uint16_t>(record.originalHeight);
        plan.originalRotation = record.originalRotation;
        plan.outputCount = static_cast<std::uint16_t>(
            std::min(record.originalOutputs.size(), X11ModeRestorePlan::kMaxOutputs));
        for (std::size_t index = 0; index < plan.outputCount; ++index)
        {
            plan.outputs[index] = static_cast<std::uint32_t>(record.originalOutputs[index]);
        }
        plan.screenWidth = static_cast<std::uint16_t>(record.screenWidth);
        plan.screenHeight = static_cast<std::uint16_t>(record.screenHeight);
        plan.screenWidthMm = record.screenWidthMm;
        plan.screenHeightMm = record.screenHeightMm;
        int minimumWidth = 0;
        int minimumHeight = 0;
        int maximumWidth = 0;
        int maximumHeight = 0;
        XRRGetScreenSizeRange(display, connection_.GetRoot(), &minimumWidth, &minimumHeight,
                              &maximumWidth, &maximumHeight);
        plan.minimumWidth = static_cast<std::uint16_t>(minimumWidth);
        plan.minimumHeight = static_cast<std::uint16_t>(minimumHeight);
        plan.maximumWidth = static_cast<std::uint16_t>(maximumWidth);
        plan.maximumHeight = static_cast<std::uint16_t>(maximumHeight);
        return X11ModeGuardian::Start(plan);
#else
        (void) record;
        (void) appliedMode;
        return nullptr;
#endif
    }

} // namespace CNA::Platform::X11
