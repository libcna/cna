// SPDX-License-Identifier: MS-PL
// Every one of XNA's seven SpriteBatch.Draw overloads rasterizes, and honours its own parameters.
//
// SpriteBatchTests.cpp records what each overload hands the renderer, and parity_sprite_geometry
// covers the geometry family in depth; neither proves that each public overload, end to end,
// produces its own pixels. Each overload here draws into its own 16x16 slot from one 2x2 texture
// (red | green over blue | white) under PointClamp, and each check reads a pixel whose colour depends
// on that overload's own arguments -- native size, source rectangle, tint, float and Vector2 scale,
// each SpriteEffects flip, and rotation about the destination rectangle's origin -- so an overload
// that drew nothing, or ignored one of its arguments, reads a different colour.
//
// The three CNAEXT-only convenience overloads (raw float x/y and non-optional source rectangle)
// are not XNA API and are not exercised here.
//
// Runs with PresentationMode::NativeBackBuffer so logical and physical pixels map 1:1.
//
// First written for the retired SDL_RENDERER renderer (Task 666).
//
// Exit code 0 = all PASS, 1 = at least one FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <vector>
#include "ConformanceSupport.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSlot = 16;
    constexpr int kPad = 4;
    constexpr int kSlots = 7;

    const Color kClear(32, 32, 32, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kGreen(0, 255, 0, 255);
    const Color kBlue(0, 0, 255, 255);
    const Color kWhite(255, 255, 255, 255);

    bool colourMatch(const Color& got, const Color& want, int tol = 24)
    {
        return std::abs(got.getRProperty() - want.getRProperty()) <= tol
            && std::abs(got.getGProperty() - want.getGProperty()) <= tol
            && std::abs(got.getBProperty() - want.getBProperty()) <= tol;
    }

    int SlotX(int index) { return kPad + index * (kSlot + kPad); }
    constexpr int kSlotY = kPad;
}

class SpriteBatchDrawOverloadsTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch>           sb_;
    std::unique_ptr<Texture2D>             quad_; // 2x2: red | green / blue | white
    bool done_   = false;
    int  result_ = 0;

    void probe(GraphicsDevice& dev, int x, int y, const Color& want, const char* label)
    {
        Color got(0, 0, 0, 0);
        const Rectangle region(x, y, 1, 1);
        dev.GetBackBufferData(&region, &got, 0, 1);
        const bool ok = colourMatch(got, want);
        std::printf("[%s] %s: got=(%d,%d,%d) want=(%d,%d,%d)\n", ok ? "PASS" : "FAIL", label,
                    got.getRProperty(), got.getGProperty(), got.getBProperty(),
                    want.getRProperty(), want.getGProperty(), want.getBProperty());
        if (!ok) result_ = 1;
    }

