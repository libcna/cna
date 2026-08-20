// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace CnaTest::EngineLayer {

    /**
     * @brief One texel of a depth image, in whatever encoding the prepass currently uses.
     *
     * plan_modern.md `MOD-2035`. Every screen-space test used to write `Color(v, v, v, 255)` and
     * call it depth, which was the right bytes only while the prepass stored depth unpacked. It
     * does not any more, and a test that hand-builds a depth image has to build the one the passes
     * actually decode -- so the encoding is asked for here rather than assumed in eight files.
     *
     * @param device The device whose prepass encoding applies.
     * @param depth  The linear depth, 0 at the eye and 1 at the far plane.
     * @return The texel a prepass would have written.
     */
    [[nodiscard]] inline Microsoft::Xna::Framework::Color DepthTexel(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const float depth)
    {
        const float clamped = std::clamp(depth, 0.0f, 1.0f);
        if (!CNA::Graphics::DepthNormalPrepass::usesPackedDepthEXT(device))
        {
            const int value = static_cast<int>(clamped * 255.0f + 0.5f);
            return Microsoft::Xna::Framework::Color(value, value, value, 255);
        }
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
        CNA::Graphics::DepthNormalPrepass::packDepth(clamped, r, g, b, a);
        const auto channel = [](const float v) {
            return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        return Microsoft::Xna::Framework::Color(channel(r), channel(g), channel(b), channel(a));
    }

    /**
     * @brief The same, from a 0..255 byte the older tests were written in terms of.
     *
     * @param device    The device whose prepass encoding applies.
     * @param depthByte The depth as the tests used to spell it, 0..255.
     * @return The texel a prepass would have written.
     */
    [[nodiscard]] inline Microsoft::Xna::Framework::Color DepthTexelFromByte(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device, const int depthByte)
    {
        return DepthTexel(device, static_cast<float>(depthByte) / 255.0f);
    }

    /**
     * @brief Whether this renderer really runs the shader source an effect is built from.
     *
     * plan_modern.md `MOD-1699`, restated for tests. `GraphicsCapability::CustomEffects` means the
     * renderer *accepts* an effect; SOFTWARE and HEADLESS accept any source and keep rendering with
     * their own fixed path. Both questions together are what "this shader will run" means.
     *
     * @param device The device to ask.
     * @return True when a custom effect's source actually shades the draw.
     */
    [[nodiscard]] inline bool RunsShaderSource(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
    {
        return device.SupportsCapability(CNA::GraphicsCapability::CustomEffects)
            && device.ExecutesShaderEffectSourceEXT();
    }

    /**
     * @brief Whether this renderer can make a `RenderTarget2D` the current render target.
     *
     * plan_modern.md `MOD-1697`. Weaker than @ref CanReadRenderTargets and asked separately: Stub
     * refuses the bind outright, so a pipeline that only ever *writes* offscreen still cannot run
     * there, while Headless binds happily and only refuses the read back.
     *
     * @param device The device to probe.
     * @return True when an offscreen target can be bound and unbound.
     */
    [[nodiscard]] inline bool CanBindRenderTargets(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
    {
        using namespace Microsoft::Xna::Framework;
        using namespace Microsoft::Xna::Framework::Graphics;
        try
        {
            RenderTarget2D target(device, 1, 1);
            device.SetRenderTarget(&target);
            device.SetRenderTarget(nullptr);
            return true;
        }
        catch (...)
        {
            try
            {
                device.SetRenderTarget(nullptr);
            }
            catch (...)
            {
                // Best-effort cleanup; the probe's answer is already decided.
            }
            return false;
        }
    }

    /**
     * @brief Whether this renderer can read a render target's pixels back to the CPU.
     *
     * plan_modern.md `MOD-1690`/`MOD-1696`. Not a `GraphicsCapability`, because the answer is not
     * a promise a renderer publishes -- it is what `Texture2D::GetData` does when handed a render
     * target, and the Headless renderer refuses it. Every engine-layer test that inspects a pass's
     * *output* needs this, and asking by probing is the only way that cannot drift from the truth.
     *
     * @param device The device to probe.
     * @return True when a render target's contents can be read.
     */
    [[nodiscard]] inline bool CanReadRenderTargets(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
    {
        using namespace Microsoft::Xna::Framework;
        using namespace Microsoft::Xna::Framework::Graphics;
        try
        {
            RenderTarget2D target(device, 1, 1);
            device.SetRenderTarget(&target);
            device.Clear(Color::Black);
            device.SetRenderTarget(nullptr);
            Color pixel = Color::White;
            target.GetData(&pixel, 1);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    /**
     * @brief Whether this renderer really stores and returns cube-map face data.
     *
     * plan_modern.md `MOD-1696`. The image-based-lighting precompute is pure CPU arithmetic, but
     * it still has to *put* its results somewhere: a `TextureCube`. Headless accepts the object and
     * refuses the data, so the precompute cannot run there -- a limitation worth probing rather
     * than assuming in either direction.
     *
     * @param device The device to probe.
     * @return True when a cube face round-trips.
     */
    [[nodiscard]] inline bool CanStoreCubeFaces(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
    {
        using namespace Microsoft::Xna::Framework;
        using namespace Microsoft::Xna::Framework::Graphics;
        try
        {
            TextureCube cube(device, 2, false, SurfaceFormat::Color);
            const std::vector<Color> face(4, Color::White);
            cube.SetData(CubeMapFace::PositiveX, face.data(), 4);
            std::vector<Color> read(4, Color::Black);
            cube.GetData(CubeMapFace::PositiveX, read.data(), 4);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    /**
     * @brief Whether this renderer stores cube-map face data into a mip level other than zero.
     *
     * plan_modern.md `MOD-1625`. A third question, narrower than @ref CanStoreCubeFaces and found
     * by the renderer that answers the two differently: D3D12 round-trips an unmipped face and
     * then refuses `SetData` into mip 1 of a mipped cube. The specular prefilter writes one
     * roughness per mip, so it needs this one and not the other.
     *
     * @param device The device to probe.
     * @return True when a non-zero cube mip level round-trips.
     */
    [[nodiscard]] inline bool CanStoreCubeMipLevels(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
    {
        using namespace Microsoft::Xna::Framework;
        using namespace Microsoft::Xna::Framework::Graphics;
        try
        {
            TextureCube cube(device, 4, true, SurfaceFormat::Color);
            const std::vector<Color> mip(4, Color::White);
            cube.SetData(CubeMapFace::PositiveX, 1, nullptr, mip.data(), 0, 4);
            std::vector<Color> read(4, Color::Black);
            cube.GetData(CubeMapFace::PositiveX, 1, nullptr, read.data(), 0, 4);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    /**
     * @brief Whether this renderer really renders into one face of a cube render target.
     *
     * plan_modern.md `MOD-1696`. A separate question from @ref CanStoreCubeFaces: storing texel
     * data into a sampled cube and *rendering into* one face of a cube target are different code
     * paths, and Headless has neither -- but its cube-face bind is a no-op that records the face
     * and leaves the back buffer current, so a small probe cube would be accepted and drawn
     * nowhere. The probe therefore uses a face larger than the back buffer, which is what makes a
     * fake bind observable: the face-sized viewport that follows is rejected as out of bounds.
     * Point-light shadow maps -- 512 pixels a face at the lowest quality -- hit exactly that.
     *
     * @param device The device to probe.
     * @return True when a cube face can genuinely be made the current render target.
     */
    [[nodiscard]] inline bool CanBindCubeRenderTargetFaces(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device)
    {
        using namespace Microsoft::Xna::Framework;
        using namespace Microsoft::Xna::Framework::Graphics;

        const PresentationParameters& presentation = device.getPresentationParametersProperty();
        const int backBuffer = std::max(presentation.getBackBufferWidthProperty(),
                                        presentation.getBackBufferHeightProperty());
        // The next power of two above the back buffer: a size no back buffer can contain, and
        // still a legal texture dimension everywhere.
        int size = 16;
        while (size <= backBuffer)
            size *= 2;

        try
        {
            RenderTargetCube cube(device, size, false, SurfaceFormat::Color, DepthFormat::Depth24);
            device.SetRenderTarget(&cube, CubeMapFace::PositiveX);
            device.SetRenderTarget(static_cast<RenderTargetCube*>(nullptr), CubeMapFace::PositiveX);
            return true;
        }
        catch (...)
        {
            try
            {
                device.SetRenderTarget(static_cast<RenderTargetCube*>(nullptr),
                                       CubeMapFace::PositiveX);
            }
            catch (...)
            {
                // Best-effort cleanup; the probe's answer is already decided.
            }
            return false;
        }
    }

    /**
     * @brief Whether this renderer allows a **second** `GraphicsDevice` in the same process.
     *
     * plan_modern.md `MOD-1695`. Most renderers do; TinyGL keeps its context in one process-wide
     * global (`glInit`/`glClose`) with no make-current entry point, so a second one is refused by
     * name rather than quietly sharing state. That is a documented boundary, not a defect, and a
     * multi-device test on such a renderer should skip rather than fail.
     *
     * Probed by doing, like the other five: constructing one is the only honest way to ask, since
     * the limit belongs to the native library rather than to anything CNA declares.
     *
     * @return True when a second device can be created alongside the caller's.
     */
    [[nodiscard]] inline bool SupportsASecondDevice()
    {
        try
        {
            // Both, in here. Constructing one and calling that a pass would answer "can this
            // process have a device at all", which every renderer answers yes to -- the question
            // is whether a *second* one may exist beside the first.
            Microsoft::Xna::Framework::Graphics::GraphicsDevice first;
            Microsoft::Xna::Framework::Graphics::GraphicsDevice second;
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    /**
     * @brief Skips the current test when the renderer cannot read render targets back.
     *
     * Written as a macro rather than a function because `GTEST_SKIP()` returns from the frame it
     * is written in: a helper that called it would skip itself and let the test carry on.
     */
#define CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(device)                                            \
    do {                                                                                           \
        if (!::CnaTest::EngineLayer::CanReadRenderTargets(device))                                 \
            GTEST_SKIP() << "this renderer cannot read a render target back to the CPU, so the "   \
                            "pass output this test inspects cannot be observed here";              \
    } while (false)

    /** @brief Skips the current test when the renderer does not store cube-map face data. */
#define CNA_SKIP_WITHOUT_CUBE_FACE_STORAGE(device)                                                 \
    do {                                                                                           \
        if (!::CnaTest::EngineLayer::CanStoreCubeFaces(device))                                    \
            GTEST_SKIP() << "this renderer does not store cube-map face data, so the cube this "   \
                            "test builds cannot hold what is written to it";                       \
    } while (false)

    /** @brief Skips the current test when the renderer does not store non-zero cube mip levels. */
#define CNA_SKIP_WITHOUT_CUBE_MIP_STORAGE(device)                                                  \
    do {                                                                                           \
        if (!::CnaTest::EngineLayer::CanStoreCubeMipLevels(device))                                \
            GTEST_SKIP() << "this renderer does not store cube-map data into a mip level other "   \
                            "than zero, so the prefiltered chain this test reads cannot be "       \
                            "written here";                                                        \
    } while (false)

    /** @brief Skips the current test when the renderer cannot render into a cube face. */
#define CNA_SKIP_WITHOUT_CUBE_RENDER_TARGETS(device)                                               \
    do {                                                                                           \
        if (!::CnaTest::EngineLayer::CanBindCubeRenderTargetFaces(device))                         \
            GTEST_SKIP() << "this renderer cannot bind a cube render target face, so the per-face " \
                            "passes this test opens have nowhere to draw";                         \
    } while (false)

    /** @brief Skips the current test when the renderer will not run a custom effect's source. */
#define CNA_SKIP_WITHOUT_SHADER_EXECUTION(device)                                                  \
    do {                                                                                           \
        if (!::CnaTest::EngineLayer::RunsShaderSource(device))                                     \
            GTEST_SKIP() << "this renderer does not run custom shader source, so the effect this "  \
                            "test depends on would be accepted and then ignored";                  \
    } while (false)

    /** @brief Skips the current test when the renderer cannot bind an offscreen render target. */
#define CNA_SKIP_WITHOUT_RENDER_TARGETS(device)                                                    \
    do {                                                                                           \
        if (!::CnaTest::EngineLayer::CanBindRenderTargets(device))                                 \
            GTEST_SKIP() << "this renderer cannot bind an offscreen render target, so the frame "  \
                            "this test runs has nowhere to render";                                \
    } while (false)

    /** @brief Skips the current test when the renderer allows only one device per process. */
#define CNA_SKIP_WITHOUT_A_SECOND_DEVICE()                                                         \
    do {                                                                                           \
        if (!::CnaTest::EngineLayer::SupportsASecondDevice())                                      \
            GTEST_SKIP() << "this renderer keeps its context in one process-wide global, so the "  \
                            "second device this test needs cannot exist here";                     \
    } while (false)

} // namespace CnaTest::EngineLayer

#endif // CNA_CNAEXT
