// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-196: bgfx's Noop renderer has no native backbuffer and deliberately advertises no
// BGFX_CAPS_FORMAT_TEXTURE_BACKBUFFER bit for any texture format. CNA nevertheless called
// bgfx::reset during ordinary GraphicsDeviceManager::CreateDevice, which hit bgfx_p.h's native
// format assertion before bgfx could reach its unchanged-reset early return.
//
// Each renderer leg is process-isolated. The supervisor distinguishes:
//   * successful initialization/reset/teardown (exit 0),
//   * a deterministic catchable public rejection (exit 40),
//   * an accidental fallback to a renderer other than the requested one (exit 41),
//   * a fixture check failure (exit 42), and
//   * native fatal/assert/abort/segfault/timeout (signal classification).

#include "CNA/GraphicsCapability.hpp"
#include "System/Environment.hpp"
#include "CNA/Internal/Renderers/Bgfx/BgfxRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentInterval.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#define CNA_GFX196_CAN_FORK 1
#else
#define CNA_GFX196_CAN_FORK 0
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kRejectedExit = 40;
    constexpr int kFallbackExit = 41;
    constexpr int kCheckFailureExit = 42;

    enum class Scenario
    {
        NoopDefault,
        NoopExplicitColor,
        NoopResetResizeCycles,
        OpenGLControl
    };

    const char* RendererName(bgfx::RendererType::Enum type)
    {
        return type == bgfx::RendererType::Noop ? "Noop" :
               type == bgfx::RendererType::OpenGL ? "OpenGL" :
               bgfx::getRendererName(type);
    }

    class Gfx196Game final : public Game
    {
        std::unique_ptr<GraphicsDeviceManager> manager_;
        Scenario scenario_;
        int checks_ = 0;
        int failures_ = 0;
        bool fallback_ = false;
        bool done_ = false;

        void Check(bool condition, const std::string& label)
        {
            ++checks_;
            if (!condition) ++failures_;
            std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        }

        void CheckActive(bgfx::RendererType::Enum requested, const std::string& label)
        {
            const bgfx::RendererType::Enum active = bgfx::getRendererType();
            std::printf("[INFO] %s requested renderer=%s, active renderer=%s\n",
                        label.c_str(), RendererName(requested), RendererName(active));
            if (active != requested) fallback_ = true;
            Check(active == requested, label + ": requested renderer remains active (no fallback)");
        }

        void CheckNoopContract(GraphicsDevice& device, const std::string& label)
        {
            CheckActive(bgfx::RendererType::Noop, label);
            const bgfx::Caps* caps = bgfx::getCaps();
            const uint32_t bgraCaps = caps->formats[bgfx::TextureFormat::BGRA8];
            const bool bgraBackbuffer =
                (bgraCaps & BGFX_CAPS_FORMAT_TEXTURE_BACKBUFFER) != 0;
            int backbufferFormats = 0;
            for (int i = 0; i < bgfx::TextureFormat::Count; ++i)
            {
                if ((caps->formats[i] & BGFX_CAPS_FORMAT_TEXTURE_BACKBUFFER) != 0)
                    ++backbufferFormats;
            }
            std::printf("[INFO] %s BGRA8 format caps=0x%08x, BACKBUFFER=%s, "
                        "all BACKBUFFER-capable formats=%d, maxMsaa=%u\n",
                        label.c_str(), static_cast<unsigned>(bgraCaps),
                        bgraBackbuffer ? "set" : "clear", backbufferFormats,
                        static_cast<unsigned>(caps->limits.maxMsaa));
            Check(!bgraBackbuffer && backbufferFormats == 0,
                  label + ": Noop still reports no native backbuffer format");
            Check(caps->limits.maxMsaa == 0,
                  label + ": Noop keeps the GFX-185 zero render-target MSAA limit");

            constexpr std::array<CNA::GraphicsCapability, 9> capabilities = {
                CNA::GraphicsCapability::ThreeD,
                CNA::GraphicsCapability::DepthStencilBuffer,
                CNA::GraphicsCapability::MultiSampleAntiAliasing,
                CNA::GraphicsCapability::MultipleRenderTargets,
                CNA::GraphicsCapability::AnisotropicFiltering,
                CNA::GraphicsCapability::WireFrame,
                CNA::GraphicsCapability::OcclusionQuery,
                CNA::GraphicsCapability::CustomEffects,
                CNA::GraphicsCapability::Texture3D,
            };
            bool anyReported = false;
            for (const CNA::GraphicsCapability capability : capabilities)
                anyReported = anyReported || device.SupportsCapability(capability);
            Check(!anyReported,
                  label + ": CNA reports no real rendering capability for the Noop renderer");
        }

        void CheckPublicFormatContract(GraphicsDevice& device)
        {
            SurfaceFormat selectedFormat = SurfaceFormat::Bgr565;
            DepthFormat selectedDepth = DepthFormat::None;
            int selectedMsaa = -1;
            const bool exact = device.getAdapterProperty().QueryBackBufferFormat(
                GraphicsProfile::Reach, SurfaceFormat::Bgr565, DepthFormat::Depth24,
                4, selectedFormat, selectedDepth, selectedMsaa);
            Check(!exact && selectedFormat == SurfaceFormat::Color &&
                      selectedDepth == DepthFormat::Depth24 && selectedMsaa == 0,
                  "public backbuffer negotiation rejects Bgr565/4x and selects Color/0x");
        }

        void RunResetResizeCycles(GraphicsDevice& device)
        {
            struct ResetCase
            {
                int width;
                int height;
                PresentInterval interval;
            };
            constexpr std::array<ResetCase, 8> cases = {{
                {0, 0, PresentInterval::Immediate},
                {1, 1, PresentInterval::One},
                {1, 2, PresentInterval::Immediate},
                {2, 1, PresentInterval::One},
                {96, 64, PresentInterval::Immediate},
                {320, 180, PresentInterval::One},
                {1, 1, PresentInterval::Immediate},
                {800, 480, PresentInterval::One},
            }};

            for (std::size_t i = 0; i < cases.size(); ++i)
            {
                PresentationParameters pp =
                    device.getPresentationParametersProperty().Clone();
                pp.setBackBufferWidthProperty(cases[i].width);
                pp.setBackBufferHeightProperty(cases[i].height);
                pp.setBackBufferFormatProperty(SurfaceFormat::Color);
                pp.setPresentationIntervalProperty(cases[i].interval);
                device.Reset(pp);

                const PresentationParameters& applied =
                    device.getPresentationParametersProperty();
                const std::string prefix = "reset cycle " + std::to_string(i) +
                    " (" + std::to_string(cases[i].width) + "x" +
                    std::to_string(cases[i].height) + ")";
                Check(applied.getBackBufferWidthProperty() == cases[i].width &&
                          applied.getBackBufferHeightProperty() == cases[i].height,
                      prefix + ": public dimensions retained");
                Check(applied.getBackBufferFormatProperty() == SurfaceFormat::Color,
                      prefix + ": explicit Color format retained");
                CheckActive(bgfx::RendererType::Noop, prefix);
            }

            manager_->setPreferredBackBufferWidthProperty(73);
            manager_->setPreferredBackBufferHeightProperty(41);
            manager_->setSynchronizeWithVerticalRetraceProperty(false);
            manager_->ApplyChanges();
            Check(device.getPresentationParametersProperty().getBackBufferWidthProperty() == 73 &&
                      device.getPresentationParametersProperty().getBackBufferHeightProperty() == 41,
                  "GraphicsDeviceManager resize applies an ordinary 73x41 backbuffer");

            SDL_Window* window = reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty());
            Check(window != nullptr, "Noop keeps the normal CNA window contract");
            if (window != nullptr)
            {
                Check(SDL_SetWindowSize(window, 1, 1),
                      "external client resize reaches the minimal 1x1 window size");
                Check(SDL_SyncWindow(window),
                      "minimal client resize is synchronized for an immediate observation");
                device.Clear(Color(1, 2, 3, 255));
                int width = 0;
                int height = 0;
                device.GetRenderer().GetViewportSize(width, height);
                Check(width == 1 && height == 1,
                      "Noop resize path observes the minimal 1x1 client size");

                Check(SDL_SetWindowSize(window, 127, 79),
                      "external client resize reaches an ordinary 127x79 window size");
                Check(SDL_SyncWindow(window),
                      "ordinary client resize is synchronized for an immediate observation");
                device.Clear(Color(4, 5, 6, 255));
                device.GetRenderer().GetViewportSize(width, height);
                Check(width == 127 && height == 79,
                      "Noop resize path observes the ordinary 127x79 client size");
            }
        }

        void RunOpenGLControl(GraphicsDevice& device)
        {
            CheckActive(bgfx::RendererType::OpenGL, "OpenGL control initialization");
            const uint32_t caps =
                bgfx::getCaps()->formats[bgfx::TextureFormat::BGRA8];
            Check((caps & BGFX_CAPS_FORMAT_TEXTURE_BACKBUFFER) != 0,
                  "OpenGL advertises the BGRA8 native backbuffer capability");

            for (const auto dims : {std::array<int, 2>{1, 1},
                                    std::array<int, 2>{96, 64},
                                    std::array<int, 2>{127, 79}})
            {
                PresentationParameters pp =
                    device.getPresentationParametersProperty().Clone();
                pp.setBackBufferWidthProperty(dims[0]);
                pp.setBackBufferHeightProperty(dims[1]);
                pp.setBackBufferFormatProperty(SurfaceFormat::Color);
                device.Reset(pp);
                CheckActive(bgfx::RendererType::OpenGL,
                            "OpenGL reset " + std::to_string(dims[0]) + "x" +
                            std::to_string(dims[1]));
            }
            device.Clear(Color(9, 17, 33, 255));
            Check(device.SupportsCapability(CNA::GraphicsCapability::ThreeD),
                  "OpenGL real capability reporting remains enabled");
        }

    protected:
        void Draw(const GameTime&) override
        {
            if (done_) return;
            done_ = true;

            GraphicsDevice& device = getGraphicsDeviceProperty();
            switch (scenario_)
            {
            case Scenario::NoopDefault:
                CheckNoopContract(device, "default device creation");
                Check(device.getPresentationParametersProperty().getBackBufferFormatProperty() ==
                          SurfaceFormat::Color,
                      "default public backbuffer format is Color");
                CheckPublicFormatContract(device);
                break;
            case Scenario::NoopExplicitColor:
                CheckNoopContract(device, "explicit Color device creation");
                Check(device.getPresentationParametersProperty().getBackBufferFormatProperty() ==
                          SurfaceFormat::Color,
                      "explicitly selected public Color format survives device creation");
                Check(device.getPresentationParametersProperty().getBackBufferWidthProperty() == 1 &&
                          device.getPresentationParametersProperty().getBackBufferHeightProperty() == 1,
                      "explicit minimal 1x1 initial size survives device creation");
                break;
            case Scenario::NoopResetResizeCycles:
                CheckNoopContract(device, "reset/resize device creation");
                RunResetResizeCycles(device);
                CheckNoopContract(device, "after repeated reset/resize cycles");
                break;
            case Scenario::OpenGLControl:
                RunOpenGLControl(device);
                break;
            }

            std::printf("[INFO] REMED-GFX-196 child: %d/%d checks passed\n",
                        checks_ - failures_, checks_);
            std::fflush(stdout);
            Exit();
        }

    public:
        explicit Gfx196Game(Scenario scenario)
            : scenario_(scenario)
        {
            manager_ = std::make_unique<GraphicsDeviceManager>(this);
            if (scenario == Scenario::NoopExplicitColor)
            {
                manager_->setPreferredBackBufferWidthProperty(1);
                manager_->setPreferredBackBufferHeightProperty(1);
                manager_->setPreferredBackBufferFormatProperty(SurfaceFormat::Color);
                manager_->setSynchronizeWithVerticalRetraceProperty(false);
            }
            else if (scenario == Scenario::NoopResetResizeCycles ||
                     scenario == Scenario::OpenGLControl)
            {
                manager_->setPreferredBackBufferWidthProperty(96);
                manager_->setPreferredBackBufferHeightProperty(64);
                manager_->setPreferredBackBufferFormatProperty(SurfaceFormat::Color);
            }
        }

        [[nodiscard]] int Result() const
        {
            if (fallback_) return kFallbackExit;
            return failures_ == 0 ? 0 : kCheckFailureExit;
        }
    };

    Scenario ParseScenario(const char* value)
    {
        if (std::strcmp(value, "noop-default") == 0) return Scenario::NoopDefault;
        if (std::strcmp(value, "noop-explicit") == 0) return Scenario::NoopExplicitColor;
        if (std::strcmp(value, "noop-cycles") == 0) return Scenario::NoopResetResizeCycles;
        if (std::strcmp(value, "opengl") == 0) return Scenario::OpenGLControl;
        throw std::runtime_error(std::string("unknown GFX-196 scenario: ") + value);
    }

    int RunChild(const char* scenarioName)
    {
        try
        {
            Gfx196Game game(ParseScenario(scenarioName));
            game.Run();
            return game.Result();
        }
        catch (const std::exception& e)
        {
            std::printf("[REJECT] deterministic catchable rejection: %s\n", e.what());
            std::fflush(stdout);
            return kRejectedExit;
        }
        catch (...)
        {
            std::printf("[REJECT] deterministic catchable non-standard rejection\n");
            std::fflush(stdout);
            return kRejectedExit;
        }
    }

