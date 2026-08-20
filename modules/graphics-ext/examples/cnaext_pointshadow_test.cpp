// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-1009..MOD-1011: point and spot shadows as an application uses them.
//
// A lamp inside a box, which is the scene a point light exists for: it casts in every direction at
// once, so all six cube faces have to be right for the picture to be. A spot light on a plane
// follows, for the single-map path.
//
// MOD-1009 asked for committed golden images. These are rendered checks with the frame printed
// instead -- the same deviation recorded for MOD-852 and MOD-912, and the same reason: a golden
// records that a frame looked a particular way on the machine that made it, while a stated
// property records what was being relied on. Here that property is specifically about the *six
// faces*: a lamp in a box must light every wall it faces, which a single-face bug leaves partly
// dark while still producing a perfectly plausible image.
//
// Check A -- the renderer rasterizes 3D and compiles custom effects, or the program SKIPs.
// Check B -- the lamp lights all four walls of the box (every horizontal cube face is sampled).
// Check C -- a caster between the lamp and the floor darkens the floor beneath it.
// Check D -- the spot light is confined to its cone and its shadow darkens what it occludes.
// Check E -- with no punctual light the same scene renders at the ambient floor alone.
//
// `--benchmark` times six-face generation against a single directional map (MOD-1011), which is
// the number that justifies point shadows defaulting off.
//
// Exit code 0 = all checks PASS, 1 = any FAIL, 77 = SKIP.

#include "CNA/Graphics/CubeShadowMap.hpp"
#include "CNA/Graphics/PointLightEXT.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/SpotLightEXT.hpp"
#include "CNA/Graphics/SpotShadowMap.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/PunctualLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "System/NotSupportedException.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Graphics::CubeShadowMap;
using CNA::Graphics::PointLightEXT;
using CNA::Graphics::ShadowMap;
using CNA::Graphics::ShadowQuality;
using CNA::Graphics::SpotLightEXT;
using CNA::Graphics::SpotShadowMap;
using CNA::GraphicsCapability;

namespace
{
    constexpr int   kFrame       = 128;
    constexpr float kRoomHalf    = 8.0f;
    constexpr float kLampHeight  = 4.0f;
    constexpr float kCasterHigh  = 2.0f;
    constexpr float kCasterHalf  = 1.5f;
    constexpr float kRange       = 40.0f;

    /// A horizontal quad, used for the floor, the ceiling and the caster.
    void AppendHorizontal(std::vector<VertexPositionNormalTexture>& out, float y, float halfExtent,
                          const Vector3& normal)
    {
        const Vector2 uv(0.0f, 0.0f);
        const float e = halfExtent;
        const auto v = [&](float x, float z) {
            return VertexPositionNormalTexture(Vector3(x, y, z), normal, uv);
        };
        out.push_back(v(-e, -e)); out.push_back(v(e, -e)); out.push_back(v(e, e));
        out.push_back(v(-e, -e)); out.push_back(v(e, e));  out.push_back(v(-e, e));
    }

    /// One of the four walls, facing inward.
    void AppendWall(std::vector<VertexPositionNormalTexture>& out, int side)
    {
        const Vector2 uv(0.0f, 0.0f);
        const float e = kRoomHalf;
        const float h = kRoomHalf;
        Vector3 normal(0.0f, 0.0f, 0.0f);
        std::array<Vector3, 4> corners{};
        switch (side)
        {
        case 0:   // -X wall, facing +X
            normal = Vector3(1.0f, 0.0f, 0.0f);
            corners = {Vector3(-e, 0, -e), Vector3(-e, 0, e), Vector3(-e, h, e), Vector3(-e, h, -e)};
            break;
        case 1:   // +X wall
            normal = Vector3(-1.0f, 0.0f, 0.0f);
            corners = {Vector3(e, 0, e), Vector3(e, 0, -e), Vector3(e, h, -e), Vector3(e, h, e)};
            break;
        case 2:   // -Z wall
            normal = Vector3(0.0f, 0.0f, 1.0f);
            corners = {Vector3(e, 0, -e), Vector3(-e, 0, -e), Vector3(-e, h, -e), Vector3(e, h, -e)};
            break;
        default:  // +Z wall
            normal = Vector3(0.0f, 0.0f, -1.0f);
            corners = {Vector3(-e, 0, e), Vector3(e, 0, e), Vector3(e, h, e), Vector3(-e, h, e)};
            break;
        }
        out.push_back(VertexPositionNormalTexture(corners[0], normal, uv));
        out.push_back(VertexPositionNormalTexture(corners[1], normal, uv));
        out.push_back(VertexPositionNormalTexture(corners[2], normal, uv));
        out.push_back(VertexPositionNormalTexture(corners[0], normal, uv));
        out.push_back(VertexPositionNormalTexture(corners[2], normal, uv));
        out.push_back(VertexPositionNormalTexture(corners[3], normal, uv));
    }

