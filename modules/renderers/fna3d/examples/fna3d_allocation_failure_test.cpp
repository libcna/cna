// SPDX-License-Identifier: MS-PL
// FNA3D-50: deterministic native-allocation failure.
//
// FNA3D's drivers fail a creation only when the GPU or the driver runs out of something, so
// without a seam none of the renderer's rollback paths ever runs. Fna3dAllocationFailureInjectionEXT
// fails one chosen allocation point: a native creation returns null without reaching FNA3D, and a
// wrapper allocation that would take ownership of a native object throws std::bad_alloc. Every
// scenario below arms ordinals 0, 1, 2, ... in turn until its operation passes all of its points,
// and for each failed point requires that
//   - the operation reports the failure (it throws or returns null) rather than half-succeeding;
//   - every native object family is back at the count it had before the operation, so nothing that
//     was created before the failing point leaked;
//   - the same operation, run again with nothing injected, succeeds -- no stale state remains.
// Renderer construction may instead degrade where a failed point is only a capability probe; the
// renderer it produces must then still clear and read back, and answer its capabilities honestly.
// The growth scenarios additionally draw after a failed growth, because the old buffer, not a null
// one, must still be bound. At the end every family must be back at zero.
//
// Exit code 0 = all checks PASS, 1 = any FAILs, 77 = skipped (no display).

#include "CNA/GraphicsCapability.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dNativeAllocation.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dRenderer.hpp"
#include "CNA/Internal/Renderers/Fna3d/Fna3dWindowFlags.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using CNA::Internal::Renderers::GpuDrawParams;
using CNA::Internal::Renderers::GraphicsRendererCreateArgs;
using CNA::Internal::Renderers::IIndexBufferRenderer;
using CNA::Internal::Renderers::ImageData;
using CNA::Internal::Renderers::IVertexBufferRenderer;
using CNA::Internal::Renderers::Fna3d::Fna3dAllocationFailureInjectionEXT;
using CNA::Internal::Renderers::Fna3d::Fna3dNativeObjectEXT;
using CNA::Internal::Renderers::Fna3d::Fna3dRenderer;
using CNA::Internal::Renderers::Fna3d::Detail::PrepareWindowNeedsOpenGl;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;

namespace
{
    using Injection = Fna3dAllocationFailureInjectionEXT;

    constexpr std::size_t kFamilies = static_cast<std::size_t>(Fna3dNativeObjectEXT::Count);
    constexpr std::uint32_t kMaxPoints = 64;
    constexpr int kSize = 32;
    constexpr const char* kFamilyNames[kFamilies] = {
        "device", "effect", "texture", "renderbuffer", "vertex buffer", "index buffer", "query"};

    using Live = std::array<std::int64_t, kFamilies>;

    enum class Outcome
    {
        Succeeded,
        ReturnedNull,
        Threw
    };

