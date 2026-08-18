// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-852/MOD-856: the shadow subsystem as an application actually uses it.
//
// The unit tests reach into the pieces -- the fitted matrices, the state each effect hands to the
// renderer, a quad's shadow on another quad. This is the whole thing from the outside: a cube on a
// plane, a sun, and the two public calls a game makes. It goes through
// `RenderPipeline::setShadowScene`, so the ordering MOD-858 fixed is exercised by the same path a
// game would take rather than only by a test that knows where to look.
//
// What it checks is trigonometry rather than an image. The camera looks straight down at the
// plane, so a cube centred `h` above it, lit by a sun tilted `theta` from vertical toward -X,
// casts its shadow centred at world x = -h*tan(theta) -- a figure the renderer has no part in.
// Sweeping theta and comparing the shadow's measured centroid against that formula is a stronger
// statement than a committed PNG: a golden records that a shadow was in some place on the machine
// that made it, this records that it is in the *right* place, and says where that is.
//
// The sun is movable in the sense that matters here: `--sun-degrees A,B,C` renders one frame per
// angle. There is no interactive window -- this environment cannot verify one, and a control loop
// nobody has run is worse than no control loop.
//
// Check A -- the renderer rasterizes 3D and compiles custom effects, or the program SKIPs.
// Check B -- at each sun angle, something on the plane is actually shadowed.
// Check C -- at each sun angle, the shadow's centroid sits where -h*tan(theta) says it should.
// Check D -- the shadow moves monotonically as the sun tilts, which no single-angle check shows.
// Check E -- with `RenderPipelineSettings::setShadowsEnabled(false)` the plane is uniformly lit.
//
// `--benchmark` additionally times the shadow pass at each quality (MOD-857). Off by default: a
// timing is a recording of one machine, not a budget, and spending it in every CI run buys nothing.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
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
#include <utility>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::DirectionalLightEXT;
using CNA::Graphics::RenderPipeline;
using CNA::Graphics::ShadowMap;
using CNA::Graphics::ShadowQuality;
using CNA::GraphicsCapability;

namespace
{
    constexpr int   kFrame       = 128;
    constexpr float kGroundHalf  = 10.0f;
    constexpr float kCubeHalf    = 1.5f;
    /// The cube's centre height. Every expected shadow position below is -kCubeCentre*tan(theta).
    constexpr float kCubeCentre  = 3.0f;

    std::vector<float> ParseAngles(int argc, char** argv)
    {
        std::vector<float> angles{0.0f, 25.0f, 45.0f};
        for (int i = 1; i < argc; ++i)
        {
            if (std::strcmp(argv[i], "--sun-degrees") != 0 || i + 1 >= argc)
                continue;
            angles.clear();
            std::string list = argv[++i];
            std::size_t start = 0;
            while (start <= list.size())
            {
                const std::size_t comma = list.find(',', start);
                const std::string piece = list.substr(
                    start, comma == std::string::npos ? std::string::npos : comma - start);
                if (!piece.empty())
                    angles.push_back(std::stof(piece));
                if (comma == std::string::npos)
                    break;
                start = comma + 1;
            }
        }
        return angles;
    }

    void AppendQuad(std::vector<VertexPositionNormalTexture>& out, const Vector3& a,
                    const Vector3& b, const Vector3& c, const Vector3& d, const Vector3& normal)
    {
        const Vector2 uv(0.0f, 0.0f);
        out.push_back(VertexPositionNormalTexture(a, normal, uv));
        out.push_back(VertexPositionNormalTexture(b, normal, uv));
        out.push_back(VertexPositionNormalTexture(c, normal, uv));
        out.push_back(VertexPositionNormalTexture(a, normal, uv));
        out.push_back(VertexPositionNormalTexture(c, normal, uv));
        out.push_back(VertexPositionNormalTexture(d, normal, uv));
    }

    /// A closed cube. Every face is written into the shadow map, which is what makes the silhouette
    /// a real one rather than the outline of a single quad.
    std::vector<VertexPositionNormalTexture> Cube(const Vector3& centre, float half)
    {
        std::vector<VertexPositionNormalTexture> out;
        out.reserve(36);
        const float e = half;
        const auto p = [&](float x, float y, float z) {
            return Vector3(centre.X + x * e, centre.Y + y * e, centre.Z + z * e);
        };
        AppendQuad(out, p(-1, 1, -1), p(1, 1, -1), p(1, 1, 1), p(-1, 1, 1), Vector3(0, 1, 0));
        AppendQuad(out, p(-1, -1, 1), p(1, -1, 1), p(1, -1, -1), p(-1, -1, -1), Vector3(0, -1, 0));
        AppendQuad(out, p(-1, -1, 1), p(-1, 1, 1), p(-1, 1, -1), p(-1, -1, -1), Vector3(-1, 0, 0));
        AppendQuad(out, p(1, -1, -1), p(1, 1, -1), p(1, 1, 1), p(1, -1, 1), Vector3(1, 0, 0));
        AppendQuad(out, p(-1, -1, 1), p(1, -1, 1), p(1, 1, 1), p(-1, 1, 1), Vector3(0, 0, 1));
        AppendQuad(out, p(1, -1, -1), p(-1, -1, -1), p(-1, 1, -1), p(1, 1, -1), Vector3(0, 0, -1));
        return out;
    }