    Matrix TopDownView()
    {
        return Matrix::CreateLookAt(Vector3(0.0f, 30.0f, 0.0f), Vector3::Zero,
                                    Vector3(0.0f, 0.0f, 1.0f));
    }

    Matrix Projection()
    {
        return Matrix::CreateOrthographic(kRoomHalf * 2.0f, kRoomHalf * 2.0f, 0.1f, 80.0f);
    }
}

class PointShadowExample : public Game
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

    static int At(const std::vector<Color>& pixels, int x, int y)
    {
        return pixels[static_cast<std::size_t>(y) * kFrame + static_cast<std::size_t>(x)]
            .getRProperty();
    }

    static void PrintAsciiFrame(const std::vector<Color>& pixels)
    {
        int brightest = 0;
        for (const Color& p : pixels)
            brightest = std::max(brightest, static_cast<int>(p.getRProperty()));
        for (int row = 0; row < 16; ++row)
        {
            std::string line;
            for (int column = 0; column < 32; ++column)
            {
                const int value = At(pixels, column * kFrame / 32, row * kFrame / 16);
                line += (value > brightest * 3 / 4) ? '#'
                      : ((value > brightest / 3) ? '+' : '.');
            }
            std::printf("    |%s|\n", line.c_str());
        }
    }

    /// Ambient only, so anything brighter came from the punctual light and nothing else. With a
    /// directional slot on, a light contributing nothing would still look plausible.
    static void ConfigureAmbientOnly(BasicEffect& effect)
    {
        effect.setLightingEnabledProperty(true);
        effect.setPreferPerPixelLightingProperty(true);
        effect.setTextureEnabledProperty(false);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setAmbientLightColorProperty(Vector3(0.08f, 0.08f, 0.08f));
        effect.setSpecularColorProperty(Vector3::Zero);
        effect.setEmissiveColorProperty(Vector3::Zero);
        effect.getDirectionalLight0Property().setEnabledProperty(false);
        effect.getDirectionalLight1Property().setEnabledProperty(false);
        effect.getDirectionalLight2Property().setEnabledProperty(false);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(TopDownView());
        effect.setProjectionProperty(Projection());
    }

    void DrawScene(GraphicsDevice& device, BasicEffect& effect,
                   const std::vector<VertexPositionNormalTexture>& geometry)
    {
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        device.setBlendStateProperty(BlendState::Opaque);
        device.Clear(Color::Black);
        effect.Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, geometry.data(), 0,
                                  static_cast<int>(geometry.size()) / 3);
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
            std::printf("SKIP: this renderer does not raster 3D or cannot compile the punctual "
                        "caster's shader (a documented capability boundary, not a defect)\n");
            std::exit(77);
        }

        std::vector<VertexPositionNormalTexture> floor;
        AppendHorizontal(floor, 0.0f, kRoomHalf, Vector3(0.0f, 1.0f, 0.0f));

        std::vector<VertexPositionNormalTexture> room = floor;
        for (int side = 0; side < 4; ++side)
            AppendWall(room, side);

        std::vector<VertexPositionNormalTexture> caster;
        AppendHorizontal(caster, kCasterHigh, kCasterHalf, Vector3(0.0f, 1.0f, 0.0f));

        std::vector<Color> pixels(static_cast<std::size_t>(kFrame) * kFrame, Color::Transparent);
        const auto readBack = [&] {
            try { device.GetBackBufferData(pixels.data(), static_cast<int>(pixels.size())); }
            catch (const System::NotSupportedException&)
            {
                std::printf("SKIP: this renderer has no readable back buffer\n");
                std::exit(77);
            }
        };

        // Check E first, so the ambient floor is a measured number rather than an assumption.
        BasicEffect plain(device);
        ConfigureAmbientOnly(plain);
        DrawScene(device, plain, room);
        readBack();
        const int ambientFloor = At(pixels, kFrame / 2, kFrame / 2);
        std::printf("--- ambient only --- floor reads %d\n", ambientFloor);
        check(ambientFloor > 0 && ambientFloor < 60,
              "with no punctual light the scene sits at the ambient floor");

        // Check B: the lamp lights all four walls, which means all four horizontal cube faces.
        CubeShadowMap cube(device, ShadowQuality::Medium);
        PointLightEXT light;
        light.Position = Vector3(0.0f, kLampHeight, 0.0f);
        light.Range    = kRange;

        cube.update(light);
        for (int face = 0; face < CubeShadowMap::kFaceCount; ++face)
        {
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            device.setDepthStencilStateProperty(DepthStencilState::Default);
            cube.begin(face);
            device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0,
                                      static_cast<int>(caster.size()) / 3);
            cube.end();
        }

        BasicEffect lamp(device);
        ConfigureAmbientOnly(lamp);
        PunctualLightEXT punctual;
        punctual.Kind            = PunctualLightKindEXT::Point;
        punctual.Position        = light.Position;
        punctual.DiffuseColor    = Vector3(120.0f, 120.0f, 120.0f);
        punctual.Range           = light.Range;
        punctual.ShadowCube      = cube.getShadowTexture();
        punctual.ShadowDepthBias = cube.getDepthBias();
        lamp.setPunctualLightEXT(punctual);

        DrawScene(device, lamp, room);
        readBack();
        std::printf("--- point light in a box ---\n");
        PrintAsciiFrame(pixels);

        // The four walls, seen from directly above, sit at the four edges of the frame. Each is
        // lit through a different horizontal cube face, so all four being brighter than ambient is
        // the six-face check a single probe could never make.
        const int wallMinusX = At(pixels, 2, kFrame / 2);
        const int wallPlusX  = At(pixels, kFrame - 3, kFrame / 2);
        const int wallMinusZ = At(pixels, kFrame / 2, 2);
        const int wallPlusZ  = At(pixels, kFrame / 2, kFrame - 3);
        std::printf("    walls: -X %d, +X %d, -Z %d, +Z %d\n",
                    wallMinusX, wallPlusX, wallMinusZ, wallPlusZ);
        check(wallMinusX > ambientFloor && wallPlusX > ambientFloor &&
              wallMinusZ > ambientFloor && wallPlusZ > ambientFloor,
              "the lamp lights all four walls, so every horizontal cube face carries its share");

        // Check C: the caster's shadow on the floor. Sampled just outside the caster's own
        // outline but well inside its shadow, which at height 2 under a lamp at height 4 reaches
        // twice the caster's own half-extent.
        const int underCaster = At(pixels, kFrame / 2, kFrame / 2);
        const int litFloor = At(pixels,
                                static_cast<int>((5.0f / (kRoomHalf * 2.0f) + 0.5f) * kFrame),
                                kFrame / 2);
        std::printf("    floor: under caster %d, lit %d\n", underCaster, litFloor);
        check(underCaster < litFloor,
              "the caster's cube shadow darkens the floor beneath it");

        // Check D: the spot light.
        SpotShadowMap spot(device, ShadowQuality::Medium);
        SpotLightEXT cone;
        cone.Position   = Vector3(0.0f, kLampHeight, 0.0f);
        cone.Direction  = Vector3(0.0f, -1.0f, 0.0f);
        cone.Range      = kRange;
        cone.OuterAngle = 0.7f;

        std::vector<VertexPositionNormalTexture> smallCaster;
        AppendHorizontal(smallCaster, kCasterHigh, 0.8f, Vector3(0.0f, 1.0f, 0.0f));

        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.setDepthStencilStateProperty(DepthStencilState::Default);
        spot.begin(cone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, smallCaster.data(), 0,
                                  static_cast<int>(smallCaster.size()) / 3);
        spot.end();

        BasicEffect spotEffect(device);
        ConfigureAmbientOnly(spotEffect);
        PunctualLightEXT spotLight;
        spotLight.Kind                 = PunctualLightKindEXT::Spot;
        spotLight.Position             = cone.Position;
        spotLight.Direction            = cone.Direction;
        spotLight.DiffuseColor         = Vector3(120.0f, 120.0f, 120.0f);
        spotLight.Range                = cone.Range;
        spotLight.InnerAngle           = 0.6f;
        spotLight.OuterAngle           = cone.OuterAngle;
        spotLight.ShadowMap            = spot.getShadowTexture();
        spotLight.ShadowViewProjection = spot.getLightViewProjection();
        spotLight.ShadowDepthBias      = spot.getDepthBias();
        spotEffect.setPunctualLightEXT(spotLight);

        DrawScene(device, spotEffect, floor);
        readBack();
        std::printf("--- spot light on a plane ---\n");
        PrintAsciiFrame(pixels);

        const int spotCentre = At(pixels, kFrame / 2, kFrame / 2);
        const int spotRing   = At(pixels,
                                  static_cast<int>((2.6f / (kRoomHalf * 2.0f) + 0.5f) * kFrame),
                                  kFrame / 2);
        const int spotOutside = At(pixels, 2, kFrame / 2);
        std::printf("    spot: centre %d, lit ring %d, outside cone %d\n",
                    spotCentre, spotRing, spotOutside);
        check(spotCentre < spotRing && spotOutside <= ambientFloor + 2,
              "the spot's shadow darkens its centre and its cone confines the light");

        if (benchmark_)
            RunBenchmark(device, caster);

        std::printf("=== %d/%d PASS ===\n", passCount_, checkCount_);
        result_ = (passCount_ == checkCount_) ? 0 : 1;
        Exit();
    }

    /// MOD-1011: six faces against one directional map, which is the figure that justifies point
    /// shadows defaulting off.
    void RunBenchmark(GraphicsDevice& device,
                      const std::vector<VertexPositionNormalTexture>& caster)
    {
        constexpr int kIterations = 10;
        const int primitives = static_cast<int>(caster.size()) / 3;
        std::printf("--- generation cost, %d casting triangles ---\n", primitives);

        {
            ShadowMap single(device, ShadowQuality::Medium);
            const BoundingBox bounds(Vector3(-kRoomHalf, -1.0f, -kRoomHalf),
                                     Vector3(kRoomHalf, kRoomHalf, kRoomHalf));
            CNA::Graphics::DirectionalLightEXT sun;
            single.begin(sun, bounds);
            device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0, primitives);
            single.end();

            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < kIterations; ++i)
            {
                single.begin(sun, bounds);
                device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0, primitives);
                single.end();
            }
            const auto finish = std::chrono::steady_clock::now();
            std::printf("    directional, 1 map    %6.2f ms/frame\n",
                        std::chrono::duration<double, std::milli>(finish - start).count()
                            / kIterations);
        }

        {
            CubeShadowMap cube(device, ShadowQuality::Medium);
            PointLightEXT light;
            light.Range = kRange;
            cube.update(light);
            for (int face = 0; face < CubeShadowMap::kFaceCount; ++face)
            {
                cube.begin(face);
                device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0, primitives);
                cube.end();
            }

            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < kIterations; ++i)
            {
                cube.update(light);
                for (int face = 0; face < CubeShadowMap::kFaceCount; ++face)
                {
                    cube.begin(face);
                    device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0,
                                              primitives);
                    cube.end();
                }
            }
            const auto finish = std::chrono::steady_clock::now();
            std::printf("    point, 6 faces        %6.2f ms/frame\n",
                        std::chrono::duration<double, std::milli>(finish - start).count()
                            / kIterations);
        }

        {
            SpotShadowMap spot(device, ShadowQuality::Medium);
            SpotLightEXT cone;
            cone.Range = kRange;
            spot.begin(cone);
            device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0, primitives);
            spot.end();

            const auto start = std::chrono::steady_clock::now();
            for (int i = 0; i < kIterations; ++i)
            {
                spot.begin(cone);
                device.DrawUserPrimitives(PrimitiveType::TriangleList, caster.data(), 0, primitives);
                spot.end();
            }
            const auto finish = std::chrono::steady_clock::now();
            std::printf("    spot, 1 map           %6.2f ms/frame\n",
                        std::chrono::duration<double, std::milli>(finish - start).count()
                            / kIterations);
        }
    }

public:
    explicit PointShadowExample(bool benchmark) : benchmark_(benchmark)
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
        PointShadowExample game(benchmark);
        game.Run();
        return game.getResult();
    }
    catch (const CNA::Platform::PlatformException& e)
    {
        std::printf("SKIP: no video subsystem here (%s)\n", e.what());
        return 77;
    }
}
