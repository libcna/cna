// SPDX-License-Identifier: MS-PL
// SKIA-135: public packed Texture2D transfers, native mip precision, sampling, and refusal gates.

#include "CNA/Internal/Renderers/Skia/SkiaRenderer.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaTextureRenderer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgr565.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Bgra4444.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using CNA::Internal::Renderers::Skia::SkiaRenderer;
using CNA::Internal::Renderers::Skia::SkiaResourceStats;
using CNA::Internal::Renderers::Skia::SkiaTextureRenderer;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Graphics::PackedVector;

namespace
{
    template<typename Packed, typename Word>
    [[nodiscard]] Packed MakePacked(Word word)
    {
        Packed result;
        result.setPackedValueProperty(word);
        return result;
    }

    template<typename Packed, typename Word>
    [[nodiscard]] std::vector<Packed> MakePackedVector(const std::vector<Word>& words)
    {
        std::vector<Packed> result;
        result.reserve(words.size());
        for (const Word word : words)
            result.push_back(MakePacked<Packed>(word));
        return result;
    }

    template<typename Packed, typename Word>
    [[nodiscard]] bool SameWords(const std::vector<Packed>& actual,
                                 const std::vector<Word>& expected,
                                 std::size_t actualOffset = 0u)
    {
        if (actual.size() < actualOffset + expected.size())
            return false;
        for (std::size_t index = 0; index < expected.size(); ++index)
        {
            if (actual[actualOffset + index].getPackedValueProperty() != expected[index])
                return false;
        }
        return true;
    }

    [[nodiscard]] std::uint16_t AverageBgr565(const std::vector<std::uint16_t>& words)
    {
        std::uint32_t red = 0u;
        std::uint32_t green = 0u;
        std::uint32_t blue = 0u;
        for (const std::uint16_t word : words)
        {
            red += (word >> 11u) & 0x1Fu;
            green += (word >> 5u) & 0x3Fu;
            blue += word & 0x1Fu;
        }
        const auto average = [count = static_cast<std::uint32_t>(words.size())](
                                 std::uint32_t sum) {
            return (sum + count / 2u) / count;
        };
        return static_cast<std::uint16_t>(
            (average(red) << 11u) | (average(green) << 5u) | average(blue));
    }

    [[nodiscard]] std::uint16_t AverageBgra4444(const std::vector<std::uint16_t>& words)
    {
        std::uint32_t red = 0u;
        std::uint32_t green = 0u;
        std::uint32_t blue = 0u;
        std::uint32_t alpha = 0u;
        for (const std::uint16_t word : words)
        {
            red += (word >> 8u) & 0xFu;
            green += (word >> 4u) & 0xFu;
            blue += word & 0xFu;
            alpha += (word >> 12u) & 0xFu;
        }
        const auto average = [count = static_cast<std::uint32_t>(words.size())](
                                 std::uint32_t sum) {
            return (sum + count / 2u) / count;
        };
        return static_cast<std::uint16_t>(
            (average(alpha) << 12u) | (average(red) << 8u)
            | (average(green) << 4u) | average(blue));
    }

    [[nodiscard]] std::uint32_t AverageRgba1010102(const std::vector<std::uint32_t>& words)
    {
        std::uint64_t red = 0u;
        std::uint64_t green = 0u;
        std::uint64_t blue = 0u;
        std::uint64_t alpha = 0u;
        for (const std::uint32_t word : words)
        {
            red += word & 0x3FFu;
            green += (word >> 10u) & 0x3FFu;
            blue += (word >> 20u) & 0x3FFu;
            alpha += (word >> 30u) & 0x3u;
        }
        const auto average = [count = static_cast<std::uint64_t>(words.size())](
                                 std::uint64_t sum) {
            return static_cast<std::uint32_t>((sum + count / 2u) / count);
        };
        return average(red) | (average(green) << 10u) | (average(blue) << 20u)
            | (average(alpha) << 30u);
    }

    [[nodiscard]] bool NearChannel(std::uint8_t actual, int expected, int tolerance = 1)
    {
        return std::abs(static_cast<int>(actual) - expected) <= tolerance;
    }