    std::vector<VertexPositionNormalTexture> Ground()
    {
        std::vector<VertexPositionNormalTexture> out;
        const float e = kGroundHalf;
        AppendQuad(out, Vector3(-e, 0, -e), Vector3(e, 0, -e), Vector3(e, 0, e), Vector3(-e, 0, e),
                   Vector3(0, 1, 0));
        return out;
    }

    Matrix TopDownView()
    {
        return Matrix::CreateLookAt(Vector3(0.0f, 30.0f, 0.0f), Vector3::Zero,
                                    Vector3(0.0f, 0.0f, 1.0f));
    }

    Matrix FitToGround()
    {
        return Matrix::CreateOrthographic(kGroundHalf * 2.0f, kGroundHalf * 2.0f, 0.1f, 80.0f);
    }

    /// Where a world position lands in the frame, from the camera matrices alone.
    void ExpectedPixel(const Vector3& world, float& x, float& y)
    {
        const Matrix vp = TopDownView() * FitToGround();
        const float cx = world.X * vp.M11 + world.Y * vp.M21 + world.Z * vp.M31 + vp.M41;
        const float cy = world.X * vp.M12 + world.Y * vp.M22 + world.Z * vp.M32 + vp.M42;
        const float w  = world.X * vp.M14 + world.Y * vp.M24 + world.Z * vp.M34 + vp.M44;
        const float inverseW = std::abs(w) > 1e-6f ? 1.0f / w : 1.0f;
        x = (cx * inverseW * 0.5f + 0.5f) * kFrame;
        y = (0.5f - cy * inverseW * 0.5f) * kFrame;
    }
}

class ShadowMapExample : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::vector<float> angles_;
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

    /// The shadow's centroid, and how many pixels it covers. The plane is uniformly lit apart from
    /// the shadow, so "darker than the brightest pixel by a clear margin" is an exact description
    /// of the shadowed set and not a heuristic.
    static int ShadowCentroid(const std::vector<Color>& pixels, float& cx, float& cy)
    {
        int brightest = 0;
        for (const Color& pixel : pixels)
            brightest = std::max(brightest, static_cast<int>(pixel.getRProperty()));

        double sumX = 0.0, sumY = 0.0;
        int count = 0;
        for (int y = 0; y < kFrame; ++y)
            for (int x = 0; x < kFrame; ++x)
                if (static_cast<int>(At(pixels, x, y).getRProperty()) < brightest - 24)
                {
                    sumX += x;
                    sumY += y;
                    ++count;
                }
        if (count > 0)
        {
            cx = static_cast<float>(sumX / count);
            cy = static_cast<float>(sumY / count);
        }
        return count;
    }

    /// A coarse picture for a human reading the log. The numeric checks are the test; this is so a
    /// failure can be looked at rather than only read about.
    static void PrintAsciiFrame(const std::vector<Color>& pixels)
    {
        int brightest = 0;
        for (const Color& pixel : pixels)
            brightest = std::max(brightest, static_cast<int>(pixel.getRProperty()));
        for (int row = 0; row < 16; ++row)
        {
            std::string line;
            for (int column = 0; column < 32; ++column)
            {
                const int x = column * kFrame / 32;
                const int y = row * kFrame / 16;
                const int value = At(pixels, x, y).getRProperty();
                line += (value < brightest - 24) ? '#' : '.';
            }
            std::printf("    %s\n", line.c_str());
        }
    }

    static void ConfigureGroundEffect(BasicEffect& effect, const Vector3& sunDirection)
    {
        effect.setLightingEnabledProperty(true);
        effect.setTextureEnabledProperty(false);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setAmbientLightColorProperty(Vector3(0.15f, 0.15f, 0.15f));
        effect.setSpecularColorProperty(Vector3::Zero);
        effect.setEmissiveColorProperty(Vector3::Zero);

        auto& light = effect.getDirectionalLight0Property();
        light.setEnabledProperty(true);
        light.setDirectionProperty(sunDirection);
        light.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        light.setSpecularColorProperty(Vector3::Zero);
        effect.getDirectionalLight1Property().setEnabledProperty(false);
        effect.getDirectionalLight2Property().setEnabledProperty(false);

        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(TopDownView());
        effect.setProjectionProperty(FitToGround());
    }

