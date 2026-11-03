// SPDX-License-Identifier: MS-PL
// MTH-CONF-005: GetData observes completed draws, including through a Texture2D reference.
#include "common/PixelTestGame.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererCategory.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfVector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include <array>
#include <cmath>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 16;
    const std::array<Color, 4> kColors{
        Color(204, 51, 102, 153), Color(51, 204, 153, 102),
        Color(102, 153, 204, 51), Color(153, 102, 51, 204)};
    const Color kReplacement(17, 39, 71, 123);

    class ReadbackContract final : public CNA::Examples::PixelTestGame
    {
        std::unique_ptr<GraphicsDeviceManager> manager_;
        int skipped_ = 0;

        void Skip(const std::string& label, const char* reason)
        {
            ++skipped_;
            std::printf("[SKIP] %s: %s\n", label.c_str(), reason);
        }

        void Draw(SpriteBatch& batch, Texture2D& white, Rectangle rect, Color color)
        {
            batch.Begin(SpriteSortMode::Immediate, BlendState::Opaque, &SamplerState::PointClamp,
                        &DepthStencilState::None, &RasterizerState::CullNone);
            batch.Draw(white, rect, color);
            batch.End();
        }

        template<class T>
        bool Matches(const T& got, Color expected)
        {
            if constexpr (std::is_same_v<T, Color>)
                return got == expected; // Flat RGBA8 interiors, no blending: exact bytes.
            else
            {
                const auto a = got.ToVector4();
                const auto b = expected.ToVector4();
                // Binary16 storage rounds normalized byte values by <= 0.000245;
                // 0.001 permits storage/interpolation rounding, not an 8-bit channel error.
                return std::abs(a.X-b.X) <= 0.001f && std::abs(a.Y-b.Y) <= 0.001f &&
                       std::abs(a.Z-b.Z) <= 0.001f && std::abs(a.W-b.W) <= 0.001f;
            }
        }

        template<class T>
        void ReadAll(RenderTarget2D& target, const std::string& label, bool replaced, bool discard)
        {
            Texture2D& texture = target;
            for (int route = 0; route < 4; ++route)
            {
                std::vector<T> data(kSize*kSize + 2);
                // Sentinels catch startIndex/count over-writes on every route that accepts them.
                const T sentinel = [] {
                    if constexpr (std::is_same_v<T, Color>) return Color(7, 11, 13, 19);
                    else return T(0.125f, 0.25f, 0.5f, 0.75f);
                }();
                data.front() = data.back() = sentinel;
                if (route == 0) target.GetData(data.data()+1, kSize*kSize);
                if (route == 1) texture.GetData(data.data(), 1, kSize*kSize);
                if (route == 2) target.GetData(0, nullptr, data.data(), 1, kSize*kSize);
                if (route == 3) texture.GetData(0, nullptr, data.data(), 1, kSize*kSize);
                bool ok = true;
                for (int y = 0; y < kSize; ++y)
                    for (int x = 0; x < kSize; ++x)
                    {
                        const Color expected = replaced && y >= kSize/2 ? kReplacement :
                            discard ? Color::Transparent : kColors[(y/8)*2+x/8];
                        ok &= Matches(data[1+y*kSize+x], expected);
                    }
                ExpectTrue((label + " full read route " + std::to_string(route) + " RGBA").c_str(), ok);
                ExpectTrue((label + " output bounds").c_str(), data.front() == sentinel && data.back() == sentinel);
            }
            // A non-square, off-origin rectangle crossing both colour boundaries detects stride,
            // vertical orientation and accidental whole-resource readback.
            const Rectangle rect(6, 5, 5, 7);
            std::array<T, 37> data{};
            const T sentinel = data.front();
            texture.GetData(0, &rect, data.data(), 1, 35);
            bool ok = data.front() == sentinel && data.back() == sentinel;
            for (int y = 0; y < rect.Height; ++y)
                for (int x = 0; x < rect.Width; ++x)
                {
                    const int px = x+rect.X, py = y+rect.Y;
                    const Color expected = replaced && py >= 8 ? kReplacement :
                        discard ? Color::Transparent : kColors[(py/8)*2+px/8];
                    ok &= Matches(data[1+y*rect.Width+x], expected);
                }
            ExpectTrue((label + " rectangle/level/startIndex RGBA").c_str(), ok);
        }

        template<class T>
        void Leg(SurfaceFormat format, int samples, RenderTargetUsage usage)
        {
            auto& dev = getGraphicsDeviceProperty();
            const std::string label = std::string(format == SurfaceFormat::Color ? "Color" : "HalfVector4") +
                "/" + std::to_string(samples) + (usage == RenderTargetUsage::PreserveContents ? "/preserve" : "/discard");
            if (format == SurfaceFormat::HalfVector4 && !dev.SupportsCapability(CNA::GraphicsCapability::HalfFloatRenderTargets))
            { Skip(label, "GraphicsCapability::HalfFloatRenderTargets=false"); return; }
            if (samples && !dev.SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing))
            { Skip(label, "GraphicsCapability::MultiSampleAntiAliasing=false"); return; }
            RenderTarget2D target(dev, kSize, kSize, samples == 0, format, DepthFormat::None, samples, usage);
            std::printf("[CASE] %s: applied format=%d samples=%d levels=%d\n", label.c_str(),
                static_cast<int>(target.getFormatProperty()), target.getMultiSampleCountProperty(), target.getLevelCountProperty());
            if (target.getFormatProperty() != format)
            { Skip(label, "requested format substituted; requested-format readback not covered"); return; }
            if (samples && target.getMultiSampleCountProperty() != samples)
            { Skip(label, "requested 4x target not supported for this format (applied count printed above)"); return; }
            RenderTarget2D other(dev, kSize, kSize);
            Texture2D white(dev, 1, 1);
            const Color texel = Color::White;
            white.SetData(&texel, 1);
            SpriteBatch batch(dev);
            dev.SetRenderTarget(&target);
            dev.Clear(Color::Transparent);
            for (int i = 0; i < 4; ++i)
                Draw(batch, white, Rectangle((i%2)*8, (i/2)*8, 8, 8), kColors[i]);
            dev.SetRenderTarget(nullptr); // Resolve before GetData; never read a bound target.
            ReadAll<T>(target, label + " after draw/resolve", false, false);
            if (samples == 0)
            {
                if (ExpectTrue("mipmapped target exposes level 1", target.getLevelCountProperty() > 1))
                {
                    // (1,1) in level 1 is wholly inside the constant top-left quadrant.
                    const Rectangle rect(1, 1, 2, 2);
                    std::array<T, 4> mip{};
                    static_cast<Texture2D&>(target).GetData(1, &rect, mip.data(), 0, 4);
                    bool ok = true;
                    for (const auto& value : mip) ok &= Matches(value, kColors[0]);
                    ExpectTrue((label + " nonzero mip level RGBA").c_str(), ok);
                }
            }
            dev.SetRenderTarget(&other);
            dev.Clear(Color::Yellow);
            dev.SetRenderTarget(&target);
            const bool discard = usage == RenderTargetUsage::DiscardContents;
            // DiscardContents makes old pixels undefined on rebind. Initialize explicitly;
            // do not assert FNA's diagnostic discard colour or that old contents disappear.
            if (discard) dev.Clear(Color::Transparent);
            Draw(batch, white, Rectangle(0, 8, 16, 8), kReplacement);
            dev.SetRenderTarget(nullptr);
            ReadAll<T>(target, label + " after target switch", true, discard);
            dev.getTexturesProperty()(0, nullptr);
        }

    public:
        ReadbackContract()
        {
            manager_ = std::make_unique<GraphicsDeviceManager>(this);
            manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
            manager_->setPreferredBackBufferWidthProperty(kSize);
            manager_->setPreferredBackBufferHeightProperty(kSize);
        }
        void RunTest() override
        {
            std::printf("[RENDERER] %s\n", std::string(getGraphicsDeviceProperty().GetGraphicsRendererName()).c_str());
            for (const int samples : {0, 4})
                for (const auto usage : {RenderTargetUsage::PreserveContents, RenderTargetUsage::DiscardContents})
                {
                    Leg<Color>(SurfaceFormat::Color, samples, usage);
                    Leg<PackedVector::HalfVector4>(SurfaceFormat::HalfVector4, samples, usage);
                }
            std::printf("[SUMMARY] capability legs skipped=%d result=%s\n", skipped_, getResultProperty() ? "FAIL" : "PASS");
        }
    };
}
int main()
{
    const auto renderer = CNA::GraphicsRendererSelection::GetSelected();
    if (CNA::getGraphicsRendererCategory(renderer) == CNA::GraphicsRendererCategory::Diagnostic)
    {
        std::printf("[SKIP] render_target_readback: %s makes no pixel claim\n", std::string(CNA::getGraphicsRendererName(renderer)).c_str());
        return CNA::Examples::kSkipExitCode;
    }
    return CNA::Examples::RunPixelTest<ReadbackContract>();
}