    int failures = 0;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        if (!condition)
        {
            ++failures;
        }
    }

    Live Snapshot()
    {
        Live live{};
        for (std::size_t family = 0; family < kFamilies; ++family)
        {
            live[family] =
                Injection::GetLiveCountEXT(static_cast<Fna3dNativeObjectEXT>(family));
        }
        return live;
    }

    std::string Describe(const Live& before, const Live& after)
    {
        std::string text;
        for (std::size_t family = 0; family < kFamilies; ++family)
        {
            if (before[family] != after[family])
            {
                text += std::string(text.empty() ? "" : ", ") + kFamilyNames[family] + " " +
                        std::to_string(before[family]) + " -> " + std::to_string(after[family]);
            }
        }
        return text;
    }

    template <typename Operation>
    Outcome Attempt(Operation&& operation, std::string& error)
    {
        try
        {
            return operation();
        }
        catch (const std::exception& exception)
        {
            error = exception.what();
        }
        catch (...)
        {
            error = "non-standard exception";
        }
        return Outcome::Threw;
    }

    std::string Point(const char* label, std::uint32_t ordinal)
    {
        return std::string(label) + ", allocation point " + std::to_string(ordinal);
    }

    std::unique_ptr<Fna3dRenderer> CreateRenderer(SDL_Window* window)
    {
        GraphicsRendererCreateArgs args;
        args.surface.windowId = SDL_GetWindowID(window);
        args.virtualWidth = kSize;
        args.virtualHeight = kSize;
        args.swapInterval = 0;
        return std::make_unique<Fna3dRenderer>(args);
    }

    /**
     * Walks every allocation point `operation` passes on one long-lived renderer. The operation
     * must release everything it creates before it returns, so the live counts after it equal the
     * counts before it whether it failed or not.
     */
    template <typename Operation>
    void WalkAllocationPoints(const char* label, std::uint32_t minimumPoints, bool mayDegrade,
                              Operation&& operation)
    {
        std::uint32_t points = 0;
        bool clean = true;
        for (std::uint32_t ordinal = 0; ordinal < kMaxPoints; ++ordinal)
        {
            const Live before = Snapshot();
            std::string error;
            Injection::ArmEXT(ordinal);
            const Outcome outcome = Attempt(operation, error);
            const bool consumed = !Injection::IsArmedEXT();
            Injection::ClearEXT();
            const Live after = Snapshot();
            if (after != before)
            {
                Check(false, Point(label, ordinal) + " leaked: " + Describe(before, after));
                clean = false;
            }
            if (!consumed)
            {
                if (outcome != Outcome::Succeeded)
                {
                    Check(false, std::string(label) + " fails with nothing injected: " + error);
                    clean = false;
                }
                break;
            }
            ++points;
            if (outcome == Outcome::Succeeded && !mayDegrade)
            {
                Check(false, Point(label, ordinal) + " failed, yet the operation reported success");
                clean = false;
            }

            std::string retryError;
            const Outcome retry = Attempt(operation, retryError);
            const Live afterRetry = Snapshot();
            if (retry != Outcome::Succeeded || afterRetry != before)
            {
                Check(false, Point(label, ordinal) + " left stale state: the retry " +
                                 (retry == Outcome::Succeeded ? "leaked " +
                                                                    Describe(before, afterRetry)
                                                              : "failed: " + retryError));
                clean = false;
            }
        }
        Check(clean && points >= minimumPoints,
              std::string(label) + ": " + std::to_string(points) +
                  " allocation points, each failure reported and rolled back");
    }

    /**
     * Walks the allocation points of an operation whose success leaves a resource behind in the
     * renderer (a lazily created internal resource, a grown buffer): every ordinal gets a fresh
     * renderer, prepared identically, so the point sequence is the same each time. `teardown`
     * releases what `prepare` made while its renderer is still alive.
     *
     * A failed point may legitimately leave behind what the operation completed before it -- a
     * lazily created resource the renderer keeps -- so the leak check here is against a clean run:
     * a failure keeps no more of any family than a success would, a failure followed by a
     * successful retry holds exactly what one clean success holds, and the renderer's teardown
     * returns every family to where it was before the renderer existed.
     */
    template <typename Prepare, typename Operation, typename Verify, typename Teardown>
    void WalkOnFreshRenderers(const char* label, std::uint32_t minimumPoints, SDL_Window* window,
                              Prepare&& prepare, Operation&& operation, Verify&& verify,
                              Teardown&& teardown)
    {
        const Live idle = Snapshot();
        Live cleanRun{};
        {
            std::unique_ptr<Fna3dRenderer> renderer = CreateRenderer(window);
            prepare(*renderer);
            const Live before = Snapshot();
            std::string error;
            if (Attempt([&] { return operation(*renderer); }, error) != Outcome::Succeeded ||
                !verify(*renderer))
            {
                Check(false, std::string(label) + " fails with nothing injected: " + error);
                teardown();
                return;
            }
            const Live after = Snapshot();
            for (std::size_t family = 0; family < kFamilies; ++family)
            {
                cleanRun[family] = after[family] - before[family];
            }
            teardown();
        }

        const auto holdsCleanRun = [&](const Live& before, const Live& now, bool atMost) {
            for (std::size_t family = 0; family < kFamilies; ++family)
            {
                const std::int64_t held = now[family] - before[family];
                if (atMost ? held > cleanRun[family] : held != cleanRun[family])
                {
                    return false;
                }
            }
            return true;
        };
        const auto plus = [&](const Live& before) {
            Live expected = before;
            for (std::size_t family = 0; family < kFamilies; ++family)
            {
                expected[family] += cleanRun[family];
            }
            return expected;
        };

        std::uint32_t points = 0;
        bool clean = true;
        for (std::uint32_t ordinal = 0; ordinal < kMaxPoints; ++ordinal)
        {
            bool done = false;
            {
                std::unique_ptr<Fna3dRenderer> renderer = CreateRenderer(window);
                prepare(*renderer);

                const Live before = Snapshot();
                std::string error;
                Injection::ArmEXT(ordinal);
                const Outcome outcome = Attempt([&] { return operation(*renderer); }, error);
                const bool consumed = !Injection::IsArmedEXT();
                Injection::ClearEXT();
                const Live after = Snapshot();

                if (!consumed)
                {
                    done = true;
                }
                else
                {
                    ++points;
                    if (!holdsCleanRun(before, after, true))
                    {
                        Check(false, Point(label, ordinal) + " leaked: " +
                                         Describe(plus(before), after) + " beyond a clean run");
                        clean = false;
                    }
                    if (outcome == Outcome::Succeeded)
                    {
                        Check(false,
                              Point(label, ordinal) + " failed, yet the operation reported success");
                        clean = false;
                    }

                    std::string retryError;
                    const Outcome retry =
                        Attempt([&] { return operation(*renderer); }, retryError);
                    const Live afterRetry = Snapshot();
                    if (retry != Outcome::Succeeded || !verify(*renderer))
                    {
                        Check(false, Point(label, ordinal) + " left stale state: the retry " +
                                         (retry == Outcome::Succeeded ? "renders wrongly"
                                                                      : "failed: " + retryError));
                        clean = false;
                    }
                    else if (!holdsCleanRun(before, afterRetry, false))
                    {
                        Check(false, Point(label, ordinal) + " and its retry hold " +
                                         Describe(plus(before), afterRetry) +
                                         " against one clean run");
                        clean = false;
                    }
                }
                teardown();
            }
            if (Snapshot() != idle)
            {
                Check(false, Point(label, ordinal) + " leaked past its renderer: " +
                                 Describe(idle, Snapshot()));
                clean = false;
            }
            if (done)
            {
                break;
            }
        }
        Check(clean && points >= minimumPoints,
              std::string(label) + ": " + std::to_string(points) +
                  " allocation points, each failure reported and rolled back");
    }

    bool PixelIs(Fna3dRenderer& renderer, int x, int y, std::uint8_t r, std::uint8_t g,
                 std::uint8_t b)
    {
        std::uint8_t pixel[4] = {};
        renderer.ReadBackbuffer(x, y, 1, 1, pixel);
        const auto near = [](std::uint8_t actual, std::uint8_t expected) {
            return actual + 2 >= expected && actual <= expected + 2;
        };
        return near(pixel[0], r) && near(pixel[1], g) && near(pixel[2], b);
    }

    struct ColoredVertex
    {
        float x;
        float y;
        float z;
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        std::uint8_t a;
    };
    static_assert(sizeof(ColoredVertex) == 16, "the stride-16 VertexPositionColor layout");

    // Both windings of one triangle covering the whole viewport, so the draw does not depend on
    // the cull mode; any further vertices are degenerate.
    std::vector<ColoredVertex> FullScreen(std::size_t count, std::uint8_t r, std::uint8_t g,
                                          std::uint8_t b)
    {
        std::vector<ColoredVertex> vertices(count, ColoredVertex{0, 0, 0, 0, 0, 0, 255});
        const ColoredVertex corners[6] = {
            {-1, -1, 0, r, g, b, 255}, {3, -1, 0, r, g, b, 255}, {-1, 3, 0, r, g, b, 255},
            {-1, -1, 0, r, g, b, 255}, {-1, 3, 0, r, g, b, 255}, {3, -1, 0, r, g, b, 255}};
        for (std::size_t i = 0; i < 6 && i < count; ++i)
        {
            vertices[i] = corners[i];
        }
        return vertices;
    }

    bool DrawsColour(Fna3dRenderer& renderer, const IVertexBufferRenderer& vertices,
                     const IIndexBufferRenderer* indices, std::uint8_t r, std::uint8_t g,
                     std::uint8_t b)
    {
        renderer.ClearColorAndDepth(0.0f, 0.0f, 0.0f, 1.0f, 1.0f);
        const Matrix& identity = Matrix::getIdentityProperty();
        if (indices != nullptr)
        {
            renderer.DrawIndexedColoredPrimitives(vertices, *indices, identity, identity, identity,
                                                  PrimitiveType::TriangleList, 2);
        }
        else
        {
            renderer.DrawColoredPrimitives(vertices, identity, identity, identity,
                                           PrimitiveType::TriangleList, 2);
        }
        return PixelIs(renderer, kSize / 2, kSize / 2, r, g, b);
    }

    std::vector<std::uint8_t> ReadEffect(const char* name)
    {
        std::ifstream file(std::string(CNA_FNA3D_TEST_EFFECTS_DIR) + "/" + name,
                           std::ios::binary);
        return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    }

    void RendererConstruction(SDL_Window* window)
    {
        // A device and the eleven stock effects are hard requirements; the volume-readback,
        // occlusion-query and compressed-readback probes only lower a capability.
        WalkAllocationPoints("renderer construction", 12, true, [&] {
            auto renderer = CreateRenderer(window);
            renderer->Clear(0.0f, 1.0f, 0.0f, 1.0f);
            if (!PixelIs(*renderer, 1, 1, 0, 255, 0))
            {
                throw std::runtime_error("the renderer does not clear and read back");
            }
            const bool queries =
                renderer->SupportsCapability(CNA::GraphicsCapability::OcclusionQuery);
            if ((renderer->CreateOcclusionQuery() != nullptr) != queries)
            {
                throw std::runtime_error("occlusion-query capability and creation disagree");
            }
            return Outcome::Succeeded;
        });
    }

    void Resources(Fna3dRenderer& renderer)
    {
        const ImageData image{1, 1, {20, 40, 60, 255}};
        WalkAllocationPoints("Texture2D", 2, false, [&] {
            auto texture = renderer.CreateTexture(image);
            std::uint8_t pixel[4] = {};
            if (!texture->GetData(0, 0, 0, 1, 1, pixel, sizeof(pixel)) || pixel[1] != 40)
            {
                throw std::runtime_error("the texture does not round-trip");
            }
            return Outcome::Succeeded;
        });
        WalkAllocationPoints("Texture3D", 2, false, [&] {
            return renderer.CreateTexture3D(2, 2, 2, false, 0) != nullptr ? Outcome::Succeeded
                                                                            : Outcome::ReturnedNull;
        });
        WalkAllocationPoints("TextureCube", 2, false, [&] {
            return renderer.CreateTextureCube(2, true, 0) != nullptr ? Outcome::Succeeded
                                                                      : Outcome::ReturnedNull;
        });

        // Depth24Stencil8 and a 4x request: the colour texture, the MSAA colour renderbuffer
        // where the driver multisamples, and the depth/stencil renderbuffer.
        constexpr int kDepth24Stencil8 = 3;
        WalkAllocationPoints("RenderTarget2D", 2, false, [&] {
            auto target = renderer.CreateRenderTarget2D(8, 8, kDepth24Stencil8, false, true, 4);
            renderer.SetRenderTarget2D(target.get());
            renderer.ClearColorAndDepth(1.0f, 0.0f, 0.0f, 1.0f, 1.0f);
            renderer.SetRenderTarget2D(nullptr);
            return Outcome::Succeeded;
        });
        WalkAllocationPoints("RenderTargetCube", 2, false, [&] {
            (void) renderer.CreateRenderTargetCube(4, kDepth24Stencil8, false, true, 4);
            return Outcome::Succeeded;
        });

        WalkAllocationPoints("vertex buffer", 1, false, [&] {
            (void) renderer.CreateVertexBuffer(3);
            return Outcome::Succeeded;
        });
        WalkAllocationPoints("16-bit index buffer", 1, false, [&] {
            (void) renderer.CreateIndexBuffer16(3);
            return Outcome::Succeeded;
        });
        WalkAllocationPoints("32-bit index buffer", 1, false, [&] {
            (void) renderer.CreateIndexBuffer32(3);
            return Outcome::Succeeded;
        });

        if (renderer.SupportsCapability(CNA::GraphicsCapability::OcclusionQuery))
        {
            WalkAllocationPoints("occlusion query", 1, false, [&] {
                (void) renderer.CreateOcclusionQuery();
                return Outcome::Succeeded;
            });
        }
        else
        {
            Check(renderer.CreateOcclusionQuery() == nullptr,
                  "occlusion query: the selected driver has none, and none is created");
        }

        const std::vector<std::uint8_t> basicEffect = ReadEffect("BasicEffect.fxb");
        Check(!basicEffect.empty(), "the BasicEffect binary is readable");
        if (!basicEffect.empty())
        {
            WalkAllocationPoints("compiled effect", 1, false, [&] {
                (void) renderer.CreateCompiledEffect(basicEffect.data(), basicEffect.size());
                return Outcome::Succeeded;
            });
            const auto source =
                renderer.CreateCompiledEffect(basicEffect.data(), basicEffect.size());
            WalkAllocationPoints("compiled effect clone", 1, false, [&] {
                (void) source->Clone();
                return Outcome::Succeeded;
            });
        }
    }

    void BufferGrowth(Fna3dRenderer& renderer)
    {
        // A growth allocates the replacement before it releases the buffer it replaces. When the
        // replacement fails, the old buffer must still be bound -- with its vertices -- by the next
        // draw; a null FNA3D buffer would reach the driver instead.
        const std::vector<ColoredVertex> red = FullScreen(6, 255, 0, 0);
        const std::vector<ColoredVertex> green = FullScreen(600, 0, 255, 0);
        auto vertices = renderer.CreateVertexBuffer(6);
        vertices->SetData(red.data(), 6, sizeof(ColoredVertex));
        Check(DrawsColour(renderer, *vertices, nullptr, 255, 0, 0),
              "vertex buffer: draws its vertices before the growth");

        Live before = Snapshot();
        Injection::ArmEXT(0);
        std::string error;
        const Outcome grown = Attempt(
            [&] {
                vertices->SetData(green.data(), static_cast<int>(green.size()),
                                  sizeof(ColoredVertex));
                return Outcome::Succeeded;
            },
            error);
        const bool consumed = !Injection::IsArmedEXT();
        Injection::ClearEXT();
        Check(consumed && grown == Outcome::Threw, "vertex buffer: a failed growth throws");
        Check(Snapshot() == before && vertices->GetVertexCount() == 6,
              "vertex buffer: a failed growth keeps the buffer and its vertex count");
        Check(DrawsColour(renderer, *vertices, nullptr, 255, 0, 0),
              "vertex buffer: the next draw still binds the old vertices");
        vertices->SetData(green.data(), static_cast<int>(green.size()), sizeof(ColoredVertex));
        Check(vertices->GetVertexCount() == 600 &&
                  DrawsColour(renderer, *vertices, nullptr, 0, 255, 0),
              "vertex buffer: the growth succeeds once nothing is injected");

        // The same for indices, including the switch from 16- to 32-bit elements the failed
        // upload asked for: the buffer must keep both its indices and its element size.
        vertices->SetData(red.data(), 6, sizeof(ColoredVertex));
        const std::uint16_t indices16[6] = {0, 1, 2, 3, 4, 5};
        std::vector<std::uint32_t> indices32(4096, 0);
        for (std::uint32_t i = 0; i < 6; ++i)
        {
            indices32[i] = i;
        }
        auto indices = renderer.CreateIndexBuffer16(6);
        indices->SetData16(indices16, 6);
        Check(DrawsColour(renderer, *vertices, indices.get(), 255, 0, 0),
              "index buffer: draws its indices before the growth");

        before = Snapshot();
        Injection::ArmEXT(0);
        const Outcome indexGrown = Attempt(
            [&] {
                indices->SetData32(indices32.data(), static_cast<int>(indices32.size()));
                return Outcome::Succeeded;
            },
            error);
        const bool indexConsumed = !Injection::IsArmedEXT();
        Injection::ClearEXT();
        Check(indexConsumed && indexGrown == Outcome::Threw, "index buffer: a failed growth throws");
        Check(Snapshot() == before && indices->GetIndexCount() == 6 && !indices->IsThirtyTwoBit(),
              "index buffer: a failed growth keeps the buffer, its index count and element size");
        Check(DrawsColour(renderer, *vertices, indices.get(), 255, 0, 0),
              "index buffer: the next draw still binds the old indices");
        indices->SetData32(indices32.data(), static_cast<int>(indices32.size()));
        Check(indices->IsThirtyTwoBit() && DrawsColour(renderer, *vertices, indices.get(), 255, 0, 0),
              "index buffer: the growth succeeds once nothing is injected");
    }

    void InternalResources(SDL_Window* window)
    {
        // SpriteBatch's own vertex and index buffers grow when a batch is larger than any before.
        // A warm-up batch of 10 sprites sizes them for 256 quads; 300 needs both to grow. The last
        // sprite is the only visible one, so a retry that draws with an index buffer still sized
        // for 256 quads loses it.
        const ImageData greenTexel{1, 1, {0, 255, 0, 255}};
        std::unique_ptr<CNA::Internal::Renderers::ITextureRenderer> texture;
        std::unique_ptr<CNA::Internal::Renderers::ISpriteBatchRenderer> sprites;
        const auto drawSprites = [&](int count) {
            sprites->Begin();
            for (int i = 0; i + 1 < count; ++i)
            {
                sprites->Draw(*texture, Rectangle(-100, -100, 1, 1), Rectangle(0, 0, 1, 1),
                              Color::White);
            }
            sprites->Draw(*texture, Rectangle(10, 10, 8, 8), Rectangle(0, 0, 1, 1), Color::White);
            sprites->End();
        };
        WalkOnFreshRenderers(
            "SpriteBatch buffer growth", 2, window,
            [&](Fna3dRenderer& renderer) {
                texture = renderer.CreateTexture(greenTexel);
                sprites = renderer.CreateSpriteBatch();
                renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
                drawSprites(10);
            },
            [&](Fna3dRenderer& renderer) {
                renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
                drawSprites(300);
                return Outcome::Succeeded;
            },
            [&](Fna3dRenderer& renderer) { return PixelIs(renderer, 14, 14, 0, 255, 0); },
            [&] {
                sprites.reset();
                texture.reset();
            });

        // EnvironmentMapEffect with neither a texture nor an environment map samples an opaque-black
        // texture and cube that the renderer creates on first use.
        WalkOnFreshRenderers(
            "lazily created internal textures", 2, window, [](Fna3dRenderer&) {},
            [](Fna3dRenderer& renderer) {
                GpuDrawParams params;
                params.envMapping = true;
                params.envMap = nullptr;
                const Matrix& identity = Matrix::getIdentityProperty();
                renderer.ApplyStockEffectEXT(identity, identity, identity, params);
                return Outcome::Succeeded;
            },
            [](Fna3dRenderer&) { return true; }, [] {});
    }
}

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::printf("[SKIP] no GPU/display available: %s\n", SDL_GetError());
        return 77;
    }

    const SDL_WindowFlags flags = static_cast<SDL_WindowFlags>(
        (PrepareWindowNeedsOpenGl() ? static_cast<std::uint64_t>(SDL_WINDOW_OPENGL) : 0u) |
        static_cast<std::uint64_t>(SDL_WINDOW_HIDDEN));
    SDL_Window* window = SDL_CreateWindow("CNA FNA3D allocation failure", kSize, kSize, flags);
    if (window == nullptr)
    {
        std::printf("[SKIP] FNA3D test windows unavailable: %s\n", SDL_GetError());
        SDL_Quit();
        return 77;
    }

    try
    {
        Check(Snapshot() == Live{}, "no native FNA3D object exists before the first device");

        RendererConstruction(window);
        {
            auto renderer = CreateRenderer(window);
            Resources(*renderer);
            BufferGrowth(*renderer);
        }
        InternalResources(window);

        Check(Snapshot() == Live{},
              "every native FNA3D object created by this test was released: " +
                  Describe(Live{}, Snapshot()));
    }
    catch (const std::exception& error)
    {
        Injection::ClearEXT();
        std::printf("[FAIL] unexpected exception: %s\n", error.what());
        ++failures;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return failures == 0 ? 0 : 1;
}
