// SPDX-License-Identifier: MS-PL
// REMED-GFX-028: transactional SDL_GPU renderer construction and lazy-initialization lifetime
// regression. Failure injection is per renderer instance and still uses real SDL_GPU resources.
// Every failed constructor must balance acquisition/release callbacks, leave no window-registry
// entry, preserve the caller-owned SDL_Window, and permit an immediately succeeding renderer.

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include "Microsoft/Xna/Framework/Matrix.hpp"

#include "common/PixelTestGame.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>

using namespace CNA::Internal::Renderers;
using namespace CNA::Internal::Renderers::SdlGpu;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr std::size_t kResourceKindCount = 7;

    struct ResourceTracker
    {
        std::array<int, kResourceKindCount> acquired{};
        std::array<int, kResourceKindCount> released{};

        static void OnResource(void* context, SdlGpuResourceKindEXT resource,
                               SdlGpuResourceEventEXT event) noexcept
        {
            auto& tracker = *static_cast<ResourceTracker*>(context);
            const std::size_t index = static_cast<std::size_t>(resource);
            if (event == SdlGpuResourceEventEXT::Acquired)
                ++tracker.acquired[index];
            else
                ++tracker.released[index];
        }

        [[nodiscard]] int Acquired(SdlGpuResourceKindEXT resource) const
        {
            return acquired[static_cast<std::size_t>(resource)];
        }

        [[nodiscard]] int Released(SdlGpuResourceKindEXT resource) const
        {
            return released[static_cast<std::size_t>(resource)];
        }

        [[nodiscard]] bool Balanced() const
        {
            return acquired == released;
        }
    };

    struct FailureCase
    {
        SdlGpuFailurePointEXT point;
        const char* label;
    };

    constexpr std::array<FailureCase, 30> kConstructionFailures{{
        {SdlGpuFailurePointEXT::DeviceCreation, "device creation"},
        {SdlGpuFailurePointEXT::WindowClaim, "window claiming"},
        {SdlGpuFailurePointEXT::SwapchainSetup, "swapchain setup"},
        {SdlGpuFailurePointEXT::DepthStencilFormatQuery, "depth/stencil format query"},
        {SdlGpuFailurePointEXT::SpriteVertexShaderCreation, "sprite vertex shader"},
        {SdlGpuFailurePointEXT::SpriteFragmentShaderCreation, "sprite fragment shader"},
        {SdlGpuFailurePointEXT::ColoredVertexShaderCreation, "colored vertex shader"},
        {SdlGpuFailurePointEXT::ColoredFragmentShaderCreation, "colored fragment shader"},
        {SdlGpuFailurePointEXT::TexturedVertexShaderCreation, "textured vertex shader"},
        {SdlGpuFailurePointEXT::ColoredTexturedVertexShaderCreation, "colored-textured vertex shader"},
        {SdlGpuFailurePointEXT::TexturedFragmentShaderCreation, "textured fragment shader"},
        {SdlGpuFailurePointEXT::LitTexturedVertexShaderCreation, "lit-textured vertex shader"},
        {SdlGpuFailurePointEXT::LitTexturedFragmentShaderCreation, "lit-textured fragment shader"},
        {SdlGpuFailurePointEXT::AlphaTestVertexShaderCreation, "alpha-test vertex shader"},
        {SdlGpuFailurePointEXT::AlphaTestColoredVertexShaderCreation, "alpha-test colored vertex shader"},
        {SdlGpuFailurePointEXT::AlphaTestFragmentShaderCreation, "alpha-test fragment shader"},
        {SdlGpuFailurePointEXT::DualTextureVertexShaderCreation, "dual-texture vertex shader"},
        {SdlGpuFailurePointEXT::DualTextureColoredVertexShaderCreation, "dual-texture colored vertex shader"},
        {SdlGpuFailurePointEXT::DualTextureFragmentShaderCreation, "dual-texture fragment shader"},
        {SdlGpuFailurePointEXT::EnvMapVertexShaderCreation, "environment-map vertex shader"},
        {SdlGpuFailurePointEXT::EnvMapFragmentShaderCreation, "environment-map fragment shader"},
        {SdlGpuFailurePointEXT::SkinnedVertexShaderCreation, "skinned vertex shader"},
        {SdlGpuFailurePointEXT::SkinnedColoredVertexShaderCreation, "skinned-colored vertex shader"},
        {SdlGpuFailurePointEXT::SkinnedColoredFragmentShaderCreation, "skinned-colored fragment shader"},
        {SdlGpuFailurePointEXT::PbrVertexShaderCreation, "PBR vertex shader"},
        {SdlGpuFailurePointEXT::PbrSkinnedVertexShaderCreation, "skinned PBR vertex shader"},
        {SdlGpuFailurePointEXT::PbrFragmentShaderCreation, "PBR fragment shader"},
        {SdlGpuFailurePointEXT::WindowMetricsInitialization, "window metrics"},
        {SdlGpuFailurePointEXT::RendererRegistration, "renderer registration"},
        {SdlGpuFailurePointEXT::AfterRendererRegistration, "post-registration commit"}
    }};

    class TestRun
    {
    public:
        void Check(bool ok, const std::string& label)
        {
            ++checks_;
            if (!ok)
            {
                ++failures_;
                std::printf("[FAIL] %s\n", label.c_str());
            }
        }

        [[nodiscard]] int Result() const
        {
            std::printf("=== %d/%d PASS ===\n", checks_ - failures_, checks_);
            return failures_ == 0 ? 0 : 1;
        }

        void ExerciseConstructionFailure(SDL_Window* window, const FailureCase& failure)
        {
            ResourceTracker failedTracker;
            const SdlGpuTestHooksEXT failedHooks{
                failure.point, &failedTracker, &ResourceTracker::OnResource};

            bool threw = false;
            std::string diagnostic;
            try
            {
                SdlGpuRenderer renderer(
                    window, 64, 64, CnaPresentationMode::FixedHeightDynamicWidth, 0,
                    failedHooks);
            }
            catch (const std::exception& exception)
            {
                threw = true;
                diagnostic = exception.what();
            }

            const std::string prefix = std::string("constructor failure at ") + failure.label;
            Check(threw, prefix + " throws");
            Check(diagnostic.find("CNA SDL_GPU: injected failure during") != std::string::npos,
                  prefix + " preserves stage diagnostic");
            Check(failedTracker.Balanced(),
                  prefix + " releases every acquired device/window/shader resource");
            Check(IGraphicsRenderer::GetForWindow(SDL_GetWindowID(window)) == nullptr,
                  prefix + " never leaves a usable renderer registered");
            Check(SDL_GetWindowID(window) != 0,
                  prefix + " preserves the caller-owned SDL window");

            ResourceTracker successTracker;
            const SdlGpuTestHooksEXT successHooks{
                SdlGpuFailurePointEXT::None, &successTracker, &ResourceTracker::OnResource};
            bool usable = true;
            try
            {
                {
                    SdlGpuRenderer renderer(
                        window, 64, 64, CnaPresentationMode::FixedHeightDynamicWidth, 0,
                        successHooks);
                    usable = IGraphicsRenderer::GetForWindow(SDL_GetWindowID(window)) == &renderer &&
                             renderer.Device() != nullptr;
                    renderer.Clear(0.05f, 0.10f, 0.15f, 1.0f);
                    renderer.Present();
                }
                usable = usable && IGraphicsRenderer::GetForWindow(SDL_GetWindowID(window)) == nullptr;
            }
            catch (const std::exception& exception)
            {
                usable = false;
                std::printf("       unexpected recovery exception: %s\n", exception.what());
            }
            Check(usable, prefix + " permits an immediately usable succeeding renderer");
            Check(successTracker.Balanced(),
                  prefix + " succeeding renderer destroys every resource exactly once");
            Check(successTracker.Acquired(SdlGpuResourceKindEXT::Device) == 1 &&
                      successTracker.Released(SdlGpuResourceKindEXT::Device) == 1 &&
                      successTracker.Acquired(SdlGpuResourceKindEXT::WindowClaim) == 1 &&
                      successTracker.Released(SdlGpuResourceKindEXT::WindowClaim) == 1 &&
                      successTracker.Acquired(SdlGpuResourceKindEXT::Shader) == 23 &&
                      successTracker.Released(SdlGpuResourceKindEXT::Shader) == 23,
                  prefix + " succeeding renderer owns one device/claim and 23 shaders");
        }

        void ExerciseLazyFailure(SDL_Window* window, SdlGpuFailurePointEXT point,
                                 const char* label)
        {
            ResourceTracker tracker;
            const SdlGpuTestHooksEXT hooks{point, &tracker, &ResourceTracker::OnResource};
            bool threw = false;
            bool recovered = false;
            try
            {
                {
                    SdlGpuRenderer renderer(
                        window, 64, 64, CnaPresentationMode::FixedHeightDynamicWidth, 0,
                        hooks);

                    try
                    {
                        if (point == SdlGpuFailurePointEXT::FrameCommandBufferAcquisition)
                        {
                            renderer.Clear(0.1f, 0.2f, 0.3f, 1.0f);
                            renderer.Present();
                        }
                        else
                        {
                            renderer.InitializeSpritePipelineAndSamplerForTestEXT();
                        }
                    }
                    catch (const std::exception& exception)
                    {
                        threw = std::string(exception.what()).find(
                                    "CNA SDL_GPU: injected failure during") !=
                                std::string::npos;
                    }

                    if (point == SdlGpuFailurePointEXT::FrameCommandBufferAcquisition)
                    {
                        const bool commandBalancedAfterFailure =
                            tracker.Acquired(SdlGpuResourceKindEXT::FrameCommandBuffer) ==
                            tracker.Released(SdlGpuResourceKindEXT::FrameCommandBuffer);
                        Check(commandBalancedAfterFailure,
                              std::string(label) +
                                  " failure closes/submits its frame command resource");
                        renderer.Present();
                    }
                    else
                    {
                        renderer.InitializeSpritePipelineAndSamplerForTestEXT();
                    }
                    recovered = IGraphicsRenderer::GetForWindow(SDL_GetWindowID(window)) == &renderer;
                }
            }
            catch (const std::exception& exception)
            {
                std::printf("       unexpected lazy recovery exception: %s\n", exception.what());
            }

            Check(threw, std::string(label) + " injected failure is observed");
            Check(recovered, std::string(label) + " failure is one-shot and retryable");
            Check(tracker.Balanced(),
                  std::string(label) + " resources are destroyed exactly once");
            Check(IGraphicsRenderer::GetForWindow(SDL_GetWindowID(window)) == nullptr,
                  std::string(label) + " teardown unregisters exactly once");
        }

        void ExerciseFallbackFailure(SDL_Window* window, SdlGpuFailurePointEXT point,
                                     const char* label)
        {
            ResourceTracker tracker;
            const SdlGpuTestHooksEXT hooks{point, &tracker, &ResourceTracker::OnResource};
            bool threw = false;
            bool rolledBack = false;
            bool recovered = false;
            try
            {
                {
                    SdlGpuRenderer renderer(
                        window, 64, 64, CnaPresentationMode::FixedHeightDynamicWidth, 0,
                        hooks);
                    ImageData pixels{1, 1, {255, 255, 255, 255}, 1};
                    auto texture = renderer.CreateTexture(pixels);
                    auto vertices = renderer.CreateVertexBuffer(3);
                    std::array<float, 36> vertexData{};
                    vertices->SetData(vertexData.data(), 3, 48);

                    GpuDrawParams params{};
                    params.pbr = true;
                    params.texture0 = texture.get();
                    try
                    {
                        renderer.DrawPrimitivesEx(
                            *vertices, Matrix::getIdentityProperty(),
                            Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                            PrimitiveType::TriangleList, 1, params);
                    }
                    catch (const std::exception& exception)
                    {
                        threw = std::string(exception.what()).find(
                                    "CNA SDL_GPU: injected failure during") !=
                                std::string::npos;
                    }

                    rolledBack =
                        tracker.Acquired(SdlGpuResourceKindEXT::DefaultTexture) ==
                        tracker.Released(SdlGpuResourceKindEXT::DefaultTexture);

                    renderer.DrawPrimitivesEx(
                        *vertices, Matrix::getIdentityProperty(),
                        Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                        PrimitiveType::TriangleList, 1, params);
                    renderer.Present();
                    recovered = true;
                }
            }
            catch (const std::exception& exception)
            {
                std::printf("       unexpected fallback recovery exception: %s\n",
                            exception.what());
            }

            Check(threw, std::string(label) + " injected failure is observed");
            Check(rolledBack,
                  std::string(label) + " rolls back an earlier fallback allocation");
            Check(recovered, std::string(label) + " can be initialized immediately afterward");
            Check(tracker.Balanced(),
                  std::string(label) + " resources are destroyed exactly once");
        }

        void ExerciseIndependentInstances(SDL_Window* firstWindow, SDL_Window* secondWindow)
        {
            ResourceTracker firstTracker;
            ResourceTracker secondTracker;
            const SdlGpuTestHooksEXT firstHooks{
                SdlGpuFailurePointEXT::None, &firstTracker, &ResourceTracker::OnResource};
            const SdlGpuTestHooksEXT secondHooks{
                SdlGpuFailurePointEXT::None, &secondTracker, &ResourceTracker::OnResource};

            bool independent = false;
            try
            {
                auto first = std::make_unique<SdlGpuRenderer>(
                    firstWindow, 64, 64, CnaPresentationMode::FixedHeightDynamicWidth, 0,
                    firstHooks);
                auto second = std::make_unique<SdlGpuRenderer>(
                    secondWindow, 64, 64, CnaPresentationMode::FixedHeightDynamicWidth, 0,
                    secondHooks);

                independent =
                    first->Device() != second->Device() &&
                    IGraphicsRenderer::GetForWindow(SDL_GetWindowID(firstWindow)) == first.get() &&
                    IGraphicsRenderer::GetForWindow(SDL_GetWindowID(secondWindow)) == second.get();

                first.reset();
                independent =
                    independent &&
                    IGraphicsRenderer::GetForWindow(SDL_GetWindowID(firstWindow)) == nullptr &&
                    IGraphicsRenderer::GetForWindow(SDL_GetWindowID(secondWindow)) == second.get();
                second->Clear(0.2f, 0.1f, 0.05f, 1.0f);
                second->Present();
                second.reset();
            }
            catch (const std::exception& exception)
            {
                std::printf("       unexpected multi-instance exception: %s\n",
                            exception.what());
                independent = false;
            }

            Check(independent,
                  "multiple renderers own distinct devices and one teardown leaves the other usable");
            Check(firstTracker.Balanced() && secondTracker.Balanced(),
                  "multiple renderer resource callbacks remain instance-local and balanced");
            Check(SDL_GetWindowID(firstWindow) != 0 && SDL_GetWindowID(secondWindow) != 0,
                  "multiple renderer teardown preserves both caller-owned windows");
        }

    private:
        int checks_ = 0;
        int failures_ = 0;
    };
}

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("[FAIL] SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* firstWindow =
        SDL_CreateWindow("CNA GFX-028 primary", 64, 64, SDL_WINDOW_HIDDEN);
    SDL_Window* secondWindow =
        SDL_CreateWindow("CNA GFX-028 secondary", 64, 64, SDL_WINDOW_HIDDEN);
    if (firstWindow == nullptr || secondWindow == nullptr)
    {
        std::printf("[FAIL] SDL_CreateWindow: %s\n", SDL_GetError());
        if (secondWindow != nullptr) SDL_DestroyWindow(secondWindow);
        if (firstWindow != nullptr) SDL_DestroyWindow(firstWindow);
        SDL_Quit();
        return 1;
    }

    TestRun test;
    for (const FailureCase& failure : kConstructionFailures)
        test.ExerciseConstructionFailure(firstWindow, failure);

    test.ExerciseLazyFailure(
        firstWindow, SdlGpuFailurePointEXT::FrameCommandBufferAcquisition,
        "frame command-buffer acquisition");
    test.ExerciseLazyFailure(
        firstWindow, SdlGpuFailurePointEXT::GraphicsPipelineCreation,
        "graphics pipeline creation");
    test.ExerciseLazyFailure(
        firstWindow, SdlGpuFailurePointEXT::SamplerCreation,
        "sampler creation");
    test.ExerciseFallbackFailure(
        firstWindow, SdlGpuFailurePointEXT::DefaultWhiteTextureCreation,
        "default white texture creation");
    test.ExerciseFallbackFailure(
        firstWindow, SdlGpuFailurePointEXT::DefaultFlatNormalTextureCreation,
        "default flat-normal texture creation");
    test.ExerciseIndependentInstances(firstWindow, secondWindow);

    SDL_DestroyWindow(secondWindow);
    SDL_DestroyWindow(firstWindow);
    SDL_Quit();
    return test.Result();
}