protected:
    void Draw(const GameTime&) override
    {
        auto& device = getGraphicsDeviceProperty();

        // MOD-1699: `CustomEffects` says a renderer can compile *a* custom effect, not that it
        // takes this layer's shader language or that its lit shaders sample a shadow at all. The
        // Vulkan renderer answers true to the first and false to both of the others, and without
        // this second question a shadow test there does not fail -- it crashes, mid-draw, with no
        // effect applied.
        if (!device.SupportsCapability(GraphicsCapability::ThreeD) ||
            !device.SupportsCapability(GraphicsCapability::CustomEffects) ||
            !device.SupportsShadowSamplingEXT())
        {
            std::printf("SKIP: this renderer does not raster 3D, cannot compile the caster's "
                        "shader, or does not sample shadows (a documented capability boundary, not a defect)\n");
            std::exit(77);
        }

        const auto cube   = Cube(Vector3(0.0f, kCubeCentre, 0.0f), kCubeHalf);
        const auto ground = Ground();
        const BoundingBox sceneBounds(
            Vector3(-kGroundHalf, -1.0f, -kGroundHalf),
            Vector3(kGroundHalf, kCubeCentre + kCubeHalf + 1.0f, kGroundHalf));

        ShadowMap shadowMap(device, ShadowQuality::High);
        RenderPipeline pipeline(device);
        pipeline.resize(kFrame, kFrame);
        pipeline.getSettings().setShadowsEnabled(true);

        BasicEffect groundEffect(device);
        groundEffect.setShadowMapEXT(shadowMap.getShadowTexture());
        groundEffect.setShadowFilterRadiusEXT(shadowMap.getFilterRadius());
        groundEffect.setShadowsEnabledEXT(true);

        std::vector<float> measuredX;
        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);

        for (const float degrees : angles_)
        {
            const float theta = degrees * 3.14159265f / 180.0f;
            // Tilted from straight down toward -X, so the shadow moves along -X as it grows.
            const Vector3 sunDirection(-std::sin(theta), -std::cos(theta), 0.0f);

            DirectionalLightEXT sun;
            sun.Direction = sunDirection;
            ConfigureGroundEffect(groundEffect, sunDirection);

            pipeline.setShadowScene(&shadowMap, sun, sceneBounds, [&] {
                device.setRasterizerStateProperty(RasterizerState::CullNone);
                device.setDepthStencilStateProperty(DepthStencilState::Default);
                device.DrawUserPrimitives(PrimitiveType::TriangleList, cube.data(), 0, 12);
            });

            // Two frames: the first produces the light matrix, the second draws with it. A game
            // sets the matrix each frame from the pass it has just run, which is the same thing.
            for (int frame = 0; frame < 2; ++frame)
            {
                pipeline.begin(Color::Black);
                groundEffect.setLightViewProjectionEXT(shadowMap.getLightViewProjection());
                device.setRasterizerStateProperty(RasterizerState::CullNone);
                device.setDepthStencilStateProperty(DepthStencilState::Default);
                device.setBlendStateProperty(BlendState::Opaque);
                groundEffect.Apply();
                device.DrawUserPrimitives(PrimitiveType::TriangleList, ground.data(), 0, 2);
                pipeline.end();
            }

            try { device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size())); }
            catch (const System::NotSupportedException&)
            {
                std::printf("SKIP: this renderer has no readable back buffer\n");
                std::exit(77);
            }

            const std::string at = " (sun " + std::to_string(static_cast<int>(degrees)) + " deg)";
            std::printf("--- sun %d degrees ---\n", static_cast<int>(degrees));
            PrintAsciiFrame(pixels);

            float gotX = 0.0f, gotY = 0.0f;
            const int shadowed = ShadowCentroid(pixels, gotX, gotY);
            check(shadowed > 0, "the cube casts a shadow on the plane" + at);
            if (shadowed == 0)
            {
                measuredX.push_back(0.0f);
                continue;
            }

            // The ray from the cube's centre along the sun direction meets y=0 at
            // x = -centre*tan(theta). No part of the renderer takes part in that figure.
            float wantX = 0.0f, wantY = 0.0f;
            ExpectedPixel(Vector3(-kCubeCentre * std::tan(theta), 0.0f, 0.0f), wantX, wantY);
            const float error = std::sqrt((gotX - wantX) * (gotX - wantX) +
                                          (gotY - wantY) * (gotY - wantY));
            std::printf("    centroid (%.1f, %.1f), expected (%.1f, %.1f), error %.1f px\n",
                        gotX, gotY, wantX, wantY, error);
            // Three pixels: the shadow is a projected cube, not a point, and its centroid drifts
            // from the centre ray as the projection stretches. A wrong axis costs tens.
            check(error < 3.0f, "the shadow's centroid is where the sun angle puts it" + at);
            measuredX.push_back(gotX);
        }

        // The sweep as a whole: a shadow pinned correctly at one angle could still be pinned by
        // accident. Tilting the sun further must move it further from directly under the cube,
        // every time. Measured as a distance rather than a signed step, because which screen
        // direction world -X becomes is a property of the camera, not of the shadow -- with this
        // view looking straight down and +Z as up, it is screen +x.
        bool monotonic = true;
        for (std::size_t i = 1; i < measuredX.size(); ++i)
        {
            const float previous = std::abs(measuredX[i - 1] - kFrame * 0.5f);
            const float current  = std::abs(measuredX[i] - kFrame * 0.5f);
            if (angles_[i] > angles_[i - 1] && !(current > previous + 0.5f))
                monotonic = false;
        }
        if (measuredX.size() > 1)
            check(monotonic, "the shadow moves further as the sun tilts further");

        // And the off switch, through the settings rather than through the effect: no shadow pass
        // ran, so the map holds nothing, and the plane is uniformly lit.
        pipeline.getSettings().setShadowsEnabled(false);
        groundEffect.setShadowsEnabledEXT(false);
        pipeline.begin(Color::Black);
        groundEffect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, ground.data(), 0, 2);
        pipeline.end();
        device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size()));
        float unusedX = 0.0f, unusedY = 0.0f;
        check(!pipeline.didShadowPassRun() && ShadowCentroid(pixels, unusedX, unusedY) == 0,
              "with shadows disabled no pass runs and the plane is uniformly lit");

        if (benchmark_)
            RunBenchmark(device, cube, sceneBounds);

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

    /// plan_modern.md MOD-857. Off by default -- a timing on a software rasterizer is a recording,
    /// not a budget, and running it in every CI job would spend minutes to learn nothing.
    void RunBenchmark(GraphicsDevice& device,
                      const std::vector<VertexPositionNormalTexture>& cube,
                      const BoundingBox& sceneBounds)
    {
        DirectionalLightEXT sun;
        sun.Direction = Vector3(-0.7071f, -0.7071f, 0.0f);

        const std::array<std::pair<ShadowQuality, const char*>, 4> levels{{
            {ShadowQuality::Low, "Low (512)"},
            {ShadowQuality::Medium, "Medium (1024)"},
            {ShadowQuality::High, "High (2048)"},
            {ShadowQuality::Ultra, "Ultra (4096)"},
        }};

        std::printf("--- shadow pass cost, %d casting triangles ---\n",
                    static_cast<int>(cube.size()) / 3);
        // Screen resolution is absent from this table on purpose: the pass renders into the map,
        // never into the frame, so its cost is a function of map size and caster count and nothing
        // else. A 720p and a 1080p column would print the same number twice and imply otherwise.
        for (const auto& [quality, label] : levels)
        {
            ShadowMap map(device, quality);
            // One untimed pass first: the caster program is compiled on its first use, and folding
            // a shader compile into the first measurement would make Low look like the slow one.
            map.begin(sun, sceneBounds);
            device.DrawUserPrimitives(PrimitiveType::TriangleList, cube.data(), 0, 12);
            map.end();

            constexpr int kIterations = 20;
            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < kIterations; ++i)
            {
                map.begin(sun, sceneBounds);
                device.DrawUserPrimitives(PrimitiveType::TriangleList, cube.data(), 0, 12);
                map.end();
            }
            // A readback, once, after the loop: without it the timing would measure how fast this
            // program can queue work rather than how long the work takes.
            std::vector<Color> drain(16, Color::Black);
            try { map.getShadowTexture()->GetData(0, nullptr, drain.data(), 0, 0); }
            catch (const std::exception&) { /* the sync is the point, not the pixels */ }
            const auto finish = std::chrono::steady_clock::now();

            const double milliseconds =
                std::chrono::duration<double, std::milli>(finish - start).count() / kIterations;
            std::printf("    %-14s %6.2f ms/pass\n", label, milliseconds);
        }
    }

public:
    ShadowMapExample(std::vector<float> angles, bool benchmark)
        : angles_(std::move(angles)), benchmark_(benchmark)
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
        ShadowMapExample game(ParseAngles(argc, argv), benchmark);
        game.Run();
        return game.getResult();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        // No usable video subsystem is a property of where this is running, not a defect in what
        // it tests. Reported as a SKIP, with the platform's own reason, rather than as a failure
        // that would look like a broken shadow.
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
