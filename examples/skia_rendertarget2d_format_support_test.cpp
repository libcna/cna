// SPDX-License-Identifier: MS-PL
// SKIA-142: construction, exact transfer round-trip, mip generation, active-canvas readback,
// sampling and resource accounting for the thirteen non-Color FNA-renderable RenderTarget2D
// formats (Color itself is already covered by the pre-existing Skia_RenderTarget2D_* suite).

#include "CNA/Internal/Backends/Skia/SkiaGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Skia/SkiaRenderTargetBackend.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfSingle.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rg32.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba1010102.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/Rgba64.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <memory>
#include <string>

using CNA::Internal::Backends::Skia::SkiaGraphicsBackend;
using CNA::Internal::Backends::Skia::SkiaRenderTargetBackend;
using CNA::Internal::Backends::Skia::SkiaResourceStats;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace Microsoft::Xna::Framework::Graphics::PackedVector;

namespace
{
    template<typename Callable>
    [[nodiscard]] bool Throws(Callable&& callable)
    {
        try { callable(); }
        catch (const std::exception&) { return true; }
        return false;
    }

    [[nodiscard]] bool SameStats(const SkiaResourceStats& left,
                                 const SkiaResourceStats& right) noexcept
    {
        return left.renderTargets == right.renderTargets
            && left.targetSurfaceBytes == right.targetSurfaceBytes
            && left.mipChains2D == right.mipChains2D
            && left.mipChain2DStorageBytes == right.mipChain2DStorageBytes
            && left.targetSnapshots == right.targetSnapshots
            && left.targetSnapshotBytes == right.targetSnapshotBytes;
    }

    [[nodiscard]] SkiaRenderTargetBackend* SkiaBackend(RenderTarget2D& target)
    {
        return dynamic_cast<SkiaRenderTargetBackend*>(target.GetRenderTargetBackend());
    }

    template<typename Packed>
    [[nodiscard]] bool SamePackedValue(const Packed& a, const Packed& b) noexcept
    {
        return a.getPackedValueProperty() == b.getPackedValueProperty();
    }

    [[nodiscard]] bool SameFloat(const float& a, const float& b) noexcept
    {
        return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
    }

    [[nodiscard]] bool SameVector2(const Vector2& a, const Vector2& b) noexcept
    {
        return SameFloat(a.X, b.X) && SameFloat(a.Y, b.Y);
    }

    [[nodiscard]] bool SameVector4(const Vector4& a, const Vector4& b) noexcept
    {
        return SameFloat(a.X, b.X) && SameFloat(a.Y, b.Y)
            && SameFloat(a.Z, b.Z) && SameFloat(a.W, b.W);
    }

    [[nodiscard]] bool SameColor(const Color& a, const Color& b) noexcept
    {
        return a.getRProperty() == b.getRProperty() && a.getGProperty() == b.getGProperty()
            && a.getBProperty() == b.getBProperty() && a.getAProperty() == b.getAProperty();
    }

    [[nodiscard]] bool SameByte(const std::uint8_t& a, const std::uint8_t& b) noexcept
    {
        return a == b;
    }

    [[nodiscard]] bool SameUShort(const std::uint16_t& a, const std::uint16_t& b) noexcept
    {
        return a == b;
    }
}

