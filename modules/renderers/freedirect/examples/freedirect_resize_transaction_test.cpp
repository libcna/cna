// SPDX-License-Identifier: MS-PL
// REMED-GFX-029: DIRECTX3 backbuffer resize must be an all-or-nothing surface-set transaction.
//
// This regression uses real free-direct IDirectDraw surfaces. Instance-local hooks inject one
// failure at every meaningful replacement stage and report native COM pointer identities plus
// acquisition/release edges. The checks therefore distinguish "resize threw" from the required
// outcome: the original primary/shadow pair is still the live pair, every temporary is released
// once, and the original pair is not released until a later successful commit.

#include "CNA/Internal/Renderers/FreeDirect/FreeDirectRenderer.hpp"

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentInterval.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstdio>
#include <limits>
#include <string>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::FreeDirect;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kWidthA = 64;
    constexpr int kHeightA = 48;
    constexpr int kWidthB = 80;
    constexpr int kHeightB = 60;
    constexpr int kWidthC = 96;
    constexpr int kHeightC = 72;

    int passCount = 0;
    int failCount = 0;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        if (condition)
            ++passCount;
        else
            ++failCount;
    }

    struct ResourceRecord
    {
        FreeDirectResourceKindEXT kind{};
        FreeDirectResourceEventEXT event{};
        const void* identity = nullptr;
    };

    struct ResourceTracker
    {
        std::array<ResourceRecord, 128> records{};
        std::size_t count = 0;

        static void OnResource(void* context, FreeDirectResourceKindEXT kind,
                               FreeDirectResourceEventEXT event, const void* identity) noexcept
        {
            auto& tracker = *static_cast<ResourceTracker*>(context);
            if (tracker.count < tracker.records.size())
                tracker.records[tracker.count++] = ResourceRecord{kind, event, identity};
        }

        void Clear() noexcept
        {
            count = 0;
        }

        [[nodiscard]] int Count(FreeDirectResourceKindEXT kind, FreeDirectResourceEventEXT event) const
        {
            int result = 0;
            for (std::size_t i = 0; i < count; ++i)
            {
                if (records[i].kind == kind && records[i].event == event)
                    ++result;
            }
            return result;
        }

        [[nodiscard]] int Count(FreeDirectResourceKindEXT kind, FreeDirectResourceEventEXT event,
                                const void* identity) const
        {
            int result = 0;
            for (std::size_t i = 0; i < count; ++i)
            {
                if (records[i].kind == kind && records[i].event == event &&
                    records[i].identity == identity)
                {
                    ++result;
                }
            }
            return result;
        }

        [[nodiscard]] bool EveryAcquisitionWasReleasedExactlyOnce() const
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                const auto& record = records[i];
                if (record.event == FreeDirectResourceEventEXT::Acquired &&
                    Count(record.kind, FreeDirectResourceEventEXT::Released, record.identity) != 1)
                {
                    return false;
                }
            }
            return true;
        }
    };

    [[nodiscard]] FreeDirectTestHooksEXT Hooks(
        ResourceTracker& tracker,
        FreeDirectResizeFailurePointEXT failure = FreeDirectResizeFailurePointEXT::None)
    {
        return FreeDirectTestHooksEXT{failure, &tracker, &ResourceTracker::OnResource};
    }

    [[nodiscard]] PresentationParameters Parameters(
        int width, int height, DepthFormat depth = DepthFormat::Depth24,
        PresentInterval interval = PresentInterval::Immediate)
    {
        PresentationParameters result;
        result.setBackBufferWidthProperty(width);
        result.setBackBufferHeightProperty(height);
        result.setDepthStencilFormatProperty(depth);
        result.setPresentationIntervalProperty(interval);
        return result;
    }

    [[nodiscard]] bool SameViewport(const Viewport& lhs, const Viewport& rhs)
    {
        return lhs.getXProperty() == rhs.getXProperty() &&
               lhs.getYProperty() == rhs.getYProperty() &&
               lhs.getWidthProperty() == rhs.getWidthProperty() &&
               lhs.getHeightProperty() == rhs.getHeightProperty() &&
               lhs.getMinDepthProperty() == rhs.getMinDepthProperty() &&
               lhs.getMaxDepthProperty() == rhs.getMaxDepthProperty();
    }

    [[nodiscard]] bool SameRectangle(const Rectangle& lhs, const Rectangle& rhs)
    {
        return lhs.X == rhs.X && lhs.Y == rhs.Y &&
               lhs.Width == rhs.Width && lhs.Height == rhs.Height;
    }

    [[nodiscard]] bool SameColor(const Color& lhs, const Color& rhs)
    {
        return lhs.getRProperty() == rhs.getRProperty() &&
               lhs.getGProperty() == rhs.getGProperty() &&
               lhs.getBProperty() == rhs.getBProperty() &&
               lhs.getAProperty() == rhs.getAProperty();
    }

    [[nodiscard]] Color ReadPixel(GraphicsDevice& device, int x, int y)
    {
        const Rectangle region(x, y, 1, 1);
        Color result(0, 0, 0, 0);
        device.GetBackBufferData(&region, &result, 0, 1);
        return result;
    }

    [[nodiscard]] bool SameSurfaceSet(const FreeDirectTestStateEXT& lhs, const FreeDirectTestStateEXT& rhs)
    {
        return lhs.directDraw == rhs.directDraw &&
               lhs.primarySurface == rhs.primarySurface &&
               lhs.shadowBackBuffer == rhs.shadowBackBuffer;
    }

    [[nodiscard]] bool SameDepthState(const DepthStencilState& state,
                                      bool enabled, bool writes, CompareFunction function,
                                      int referenceStencil)
    {
        return state.getDepthBufferEnableProperty() == enabled &&
               state.getDepthBufferWriteEnableProperty() == writes &&
               state.getDepthBufferFunctionProperty() == function &&
               state.getReferenceStencilProperty() == referenceStencil;
    }

    void RenderAndPresent(GraphicsDevice& device, Texture2D& texture,
                          int x, int y, const std::string& label)
    {
        bool usable = true;
        try
        {
            device.Clear(Color(2, 4, 6, 255));
            SpriteBatch batch(device);
            batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
            batch.Draw(texture, static_cast<float>(x), static_cast<float>(y));
            batch.End();
            usable = SameColor(ReadPixel(device, x, y), Color(220, 40, 90, 255));
            device.Present();
        }
        catch (const std::exception& exception)
        {
            usable = false;
            std::printf("       unexpected render/present exception: %s\n", exception.what());
        }
        Check(usable, label);
    }

    [[nodiscard]] const char* FailureName(FreeDirectResizeFailurePointEXT point)
    {
        switch (point)
        {
            case FreeDirectResizeFailurePointEXT::ShadowBackBufferCreation: return "shadow creation";
            case FreeDirectResizeFailurePointEXT::ShadowBackBufferValidation: return "shadow validation";
            case FreeDirectResizeFailurePointEXT::DisplayModeBinding: return "display binding";
            case FreeDirectResizeFailurePointEXT::PrimarySurfaceCreation: return "primary creation";
            case FreeDirectResizeFailurePointEXT::PrimarySurfaceValidation: return "primary validation";
            case FreeDirectResizeFailurePointEXT::SurfaceSetCommit: return "surface-set commit";
            case FreeDirectResizeFailurePointEXT::None: return "none";
        }
        return "unknown";
    }

    void CheckFailureTemporaryCounts(
        const ResourceTracker& tracker, FreeDirectResizeFailurePointEXT point,
        const std::string& prefix)
    {
        int expectedShadow = 0;
        int expectedPrimary = 0;
        switch (point)
        {
            case FreeDirectResizeFailurePointEXT::ShadowBackBufferCreation:
                break;
            case FreeDirectResizeFailurePointEXT::ShadowBackBufferValidation:
            case FreeDirectResizeFailurePointEXT::DisplayModeBinding:
            case FreeDirectResizeFailurePointEXT::PrimarySurfaceCreation:
                expectedShadow = 1;
                break;
            case FreeDirectResizeFailurePointEXT::PrimarySurfaceValidation:
            case FreeDirectResizeFailurePointEXT::SurfaceSetCommit:
                expectedShadow = 1;
                expectedPrimary = 1;
                break;
            case FreeDirectResizeFailurePointEXT::None:
                break;
        }

        Check(
            tracker.Count(
                FreeDirectResourceKindEXT::ShadowBackBuffer,
                FreeDirectResourceEventEXT::Acquired) == expectedShadow &&
            tracker.Count(
                FreeDirectResourceKindEXT::PrimarySurface,
                FreeDirectResourceEventEXT::Acquired) == expectedPrimary,
            prefix + " acquires only the temporaries reached before that stage");
        Check(
            tracker.EveryAcquisitionWasReleasedExactlyOnce(),
            prefix + " releases every acquired temporary exactly once");
    }

    void RunLifecycleCase(DepthFormat depthFormat)
    {
        const std::string prefix =
            depthFormat == DepthFormat::None ? "depthless constructor" : "depth-backed request";
        ResourceTracker tracker;
        Check(SDL_InitSubSystem(SDL_INIT_VIDEO), prefix + " initializes SDL video");
        SDL_Window* window = SDL_CreateWindow("DIRECTX3 resize lifecycle", 48, 32, SDL_WINDOW_HIDDEN);
        Check(window != nullptr, prefix + " creates caller-owned SDL window");
        if (window == nullptr)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return;
        }

        FreeDirectTestStateEXT state{};
        {
            GraphicsRendererCreateArgs args;
            args.surface.windowId = SDL_GetWindowID(window);
            args.virtualWidth = 48;
            args.virtualHeight = 32;
            args.depthStencilFormat = static_cast<int>(depthFormat);
            FreeDirectRenderer renderer(args, Hooks(tracker));
            state = renderer.GetTestStateEXT();
            Check(
                state.directDraw != nullptr && state.primarySurface != nullptr &&
                    state.shadowBackBuffer != nullptr,
                prefix + " owns one complete native surface set");
            Check(
                tracker.Count(
                    FreeDirectResourceKindEXT::DirectDraw,
                    FreeDirectResourceEventEXT::Acquired, state.directDraw) == 1 &&
                tracker.Count(
                    FreeDirectResourceKindEXT::PrimarySurface,
                    FreeDirectResourceEventEXT::Acquired, state.primarySurface) == 1 &&
                tracker.Count(
                    FreeDirectResourceKindEXT::ShadowBackBuffer,
                    FreeDirectResourceEventEXT::Acquired, state.shadowBackBuffer) == 1,
                prefix + " acquires each constructor resource exactly once");
            Check(
                !renderer.SupportsDepthStencil(),
                prefix + " retains DIRECTX3's established depthless capability");
        }

        Check(
            tracker.Count(
                FreeDirectResourceKindEXT::ShadowBackBuffer,
                FreeDirectResourceEventEXT::Released, state.shadowBackBuffer) == 1 &&
            tracker.Count(
                FreeDirectResourceKindEXT::PrimarySurface,
                FreeDirectResourceEventEXT::Released, state.primarySurface) == 1 &&
            tracker.Count(
                FreeDirectResourceKindEXT::DirectDraw,
                FreeDirectResourceEventEXT::Released, state.directDraw) == 1,
            prefix + " shutdown releases each constructor resource exactly once");
        Check(SDL_GetWindowID(window) != 0, prefix + " preserves the caller-owned SDL window");

        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    void RunConstructorFailureCase()
    {
        ResourceTracker tracker;
        Check(SDL_InitSubSystem(SDL_INIT_VIDEO), "constructor-failure case initializes SDL video");
        SDL_Window* window = SDL_CreateWindow(
            "DIRECTX3 resize constructor failure", 48, 32, SDL_WINDOW_HIDDEN);
        Check(window != nullptr, "constructor-failure case creates caller-owned SDL window");
        if (window == nullptr)
        {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            return;
        }

        bool threw = false;
        try
        {
            GraphicsRendererCreateArgs args;
            args.surface.windowId = SDL_GetWindowID(window);
            args.virtualWidth = 4097;
            args.virtualHeight = 32;
            FreeDirectRenderer renderer(args, Hooks(tracker));
        }
        catch (const std::exception&)
        {
            threw = true;
        }
        Check(threw, "unsupported constructor dimensions throw through existing native validation");
        Check(
            tracker.Count(
                FreeDirectResourceKindEXT::DirectDraw,
                FreeDirectResourceEventEXT::Acquired) == 1 &&
            tracker.Count(
                FreeDirectResourceKindEXT::DirectDraw,
                FreeDirectResourceEventEXT::Released) == 1,
            "failed construction releases its DirectDraw owner exactly once");
        Check(
            tracker.Count(
                FreeDirectResourceKindEXT::PrimarySurface,
                FreeDirectResourceEventEXT::Acquired) ==
                tracker.Count(
                    FreeDirectResourceKindEXT::PrimarySurface,
                    FreeDirectResourceEventEXT::Released) &&
            tracker.Count(
                FreeDirectResourceKindEXT::ShadowBackBuffer,
                FreeDirectResourceEventEXT::Acquired) ==
                tracker.Count(
                    FreeDirectResourceKindEXT::ShadowBackBuffer,
                    FreeDirectResourceEventEXT::Released),
            "failed construction balances every surface acquisition");
        Check(SDL_GetWindowID(window) != 0, "failed construction preserves the caller-owned window");

        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    void RunFailureStage(FreeDirectResizeFailurePointEXT failure)
    {
        const std::string prefix = std::string("failure at ") + FailureName(failure);
        ResourceTracker tracker;
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach,
            Parameters(kWidthA, kHeightA, DepthFormat::Depth24, PresentInterval::Immediate));
        auto& renderer = static_cast<FreeDirectRenderer&>(device.GetRenderer());

        Texture2D texture(device, 1, 1);
        const Color source(220, 40, 90, 255);
        texture.SetData(&source, 1);

        RenderTarget2D depthless(
            device, 17, 13, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        RenderTarget2D depthBacked(
            device, 19, 15, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
            RenderTargetUsage::PreserveContents);
        Check(
            !depthless.GetRenderTargetRenderer()->HasRealDepthBuffer(false) &&
                !depthBacked.GetRenderTargetRenderer()->HasRealDepthBuffer(true),
            prefix + " covers depthless and depth-requested targets without inventing DIRECTX3 depth");

        device.Clear(Color(11, 22, 33, 255));
        device.SetRenderTarget(&depthBacked);
        const Color targetColor(31, 73, 121, 255);
        device.Clear(targetColor);

        Viewport customViewport(2, 3, 11, 9);
        customViewport.setMinDepthProperty(0.2f);
        customViewport.setMaxDepthProperty(0.8f);
        const Rectangle customScissor(1, 2, 8, 7);
        device.setViewportProperty(customViewport);
        device.setScissorRectangleProperty(customScissor);

        DepthStencilState customDepth;
        customDepth.setDepthBufferEnableProperty(true);
        customDepth.setDepthBufferWriteEnableProperty(false);
        customDepth.setDepthBufferFunctionProperty(CompareFunction::GreaterEqual);
        customDepth.setReferenceStencilProperty(37);
        device.setDepthStencilStateProperty(customDepth);

        renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));
        const FreeDirectTestStateEXT before = renderer.GetTestStateEXT();
        const PresentationParameters ppBefore =
            device.getPresentationParametersProperty().Clone();
        const Viewport viewportBefore = device.getViewportProperty();
        const Rectangle scissorBefore = device.getScissorRectangleProperty();

        tracker.Clear();
        renderer.SetTestHooksEXT(Hooks(tracker, failure));

        PresentationParameters replacement = ppBefore.Clone();
        replacement.setBackBufferWidthProperty(kWidthB);
        replacement.setBackBufferHeightProperty(kHeightB);
        replacement.setDepthStencilFormatProperty(DepthFormat::None);
        replacement.setPresentationIntervalProperty(PresentInterval::One);

        bool threw = false;
        std::string diagnostic;
        try
        {
            device.Reset(replacement);
        }
        catch (const std::exception& exception)
        {
            threw = true;
            diagnostic = exception.what();
        }

        const FreeDirectTestStateEXT after = renderer.GetTestStateEXT();
        Check(
            threw && diagnostic.find("CNA FreeDirect: injected resize failure during") !=
                         std::string::npos,
            prefix + " throws its stage-specific diagnostic");
        Check(
            SameSurfaceSet(before, after) &&
                tracker.Count(
                    FreeDirectResourceKindEXT::PrimarySurface,
                    FreeDirectResourceEventEXT::Released, before.primarySurface) == 0 &&
                tracker.Count(
                    FreeDirectResourceKindEXT::ShadowBackBuffer,
                    FreeDirectResourceEventEXT::Released, before.shadowBackBuffer) == 0,
            prefix + " preserves both original native surface identities without destroying them");
        Check(
            after.logicalWidth == kWidthA && after.logicalHeight == kHeightA &&
                after.presentationMode == before.presentationMode,
            prefix + " preserves renderer dimensions and presentation mode");
        Check(
            after.activeRenderTarget == before.activeRenderTarget &&
                after.activeRenderTarget != nullptr,
            prefix + " preserves the active render-target binding identity");
        Check(
            SameViewport(device.getViewportProperty(), viewportBefore) &&
                SameRectangle(device.getScissorRectangleProperty(), scissorBefore),
            prefix + " preserves viewport and scissor");
        Check(
            SameDepthState(
                device.getDepthStencilStateProperty(), true, false,
                CompareFunction::GreaterEqual, 37),
            prefix + " preserves depth state");
        Check(
            device.getPresentationParametersProperty().getBackBufferWidthProperty() ==
                    ppBefore.getBackBufferWidthProperty() &&
                device.getPresentationParametersProperty().getBackBufferHeightProperty() ==
                    ppBefore.getBackBufferHeightProperty() &&
                device.getPresentationParametersProperty().getDepthStencilFormatProperty() ==
                    ppBefore.getDepthStencilFormatProperty() &&
                device.getPresentationParametersProperty().getPresentationIntervalProperty() ==
                    ppBefore.getPresentationIntervalProperty(),
            prefix + " preserves presentation parameters");
        const auto bindings = device.GetRenderTargets();
        Check(
            bindings.size() == 1 &&
                bindings[0].getRenderTargetProperty() == &depthBacked,
            prefix + " preserves the public active-target state");
        Check(
            SameColor(ReadPixel(device, 0, 0), targetColor),
            prefix + " leaves the bound render target usable");
        CheckFailureTemporaryCounts(tracker, failure, prefix);

        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const bool originalSetUsable =
            SameSurfaceSet(before, renderer.GetTestStateEXT()) &&
            SameColor(ReadPixel(device, 0, 0), Color(11, 22, 33, 255));
        Check(originalSetUsable, prefix + " proves A -> failed B -> A content remains usable");
        if (originalSetUsable)
            RenderAndPresent(device, texture, 5, 6, prefix + " renders and presents after failure");
        else
            Check(false, prefix + " renders and presents after failure");
    }

    void RunValidationCases()
    {
        ResourceTracker tracker;
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach,
            Parameters(kWidthA, kHeightA));
        auto& renderer = static_cast<FreeDirectRenderer&>(device.GetRenderer());
        renderer.SetTestHooksEXT(Hooks(tracker));

        const FreeDirectTestStateEXT initial = renderer.GetTestStateEXT();
        Viewport customViewport(3, 4, 20, 12);
        const Rectangle customScissor(2, 3, 9, 8);
        device.setViewportProperty(customViewport);
        device.setScissorRectangleProperty(customScissor);

        tracker.Clear();
        renderer.SetVirtualResolution(kWidthA, kHeightA);
        Check(
            SameSurfaceSet(initial, renderer.GetTestStateEXT()) && tracker.count == 0 &&
                SameViewport(device.getViewportProperty(), customViewport) &&
                SameRectangle(device.getScissorRectangleProperty(), customScissor),
            "same-dimension resize is the established no-op and preserves custom state");

        renderer.SetVirtualResolution(0, kHeightB);
        renderer.SetVirtualResolution(kWidthB, 0);
        renderer.SetVirtualResolution(-1, kHeightB);
        renderer.SetVirtualResolution(kWidthB, -1);
        Check(
            SameSurfaceSet(initial, renderer.GetTestStateEXT()) && tracker.count == 0 &&
                renderer.GetTestStateEXT().logicalWidth == kWidthA &&
                renderer.GetTestStateEXT().logicalHeight == kHeightA,
            "zero and negative dimensions use existing public no-op validation");

        const auto expectNativeRejection = [&](int width, int height, const char* label)
        {
            tracker.Clear();
            bool threw = false;
            try
            {
                renderer.SetVirtualResolution(width, height);
            }
            catch (const std::exception&)
            {
                threw = true;
            }
            Check(
                threw && SameSurfaceSet(initial, renderer.GetTestStateEXT()) &&
                    renderer.GetTestStateEXT().logicalWidth == kWidthA &&
                    renderer.GetTestStateEXT().logicalHeight == kHeightA &&
                    device.getPresentationParametersProperty().getBackBufferWidthProperty() ==
                        kWidthA &&
                    device.getPresentationParametersProperty().getBackBufferHeightProperty() ==
                        kHeightA &&
                    tracker.Count(
                        FreeDirectResourceKindEXT::PrimarySurface,
                        FreeDirectResourceEventEXT::Released, initial.primarySurface) == 0 &&
                    tracker.Count(
                        FreeDirectResourceKindEXT::ShadowBackBuffer,
                        FreeDirectResourceEventEXT::Released, initial.shadowBackBuffer) == 0,
                label);
        };

        expectNativeRejection(
            4097, kHeightA,
            "unsupported dimension uses free-direct's existing 4096 cap and preserves A");
        expectNativeRejection(
            std::numeric_limits<int>::max(), kHeightA,
            "excessive dimension uses existing native validation and preserves A");
    }

    void CheckSuccessfulCommitEvents(
        const ResourceTracker& tracker, const FreeDirectTestStateEXT& oldState,
        const FreeDirectTestStateEXT& newState, const std::string& prefix)
    {
        Check(
            newState.primarySurface != nullptr &&
                newState.shadowBackBuffer != nullptr &&
                newState.primarySurface != oldState.primarySurface &&
                newState.shadowBackBuffer != oldState.shadowBackBuffer,
            prefix + " commits distinct replacement resource identities");
        Check(
            tracker.Count(
                FreeDirectResourceKindEXT::PrimarySurface,
                FreeDirectResourceEventEXT::Acquired, newState.primarySurface) == 1 &&
            tracker.Count(
                FreeDirectResourceKindEXT::ShadowBackBuffer,
                FreeDirectResourceEventEXT::Acquired, newState.shadowBackBuffer) == 1,
            prefix + " acquires each replacement exactly once");
        Check(
            tracker.Count(
                FreeDirectResourceKindEXT::PrimarySurface,
                FreeDirectResourceEventEXT::Released, oldState.primarySurface) == 1 &&
            tracker.Count(
                FreeDirectResourceKindEXT::ShadowBackBuffer,
                FreeDirectResourceEventEXT::Released, oldState.shadowBackBuffer) == 1,
            prefix + " releases each old surface exactly once after commit");
        Check(
            tracker.Count(
                FreeDirectResourceKindEXT::PrimarySurface,
                FreeDirectResourceEventEXT::Released, newState.primarySurface) == 0 &&
            tracker.Count(
                FreeDirectResourceKindEXT::ShadowBackBuffer,
                FreeDirectResourceEventEXT::Released, newState.shadowBackBuffer) == 0,
            prefix + " leaves the committed replacements live");
    }

    void RunSuccessfulSequence()
    {
        ResourceTracker tracker;
        GraphicsDevice device(
            GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::Reach,
            Parameters(kWidthA, kHeightA, DepthFormat::None));
        auto& renderer = static_cast<FreeDirectRenderer&>(device.GetRenderer());
        renderer.SetTestHooksEXT(Hooks(tracker));
        renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));

        Texture2D texture(device, 1, 1);
        const Color source(220, 40, 90, 255);
        texture.SetData(&source, 1);

        DepthStencilState customDepth;
        customDepth.setDepthBufferEnableProperty(true);
        customDepth.setDepthBufferWriteEnableProperty(false);
        customDepth.setDepthBufferFunctionProperty(CompareFunction::GreaterEqual);
        customDepth.setReferenceStencilProperty(41);
        device.setDepthStencilStateProperty(customDepth);
        device.setViewportProperty(Viewport(4, 5, 20, 15));
        device.setScissorRectangleProperty(Rectangle(3, 4, 11, 10));

        const FreeDirectTestStateEXT stateA = renderer.GetTestStateEXT();
        tracker.Clear();
        device.Reset(Parameters(kWidthB, kHeightB, DepthFormat::None));
        const FreeDirectTestStateEXT stateB = renderer.GetTestStateEXT();
        CheckSuccessfulCommitEvents(tracker, stateA, stateB, "successful A -> B");
        Check(
            stateB.logicalWidth == kWidthB && stateB.logicalHeight == kHeightB &&
                stateB.presentationMode == stateA.presentationMode &&
                device.getPresentationParametersProperty().getBackBufferWidthProperty() ==
                    kWidthB &&
                device.getPresentationParametersProperty().getBackBufferHeightProperty() ==
                    kHeightB,
            "successful A -> B updates renderer and presentation dimensions consistently");
        Check(
            SameViewport(device.getViewportProperty(), Viewport(0, 0, kWidthB, kHeightB)) &&
                SameRectangle(
                    device.getScissorRectangleProperty(),
                    Rectangle(0, 0, kWidthB, kHeightB)),
            "successful A -> B resets viewport and scissor to the new extent");
        Check(
            SameDepthState(
                device.getDepthStencilStateProperty(), true, false,
                CompareFunction::GreaterEqual, 41),
            "successful A -> B preserves depth state");
        RenderAndPresent(
            device, texture, kWidthB - 2, kHeightB - 2,
            "successful A -> B renders and presents at the new extent");

        tracker.Clear();
        device.Reset(Parameters(kWidthC, kHeightC, DepthFormat::Depth24));
        const FreeDirectTestStateEXT stateC = renderer.GetTestStateEXT();
        CheckSuccessfulCommitEvents(tracker, stateB, stateC, "successful B -> C");
        Check(
            stateC.logicalWidth == kWidthC && stateC.logicalHeight == kHeightC &&
                stateC.presentationMode == stateA.presentationMode &&
                device.getPresentationParametersProperty().getDepthStencilFormatProperty() ==
                    DepthFormat::Depth24,
            "successful B -> C updates dimensions and requested presentation depth consistently");
        Check(
            SameViewport(device.getViewportProperty(), Viewport(0, 0, kWidthC, kHeightC)) &&
                SameRectangle(
                    device.getScissorRectangleProperty(),
                    Rectangle(0, 0, kWidthC, kHeightC)),
            "successful B -> C updates viewport and scissor consistently");
        RenderAndPresent(
            device, texture, kWidthC - 2, kHeightC - 2,
            "successful B -> C renders and presents at the new extent");

        tracker.Clear();
        renderer.SetVirtualResolution(kWidthC, kHeightC);
        Check(
            SameSurfaceSet(stateC, renderer.GetTestStateEXT()) && tracker.count == 0,
            "repeated resize to C is a resource-identity-preserving no-op");

        RenderTarget2D depthless(
            device, 21, 17, false, SurfaceFormat::Color, DepthFormat::None, 0,
            RenderTargetUsage::PreserveContents);
        RenderTarget2D depthBacked(
            device, 23, 19, false, SurfaceFormat::Color, DepthFormat::Depth24, 0,
            RenderTargetUsage::PreserveContents);
        Check(
            !depthless.GetRenderTargetRenderer()->HasRealDepthBuffer(false) &&
                !depthBacked.GetRenderTargetRenderer()->HasRealDepthBuffer(true),
            "successful sequence retains honest depthless behavior for both RT configurations");

        device.SetRenderTarget(&depthless);
        device.Clear(Color(13, 29, 47, 255));
        const bool depthlessUsable =
            SameColor(ReadPixel(device, 0, 0), Color(13, 29, 47, 255));
        device.SetRenderTarget(&depthBacked);
        device.Clear(Color(17, 37, 59, 255));
        const bool depthBackedUsable =
            SameColor(ReadPixel(device, 0, 0), Color(17, 37, 59, 255));
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(
            depthlessUsable && depthBackedUsable,
            "depthless and depth-requested render targets remain usable after A -> B -> C");
    }
}

int main()
{
    RunLifecycleCase(DepthFormat::None);
    RunLifecycleCase(DepthFormat::Depth24);
    RunConstructorFailureCase();

    constexpr std::array failurePoints{
        FreeDirectResizeFailurePointEXT::ShadowBackBufferCreation,
        FreeDirectResizeFailurePointEXT::ShadowBackBufferValidation,
        FreeDirectResizeFailurePointEXT::DisplayModeBinding,
        FreeDirectResizeFailurePointEXT::PrimarySurfaceCreation,
        FreeDirectResizeFailurePointEXT::PrimarySurfaceValidation,
        FreeDirectResizeFailurePointEXT::SurfaceSetCommit};
    for (const auto failure : failurePoints)
        RunFailureStage(failure);

    RunValidationCases();
    RunSuccessfulSequence();

    std::printf("=== %d/%d PASS ===\n", passCount, passCount + failCount);
    return failCount == 0 ? 0 : 1;
}
