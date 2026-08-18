// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-1112..MOD-1114: the skybox as an application uses it.
//
// An orbit camera around a foreground object, with the sky behind it. The camera is what makes
// this worth running as a program rather than as another unit test: a sky that is correct for one
// direction and wrong for another is the normal way this fails, and only moving the camera shows
// it.
//
// MOD-1112 asked for a committed golden. This renders and states the property instead -- the same
// deviation recorded for MOD-852, MOD-912 and MOD-1009. Here the property is the one a golden of
// this scene would have been inspected for: the object stays in front of the sky at every camera
// angle, and the sky behind it changes as the camera turns.
//
// Check A -- the renderer compiles custom effects and rasters 3D, or the program SKIPs.
// Check B -- at each of eight camera angles the sky renders and the foreground object survives it.
// Check C -- the sky genuinely changes with the camera rather than being painted on the screen.
// Check D -- yaw turns the sky while the camera stands still.
//
// `--benchmark` times the sky against an empty frame (MOD-1114); it should cost one fullscreen
// pass and nothing else.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "CNA/Graphics/Skybox.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"
#include "System/NotSupportedException.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::EnvironmentProcessor;
using CNA::Graphics::Skybox;
using CNA::GraphicsCapability;

namespace
{
    constexpr int kFrame = 128;

    /// A panorama whose colour encodes its own longitude, so "the sky changed" is measurable
    /// rather than a judgement: the hue at the centre of the frame *is* the direction faced.
    std::unique_ptr<TextureCube> MakeGradientCube(GraphicsDevice& device,
                                                  EnvironmentProcessor& processor)
    {
        constexpr int kWidth  = 128;
        constexpr int kHeight = 64;
        Texture2D panorama(device, kWidth, kHeight);
        std::vector<Color> texels(static_cast<std::size_t>(kWidth) * kHeight, Color::Black);
        for (int y = 0; y < kHeight; ++y)
            for (int x = 0; x < kWidth; ++x)
            {
                const float u = static_cast<float>(x) / static_cast<float>(kWidth - 1);
                const float v = static_cast<float>(y) / static_cast<float>(kHeight - 1);
                texels[static_cast<std::size_t>(y) * kWidth + x] =
                    Color(static_cast<int>(u * 255.0f),
                          static_cast<int>((1.0f - v) * 255.0f),
                          static_cast<int>(std::abs(u - 0.5f) * 2.0f * 255.0f), 255);
            }
        panorama.SetData(texels.data(), static_cast<int>(texels.size()));
        return processor.convertEquirectangular(&panorama, 32);
    }

    Matrix Projection()
    {
        return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, 0.1f, 100.0f);
    }

    /// The orbit: the camera circles the origin at a fixed height, always looking inward.
    Matrix OrbitView(float angle)
    {
        const float radius = 6.0f;
        const Vector3 eye(std::sin(angle) * radius, 1.5f, std::cos(angle) * radius);
        return Matrix::CreateLookAt(eye, Vector3::Zero, Vector3(0.0f, 1.0f, 0.0f));
    }
}

class SkyboxExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool benchmark_ = false;
    int  passCount_ = 0;
    int  checkCount_ = 0;
    int  result_ = 1;

    void check(bool ok, const std::string& label)
    {
        ++checkCount_;
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        if (ok) ++passCount_;
    }

    static Color At(const std::vector<Color>& pixels, int x, int y)
    {
        return pixels[static_cast<std::size_t>(y) * kFrame + static_cast<std::size_t>(x)];
    }

    static void PrintAsciiFrame(const std::vector<Color>& pixels)
    {
        for (int row = 0; row < 12; ++row)
        {
            std::string line;
            for (int column = 0; column < 32; ++column)
            {
                const Color c = At(pixels, column * kFrame / 32, row * kFrame / 12);
                const bool white = c.getRProperty() > 240 && c.getGProperty() > 240 &&
                                   c.getBProperty() > 240;
                line += white ? '#' : (c.getRProperty() > 128 ? '+' : '.');
            }
            std::printf("    |%s|\n", line.c_str());
        }
    }

    /// The foreground object: a white quad facing the camera, drawn after the sky.
    void DrawForeground(GraphicsDevice& device, const Matrix& view)
    {
        const Vector3 normal(0.0f, 0.0f, 1.0f);
        const Vector2 uv(0.0f, 0.0f);
        const std::array<VertexPositionNormalTexture, 6> quad{
            VertexPositionNormalTexture(Vector3(-1.0f, -1.0f, 0.0f), normal, uv),
            VertexPositionNormalTexture(Vector3( 1.0f, -1.0f, 0.0f), normal, uv),
            VertexPositionNormalTexture(Vector3( 1.0f,  1.0f, 0.0f), normal, uv),
            VertexPositionNormalTexture(Vector3(-1.0f, -1.0f, 0.0f), normal, uv),
            VertexPositionNormalTexture(Vector3( 1.0f,  1.0f, 0.0f), normal, uv),
            VertexPositionNormalTexture(Vector3(-1.0f,  1.0f, 0.0f), normal, uv),
        };

        BasicEffect effect(device);
        effect.setLightingEnabledProperty(false);
        effect.setTextureEnabledProperty(false);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        // Billboarded, so the quad faces the orbiting camera at every angle and stays the same
        // size in frame -- otherwise "the object survived" would vary with the viewing angle for
        // reasons that have nothing to do with the sky.
        Matrix world = Matrix::Invert(view);
        world.M41 = 0.0f; world.M42 = 0.0f; world.M43 = 0.0f;
        effect.setWorldProperty(world);
        effect.setViewProperty(view);
        effect.setProjectionProperty(Projection());

        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setBlendStateProperty(BlendState::Opaque);
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad.data(), 0, 2);
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        if (!device.SupportsCapability(GraphicsCapability::ThreeD) ||
            !device.SupportsCapability(GraphicsCapability::CustomEffects))
        {
            std::printf("SKIP: this renderer does not raster 3D or cannot compile the sky shader "
                        "(a documented capability boundary, not a defect)\n");
            std::exit(77);
        }

        EnvironmentProcessor processor(device);
        auto cube = MakeGradientCube(device, processor);
        Skybox sky(device, cube.get());
        if (!sky.isSupported())
        {
            std::printf("SKIP: the sky shader did not compile on this renderer\n");
            std::exit(77);
        }

        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
        const auto readBack = [&] {
            try { device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size())); }
            catch (const System::NotSupportedException&)
            {
                std::printf("SKIP: this renderer has no readable back buffer\n");
                std::exit(77);
            }
        };

        std::vector<int> skyHues;
        bool everyAngleKeptTheObject = true;

        constexpr int kAngles = 8;
        for (int step = 0; step < kAngles; ++step)
        {
            const float angle = static_cast<float>(step) * MathHelper::TwoPi
                              / static_cast<float>(kAngles);
            const Matrix view = OrbitView(angle);

            device.Clear(Color::Black);
            sky.draw(view, Projection(), kFrame, kFrame);
            DrawForeground(device, view);
            readBack();

            if (step == 0)
            {
                std::printf("--- orbit, angle 0 ---\n");
                PrintAsciiFrame(pixels);
            }

            const Color centre = At(pixels, kFrame / 2, kFrame / 2);
            const Color corner = At(pixels, 2, kFrame / 2);
            const bool objectSurvived = centre.getRProperty() > 240 &&
                                        centre.getGProperty() > 240 &&
                                        centre.getBProperty() > 240;
            if (!objectSurvived)
                everyAngleKeptTheObject = false;
            skyHues.push_back(corner.getRProperty());
            std::printf("    angle %d: object %s, sky hue at the left edge %d\n", step,
                        objectSurvived ? "in front" : "COVERED", corner.getRProperty());
        }

        check(everyAngleKeptTheObject,
              "the foreground object stays in front of the sky at every camera angle");

        int distinctHues = 0;
        for (std::size_t i = 1; i < skyHues.size(); ++i)
            if (std::abs(skyHues[i] - skyHues[i - 1]) > 8)
                ++distinctHues;
        check(distinctHues >= kAngles / 2,
              "the sky changes as the camera orbits, so it is around the world and not on the "
              "screen");

        // Yaw with the camera still: the same view, a different sky.
        const Matrix fixed = OrbitView(0.0f);
        device.Clear(Color::Black);
        sky.draw(fixed, Projection(), kFrame, kFrame);
        readBack();
        const int before = At(pixels, kFrame / 2, kFrame / 2).getRProperty();

        sky.setYaw(MathHelper::PiOver2);
        device.Clear(Color::Black);
        sky.draw(fixed, Projection(), kFrame, kFrame);
        readBack();
        const int after = At(pixels, kFrame / 2, kFrame / 2).getRProperty();
        std::printf("--- yaw --- centre hue %d -> %d\n", before, after);
        check(std::abs(after - before) > 20,
              "yaw turns the sky while the camera stands still");
        sky.setYaw(0.0f);

        if (benchmark_)
            RunBenchmark(device, sky);

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

    /// MOD-1114: the sky should cost one fullscreen pass, so a frame with it should differ from an
    /// empty one by about the cost of covering the screen once.
    void RunBenchmark(GraphicsDevice& device, Skybox& sky)
    {
        constexpr int kIterations = 30;
        const Matrix view = OrbitView(0.0f);

        const auto time = [&](bool withSky) {
            // One untimed frame: the first draw compiles and warms whatever it warms.
            device.Clear(Color::Black);
            if (withSky) sky.draw(view, Projection(), kFrame, kFrame);

            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < kIterations; ++i)
            {
                device.Clear(Color::Black);
                if (withSky) sky.draw(view, Projection(), kFrame, kFrame);
            }
            std::vector<Color> drain(4, Color::Black);
            try { device.GetBackBufferData(drain.data(), 4); } catch (const std::exception&) {}
            const auto finish = std::chrono::steady_clock::now();
            return std::chrono::duration<double, std::milli>(finish - start).count() / kIterations;
        };

        const double empty = time(false);
        const double withSky = time(true);
        std::printf("--- sky cost at %dx%d ---\n", kFrame, kFrame);
        std::printf("    clear only        %6.3f ms/frame\n", empty);
        std::printf("    clear + sky       %6.3f ms/frame\n", withSky);
        std::printf("    the sky itself    %6.3f ms/frame\n", withSky - empty);
    }

public:
    explicit SkyboxExample(bool benchmark) : benchmark_(benchmark)
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kFrame);
        gdm_->setPreferredBackBufferHeightProperty(kFrame);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int getResult() const { return result_; }
};

int main(int argc, char** argv)
{
    try
    {
        bool benchmark = false;
        for (int i = 1; i < argc; ++i)
            if (std::strcmp(argv[i], "--benchmark") == 0)
                benchmark = true;
        SkyboxExample game(benchmark);
        game.Run();
        return game.getResult();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