class SkiaRenderTarget2DFormatSupportTest final : public Game
{
public:
    SkiaRenderTarget2DFormatSupportTest()
    {
        graphics_ = std::make_unique<GraphicsDeviceManager>(this);
        graphics_->setPreferredBackBufferWidthProperty(12);
        graphics_->setPreferredBackBufferHeightProperty(6);
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
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        auto* graphicsBackend = dynamic_cast<SkiaGraphicsBackend*>(&device.GetBackend());
        Check(graphicsBackend != nullptr, "public GraphicsDevice owns the Skia backend");
        if (!graphicsBackend)
        {
            Exit();
            return;
        }

        const SkiaResourceStats baseline = graphicsBackend->GetResourceStatsEXT();

        CheckConstructionAndTransfer<Rgba1010102>(device, *graphicsBackend,
            SurfaceFormat::Rgba1010102, "Rgba1010102", 4u,
            {Rgba1010102(1, 0, 0, 1), Rgba1010102(0, 1, 0, 0), Rgba1010102(0, 0, 1, 1),
             Rgba1010102(1, 1, 1, 0)},
            SamePackedValue<Rgba1010102>);
        CheckMipGeneration<Rgba1010102>(device, SurfaceFormat::Rgba1010102, "Rgba1010102",
            Rgba1010102(0.5f, 0.25f, 0.75f, 1.0f), SamePackedValue<Rgba1010102>);

        CheckConstructionAndTransfer<Rg32>(device, *graphicsBackend, SurfaceFormat::Rg32, "Rg32",
            4u, {Rg32(1, 0), Rg32(0, 1), Rg32(0.5f, 0.5f), Rg32(1, 1)}, SamePackedValue<Rg32>);
        CheckMipGeneration<Rg32>(device, SurfaceFormat::Rg32, "Rg32", Rg32(0.3f, 0.7f),
            SamePackedValue<Rg32>);

        CheckConstructionAndTransfer<Rgba64>(device, *graphicsBackend, SurfaceFormat::Rgba64,
            "Rgba64", 8u,
            {Rgba64(1, 0, 0, 1), Rgba64(0, 1, 0, 0), Rgba64(0, 0, 1, 1), Rgba64(1, 1, 1, 0)},
            SamePackedValue<Rgba64>);
        CheckMipGeneration<Rgba64>(device, SurfaceFormat::Rgba64, "Rgba64",
            Rgba64(0.5f, 0.25f, 0.75f, 1.0f), SamePackedValue<Rgba64>);

        CheckConstructionAndTransfer<float>(device, *graphicsBackend, SurfaceFormat::Single,
            "Single", 16u, {0.1f, 0.2f, 0.3f, 0.4f}, SameFloat);
        CheckMipGeneration<float>(device, SurfaceFormat::Single, "Single", 0.5f, SameFloat);

        CheckConstructionAndTransfer<Vector2>(device, *graphicsBackend, SurfaceFormat::Vector2,
            "Vector2", 16u,
            {Vector2(0.1f, 0.9f), Vector2(0.2f, 0.8f), Vector2(0.3f, 0.7f), Vector2(0.4f, 0.6f)},
            SameVector2);
        CheckMipGeneration<Vector2>(device, SurfaceFormat::Vector2, "Vector2",
            Vector2(0.25f, 0.75f), SameVector2);

        CheckConstructionAndTransfer<Vector4>(device, *graphicsBackend, SurfaceFormat::Vector4,
            "Vector4", 16u,
            {Vector4(0.1f, 0.2f, 0.3f, 0.4f), Vector4(0.4f, 0.3f, 0.2f, 0.1f),
             Vector4(1.0f, 0.0f, 1.0f, 0.0f), Vector4(0.0f, 1.0f, 0.0f, 1.0f)},
            SameVector4);
        CheckMipGeneration<Vector4>(device, SurfaceFormat::Vector4, "Vector4",
            Vector4(0.5f, 0.5f, 0.5f, 0.5f), SameVector4);

        CheckConstructionAndTransfer<HalfSingle>(device, *graphicsBackend,
            SurfaceFormat::HalfSingle, "HalfSingle", 2u,
            {HalfSingle(0.1f), HalfSingle(0.2f), HalfSingle(0.3f), HalfSingle(0.4f)},
            SamePackedValue<HalfSingle>);
        CheckMipGeneration<HalfSingle>(device, SurfaceFormat::HalfSingle, "HalfSingle",
            HalfSingle(0.5f), SamePackedValue<HalfSingle>);

        CheckConstructionAndTransfer<HalfVector2>(device, *graphicsBackend,
            SurfaceFormat::HalfVector2, "HalfVector2", 4u,
            {HalfVector2(0.1f, 0.9f), HalfVector2(0.2f, 0.8f), HalfVector2(0.3f, 0.7f),
             HalfVector2(0.4f, 0.6f)},
            SamePackedValue<HalfVector2>);
        CheckMipGeneration<HalfVector2>(device, SurfaceFormat::HalfVector2, "HalfVector2",
            HalfVector2(0.25f, 0.75f), SamePackedValue<HalfVector2>);

        CheckConstructionAndTransfer<HalfVector4>(device, *graphicsBackend,
            SurfaceFormat::HalfVector4, "HalfVector4", 8u,
            {HalfVector4(0.1f, 0.2f, 0.3f, 0.4f), HalfVector4(0.4f, 0.3f, 0.2f, 0.1f),
             HalfVector4(1.0f, 0.0f, 1.0f, 0.0f), HalfVector4(0.0f, 1.0f, 0.0f, 1.0f)},
            SamePackedValue<HalfVector4>);
        CheckMipGeneration<HalfVector4>(device, SurfaceFormat::HalfVector4, "HalfVector4",
            HalfVector4(0.5f, 0.5f, 0.5f, 0.5f), SamePackedValue<HalfVector4>);

        CheckConstructionAndTransfer<HalfVector4>(device, *graphicsBackend,
            SurfaceFormat::HdrBlendable, "HdrBlendable", 8u,
            {HalfVector4(2.0f, -1.0f, 0.5f, 1.0f), HalfVector4(-2.0f, 1.0f, 3.0f, 0.5f),
             HalfVector4(0.0f, 0.0f, 0.0f, 0.0f), HalfVector4(1.0f, 1.0f, 1.0f, 1.0f)},
            SamePackedValue<HalfVector4>);
        CheckMipGeneration<HalfVector4>(device, SurfaceFormat::HdrBlendable, "HdrBlendable",
            HalfVector4(2.5f, -0.5f, 0.25f, 1.0f), SamePackedValue<HalfVector4>);

        CheckConstructionAndTransfer<Color>(device, *graphicsBackend, SurfaceFormat::ColorSrgbEXT,
            "ColorSrgbEXT", 4u,
            {Color(255, 0, 0, 255), Color(0, 255, 0, 128), Color(0, 0, 255, 64),
             Color(10, 20, 30, 255)},
            SameColor);
        CheckMipGeneration<Color>(device, SurfaceFormat::ColorSrgbEXT, "ColorSrgbEXT",
            Color(120, 130, 140, 255), SameColor);

        CheckConstructionAndTransfer<std::uint8_t>(device, *graphicsBackend,
            SurfaceFormat::ByteEXT, "ByteEXT", 1u, {10u, 20u, 30u, 40u}, SameByte);
        CheckMipGeneration<std::uint8_t>(device, SurfaceFormat::ByteEXT, "ByteEXT", 99u, SameByte);

        CheckConstructionAndTransfer<std::uint16_t>(device, *graphicsBackend,
            SurfaceFormat::UShortEXT, "UShortEXT", 2u, {100u, 20000u, 40000u, 60000u},
            SameUShort);
        CheckMipGeneration<std::uint16_t>(device, SurfaceFormat::UShortEXT, "UShortEXT", 5000u,
            SameUShort);

        CheckActiveCanvasReadback(device);
        CheckSampling(device);
        CheckRemainingRefusals(device, *graphicsBackend);

        Check(SameStats(graphicsBackend->GetResourceStatsEXT(), baseline),
              "all thirteen promoted formats return Skia resource counters to baseline");

        std::printf("=== %d/%d PASS ===\n", checks_ - failures_, checks_);
        Exit();
    }

private:
    template<typename Element>
    void CheckConstructionAndTransfer(
        GraphicsDevice& device, SkiaGraphicsBackend& graphicsBackend, SurfaceFormat format,
        const char* name, std::size_t nativeBytesPerPixel, const std::array<Element, 4>& source,
        bool (*same)(const Element&, const Element&))
    {
        const SkiaResourceStats before = graphicsBackend.GetResourceStatsEXT();
        {
            RenderTarget2D target(device, 2, 2, false, format, DepthFormat::None);
            SkiaRenderTargetBackend* backend = SkiaBackend(target);
            const SkiaResourceStats allocated = graphicsBackend.GetResourceStatsEXT();
            Check(backend != nullptr && backend->FormatEXT() == format
                      && allocated.renderTargets == before.renderTargets + 1u
                      && allocated.targetSurfaceBytes
                          == before.targetSurfaceBytes + 4u * nativeBytesPerPixel,
                  std::string(name)
                      + " RenderTarget2D constructs with exact native surface accounting");

            target.SetData(source.data(), static_cast<int>(source.size()));
            std::array<Element, 4> readback{source[0], source[0], source[0], source[0]};
            target.GetData(readback.data(), 0, 4);
            bool exact = true;
            for (std::size_t index = 0; index < source.size(); ++index)
                exact = exact && same(readback[index], source[index]);
            Check(exact, std::string(name) + " level-0 SetData/GetData round-trips exactly");

            const Rectangle patchRect(1, 0, 1, 1);
            target.SetData(0, &patchRect, &source[0], 0, 1);
            std::array<Element, 4> patched{source[0], source[0], source[0], source[0]};
            target.GetData(patched.data(), 0, 4);
            Check(same(patched[1], source[0]) && same(patched[3], source[3]),
                  std::string(name) + " partial-rectangle SetData replaces only the addressed texel");
        }
        Check(SameStats(graphicsBackend.GetResourceStatsEXT(), before),
              std::string(name) + " RenderTarget2D destruction releases native surface accounting");
    }

