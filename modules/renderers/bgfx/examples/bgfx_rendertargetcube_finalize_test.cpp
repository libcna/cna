// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-138 -- a Bgfx RenderTargetCube must expose its completed public image when sampled
// and read: the selected face's resolved level 0 followed by every generated mip level. This
// fixture deliberately performs no Present, backbuffer read, retry, wait, dummy target bind or
// intermediate public read between the cube producers and their EnvironmentMapEffect consumers.
//
// Every face uses a different two-colour checkerboard, and a second producer round uses different
// colours again. Direct reads require the final round's alternating level-0 texels and its averaged
// descendants; same-frame minified consumers require that face's final average. Consequently flat,
// zero, stale, aliased and wrong-face results are independently unable to pass.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureAddressMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureFilter.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <vector>

#ifndef CNA_RENDERER_BGFX
#error "REMED-GFX-138's focused finalization fixture is Bgfx-only."
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kCubeSize = 32;
    constexpr int kConsumerSize = 8;
    constexpr int kGuard = 5;
    constexpr int kSuffix = 7;
    constexpr int kTolerance = 14;
    const Color kSentinel(7, 11, 19, 197);

    const std::array<CubeMapFace, 6> kFaces = {
        CubeMapFace::PositiveX, CubeMapFace::NegativeX,
        CubeMapFace::PositiveY, CubeMapFace::NegativeY,
        CubeMapFace::PositiveZ, CubeMapFace::NegativeZ,
    };

    const std::array<const char*, 6> kFaceNames = {"+X", "-X", "+Y", "-Y", "+Z", "-Z"};
    const std::array<int, 6> kProducerOrder = {0, 2, 4, 1, 5, 3};
    const std::array<int, 6> kReadOrder = {4, 1, 5, 0, 3, 2};

    struct FaceAim
    {
        float x;
        float y;
        float z;
    };

    // EnvironmentMapEffect's reflection-vector convention is pinned by REMED-GFX-173/181.
    const std::array<FaceAim, 6> kFaceAims = {{
        { 1.0f,  0.0f, 1.0f}, {-1.0f,  0.0f, 1.0f},
        { 0.0f,  1.0f, 1.0f}, { 0.0f, -1.0f, 1.0f},
        { 0.0f,  0.0f, 1.0f}, { 1.0f,  0.0f, 0.0f},
    }};

    [[nodiscard]] bool Exact(const Color& a, const Color& b)
    {
        return a.getRProperty() == b.getRProperty()
            && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty()
            && a.getAProperty() == b.getAProperty();
    }

    [[nodiscard]] bool Near(const Color& a, const Color& b, int tolerance = kTolerance)
    {
        const auto d = [](int x, int y) { return std::abs(x - y); };
        return d(a.getRProperty(), b.getRProperty()) <= tolerance
            && d(a.getGProperty(), b.getGProperty()) <= tolerance
            && d(a.getBProperty(), b.getBProperty()) <= tolerance
            && d(a.getAProperty(), b.getAProperty()) <= tolerance;
    }

    [[nodiscard]] std::string ColorText(const Color& c)
    {
        return "(" + std::to_string(c.getRProperty()) + "," +
               std::to_string(c.getGProperty()) + "," +
               std::to_string(c.getBProperty()) + "," +
               std::to_string(c.getAProperty()) + ")";
    }

    [[nodiscard]] Color CheckerColor(int face, int round, bool high)
    {
        const int bias = round * 17;
        const int lift = high ? 64 : 0;
        return Color(
            static_cast<bytecs>(24 + face * 27 + bias + lift),
            static_cast<bytecs>(31 + face * 19 + bias + lift),
            static_cast<bytecs>(39 + face * 13 + bias + lift),
            static_cast<bytecs>(255));
    }

    [[nodiscard]] Color CheckerAverage(int face, int round)
    {
        const Color low = CheckerColor(face, round, false);
        const Color high = CheckerColor(face, round, true);
        return Color(
            static_cast<bytecs>((low.getRProperty() + high.getRProperty()) / 2),
            static_cast<bytecs>((low.getGProperty() + high.getGProperty()) / 2),
            static_cast<bytecs>((low.getBProperty() + high.getBProperty()) / 2),
            static_cast<bytecs>(255));
    }

    [[nodiscard]] int ChainLength(int size)
    {
        int levels = 1;
        while (size > 1)
        {
            size = std::max(1, size / 2);
            ++levels;
        }
        return levels;
    }

    [[nodiscard]] int LevelSize(int level)
    {
        return std::max(1, kCubeSize >> level);
    }

    [[nodiscard]] SamplerState PointClampSampler()
    {
        SamplerState result;
        result.setFilterProperty(TextureFilter::Point);
        result.setAddressUProperty(TextureAddressMode::Clamp);
        result.setAddressVProperty(TextureAddressMode::Clamp);
        result.setAddressWProperty(TextureAddressMode::Clamp);
        return result;
    }

    [[nodiscard]] SamplerState LinearClampSampler()
    {
        SamplerState result;
        result.setFilterProperty(TextureFilter::Linear);
        result.setAddressUProperty(TextureAddressMode::Clamp);
        result.setAddressVProperty(TextureAddressMode::Clamp);
        result.setAddressWProperty(TextureAddressMode::Clamp);
        return result;
    }
}

class BgfxRenderTargetCubeFinalizeTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    bool done_ = false;
    int passed_ = 0;
    int total_ = 0;
    int result_ = 1;

    void Check(bool condition, const std::string& label)
    {
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++total_;
        if (condition) ++passed_;
    }

    void ResetDrawState(GraphicsDevice& device)
    {
        device.setBlendStateProperty(BlendState::Opaque);
        device.setDepthStencilStateProperty(DepthStencilState::None);
        device.setRasterizerStateProperty(RasterizerState::CullNone);
    }

    [[nodiscard]] std::unique_ptr<Texture2D> MakeChecker(GraphicsDevice& device, int face, int round)
    {
        auto texture = std::make_unique<Texture2D>(
            device, kCubeSize, kCubeSize, false, SurfaceFormat::Color);
        std::vector<Color> pixels(
            static_cast<std::size_t>(kCubeSize) * kCubeSize, Color::Transparent);
        for (int y = 0; y < kCubeSize; ++y)
            for (int x = 0; x < kCubeSize; ++x)
                pixels[static_cast<std::size_t>(y * kCubeSize + x)] =
                    CheckerColor(face, round, ((x ^ y) & 1) != 0);
        texture->SetData(pixels.data(), static_cast<int>(pixels.size()));
        return texture;
    }

    void RenderChecker(GraphicsDevice& device, RenderTargetCube& cube, int face, Texture2D& checker)
    {
        device.SetRenderTarget(&cube, kFaces[static_cast<std::size_t>(face)]);
        SamplerState point = PointClampSampler();
        DepthStencilState noDepth = DepthStencilState::None;
        RasterizerState noCull = RasterizerState::CullNone;
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, &noDepth, &noCull);
        spriteBatch_->Draw(checker, Rectangle(0, 0, kCubeSize, kCubeSize),
                           Rectangle(0, 0, kCubeSize, kCubeSize), Color::White);
        spriteBatch_->End();
    }

    void DrawEnvironmentFace(GraphicsDevice& device, RenderTargetCube& cube,
                             Texture2D& white, const FaceAim& aim)
    {
        ResetDrawState(device);
        device.getSamplerStatesProperty()[0] = LinearClampSampler();
        device.getSamplerStatesProperty()[1] = LinearClampSampler();

        EnvironmentMapEffect effect(device);
        effect.setWorldProperty(Matrix::getIdentityProperty());
        effect.setViewProperty(Matrix::CreateLookAt(
            Vector3(0.0f, 0.0f, 4.0f / 3.0f), Vector3::Zero, Vector3::Up));
        effect.setProjectionProperty(Matrix::CreateOrthographic(2.0f, 2.0f, 0.1f, 100.0f));
        effect.setTextureProperty(&white);
        effect.setEnvironmentMapProperty(&cube);
        effect.setEnvironmentMapAmountProperty(1.0f);
        effect.setFresnelFactorProperty(0.0f);
        effect.setEnvironmentMapSpecularProperty(Vector3::Zero);
        effect.setDiffuseColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.setAlphaProperty(1.0f);
        effect.setEmissiveColorProperty(Vector3::Zero);
        effect.setAmbientLightColorProperty(Vector3(1.0f, 1.0f, 1.0f));
        effect.DirectionalLight0.setEnabledProperty(false);
        effect.DirectionalLight1.setEnabledProperty(false);
        effect.DirectionalLight2.setEnabledProperty(false);
        effect.setFogEnabledProperty(false);
        effect.Apply();

        const float length = std::sqrt(aim.x * aim.x + aim.y * aim.y + aim.z * aim.z);
        const Vector3 normal(aim.x / length, aim.y / length, aim.z / length);
        const VertexPositionNormalTexture vertices[6] = {
            {Vector3(-1.f,  1.f, 0.f), normal, Vector2(0.f, 0.f)},
            {Vector3( 1.f, -1.f, 0.f), normal, Vector2(1.f, 1.f)},
            {Vector3(-1.f, -1.f, 0.f), normal, Vector2(0.f, 1.f)},
            {Vector3(-1.f,  1.f, 0.f), normal, Vector2(0.f, 0.f)},
            {Vector3( 1.f,  1.f, 0.f), normal, Vector2(1.f, 0.f)},
            {Vector3( 1.f, -1.f, 0.f), normal, Vector2(1.f, 1.f)},
        };
        device.DrawUserPrimitives(PrimitiveType::TriangleList, vertices, 0, 2);
    }

    void ReadCubeLevel(RenderTargetCube& cube, int face, int level, int round,
                       const std::string& label)
    {
        const int edge = LevelSize(level);
        const int count = edge * edge;
        std::vector<Color> destination(
            static_cast<std::size_t>(kGuard + count + kSuffix), kSentinel);
        bool returned = false;
        std::string exception;
        try
        {
            cube.GetData(kFaces[static_cast<std::size_t>(face)], level, nullptr,
                         destination.data(), kGuard, count);
            returned = true;
        }
        catch (const std::exception& e)
        {
            exception = e.what();
        }
        Check(returned, label + ": GetData returns" +
                        (exception.empty() ? std::string() : " (" + exception + ")"));

        int exact = 0;
        int zero = 0;
        std::set<std::array<int, 4>> distinct;
        std::string firstMismatch;
        for (int y = 0; y < edge; ++y)
        {
            for (int x = 0; x < edge; ++x)
            {
                const Color got = destination[static_cast<std::size_t>(kGuard + y * edge + x)];
                const Color expected = level == 0
                    ? CheckerColor(face, round, ((x ^ y) & 1) != 0)
                    : CheckerAverage(face, round);
                const bool correct = level == 0 ? Exact(got, expected) : Near(got, expected);
                if (correct) ++exact;
                else if (firstMismatch.empty())
                    firstMismatch = " first mismatch (" + std::to_string(x) + "," +
                        std::to_string(y) + ") got " + ColorText(got) + " expected " +
                        ColorText(expected);
                if (got.getRProperty() == 0 && got.getGProperty() == 0 &&
                    got.getBProperty() == 0 && got.getAProperty() == 0) ++zero;
                distinct.insert({got.getRProperty(), got.getGProperty(),
                                 got.getBProperty(), got.getAProperty()});
            }
        }
        Check(returned && exact == count,
              label + ": final-round content is exact/near " + std::to_string(exact) + "/" +
              std::to_string(count) + firstMismatch);
        Check(zero == 0, label + ": no texel is fabricated transparent zero");
        if (level == 0)
            Check(distinct.size() == 2,
                  label + ": level 0 retains both alternating checker colours (distinct=" +
                  std::to_string(distinct.size()) + ")");

        bool guards = true;
        for (int i = 0; i < kGuard; ++i)
            guards = guards && Exact(destination[static_cast<std::size_t>(i)], kSentinel);
        for (int i = 0; i < kSuffix; ++i)
            guards = guards && Exact(destination[static_cast<std::size_t>(kGuard + count + i)],
                                     kSentinel);
        Check(guards, label + ": protected destination prefix and suffix are untouched");
    }

    void RunConfiguration(GraphicsDevice& device, bool mipMap, int requestedSamples,
                          const std::string& label)
    {
        auto cube = std::make_unique<RenderTargetCube>(
            device, kCubeSize, mipMap, SurfaceFormat::Color, DepthFormat::None,
            requestedSamples, RenderTargetUsage::DiscardContents);
        Check(cube->getLevelCountProperty() == (mipMap ? ChainLength(kCubeSize) : 1),
              label + ": public LevelCount matches the requested chain");
        if (requestedSamples == 0)
            Check(cube->getMultiSampleCountProperty() == 0,
                  label + ": single-sample control applies no MSAA");
        else
            Check(cube->getMultiSampleCountProperty() > 1,
                  label + ": requested MSAA engages (applied " +
                  std::to_string(cube->getMultiSampleCountProperty()) + "x)");

        std::array<std::array<std::unique_ptr<Texture2D>, 6>, 2> checkers;
        for (int round = 0; round < 2; ++round)
            for (int face = 0; face < 6; ++face)
                checkers[static_cast<std::size_t>(round)][static_cast<std::size_t>(face)] =
                    MakeChecker(device, face, round);

        ResetDrawState(device);
        // Round 0 exists solely so an implementation exposing stale producer content cannot pass.
        for (int face : kProducerOrder)
            RenderChecker(device, *cube, face,
                          *checkers[0][static_cast<std::size_t>(face)]);
        // A/B/A in the final round, with every other face interleaved, exercises face identity and
        // producer-consumer ordering without any public synchronization operation.
        for (int face : kProducerOrder)
            RenderChecker(device, *cube, face,
                          *checkers[1][static_cast<std::size_t>(face)]);
        RenderChecker(device, *cube, 0, *checkers[1][0]);

        Texture2D white(device, 1, 1, false, SurfaceFormat::Color);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 1);
        std::array<std::unique_ptr<RenderTarget2D>, 6> consumers;
        for (int face : kReadOrder)
        {
            consumers[static_cast<std::size_t>(face)] = std::make_unique<RenderTarget2D>(
                device, kConsumerSize, kConsumerSize, false, SurfaceFormat::Color,
                DepthFormat::None, 0, RenderTargetUsage::DiscardContents);
            device.SetRenderTarget(consumers[static_cast<std::size_t>(face)].get());
            device.Clear(kSentinel);
            DrawEnvironmentFace(device, *cube, white,
                                kFaceAims[static_cast<std::size_t>(face)]);
        }
        device.SetRenderTargets({});

        for (int face : kReadOrder)
        {
            Color sampled = kSentinel;
            const Rectangle center(kConsumerSize / 2, kConsumerSize / 2, 1, 1);
            bool returned = false;
            std::string exception;
            try
            {
                consumers[static_cast<std::size_t>(face)]->GetData(
                    0, &center, &sampled, 0, 1);
                returned = true;
            }
            catch (const std::exception& e)
            {
                exception = e.what();
            }
            // With no mip chain the centre reflection lands on the checker's high texel; with a
            // mip chain the 8x8 consumer minifies through generated levels to the face average.
            const Color expected = mipMap
                ? CheckerAverage(face, 1)
                : CheckerColor(face, 1, true);
            Check(returned && Near(sampled, expected),
                  label + " face " + kFaceNames[static_cast<std::size_t>(face)] +
                  ": same-frame minified EnvironmentMapEffect consumer sees the final face " +
                  "(got " + ColorText(sampled) + ", expected near " + ColorText(expected) +
                  (exception.empty() ? ")" : ", exception " + exception + ")"));
            const Color stale = mipMap
                ? CheckerAverage(face, 0)
                : CheckerColor(face, 0, true);
            Check(!Near(sampled, stale),
                  label + " face " + kFaceNames[static_cast<std::size_t>(face)] +
                  ": consumer did not sample the stale first producer round");
        }

        for (int face : kReadOrder)
            for (int level = 0; level < cube->getLevelCountProperty(); ++level)
                ReadCubeLevel(*cube, face, level, 1,
                              label + " face " + kFaceNames[static_cast<std::size_t>(face)] +
                              " level " + std::to_string(level));
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& device = getGraphicsDeviceProperty();
        try
        {
            RunConfiguration(device, false, 0, "ordinary single-sample one-level cube");
            RunConfiguration(device, true, 0, "single-sample mipmapped cube");
            RunConfiguration(device, false, 4, "multisampled one-level cube");
            RunConfiguration(device, true, 4, "multisampled mipmapped cube");
        }
        catch (const std::exception& e)
        {
            Check(false, std::string("unexpected REMED-GFX-138 exception: ") + e.what());
            try { device.SetRenderTargets({}); } catch (...) {}
        }
        std::printf("[INFO] REMED-GFX-138 Bgfx cube finalization: %d/%d checks passed\n",
                    passed_, total_);
        std::fflush(stdout);
        result_ = passed_ == total_ ? 0 : 1;
        Exit();
    }

public:
    BgfxRenderTargetCubeFinalizeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    [[nodiscard]] int Result() const { return result_; }
};

int main()
{
    BgfxRenderTargetCubeFinalizeTest game;
    game.Run();
    return game.Result();
}