    template<typename Callable>
    [[nodiscard]] bool Throws(Callable&& callable)
    {
        try
        {
            callable();
        }
        catch (const std::exception&)
        {
            return true;
        }
        return false;
    }
}

class InspectablePackedTexture2D final : public Texture2D
{
public:
    using Texture2D::Texture2D;

    [[nodiscard]] SkiaTextureRenderer* SkiaRenderer() const
    {
        return dynamic_cast<SkiaTextureRenderer*>(GetRendererRaw());
    }
};

class SkiaTexture2DPackedFormatsTest final : public Game
{
public:
    SkiaTexture2DPackedFormatsTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(20);
        graphics_->setPreferredBackBufferHeightProperty(4);
        graphics_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    [[nodiscard]] int Result() const noexcept { return failures_ == 0 ? 0 : 1; }

protected:
    void Initialize() override
    {
        Game::Initialize();
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
    }

    void Draw(const GameTime&) override
    {
        if (done_)
            return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto* graphicsRenderer = dynamic_cast<SkiaRenderer*>(&device.GetRenderer());
        Check(graphicsRenderer != nullptr, "public GraphicsDevice owns the Skia renderer");
        if (!graphicsRenderer)
        {
            Exit();
            return;
        }
        const SkiaResourceStats baseline = graphicsRenderer->GetResourceStatsEXT();

        ExercisePacked<std::uint16_t, Bgr565>(
            device, *graphicsRenderer, SurfaceFormat::Bgr565, "Bgr565",
            {0xF800u, 0x07E0u, 0x001Fu, 0xFFFFu, 0x0000u, 0x780Fu},
            {0x1234u, 0xABCDu}, AverageBgr565, 24u, 14u);
        ExercisePacked<std::uint16_t, Bgra4444>(
            device, *graphicsRenderer, SurfaceFormat::Bgra4444, "Bgra4444",
            {0xFF00u, 0xF0F0u, 0xF00Fu, 0xFFFFu, 0x0000u, 0x8787u},
            {0x9123u, 0xE456u}, AverageBgra4444, 48u, 14u);
        ExercisePacked<std::uint32_t, Rgba1010102>(
            device, *graphicsRenderer, SurfaceFormat::Rgba1010102, "Rgba1010102",
            {0xC00003FFu, 0xC00FFCu, 0xFFF00000u, 0xFFFFFFFFu, 0x00000000u,
             0x601801FFu},
            {0xD2345678u, 0x89ABCDEFu}, AverageRgba1010102, 48u, 28u);

        Check(SameTextureStats(graphicsRenderer->GetResourceStatsEXT(), baseline),
              "packed transfer fixtures release every image view and mip chain");
        CheckTypedRefusalIsAtomic(device);
        CheckRenderTargetRefusal(device, *graphicsRenderer);
        CheckRenderTargetPromotion(device, *graphicsRenderer);
        CheckSampling(device, *graphicsRenderer);
        Check(SameTextureStats(graphicsRenderer->GetResourceStatsEXT(), baseline),
              "packed sampling fixtures return all texture counters to baseline");

        std::printf("=== %d/%d PASS ===\n", checks_ - failures_, checks_);
        Exit();
    }

private:
    template<typename Word, typename Packed, typename Average>
    void ExercisePacked(GraphicsDevice& device, SkiaRenderer& graphicsRenderer,
                        SurfaceFormat format, const char* name,
                        std::vector<Word> expected, const std::vector<Word>& patch,
                        Average&& average, std::size_t expectedImageBytes,
                        std::size_t expectedMipBytes)
    {
        static_assert(std::is_unsigned_v<Word>);
        const SkiaResourceStats before = graphicsRenderer.GetResourceStatsEXT();
        {
            InspectablePackedTexture2D texture(device, 3, 2, true, format);
            SkiaTextureRenderer* renderer = texture.SkiaRenderer();
            const SkiaResourceStats allocated = graphicsRenderer.GetResourceStatsEXT();
            Check(texture.getFormatProperty() == format && texture.getLevelCountProperty() == 2
                      && renderer && renderer->FormatEXT() == format,
                  std::string(name) + " constructs the exact public/renderer format and mip count");
            Check(allocated.textureRenderers == before.textureRenderers + 1u
                      && allocated.textureImageViews == before.textureImageViews + 2u
                      && allocated.textureImageBytes == before.textureImageBytes + expectedImageBytes
                      && allocated.mipChains2D == before.mipChains2D + 1u
                      && allocated.mipChain2DStorageBytes
                          == before.mipChain2DStorageBytes + expectedMipBytes,
                  std::string(name) + " accounts its native transfer and working-view bytes");

            const Word guard = static_cast<Word>(~Word{0} - Word{1});
            std::vector<Packed> upload(8, MakePacked<Packed>(guard));
            const std::vector<Packed> packedExpected = MakePackedVector<Packed>(expected);
            for (std::size_t index = 0; index < packedExpected.size(); ++index)
                upload[index + 1u] = packedExpected[index];
            texture.SetData(0, nullptr, upload.data(), 1, 7);

            std::vector<Packed> read(8, MakePacked<Packed>(guard));
            texture.GetData(read.data(), 1, 7);
            Check(SameWords<Packed>(read, expected, 1u)
                      && read.front().getPackedValueProperty() == guard
                      && read.back().getPackedValueProperty() == guard,
                  std::string(name) + " full transfer is word-exact and preserves caller guards");

            bool littleEndian = renderer != nullptr;
            if (renderer)
            {
                const std::uint8_t* raw = renderer->MipChainEXT().LevelData(0);
                for (std::size_t index = 0; index < expected.size(); ++index)
                {
                    for (std::size_t byte = 0; byte < sizeof(Word); ++byte)
                    {
                        littleEndian = littleEndian
                            && raw[index * sizeof(Word) + byte]
                                == static_cast<std::uint8_t>(expected[index] >> (byte * 8u));
                    }
                }
            }
            Check(littleEndian, std::string(name) + " renderer storage is explicitly little-endian");

            std::array<Packed, 1> generated{MakePacked<Packed>(Word{0})};
            texture.GetData(1, nullptr, generated.data(), 0, 1);
            Check(generated[0].getPackedValueProperty() == average(expected)
                      && renderer && renderer->MipGenerationCountEXT(1) == 1u,
                  std::string(name) + " mip generation averages native channel precision once");

            const Rectangle patchRectangle(1, 0, 1, 2);
            std::vector<Packed> patchUpload(4, MakePacked<Packed>(guard));
            patchUpload[1] = MakePacked<Packed>(patch[0]);
            patchUpload[2] = MakePacked<Packed>(patch[1]);
            texture.SetData(0, &patchRectangle, patchUpload.data(), 1, 3);
            expected[1] = patch[0];
            expected[4] = patch[1];

            std::vector<Packed> patchRead(4, MakePacked<Packed>(guard));
            texture.GetData(0, &patchRectangle, patchRead.data(), 1, 3);
            Check(patchRead[0].getPackedValueProperty() == guard
                      && patchRead[1].getPackedValueProperty() == patch[0]
                      && patchRead[2].getPackedValueProperty() == patch[1]
                      && patchRead[3].getPackedValueProperty() == guard,
                  std::string(name) + " partial rectangle is row-major and guard-safe");

            std::vector<Packed> whole(6, MakePacked<Packed>(Word{0}));
            texture.GetData(whole.data(), 6);
            texture.GetData(1, nullptr, generated.data(), 0, 1);
            Check(SameWords<Packed>(whole, expected)
                      && generated[0].getPackedValueProperty() == average(expected)
                      && renderer && renderer->MipGenerationCountEXT(1) == 2u,
                  std::string(name) + " partial parent update preserves neighbours and rebuilds mip");

            const std::vector<Packed> beforeFailure = whole;
            Check(Throws([&] { texture.SetData(0, nullptr, upload.data(), 0, 5); }),
                  std::string(name) + " rejects an undersized full upload");
            texture.GetData(whole.data(), 6);
            Check(whole == beforeFailure,
                  std::string(name) + " rejected upload leaves texture bytes unchanged");
        }
        Check(SameTextureStats(graphicsRenderer.GetResourceStatsEXT(), before),
              std::string(name) + " destruction releases exact resource accounting");
    }