    template<typename Element>
    void CheckMipGeneration(GraphicsDevice& device, SurfaceFormat format, const char* name,
                            const Element& uniformValue, bool (*same)(const Element&, const Element&))
    {
        RenderTarget2D target(device, 2, 2, true, format, DepthFormat::None);
        const std::array<Element, 4> uniform{
            uniformValue, uniformValue, uniformValue, uniformValue};
        target.SetData(uniform.data(), 4);
        Element mipRead(uniformValue);
        target.GetData(1, nullptr, &mipRead, 0, 1);
        Check(target.getLevelCountProperty() == 2 && same(mipRead, uniformValue),
              std::string(name) + " uniform level-0 SetData generates an identical level-1 mip");
    }

    void CheckActiveCanvasReadback(GraphicsDevice& device)
    {
        const Rectangle firstPixel(0, 0, 1, 1);
        {
            RenderTarget2D target(device, 2, 2, false, SurfaceFormat::ByteEXT, DepthFormat::None);
            device.SetRenderTarget(&target);
            device.Clear(Color(200, 10, 10, 255));
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            std::uint8_t value = 0;
            target.GetData(0, &firstPixel, &value, 0, 1);
            Check(value == 200, "ByteEXT RenderTarget2D GetData resyncs a Skia canvas Clear "
                                "through the active-target surface");
        }
        {
            RenderTarget2D target(device, 2, 2, false, SurfaceFormat::Single, DepthFormat::None);
            device.SetRenderTarget(&target);
            device.Clear(Color(128, 0, 0, 255));
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            float value = -1.0f;
            target.GetData(0, &firstPixel, &value, 0, 1);
            Check(std::abs(value - 128.0f / 255.0f) < 0.01f,
                  "Single RenderTarget2D GetData resyncs a Clear through the extract-subset "
                  "native F32 surface");
        }
        {
            RenderTarget2D target(device, 2, 2, false, SurfaceFormat::Vector2, DepthFormat::None);
            device.SetRenderTarget(&target);
            device.Clear(Color(64, 192, 0, 255));
            device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
            Vector2 value(-1.0f, -1.0f);
            target.GetData(0, &firstPixel, &value, 0, 1);
            Check(std::abs(value.X - 64.0f / 255.0f) < 0.01f
                      && std::abs(value.Y - 192.0f / 255.0f) < 0.01f,
                  "Vector2 RenderTarget2D GetData resyncs a Clear through the extract-subset "
                  "native F32 surface");
        }
    }

