// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0114: shared pieces of the real-desktop Wayland validation harness
// (`cna_wayland_desktop_validation`).
//
// The harness drives CNA's Wayland backend through the public platform contract, against whatever
// compositor WAYLAND_DISPLAY names -- normally a real desktop, a real GPU, real outputs at a real
// fractional scale, which is the point -- and reports each check as a PASS/FAIL/SKIP line a
// validation log can quote verbatim. It never injects input: on Wayland an ordinary client cannot,
// and the one way that exists (uinput) would type into whatever the user has focused. What needs a
// person's hands is the `interactive` scenario, which tells the person what to do and reports what
// arrived.
#pragma once

#include "CNA/Platform/IPlatform.hpp"

#include "../../../modules/platform/src/Wayland/WaylandPlatform.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace CnaWaylandValidation {

    using namespace CNA::Platform;

    /** @brief Running totals of every check. */
    struct Tally
    {
        int passed = 0;
        int failed = 0;
        int skipped = 0;
    };

    /** @brief The process-wide tally. */
    Tally& Results();
    /** @brief Records and prints a passing check. */
    void Pass(const std::string& check, const std::string& detail = {});
    /** @brief Records and prints a failing check. */
    void Fail(const std::string& check, const std::string& detail = {});
    /** @brief Records and prints a check that could not run, with the reason. */
    void Skip(const std::string& check, const std::string& reason);
    /** @brief Prints evidence that is not a check. */
    void Info(const std::string& text);
    /** @brief Passes or fails @p check on @p condition. @return @p condition. */
    bool Check(bool condition, const std::string& check, const std::string& detail = {});

    /** @brief What this process holds, for leak detection. */
    struct ProcessSample
    {
        long residentKb = 0;
        int openDescriptors = 0;
    };

    /** @brief Reads the resident set and open descriptors from /proc. */
    ProcessSample SampleProcess();

    /** @brief Milliseconds since an arbitrary monotonic epoch. */
    double NowMs();

    /** @brief `--name=value` or `--name value`, else @p fallback. */
    long OptionInt(const std::vector<std::string>& arguments, const std::string& name, long fallback);
    /** @brief `--name=value` or `--name value`, else @p fallback. */
    std::string OptionString(const std::vector<std::string>& arguments, const std::string& name,
                             const std::string& fallback);
    /** @brief True when `--name` is present. */
    bool OptionFlag(const std::vector<std::string>& arguments, const std::string& name);

    /** @brief One Wayland platform plus the event bookkeeping every scenario needs. */
    class Session
    {
    public:
        Session();
        ~Session();
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;

        /** @brief True when the platform connected and Video was acquired. */
        [[nodiscard]] bool Ok() const { return acquired_; }
        /** @brief Why not. */
        [[nodiscard]] const std::string& Error() const { return error_; }
        /** @brief The platform. */
        [[nodiscard]] Wayland::WaylandPlatform& Platform() { return *platform_; }

        /** @brief Creates a window and waits for its first configure (the platform's Show). */
        std::unique_ptr<IPlatformWindow> Make(const std::string& title, int width, int height, bool visible = true,
                                              WindowRenderIntent intent = WindowRenderIntent::None, bool highDpi = false);

        /** @brief Polls once, appending to what was seen. */
        const std::vector<PlatformEvent>& Poll();
        /** @brief Pumps until @p condition holds or @p budget passes. @return The condition. */
        bool PumpUntil(const std::function<bool()>& condition, std::chrono::milliseconds budget);
        /** @brief Pumps for a fixed time. */
        void PumpFor(std::chrono::milliseconds duration);
        /** @brief Forgets what was seen. */
        void Clear() { seen_.clear(); }
        /** @brief Everything since the last Clear(). */
        [[nodiscard]] const std::vector<PlatformEvent>& Seen() const { return seen_; }
        /** @brief Counts window events of @p kind for @p window. */
        [[nodiscard]] int Count(WindowId window, WindowEventKind kind) const;
        /** @brief Pumps until @p window reported @p kind. */
        bool WaitFor(WindowId window, WindowEventKind kind, std::chrono::milliseconds budget = std::chrono::milliseconds(3000));
        /** @brief True while the compositor connection is alive. */
        [[nodiscard]] bool Alive();
        /** @brief The connection's error, if it died. */
        [[nodiscard]] std::string ConnectionError();

    private:
        std::unique_ptr<Wayland::WaylandPlatform> platform_;
        bool acquired_ = false;
        std::string error_;
        std::vector<PlatformEvent> batch_;
        std::vector<PlatformEvent> seen_;
    };

    /**
     * @brief Presents one solid-colour frame through @p presenter at the window's pixel size.
     * @param rgb 0xRRGGBB.
     */
    void PresentSolid(IPlatformSurfacePresenter& presenter, IPlatformWindow& window, std::uint32_t rgb);

    int RunInfo(const std::vector<std::string>& arguments);
    int RunLifecycle(const std::vector<std::string>& arguments);
    int RunScale(const std::vector<std::string>& arguments);
    int RunPresenter(const std::vector<std::string>& arguments);
    int RunGl(const std::vector<std::string>& arguments);
    int RunVulkan(const std::vector<std::string>& arguments);
    int RunLifetime(const std::vector<std::string>& arguments);
    int RunStress(const std::vector<std::string>& arguments);
    int RunSoak(const std::vector<std::string>& arguments);
    int RunClipboard(const std::vector<std::string>& arguments);
    int RunInteractive(const std::vector<std::string>& arguments);

    namespace VulkanLifetime {
        /** @brief One full Vulkan build/present/teardown; returns validation errors, -1 on failure. */
        int Iteration(Session& session, bool validation, std::string& error);
    }

} // namespace CnaWaylandValidation
