// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2123: what contact shadows cost, and what they are worth.
//
// The unit tests assert the march is correct. What they cannot say is whether it is worth adding to
// a frame, and that is a cost question with a shape: the pass is one full-screen ray march, so its
// cost is linear in the step count and in the pixel count, and completely independent of the scene.
// That last part is the interesting one -- a shadow map's cost grows with the geometry it has to
// re-render, and this pass's does not grow at all -- so the two are measured in the units each is
// actually paid in rather than compared as one number.
//
// Check A -- this renderer runs the contact shadow pass, or the program SKIPs.
// Check B -- the floor beside an object darkens and the floor past the ray's reach does not.
// Check C -- the intensity dial scales the darkening rather than switching it.
// Check D -- MOD-2121, at frame scale: a thickness too thin for the gap loses the shadow entirely.
//
// `--benchmark` reports the cost per step count at 720p and 1080p (recorded in
// `docs/cnaext-perf.md`).
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/ContactShadowPass.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::ContactShadowPass;
using CNA::Graphics::DepthNormalPrepass;
using CNA::Graphics::PostProcessContext;

namespace
{
    constexpr int   kFrame        = 256;
    constexpr float kNearPlane    = 0.1f;
    constexpr float kFarPlane     = 10.0f;
    constexpr float kFloorDepth   = 5.0f;
    constexpr float kObjectDepth  = 4.85f;
    constexpr int   kObjectColumns = 96;

    Matrix Projection()
    {
        return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, kNearPlane,
                                                    kFarPlane);
    }

    float WorldUnitsPerPixel(const int height)
    {
        const float halfHeight = kFloorDepth * std::tan(MathHelper::PiOver4 * 0.5f);
        return (halfHeight * 2.0f) / static_cast<float>(height);
    }

    double MillisecondsOf(const int repeats, const std::function<void()>& work)
    {
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < repeats; ++i) work();
        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count() /
               static_cast<double>(repeats);
    }

    Color EncodeDepth(GraphicsDevice& device, const float linearDepth)
    {
        const float clamped = std::clamp(linearDepth, 0.0f, 1.0f);
        if (!DepthNormalPrepass::usesPackedDepthEXT(device))
        {
            const int value = static_cast<int>(clamped * 255.0f + 0.5f);
            return Color(value, value, value, 255);
        }
        float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
        DepthNormalPrepass::packDepth(clamped, r, g, b, a);
        const auto channel = [](const float v) {
            return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        };
        return Color(channel(r), channel(g), channel(b), channel(a));
    }

    /// A floor at kFloorDepth with an object resting on it across the left kObjectColumns columns.
    /// Written in the prepass's own encoding rather than as grey, which is the one thing a
    /// hand-built depth image gets wrong silently.
    std::unique_ptr<Texture2D> MakeContactDepth(GraphicsDevice& device, const int width,
                                                const int height)
    {
        auto texture = std::make_unique<Texture2D>(device, width, height);
        const Color object = EncodeDepth(device, kObjectDepth / kFarPlane);
        const Color floorTexel = EncodeDepth(device, kFloorDepth / kFarPlane);
        const int columns = kObjectColumns * width / kFrame;
        std::vector<Color> texels;
        texels.reserve(static_cast<std::size_t>(width) * height);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x)
                texels.push_back(x < columns ? object : floorTexel);
        texture->SetData(texels.data(), static_cast<int>(texels.size()));
        return texture;
    }

    std::unique_ptr<Texture2D> MakeFlatScene(GraphicsDevice& device, const int width,
                                             const int height, const int level)
    {
        auto texture = std::make_unique<Texture2D>(device, width, height);
        const std::vector<Color> texels(static_cast<std::size_t>(width) * height,
                                        Color(level, level, level, 255));
        texture->SetData(texels.data(), static_cast<int>(texels.size()));
        return texture;
    }
}

class ContactShadowExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool benchmark_  = false;
    int  passCount_  = 0;
    int  checkCount_ = 0;
    int  result_     = 1;

    void check(const bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    std::vector<Color> ReadFrame(GraphicsDevice& device)
    {
        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
        try { device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size())); }
        catch (...)
        {
            std::printf("SKIP: this renderer has no readable back buffer\n");
            std::exit(77);
        }
        return pixels;
    }

    static float ColumnMean(const std::vector<Color>& pixels, const int column)
    {
        float total = 0.0f;
        for (int y = 0; y < kFrame; ++y)
            total += static_cast<float>(
                pixels[static_cast<std::size_t>(y) * kFrame + column].getRProperty());
        return total / static_cast<float>(kFrame);
    }

    /// MOD-2123. The pass at several step counts and two resolutions.
    ///
    /// Both axes are reported because they are the two things a caller can actually change, and
    /// they behave differently: the step count decides the smallest occluder the march can see,
    /// while the resolution decides nothing at all about quality and is fixed by the frame. Neither
    /// depends on the scene -- there is no geometry in this measurement and there would be none in
    /// a real one either, which is the whole argument for the pass.
    void RunBenchmark(GraphicsDevice& device)
    {
        std::printf("\n-- contact shadows, per step count --\n");
        for (const int size : {720, 1080})
        {
            auto scene = MakeFlatScene(device, size, size, 200);
            auto depth = MakeContactDepth(device, size, size);
            RenderTarget2D destination(device, size, size);

            ContactShadowPass pass(device);
            pass.setLightDirection(Vector3(1.0f, 0.0f, 0.0f));
            pass.setMaxDistance(24.0f * WorldUnitsPerPixel(size));
            pass.setThickness(0.3f);
            pass.setBias(0.02f);

            PostProcessContext context;
            context.source            = scene.get();
            context.sourceDepth       = depth.get();
            context.destination       = &destination;
            context.width             = size;
            context.height            = size;
            context.projection        = Projection();
            context.inverseProjection = Matrix::Invert(Projection());
            context.nearPlane         = kNearPlane;
            context.farPlane          = kFarPlane;
            context.inverseView       = Matrix::getIdentityProperty();

            Color probe = Color::Black;
            const Rectangle oneTexel(0, 0, 1, 1);

            for (const int steps : {4, 8, 16, 32, 64})
            {
                pass.setStepCount(steps);
                pass.apply(context);
                destination.GetData(0, &oneTexel, &probe, 0, 1);   // warm, and prove the sync works

                const double ms = MillisecondsOf(20, [&] {
                    pass.apply(context);
                    // Without this the driver is free to return before running anything, and the
                    // measurement reports the cost of *submitting* a full-screen march -- which is
                    // the same 0.04 ms at every step count and every resolution, and looks like a
                    // free pass rather than an unfinished one.
                    destination.GetData(0, &oneTexel, &probe, 0, 1);
                });
                std::printf("    %4dx%-4d  %2d steps: %6.3f ms  (%.4f ms per step)\n",
                            size, size, steps, ms, ms / static_cast<double>(steps));
            }
        }
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        ContactShadowPass pass(device);
        if (!pass.isSupported(device))
        {
            std::printf("SKIP: this renderer cannot run the contact shadow pass\n");
            std::exit(77);
        }
        check(true, "the renderer runs the contact shadow pass");

        auto depth = MakeContactDepth(device, kFrame, kFrame);
        auto scene = MakeFlatScene(device, kFrame, kFrame, 200);

        PostProcessContext context;
        context.source            = scene.get();
        context.sourceDepth       = depth.get();
        context.destination       = nullptr;   // straight to the back buffer
        context.width             = kFrame;
        context.height            = kFrame;
        context.projection        = Projection();
        context.inverseProjection = Matrix::Invert(Projection());
        context.nearPlane         = kNearPlane;
        context.farPlane          = kFarPlane;
        context.inverseView       = Matrix::getIdentityProperty();

        // Light travelling in +X means the ray marches toward -X, toward the object.
        pass.setLightDirection(Vector3(1.0f, 0.0f, 0.0f));
        pass.setMaxDistance(24.0f * WorldUnitsPerPixel(kFrame));
        pass.setStepCount(32);
        pass.setThickness(0.3f);
        pass.setBias(0.02f);
        pass.setIntensity(1.0f);

        pass.apply(context);
        if (!pass.getFallbackReason().empty())
        {
            std::printf("SKIP: %s\n", pass.getFallbackReason().c_str());
            std::exit(77);
        }
        std::vector<Color> frame = ReadFrame(device);
        const float contact = ColumnMean(frame, kObjectColumns + 4);
        const float distant = ColumnMean(frame, kObjectColumns + 80);
        std::printf("    floor beside the object %.1f, floor past the ray %.1f (of 200)\n",
                    contact, distant);
        check(contact < 100.0f && distant > 190.0f,
              "the floor beside the object darkens and the floor past the ray does not");

        pass.setIntensity(0.5f);
        pass.apply(context);
        frame = ReadFrame(device);
        const float half = ColumnMean(frame, kObjectColumns + 4);
        std::printf("    half intensity: %.1f (of 200)\n", half);
        check(std::abs(half - 100.0f) < 12.0f, "the intensity dial scales the darkening");

        // MOD-2121 at frame scale. The object is 15 cm in front of the floor; told to assume 5 cm
        // of thickness the march judges the ray to have passed *behind* it, and the contact this
        // pass exists for disappears. Nothing in the depth image says which reading is right.
        pass.setIntensity(1.0f);
        pass.setThickness(0.05f);
        pass.apply(context);
        frame = ReadFrame(device);
        const float thin = ColumnMean(frame, kObjectColumns + 4);
        std::printf("    thickness 0.05 against a 0.15 gap: %.1f (of 200)\n", thin);
        check(thin > 190.0f, "a thickness too thin for the gap loses the shadow entirely");

        if (benchmark_) RunBenchmark(device);

        std::printf("%d/%d checks passed\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

public:
    explicit ContactShadowExample(const bool benchmark) : benchmark_(benchmark)
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kFrame);
        gdm_->setPreferredBackBufferHeightProperty(kFrame);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int result() const { return result_; }
};

int main(int argc, char** argv)
{
    try
    {
        bool benchmark = false;
        for (int i = 1; i < argc; ++i)
            if (std::strcmp(argv[i], "--benchmark") == 0) benchmark = true;

        ContactShadowExample example(benchmark);
        example.Run();
        return example.result();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