#if CNA_GFX196_CAN_FORK
    const char* SignalName(int signal)
    {
        switch (signal)
        {
        case SIGABRT: return "SIGABRT (native fatal/abort)";
        case SIGTRAP: return "SIGTRAP (native assertion)";
        case SIGSEGV: return "SIGSEGV";
        case SIGALRM: return "SIGALRM (timeout)";
        default: return strsignal(signal);
        }
    }

    bool RunLeg(const char* executable, const char* id, const char* renderer,
                int expectedExit)
    {
        const pid_t pid = fork();
        if (pid < 0)
        {
            std::printf("[FAIL] %s: fork failed: %s\n", id, std::strerror(errno));
            return false;
        }
        if (pid == 0)
        {
            alarm(45);
            System::Environment::SetEnvironmentVariable("CNA_BGFX_RENDERER", renderer);
            char* const argv[] = {
                const_cast<char*>(executable), const_cast<char*>("--child"),
                const_cast<char*>(id), nullptr
            };
            execv(executable, argv);
            _exit(127);
        }

        int status = 0;
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        if (WIFSIGNALED(status))
        {
            std::printf("[FATAL] %s: child terminated by %s\n",
                        id, SignalName(WTERMSIG(status)));
            return false;
        }
        if (!WIFEXITED(status))
        {
            std::printf("[FAIL] %s: child ended without an exit status\n", id);
            return false;
        }

        const int actualExit = WEXITSTATUS(status);
        if (actualExit != expectedExit)
        {
            const char* classification = actualExit == kRejectedExit ?
                "deterministic public rejection" : actualExit == kFallbackExit ?
                "accidental renderer fallback" : actualExit == kCheckFailureExit ?
                "fixture check failure" : actualExit == 127 ? "exec failure" :
                "unexpected exit";
            std::printf("[FAIL] %s: %s (exit %d, expected %d)\n",
                        id, classification, actualExit, expectedExit);
            return false;
        }

        std::printf("[PASS] %s: %s\n", id,
                    expectedExit == kRejectedExit ?
                    "deterministic public rejection, not a native fatal" :
                    "successful initialization/reset/teardown with no fallback");
        return true;
    }