    void CheckTypedRefusalIsAtomic(GraphicsDevice& device)
    {
        InspectablePackedTexture2D texture(device, 1, 1, false, SurfaceFormat::Bgr565);
        const Bgr565 red = MakePacked<Bgr565>(static_cast<std::uint16_t>(0xF800u));
        texture.SetData(&red, 1);
        const Bgra4444 wrong = MakePacked<Bgra4444>(static_cast<std::uint16_t>(0xF0F0u));
        Check(Throws([&] { texture.SetData(&wrong, 1); }),
              "mismatched packed SetData overload rejects before storage mutation");
        Bgr565 actual;
        texture.GetData(&actual, 1);
        Check(actual == red, "mismatched packed SetData leaves the previous Bgr565 word intact");

        Bgra4444 untouched = MakePacked<Bgra4444>(static_cast<std::uint16_t>(0x1234u));
        Check(Throws([&] { texture.GetData(&untouched, 1); })
                  && untouched.getPackedValueProperty() == 0x1234u,
              "mismatched packed GetData rejects without touching caller memory");
        Color colorGuard(1, 2, 3, 4);
        Check(Throws([&] { texture.GetData(&colorGuard, 1); })
                  && colorGuard == Color(1, 2, 3, 4),
              "Color transfer overload cannot reinterpret packed Texture2D storage");
    }