protected:
    void Initialize() override
    {
        Game::Initialize();
        auto& dev = getGraphicsDeviceProperty();
        sb_ = std::make_unique<SpriteBatch>(dev);
        quad_ = std::make_unique<Texture2D>(dev, 2, 2);
        const Color texels[4] = {kRed, kGreen, kBlue, kWhite};
        quad_->SetData(texels, 4);
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();
        dev.Clear(kClear);

        sb_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                   const_cast<SamplerState*>(&SamplerState::PointClamp), nullptr, nullptr);

        // 1: Draw(texture, position, color) -- native size, 2x2 pixels.
        sb_->Draw(*quad_, Vector2(static_cast<float>(SlotX(0)), kSlotY), Color::White);

        // 2: Draw(texture, position, sourceRectangle, color) -- the green texel alone, half tint.
        sb_->Draw(*quad_, Vector2(static_cast<float>(SlotX(1)), kSlotY),
                  std::optional<Rectangle>(Rectangle(1, 0, 1, 1)), Color(128, 128, 128, 255));

        // 3: Draw(texture, position, sourceRectangle, color, rotation, origin, float scale, effects,
        //    layerDepth) -- the whole texture at 8x, mirrored left to right.
        sb_->Draw(*quad_, Vector2(static_cast<float>(SlotX(2)), kSlotY), std::nullopt, Color::White,
                  0.0f, Vector2(0.0f, 0.0f), 8.0f, SpriteEffects::FlipHorizontally, 0.0f);

        // 4: Draw(texture, position, sourceRectangle, color, rotation, origin, Vector2 scale,
        //    effects, layerDepth) -- 8x wide, 4x tall, mirrored top to bottom.
        sb_->Draw(*quad_, Vector2(static_cast<float>(SlotX(3)), kSlotY), std::nullopt, Color::White,
                  0.0f, Vector2(0.0f, 0.0f), Vector2(8.0f, 4.0f), SpriteEffects::FlipVertically, 0.0f);

        // 5: Draw(texture, destinationRectangle, color) -- the whole texture stretched to the slot.
        sb_->Draw(*quad_, Rectangle(SlotX(4), kSlotY, kSlot, kSlot), Color::White);

        // 6: Draw(texture, destinationRectangle, sourceRectangle, color) -- the white texel, tinted.
        sb_->Draw(*quad_, Rectangle(SlotX(5), kSlotY, kSlot, kSlot),
                  std::optional<Rectangle>(Rectangle(1, 1, 1, 1)), Color(255, 0, 255, 255));

        // 7: Draw(texture, destinationRectangle, sourceRectangle, color, rotation, origin, effects,
        //    layerDepth) -- a quarter turn clockwise about the rectangle's top-left corner, placed
        //    so the turned sprite lands back on slot 6: texture right runs screen down, texture
        //    down runs screen left.
        sb_->Draw(*quad_, Rectangle(SlotX(6) + kSlot, kSlotY, kSlot, kSlot), std::nullopt, Color::White,
                  MathHelper::PiOver2, Vector2(0.0f, 0.0f), SpriteEffects::None, 0.0f);

        sb_->End();

        const int y = kSlotY;
        probe(dev, SlotX(0), y, kRed, "1 Draw(tex,pos,color): top-left texel at native size");
        probe(dev, SlotX(0) + 1, y + 1, kWhite, "1 Draw(tex,pos,color): bottom-right texel at native size");
        probe(dev, SlotX(0) + 3, y + 3, kClear, "1 Draw(tex,pos,color): nothing beyond the 2x2 native size");

        probe(dev, SlotX(1), y, Color(0, 128, 0, 255), "2 Draw(tex,pos,src,color): the source texel, tinted");
        probe(dev, SlotX(1) + 1, y, kClear, "2 Draw(tex,pos,src,color): a 1x1 source draws one pixel");

        probe(dev, SlotX(2) + 4, y + 4, kGreen, "3 Draw(...,float scale,FlipHorizontally,...): top-left reads top-right texel");
        probe(dev, SlotX(2) + 12, y + 12, kBlue, "3 Draw(...,float scale,FlipHorizontally,...): bottom-right reads bottom-left texel");

        probe(dev, SlotX(3) + 4, y + 2, kBlue, "4 Draw(...,Vector2 scale,FlipVertically,...): top-left reads bottom-left texel");
        probe(dev, SlotX(3) + 12, y + 6, kGreen, "4 Draw(...,Vector2 scale,FlipVertically,...): bottom-right reads top-right texel");
        probe(dev, SlotX(3) + 4, y + 12, kClear, "4 Draw(...,Vector2 scale,...): the 4x vertical scale stops at 8 rows");

        probe(dev, SlotX(4) + 4, y + 4, kRed, "5 Draw(tex,destRect,color): top-left quadrant");
        probe(dev, SlotX(4) + 12, y + 12, kWhite, "5 Draw(tex,destRect,color): bottom-right quadrant");

        probe(dev, SlotX(5) + 8, y + 8, Color(255, 0, 255, 255), "6 Draw(tex,destRect,src,color): the white texel times the tint");

        probe(dev, SlotX(6) + 4, y + 4, kBlue, "7 Draw(tex,destRect,src,color,rotation,...): top-left reads bottom-left texel");
        probe(dev, SlotX(6) + 12, y + 4, kRed, "7 Draw(tex,destRect,src,color,rotation,...): top-right reads top-left texel");
        probe(dev, SlotX(6) + 12, y + 12, kGreen, "7 Draw(tex,destRect,src,color,rotation,...): bottom-right reads top-right texel");

        Exit();
    }

public:
    SpriteBatchDrawOverloadsTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        // GetBackBufferData is HiDef-only, as in XNA 4.0.
        gdm_->setGraphicsProfileProperty(GraphicsProfile::HiDef);
        gdm_->setPreferredBackBufferWidthProperty(kSlots * (kSlot + kPad) + kPad);
        gdm_->setPreferredBackBufferHeightProperty(kSlot + 2 * kPad);
        gdm_->setPreferredPresentationModeProperty(PresentationMode::NativeBackBuffer);
    }

    int getResult() const { return result_; }
};

int main()
{
    if (!CNA::Examples::Conformance::ProbePixelOutputAvailable("spritebatch_draw_overloads"))
        return CNA::Examples::Conformance::kSkipExitCode;
    SpriteBatchDrawOverloadsTest game;
    game.Run();
    return game.getResult();
}
