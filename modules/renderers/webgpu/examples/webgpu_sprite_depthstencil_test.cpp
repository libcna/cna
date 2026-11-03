// SPDX-License-Identifier: MS-PL
// MTH-TEST-007: stock sprites must capture depth/stencil at submission, split runs when it
// changes, and select pipelines compatible with the actual depth attachment. Flat, opaque
// interior samples use exact RGBA8: no filtering, blend rounding or MSAA edge is involved.
#include "common/PixelTestGame.hpp"
#include "CNA/GraphicsRendererSelection.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <memory>
#include <string>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    class SpriteDepthStencilTest final : public CNA::Examples::PixelTestGame
    {
        std::unique_ptr<GraphicsDeviceManager> manager_;

        void DrawSprite(SpriteBatch& batch, Texture2D& white, Rectangle rect, Color color,
                        float z, const DepthStencilState& depth, SpriteSortMode mode)
        {
            SamplerState point = SamplerState::PointClamp;
            RasterizerState raster = RasterizerState::CullNone;
            batch.Begin(mode, BlendState::Opaque, &point, &depth, &raster);
            batch.Draw(white, rect, Rectangle(0, 0, 1, 1), color, 0.0f, Vector2(0, 0),
                       SpriteEffects::None, z);
            batch.End();
        }

        void Pixel(RenderTarget2D& target, int x, int y, Color expected, const char* label)
        {
            const Rectangle region(x, y, 1, 1);
            Color actual(17, 19, 23, 29);
            target.GetData(0, &region, &actual, 0, 1);
            std::printf("[PIXEL] %s got=(%d,%d,%d,%d) expected=(%d,%d,%d,%d)\n", label,
                actual.getRProperty(), actual.getGProperty(), actual.getBProperty(), actual.getAProperty(),
                expected.getRProperty(), expected.getGProperty(), expected.getBProperty(), expected.getAProperty());
            ExpectTrue(label, actual == expected);
        }

        void DepthCases(Texture2D& white, SpriteBatch& batch, DepthFormat format, SpriteSortMode mode)
        {
            auto& dev = getGraphicsDeviceProperty();
            RenderTarget2D target(dev, 64, 64, false, SurfaceFormat::Color, format);
            dev.SetRenderTarget(&target);
            dev.Clear(Color::Black);
            std::printf("[CASE] depth format=%d mode=%d\n", static_cast<int>(format), static_cast<int>(mode));
            const auto draw = [&](int column, int row, Color color, float z, const DepthStencilState& state) {
                DrawSprite(batch, white, Rectangle(column * 16, row * 16, 16, 16), color, z, state, mode);
            };
            // Default -> None -> Default with the same blend, sampler and texture. In particular,
            // the run merger cannot combine the None draw with either neighboring Default draw.
            draw(0, 0, Color::Red, 0.25f, DepthStencilState::Default);
            draw(0, 0, Color::Lime, 0.75f, DepthStencilState::Default);
            draw(1, 0, Color::Red, 0.25f, DepthStencilState::Default);
            draw(1, 0, Color::Lime, 0.75f, DepthStencilState::None);
            draw(2, 0, Color::Red, 0.25f, DepthStencilState::Default);
            draw(2, 0, Color::Lime, 0.75f, DepthStencilState::Default);

            draw(3, 0, Color::Red, 0.5f, DepthStencilState::Default);
            draw(3, 0, Color::Blue, 0.125f, DepthStencilState::DepthRead);
            draw(3, 0, Color::Lime, 0.25f, DepthStencilState::Default);

            DepthStencilState greater;
            greater.setDepthBufferFunctionProperty(CompareFunction::Greater);
            draw(0, 1, Color::Red, 0.5f, DepthStencilState::Default);
            draw(0, 1, Color::Blue, 0.75f, greater);
            draw(0, 1, Color::Lime, 0.875f, DepthStencilState::Default);

            DepthStencilState disabled;
            disabled.setDepthBufferEnableProperty(false);
            disabled.setDepthBufferWriteEnableProperty(true);
            draw(1, 1, Color::Red, 0.5f, DepthStencilState::Default);
            draw(1, 1, Color::Blue, 0.125f, disabled);
            draw(1, 1, Color::Lime, 0.25f, DepthStencilState::Default);

            // Stencil fields must not introduce a stencil aspect on depth-only or None targets.
            DepthStencilState neverStencil = DepthStencilState::None;
            neverStencil.setStencilEnableProperty(true);
            neverStencil.setStencilFunctionProperty(CompareFunction::Never);
            draw(2, 1, Color::White, 0.5f, neverStencil);
            // No subsequent draw: reading live state at replay would disable every prior test.
            dev.setDepthStencilStateProperty(DepthStencilState::None);
            dev.SetRenderTarget(nullptr);
            const bool hasDepth = format != DepthFormat::None;
            Pixel(target, 8, 8, hasDepth ? Color::Red : Color::Lime, "Default rejects far sprite iff depth exists");
            Pixel(target, 24, 8, Color::Lime, "None ignores previously written depth");
            Pixel(target, 40, 8, hasDepth ? Color::Red : Color::Lime, "Default restored after None");
            Pixel(target, 56, 8, Color::Lime, "DepthRead does not write near depth");
            Pixel(target, 8, 24, hasDepth ? Color::Blue : Color::Lime, "Greater comparison then Default restored");
            Pixel(target, 24, 24, Color::Lime, "disabled depth never writes despite write-enable");
            Pixel(target, 40, 24, format == DepthFormat::Depth24Stencil8 ? Color::Black : Color::White,
                  "stencil comparison only applies to a stencil attachment");
        }

        void StencilCases(Texture2D& white, SpriteBatch& batch, SpriteSortMode mode)
        {
            auto& dev = getGraphicsDeviceProperty();
            RenderTarget2D target(dev, 64, 64, false, SurfaceFormat::Color, DepthFormat::Depth24Stencil8);
            dev.SetRenderTarget(&target);
            dev.Clear(Color::Black);
            std::printf("[CASE] stencil mode=%d\n", static_cast<int>(mode));
            DepthStencilState stamp = DepthStencilState::None;
            stamp.setStencilEnableProperty(true);
            stamp.setStencilFunctionProperty(CompareFunction::Always);
            stamp.setStencilPassProperty(StencilOperation::Replace);
            stamp.setReferenceStencilProperty(1);
            DepthStencilState stampTwo = stamp;
            stampTwo.setReferenceStencilProperty(2);
            DrawSprite(batch, white, Rectangle(0, 0, 16, 32), Color::Red, 0.5f, stamp, mode);
            DrawSprite(batch, white, Rectangle(16, 0, 16, 32), Color::Red, 0.5f, stampTwo, mode);

            DepthStencilState gate = DepthStencilState::None;
            gate.setStencilEnableProperty(true);
            gate.setStencilFunctionProperty(CompareFunction::Equal);
            gate.setReferenceStencilProperty(1);
            DepthStencilState gateTwo = gate;
            gateTwo.setReferenceStencilProperty(2);
            DrawSprite(batch, white, Rectangle(0, 0, 48, 16), Color::Lime, 0.5f, gate, mode);
            DrawSprite(batch, white, Rectangle(0, 0, 48, 16), Color::Blue, 0.5f, gateTwo, mode);
            DrawSprite(batch, white, Rectangle(32, 16, 16, 16), Color::Yellow, 0.5f, DepthStencilState::None, mode);
            DrawSprite(batch, white, Rectangle(0, 16, 48, 16), Color::White, 0.5f, gate, mode);

            DepthStencilState maskedStamp = stamp;
            maskedStamp.setReferenceStencilProperty(3);
            maskedStamp.setStencilWriteMaskProperty(1);
            DrawSprite(batch, white, Rectangle(0, 32, 16, 16), Color::Red, 0.5f, maskedStamp, mode);
            DrawSprite(batch, white, Rectangle(0, 32, 16, 16), Color::Lime, 0.5f, gate, mode);
            DepthStencilState maskedGate = gate;
            maskedGate.setReferenceStencilProperty(3);
            maskedGate.setStencilMaskProperty(1);
            DrawSprite(batch, white, Rectangle(16, 16, 16, 16), Color::Lime, 0.5f, maskedGate, mode);
            DrawSprite(batch, white, Rectangle(0, 40, 16, 8), Color::Blue, 0.5f, maskedGate, mode);
            dev.setDepthStencilStateProperty(DepthStencilState::None);
            dev.SetRenderTarget(nullptr);
            Pixel(target, 8, 8, Color::Lime, "reference 1 survives adjacent reference 2 draws");
            Pixel(target, 24, 8, Color::Blue, "reference 2 selects its own stamp");
            Pixel(target, 40, 8, Color::Black, "unstamped pixels rejected");
            Pixel(target, 8, 24, Color::White, "stencil gate restored after disabled draw");
            Pixel(target, 24, 24, Color::Red, "read mask still rejects a different low bit");
            Pixel(target, 40, 24, Color::Yellow, "disabled stencil draws, restored gate rejects");
            Pixel(target, 8, 36, Color::Lime, "write mask stores only the selected stencil bit");
            Pixel(target, 8, 44, Color::Blue, "read mask compares only the selected stencil bit");
        }

    public:
        SpriteDepthStencilTest()
        {
            manager_ = std::make_unique<GraphicsDeviceManager>(this);
            manager_->setPreferredBackBufferWidthProperty(64);
            manager_->setPreferredBackBufferHeightProperty(64);
        }

        void RunTest() override
        {
            auto& dev = getGraphicsDeviceProperty();
            if (!ExpectTrue("selected renderer is WEBGPU", dev.GetGraphicsRendererName() == "WEBGPU")) return;
            Texture2D white(dev, 1, 1);
            const Color texel = Color::White;
            white.SetData(&texel, 1);
            SpriteBatch batch(dev);
            for (const auto mode : {SpriteSortMode::Immediate, SpriteSortMode::Deferred})
            {
                for (const auto format : {DepthFormat::None, DepthFormat::Depth16,
                                         DepthFormat::Depth24, DepthFormat::Depth24Stencil8})
                    DepthCases(white, batch, format, mode);
                StencilCases(white, batch, mode);
            }
            dev.getTexturesProperty()(0, nullptr);
        }
    };
}

int main()
{
    CNA::GraphicsRendererSelection::SetPreferred("WEBGPU");
    return CNA::Examples::RunPixelTest<SpriteDepthStencilTest>();
}
