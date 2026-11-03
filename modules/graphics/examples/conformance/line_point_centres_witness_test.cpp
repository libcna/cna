// SPDX-License-Identifier: MS-PL
// MTH-CONF-005 / L36: current renderer coverage ONLY, never native XNA evidence.
// PointListEXT is a CNA extension. A native D3D9/XNA probe is still needed to settle L35/L36.
#include "common/PixelTestGame.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "CNA/GraphicsRendererCategory.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetUsage.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include <array>
#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 16;
    class CentreWitness final : public CNA::Examples::PixelTestGame
    {
        std::unique_ptr<GraphicsDeviceManager> manager_;

        void Capture(int samples, bool point, float offset)
        {
            auto& dev = getGraphicsDeviceProperty();
            RenderTarget2D target(dev, kSize, kSize, false, SurfaceFormat::Color, DepthFormat::None,
                                  samples, RenderTargetUsage::PreserveContents);
            if (samples && target.getMultiSampleCountProperty() != 4)
            {
                std::printf("[SKIP] centre witness 4x: target applied %d samples\n", target.getMultiSampleCountProperty());
                return;
            }
            dev.SetRenderTarget(&target);
            dev.Clear(Color::Transparent);
            dev.setBlendStateProperty(BlendState::Opaque);
            dev.setDepthStencilStateProperty(DepthStencilState::None);
            dev.setRasterizerStateProperty(RasterizerState::CullNone);
            BasicEffect effect(dev);
            effect.setVertexColorEnabledProperty(true);
            const auto position = [](float x, float y) { return Vector3(x/8.0f-1.0f, 1.0f-y/8.0f, 0.5f); };
            // Pixel-space inputs before any renderer correction; dyadic NDC conversion is exact.
            const VertexPositionColor vertices[] = {
                {position(point ? 8.0f+offset : 3.0f, 8.0f+offset), Color::White},
                {position(13.0f, 8.0f+offset), Color::White}};
            effect.Apply();
            dev.DrawUserPrimitives(point ? PrimitiveType::PointListEXT : PrimitiveType::LineList, vertices, 0, 1);
            dev.SetRenderTarget(nullptr);
            std::array<Color, kSize*kSize> pixels{};
            target.GetData(pixels.data(), static_cast<int>(pixels.size()));
            int lit = 0;
            std::printf("[WITNESS-NOT-XNA] renderer=%s primitive=%s samples=%d offset=%.2f pixels=(x,y:r,g,b,a)\n",
                std::string(dev.GetGraphicsRendererName()).c_str(), point ? "PointListEXT" : "LineList",
                target.getMultiSampleCountProperty(), offset);
            for (int y = 0; y < kSize; ++y)
                for (int x = 0; x < kSize; ++x)
                {
                    const auto& c = pixels[y*kSize+x];
                    if (c == Color::Transparent) continue;
                    ++lit;
                    std::printf(" (%d,%d:%d,%d,%d,%d)", x, y, c.getRProperty(), c.getGProperty(), c.getBProperty(), c.getAProperty());
                }
            std::printf("\n");
            // This is only a functioning draw/readback check. No asserted coverage coordinates,
            // sample pattern or centre correction: those require the missing native oracle.
            ExpectTrue("witness produced at least one visible pixel (not XNA parity)", lit > 0);
        }
    public:
        CentreWitness()
        {
            manager_ = std::make_unique<GraphicsDeviceManager>(this);
            manager_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
            manager_->setPreferredBackBufferWidthProperty(kSize);
            manager_->setPreferredBackBufferHeightProperty(kSize);
        }
        void RunTest() override
        {
            std::printf("[OPEN] L35/L36 native Direct3D 9 measurement unavailable; cross-renderer agreement is not XNA evidence\n");
            for (const int samples : {0, 4})
            {
                if (samples && !getGraphicsDeviceProperty().SupportsCapability(CNA::GraphicsCapability::MultiSampleAntiAliasing))
                {
                    std::printf("[SKIP] centre witness 4x: GraphicsCapability::MultiSampleAntiAliasing=false\n");
                    continue;
                }
                for (const bool point : {false, true})
                    for (const float offset : {0.0f, 0.25f, 0.5f, 0.75f}) Capture(samples, point, offset);
            }
        }
    };
}
int main()
{
    const auto renderer = CNA::GraphicsRendererSelection::GetSelected();
    if (CNA::getGraphicsRendererCategory(renderer) == CNA::GraphicsRendererCategory::Diagnostic)
    {
        std::printf("[SKIP] line_point_centres_witness: %s makes no pixel claim\n", std::string(CNA::getGraphicsRendererName(renderer)).c_str());
        return CNA::Examples::kSkipExitCode;
    }
    return CNA::Examples::RunPixelTest<CentreWitness>();
}