    void CheckRenderTargetRefusal(GraphicsDevice& device, SkiaRenderer& graphicsRenderer)
    {
        const SkiaResourceStats before = graphicsRenderer.GetResourceStatsEXT();
        const std::array formats{SurfaceFormat::Bgr565, SurfaceFormat::Bgra4444};
        bool allRejected = true;
        for (const SurfaceFormat format : formats)
        {
            allRejected = allRejected && Throws([&] {
                RenderTarget2D target(device, 2, 2, false, format, DepthFormat::None);
                (void)target;
            });
        }
        Check(allRejected && SameTextureStats(graphicsRenderer.GetResourceStatsEXT(), before),
              "Bgr565/Bgra4444 RenderTarget2D requests reject transactionally");
    }

    void CheckRenderTargetPromotion(GraphicsDevice& device, SkiaRenderer& graphicsRenderer)
    {
        const SkiaResourceStats before = graphicsRenderer.GetResourceStatsEXT();
        {
            RenderTarget2D target(device, 2, 2, false, SurfaceFormat::Rgba1010102,
                                  DepthFormat::None);
            const SkiaResourceStats allocated = graphicsRenderer.GetResourceStatsEXT();
            Check(allocated.renderTargets == before.renderTargets + 1u
                      && allocated.targetSurfaceBytes == before.targetSurfaceBytes + 16u,
                  "SKIA-142 promotes Rgba1010102 to a constructible RenderTarget2D");
        }
        Check(SameTextureStats(graphicsRenderer.GetResourceStatsEXT(), before),
              "Rgba1010102 RenderTarget2D destruction releases its surface accounting");
    }