#endif
}

int main(int argc, char** argv)
{
    if (argc == 3 && std::strcmp(argv[1], "--child") == 0)
        return RunChild(argv[2]);

    int checks = 0;
    int failures = 0;
    auto check = [&](bool condition, const char* label)
    {
        ++checks;
        if (!condition) ++failures;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label);
    };

    // Exercise CNA's callback boundary directly. A bgfx fatal must become the exact catchable
    // diagnostic the process supervisor can distinguish from a native signal.
    try
    {
        CNA::Internal::Renderers::Bgfx::BgfxCnaCallback callback;
        callback.fatal("gfx196", 196, bgfx::Fatal::DebugCheck,
                       "GFX-196 fatal callback probe");
        check(false, "Bgfx fatal callback throws");
    }
    catch (const std::runtime_error& e)
    {
        check(std::string(e.what()) ==
                  "bgfx fatal: GFX-196 fatal callback probe",
              "Bgfx fatal callback preserves the exact diagnostic as a catchable exception");
    }

#if CNA_GFX196_CAN_FORK
    check(RunLeg(argv[0], "noop-default", "NOOP", 0),
          "default Noop initialization classification");
    check(RunLeg(argv[0], "noop-explicit", "NOOP", 0),
          "explicit Color/minimal Noop initialization classification");
    check(RunLeg(argv[0], "noop-cycles", "NOOP", 0),
          "Noop reset/resize cycle classification");
    check(RunLeg(argv[0], "opengl", "OPENGL", 0),
          "ordinary OpenGL initialization classification");
    check(RunLeg(argv[0], "noop-default", "invalid-gfx196-renderer", kRejectedExit),
          "invalid renderer override rejection classification");
#else
    std::printf("[INFO] fork unavailable; running the OpenGL control in-process\n");
    check(RunChild("opengl") == 0, "ordinary OpenGL initialization classification");
#endif

    std::printf("[INFO] REMED-GFX-196 supervisor: %d/%d checks passed\n",
                checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
