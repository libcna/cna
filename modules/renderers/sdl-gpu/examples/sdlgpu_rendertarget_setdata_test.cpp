// SPDX-License-Identifier: MS-PL
// SDLGPU-74: public RenderTarget2D/RenderTargetCube SetData storage, mip and chronology proof.

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/CubeMapFace.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "common/PixelTestGame.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#ifndef CNA_RENDERER_SDL_GPU
#error "SDLGPU-74's target upload regression is SDL_GPU-only."
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 8;

    [[nodiscard]] bool Exact(const Color& lhs, const Color& rhs)
    {
        return lhs.getRProperty() == rhs.getRProperty() &&
               lhs.getGProperty() == rhs.getGProperty() &&
               lhs.getBProperty() == rhs.getBProperty() &&
               lhs.getAProperty() == rhs.getAProperty();
    }

    [[nodiscard]] std::vector<Color> Solid(int count, const Color& color)
    {
        return std::vector<Color>(static_cast<std::size_t>(count), color);
    }

    [[nodiscard]] bool AllExact(const std::vector<Color>& pixels, const Color& color)
    {
        return std::all_of(pixels.begin(), pixels.end(),
                           [&](const Color& pixel) { return Exact(pixel, color); });
    }
}

/** @brief Runs SDLGPU-74's render-target upload and authored-mip regression matrix. */
class SdlGpuRenderTargetSetDataTest final : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
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

    [[nodiscard]] std::vector<Color> Read2D(RenderTarget2D& target, int level)
    {
        const int dimension = std::max(1, kSize >> level);
        std::vector<Color> pixels(static_cast<std::size_t>(dimension) * dimension,
                                  Color(1, 2, 3, 4));
        target.GetData(level, nullptr, pixels.data(), 0, static_cast<int>(pixels.size()));
        return pixels;
    }

    [[nodiscard]] std::vector<Color> ReadCube(
        RenderTargetCube& target, CubeMapFace face, int level)
    {
        const int dimension = std::max(1, kSize >> level);
        std::vector<Color> pixels(static_cast<std::size_t>(dimension) * dimension,
                                  Color(5, 6, 7, 8));
        target.GetData(face, level, nullptr, pixels.data(), 0,
                       static_cast<int>(pixels.size()));
        return pixels;
    }

    void Test2D(GraphicsDevice& device)
    {
        RenderTarget2D target(device, kSize, kSize, true, SurfaceFormat::Color,
                              DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

        auto level0 = Solid(kSize * kSize, Color::Red);
        target.SetData(level0.data(), static_cast<int>(level0.size()));
        Check(AllExact(Read2D(target, 0), Color::Red),
              "RenderTarget2D full level-0 SetData is byte exact");

        const Rectangle patchRect(2, 3, 3, 2);
        const auto greenPatch = Solid(patchRect.Width * patchRect.Height, Color::Green);
        target.SetData(0, &patchRect, greenPatch.data(), 0,
                       static_cast<int>(greenPatch.size()));
        const auto patched = Read2D(target, 0);
        bool patchExact = true;
        for (int y = 0; y < kSize; ++y)
        {
            for (int x = 0; x < kSize; ++x)
            {
                const bool inside = x >= patchRect.X && x < patchRect.X + patchRect.Width &&
                                    y >= patchRect.Y && y < patchRect.Y + patchRect.Height;
                if (!Exact(patched[static_cast<std::size_t>(y * kSize + x)],
                           inside ? Color::Green : Color::Red))
                    patchExact = false;
            }
        }
        Check(patchExact,
              "RenderTarget2D partial level-0 SetData preserves every untouched texel");

        constexpr int mipLevel = 2;
        constexpr int mipSize = kSize >> mipLevel;
        auto authoredMip = Solid(mipSize * mipSize, Color::Blue);
        target.SetData(mipLevel, nullptr, authoredMip.data(), 0,
                       static_cast<int>(authoredMip.size()));
        Check(AllExact(Read2D(target, mipLevel), Color::Blue),
              "RenderTarget2D authored lower mip reads back byte exact");

        const Rectangle mipPatchRect(1, 0, 1, 1);
        const Color yellow = Color::Yellow;
        target.SetData(mipLevel, &mipPatchRect, &yellow, 0, 1);
        const auto patchedMip = Read2D(target, mipLevel);
        bool mipPatchExact = true;
        for (int y = 0; y < mipSize; ++y)
        {
            for (int x = 0; x < mipSize; ++x)
            {
                const Color& expected = (x == 1 && y == 0) ? Color::Yellow : Color::Blue;
                if (!Exact(patchedMip[static_cast<std::size_t>(y * mipSize + x)], expected))
                    mipPatchExact = false;
            }
        }
        Check(mipPatchExact,
              "RenderTarget2D partial lower-mip SetData preserves authored neighbours");

        // Queue a render first, then upload without an intervening read or Present. The upload
        // must flush that older pass and remain the newest content.
        device.SetRenderTarget(&target);
        device.Clear(Color::Orange);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        level0 = Solid(kSize * kSize, Color::Cyan);
        target.SetData(level0.data(), static_cast<int>(level0.size()));
        Check(AllExact(Read2D(target, 0), Color::Cyan),
              "RenderTarget2D render-then-upload ordering keeps the later upload");

        // A later render owns level zero and regenerates the complete target chain; the older
        // authored child must no longer mask the generated result.
        device.SetRenderTarget(&target);
        device.Clear(Color::Magenta);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(AllExact(Read2D(target, mipLevel), Color::Magenta),
              "RenderTarget2D later render regenerates and replaces authored lower mips");

        target.SetData(mipLevel, &mipPatchRect, &yellow, 0, 1);
        const auto patchedGeneratedMip = Read2D(target, mipLevel);
        bool generatedPatchExact = true;
        for (int y = 0; y < mipSize; ++y)
        {
            for (int x = 0; x < mipSize; ++x)
            {
                const Color& expected = (x == 1 && y == 0) ? Color::Yellow : Color::Magenta;
                if (!Exact(patchedGeneratedMip[static_cast<std::size_t>(y * mipSize + x)],
                           expected))
                    generatedPatchExact = false;
            }
        }
        Check(generatedPatchExact,
              "RenderTarget2D partial authored update preserves generated mip neighbours");
    }

    void TestCube(GraphicsDevice& device)
    {
        RenderTargetCube target(device, kSize, true, SurfaceFormat::Color,
                                DepthFormat::None, 0, RenderTargetUsage::PreserveContents);

        auto positiveX = Solid(kSize * kSize, Color::Red);
        auto negativeZ = Solid(kSize * kSize, Color::Blue);
        target.SetData(CubeMapFace::PositiveX, positiveX.data(),
                       static_cast<int>(positiveX.size()));
        target.SetData(CubeMapFace::NegativeZ, negativeZ.data(),
                       static_cast<int>(negativeZ.size()));
        Check(AllExact(ReadCube(target, CubeMapFace::PositiveX, 0), Color::Red) &&
              AllExact(ReadCube(target, CubeMapFace::NegativeZ, 0), Color::Blue),
              "RenderTargetCube sequential face SetData retains both faces byte exact");

        const Rectangle patchRect(2, 3, 3, 2);
        const auto greenPatch = Solid(patchRect.Width * patchRect.Height, Color::Green);
        target.SetData(CubeMapFace::PositiveX, 0, &patchRect, greenPatch.data(), 0,
                       static_cast<int>(greenPatch.size()));
        const auto patched = ReadCube(target, CubeMapFace::PositiveX, 0);
        bool patchExact = true;
        for (int y = 0; y < kSize; ++y)
        {
            for (int x = 0; x < kSize; ++x)
            {
                const bool inside = x >= patchRect.X && x < patchRect.X + patchRect.Width &&
                                    y >= patchRect.Y && y < patchRect.Y + patchRect.Height;
                if (!Exact(patched[static_cast<std::size_t>(y * kSize + x)],
                           inside ? Color::Green : Color::Red))
                    patchExact = false;
            }
        }
        Check(patchExact,
              "RenderTargetCube partial face SetData preserves every untouched texel");

        constexpr int mipLevel = 1;
        constexpr int mipSize = kSize >> mipLevel;
        const auto authoredMip = Solid(mipSize * mipSize, Color::Yellow);
        target.SetData(CubeMapFace::NegativeZ, mipLevel, nullptr, authoredMip.data(), 0,
                       static_cast<int>(authoredMip.size()));
        Check(AllExact(ReadCube(target, CubeMapFace::NegativeZ, mipLevel), Color::Yellow),
              "RenderTargetCube authored face mip reads back byte exact");

        // Force a real first render pass after SetData. The small draw may land at either vertical
        // edge under backend coordinate conventions; the decisive invariant is that 60 untouched
        // authored texels survive instead of being replaced by the safety clear.
        Texture2D white(device, 1, 1, false, SurfaceFormat::Color);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 1);
        SpriteBatch sprites(device);
        device.SetRenderTarget(&target, CubeMapFace::PositiveX);
        sprites.Begin();
        sprites.Draw(white, Rectangle(0, 0, 2, 2), Color::White);
        sprites.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const auto afterFirstPass = ReadCube(target, CubeMapFace::PositiveX, 0);
        const int authoredSurvivors = static_cast<int>(std::count_if(
            afterFirstPass.begin(), afterFirstPass.end(), [](const Color& pixel) {
                return Exact(pixel, Color::Red) || Exact(pixel, Color::Green);
            }));
        Check(authoredSurvivors == 60,
              "RenderTargetCube first PreserveContents pass loads authored face bytes");

        device.SetRenderTarget(&target, CubeMapFace::NegativeZ);
        device.Clear(Color::Magenta);
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        Check(AllExact(ReadCube(target, CubeMapFace::NegativeZ, mipLevel), Color::Magenta),
              "RenderTargetCube later face render regenerates and replaces authored face mips");
    }

    void TestNumeric2DUploads(GraphicsDevice& device)
    {
        using Microsoft::Xna::Framework::Graphics::PackedVector::HalfSingle;
        using Microsoft::Xna::Framework::Graphics::PackedVector::Rgba64;

        RenderTarget2D halfTarget(device, 4, 2, false, SurfaceFormat::HalfSingle,
                                  DepthFormat::None, 0,
                                  RenderTargetUsage::PreserveContents);
        const std::vector<HalfSingle> halfSource(8, HalfSingle(2.5f));
        std::vector<HalfSingle> halfRead(8, HalfSingle(-1.0f));
        halfTarget.SetData(halfSource.data(), static_cast<int>(halfSource.size()));
        halfTarget.GetData(halfRead.data(), static_cast<int>(halfRead.size()));
        Check(std::all_of(halfRead.begin(), halfRead.end(), [](const HalfSingle& value) {
                  return value.ToSingle() == 2.5f;
              }),
              "RenderTarget2D SetData uses exact 2-byte HalfSingle storage");

        RenderTarget2D rgba64Target(device, 4, 2, false, SurfaceFormat::Rgba64,
                                    DepthFormat::None, 0,
                                    RenderTargetUsage::PreserveContents);
        const Rgba64 rgba64Value(0.25f, 0.5f, 0.75f, 1.0f);
        const std::vector<Rgba64> rgba64Source(8, rgba64Value);
        std::vector<Rgba64> rgba64Read(8);
        rgba64Target.SetData(rgba64Source.data(), static_cast<int>(rgba64Source.size()));
        rgba64Target.GetData(rgba64Read.data(), static_cast<int>(rgba64Read.size()));
        Check(std::all_of(rgba64Read.begin(), rgba64Read.end(), [&](const Rgba64& value) {
                  return value == rgba64Value;
              }),
              "RenderTarget2D SetData uses exact 8-byte Rgba64 storage");

        RenderTarget2D vectorTarget(device, 4, 2, false, SurfaceFormat::Vector4,
                                    DepthFormat::None, 0,
                                    RenderTargetUsage::PreserveContents);
        const Vector4 vectorValue(2.0f, 0.5f, 4.0f, 1.0f);
        const std::vector<Vector4> vectorSource(8, vectorValue);
        std::vector<Vector4> vectorRead(8, Vector4(-1.0f));
        vectorTarget.SetData(vectorSource.data(), static_cast<int>(vectorSource.size()));
        vectorTarget.GetData(vectorRead.data(), static_cast<int>(vectorRead.size()));
        Check(std::all_of(vectorRead.begin(), vectorRead.end(), [&](const Vector4& value) {
                  return value == vectorValue;
              }),
              "RenderTarget2D SetData uses exact 16-byte Vector4 storage");
    }

    void TestMultisamplePreservation(GraphicsDevice& device)
    {
        Texture2D white(device, 1, 1, false, SurfaceFormat::Color);
        const Color whitePixel = Color::White;
        white.SetData(&whitePixel, 1);
        Texture2D green(device, 1, 1, false, SurfaceFormat::Color);
        const Color greenPixel = Color::Green;
        green.SetData(&greenPixel, 1);
        SpriteBatch sprites(device);

        RenderTarget2D target2D(device, kSize, kSize, false, SurfaceFormat::Color,
                                DepthFormat::None, 4,
                                RenderTargetUsage::PreserveContents);
        Check(target2D.getMultiSampleCountProperty() > 1,
              "RenderTarget2D authored-preservation leg uses real multisampling");
        auto red = Solid(kSize * kSize, Color::Red);
        target2D.SetData(red.data(), static_cast<int>(red.size()));
        device.SetRenderTarget(&target2D);
        sprites.Begin();
        sprites.Draw(white, Rectangle(0, 0, 2, 2), Color::White);
        sprites.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const auto after2DPass = Read2D(target2D, 0);
        Check(std::count_if(after2DPass.begin(), after2DPass.end(), [](const Color& pixel) {
                  return Exact(pixel, Color::Red);
              }) == 60,
              "multisample RenderTarget2D seeds and preserves authored bytes for a partial draw");
        device.SetRenderTarget(&target2D);
        sprites.Begin();
        sprites.Draw(green, Rectangle(6, 6, 2, 2), Color::White);
        sprites.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const auto afterSecond2DPass = Read2D(target2D, 0);
        Check(std::count_if(
                  afterSecond2DPass.begin(), afterSecond2DPass.end(), [](const Color& pixel) {
                      return Exact(pixel, Color::Red);
                  }) == 56,
              "multisample RenderTarget2D stores samples across a later PreserveContents bind");

        RenderTargetCube cube(device, kSize, false, SurfaceFormat::Color,
                              DepthFormat::None, 4,
                              RenderTargetUsage::PreserveContents);
        Check(cube.getMultiSampleCountProperty() > 1,
              "RenderTargetCube authored-preservation leg uses real multisampling");
        auto blue = Solid(kSize * kSize, Color::Blue);
        cube.SetData(CubeMapFace::PositiveY, blue.data(), static_cast<int>(blue.size()));
        device.SetRenderTarget(&cube, CubeMapFace::PositiveY);
        sprites.Begin();
        sprites.Draw(white, Rectangle(0, 0, 2, 2), Color::White);
        sprites.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const auto afterCubePass = ReadCube(cube, CubeMapFace::PositiveY, 0);
        Check(std::count_if(afterCubePass.begin(), afterCubePass.end(), [](const Color& pixel) {
                  return Exact(pixel, Color::Blue);
              }) == 60,
              "multisample RenderTargetCube seeds and preserves authored bytes for a partial draw");
        device.SetRenderTarget(&cube, CubeMapFace::PositiveY);
        sprites.Begin();
        sprites.Draw(green, Rectangle(6, 6, 2, 2), Color::White);
        sprites.End();
        device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
        const auto afterSecondCubePass = ReadCube(cube, CubeMapFace::PositiveY, 0);
        Check(std::count_if(
                  afterSecondCubePass.begin(), afterSecondCubePass.end(), [](const Color& pixel) {
                      return Exact(pixel, Color::Blue);
                  }) == 56,
              "multisample RenderTargetCube stores samples across a later PreserveContents bind");
    }

protected:
    void Draw(const GameTime&) override
    {
        static bool done = false;
        if (done) return;
        done = true;

        try
        {
            GraphicsDevice& device = getGraphicsDeviceProperty();
            Test2D(device);
            TestCube(device);
            TestNumeric2DUploads(device);
            TestMultisamplePreservation(device);
        }
        catch (const std::exception& error)
        {
            std::printf("[FAIL] unexpected exception: %s\n", error.what());
        }

        std::printf("=== %d/%d PASS ===\n", passed_, total_);
        result_ = passed_ == total_ && total_ == 21 ? 0 : 1;
        Exit();
    }

public:
    SdlGpuRenderTargetSetDataTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    [[nodiscard]] int Result() const noexcept { return result_; }
};

int main()
{
    if (!CNA::Examples::ProbeGpuDisplayAvailable())
        return CNA::Examples::kSkipExitCode;

    SdlGpuRenderTargetSetDataTest game;
    game.Run();
    return game.Result();
}