    void CheckSampling(GraphicsDevice& device, SkiaRenderer& graphicsRenderer)
    {
        const SkiaResourceStats before = graphicsRenderer.GetResourceStatsEXT();
        {
            Texture2D bgr(device, 1, 1, false, SurfaceFormat::Bgr565);
            Texture2D bgra(device, 1, 1, false, SurfaceFormat::Bgra4444);
            Texture2D rgba(device, 1, 1, false, SurfaceFormat::Rgba1010102);
            Texture2D bgraAlpha(device, 1, 1, false, SurfaceFormat::Bgra4444);
            Texture2D rgbaAlpha(device, 1, 1, false, SurfaceFormat::Rgba1010102);

            const Bgr565 red = MakePacked<Bgr565>(static_cast<std::uint16_t>(0xF800u));
            const Bgra4444 green = MakePacked<Bgra4444>(static_cast<std::uint16_t>(0xF0F0u));
            const Rgba1010102 blue = MakePacked<Rgba1010102>(0xFFF00000u);
            const Bgra4444 quantized4444 =
                MakePacked<Bgra4444>(static_cast<std::uint16_t>(0x8700u));
            const Rgba1010102 quantized1010102 = MakePacked<Rgba1010102>(0xA0000000u);
            bgr.SetData(&red, 1);
            bgra.SetData(&green, 1);
            rgba.SetData(&blue, 1);
            bgraAlpha.SetData(&quantized4444, 1);
            rgbaAlpha.SetData(&quantized1010102, 1);

            device.Clear(Color::Transparent);
            auto* point = const_cast<SamplerState*>(&SamplerState::PointClamp);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                                point, nullptr, nullptr);
            spriteBatch_->Draw(bgr, Rectangle(0, 0, 4, 4), Color::White);
            spriteBatch_->Draw(bgra, Rectangle(4, 0, 4, 4), Color::White);
            spriteBatch_->Draw(rgba, Rectangle(8, 0, 4, 4), Color::White);
            spriteBatch_->Draw(bgraAlpha, Rectangle(12, 0, 4, 4), Color::White);
            spriteBatch_->Draw(rgbaAlpha, Rectangle(16, 0, 4, 4), Color::White);
            spriteBatch_->End();

            const auto sample = [&](int x) {
                const Rectangle region(x, 2, 1, 1);
                Color result = Color::Transparent;
                device.GetBackBufferData(&region, &result, 0, 1);
                return result;
            };
            const Color redPixel = sample(2);
            const Color greenPixel = sample(6);
            const Color bluePixel = sample(10);
            const Color bgraAlphaPixel = sample(14);
            const Color rgbaAlphaPixel = sample(18);
            std::printf("[INFO] packed alpha samples: Bgra4444=(%d,%d,%d,%d) "
                        "Rgba1010102=(%d,%d,%d,%d)\n",
                        bgraAlphaPixel.getRProperty(), bgraAlphaPixel.getGProperty(),
                        bgraAlphaPixel.getBProperty(), bgraAlphaPixel.getAProperty(),
                        rgbaAlphaPixel.getRProperty(), rgbaAlphaPixel.getGProperty(),
                        rgbaAlphaPixel.getBProperty(), rgbaAlphaPixel.getAProperty());
            Check(redPixel == Color(255, 0, 0, 255)
                      && greenPixel == Color(0, 255, 0, 255)
                      && bluePixel == Color(0, 0, 255, 255),
                  "Bgr565/Bgra4444/Rgba1010102 sample with exact channel order");
            Check(NearChannel(bgraAlphaPixel.getRProperty(), 223)
                      && NearChannel(bgraAlphaPixel.getGProperty(), 0)
                      && NearChannel(bgraAlphaPixel.getBProperty(), 0)
                      && NearChannel(bgraAlphaPixel.getAProperty(), 136),
                  "Bgra4444 expands four-bit premultiplied colour/alpha through Opaque exactly");
            Check(NearChannel(rgbaAlphaPixel.getRProperty(), 0)
                      && NearChannel(rgbaAlphaPixel.getGProperty(), 0)
                      && NearChannel(rgbaAlphaPixel.getBProperty(), 192)
                      && NearChannel(rgbaAlphaPixel.getAProperty(), 170),
                  "Rgba1010102 expands premultiplied ten/two-bit channels through Opaque exactly");
        }
        Check(SameTextureStats(graphicsRenderer.GetResourceStatsEXT(), before),
              "sampled packed textures release native views and shadows");
    }

    [[nodiscard]] static bool SameTextureStats(const SkiaResourceStats& left,
                                               const SkiaResourceStats& right)
    {
        return left.textureRenderers == right.textureRenderers
            && left.textureImageViews == right.textureImageViews
            && left.textureImageBytes == right.textureImageBytes
            && left.mipChains2D == right.mipChains2D
            && left.mipChain2DStorageBytes == right.mipChain2DStorageBytes
            && left.renderTargets == right.renderTargets
            && left.targetSurfaceBytes == right.targetSurfaceBytes;
    }

    void Check(bool pass, const std::string& label)
    {
        ++checks_;
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label.c_str());
        if (!pass)
            ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaTexture2DPackedFormatsTest game;
    game.Run();
    return game.Result();
}