    void CheckSampling(GraphicsDevice& device)
    {
        RenderTarget2D rg32(device, 1, 1, false, SurfaceFormat::Rg32, DepthFormat::None);
        const Rg32 rgValue(1.0f, 0.5f);
        rg32.SetData(&rgValue, 1);

        RenderTarget2D vector4(device, 1, 1, false, SurfaceFormat::Vector4, DepthFormat::None);
        const Vector4 v4Value(1.0f, 0.5f, 0.25f, 1.0f);
        vector4.SetData(&v4Value, 1);

        device.Clear(Color::Transparent);
        auto* point = const_cast<SamplerState*>(&SamplerState::PointClamp);
        spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque, point, nullptr, nullptr);
        spriteBatch_->Draw(rg32, Rectangle(0, 0, 4, 4), Color::White);
        spriteBatch_->Draw(vector4, Rectangle(4, 0, 4, 4), Color::White);
        spriteBatch_->End();

        const auto sample = [&](int x) {
            const Rectangle region(x, 2, 1, 1);
            Color color = Color::Transparent;
            device.GetBackBufferData(&region, &color, 0, 1);
            return color;
        };
        const auto near = [](std::uint8_t actual, int expected) {
            return std::abs(static_cast<int>(actual) - expected) <= 1;
        };
        const Color rgPixel = sample(2);
        const Color v4Pixel = sample(6);
        Check(rgPixel.getRProperty() == 255 && near(rgPixel.getGProperty(), 128)
                  && rgPixel.getBProperty() == 0 && rgPixel.getAProperty() == 255,
              "Rg32 RenderTarget2D samples through public SpriteBatch drawing");
        Check(v4Pixel.getRProperty() == 255 && near(v4Pixel.getGProperty(), 128)
                  && near(v4Pixel.getBProperty(), 64) && v4Pixel.getAProperty() == 255,
              "Vector4 RenderTarget2D samples through public SpriteBatch drawing");
    }

    void CheckRemainingRefusals(GraphicsDevice& device, SkiaGraphicsBackend& graphicsBackend)
    {
        const SkiaResourceStats before = graphicsBackend.GetResourceStatsEXT();
        const std::array<SurfaceFormat, 12> refused{
            SurfaceFormat::Dxt1, SurfaceFormat::Dxt3, SurfaceFormat::Dxt5,
            SurfaceFormat::Bc7EXT, SurfaceFormat::Bc7SrgbEXT,
            SurfaceFormat::Bgr565, SurfaceFormat::Bgra4444, SurfaceFormat::Bgra5551,
            SurfaceFormat::NormalizedByte2, SurfaceFormat::NormalizedByte4,
            SurfaceFormat::Alpha8, SurfaceFormat::ColorBgraEXT,
        };
        bool allRejected = true;
        for (const SurfaceFormat format : refused)
        {
            allRejected = allRejected && Throws([&] {
                RenderTarget2D target(device, 4, 4, false, format, DepthFormat::None);
                (void)target;
            });
        }
        Check(allRejected && SameStats(graphicsBackend.GetResourceStatsEXT(), before),
              "every remaining non-FNA-renderable SurfaceFormat still rejects "
              "RenderTarget2D construction");
    }

    void Check(bool pass, const std::string& label)
    {
        ++checks_;
        std::printf("[%s] %s\n", pass ? "PASS" : "FAIL", label.c_str());
        if (!pass) ++failures_;
    }

    std::unique_ptr<GraphicsDeviceManager> graphics_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    bool done_ = false;
    int checks_ = 0;
    int failures_ = 0;
};

int main()
{
    SkiaRenderTarget2DFormatSupportTest game;
    game.Run();
    return game.Result();
}
