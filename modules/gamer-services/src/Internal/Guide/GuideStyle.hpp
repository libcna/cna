// SPDX-License-Identifier: MS-PL
#pragma once
// The CNA system look: one palette, one typeface, one icon family and a few shapes, shared by
// every Guide surface and the avatar editor, so they read as one console system layer.
#include "GuideStrokes.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
class GraphicsDevice;
class SpriteBatch;
class SpriteFont;
class Texture2D;
}

namespace CNA::Internal::GamerServices::GuideUi {
namespace Xna = Microsoft::Xna::Framework;

/** @brief A float rectangle. */
struct Box {
    /** @brief Left. */
    float x=0;
    /** @brief Top. */
    float y=0;
    /** @brief Width. */
    float w=0;
    /** @brief Height. */
    float h=0;
    /** @brief Right edge. @return x + w. */
    [[nodiscard]] float right() const { return x+w; }
    /** @brief Bottom edge. @return y + h. */
    [[nodiscard]] float bottom() const { return y+h; }
    /** @brief Shrunk on every side. @param by Amount. @return Box. */
    [[nodiscard]] Box inset(float by) const { return {x+by,y+by,w-2*by,h-2*by}; }
    /** @brief Whether a point is inside. @param px X. @param py Y. @return Inside. */
    [[nodiscard]] bool contains(float px,float py) const { return px>=x&&py>=y&&px<right()&&py<bottom(); }
};

/** @brief The CNA system palette. */
namespace Palette {
/** @brief Dim over the game while a system screen is up. @return Color. */
Xna::Color dim();
/** @brief Panel body. @return Color. */
Xna::Color panel();
/** @brief Raised surface inside a panel (rows, tiles). @return Color. */
Xna::Color surface();
/** @brief Header band and rail. @return Color. */
Xna::Color rail();
/** @brief The CNA accent (warm coral). @return Color. */
Xna::Color accent();
/** @brief Accent tint behind a focused row. @return Color. */
Xna::Color focus();
/** @brief Primary text. @return Color. */
Xna::Color text();
/** @brief Secondary text. @return Color. */
Xna::Color muted();
/** @brief Disabled text. @return Color. */
Xna::Color faint();
/** @brief Online. @return Color. */
Xna::Color online();
/** @brief Away. @return Color. */
Xna::Color away();
/** @brief Busy. @return Color. */
Xna::Color busy();
/** @brief Offline. @return Color. */
Xna::Color offline();
/** @brief Gold (achievement, reputation). @return Color. */
Xna::Color gold();
/** @brief Error. @return Color. */
Xna::Color error();
}

/** @brief Text styles. */
enum class Font : std::uint8_t { Caption, Body, BodyBold, Heading, Title, Display, Count };
/** @brief Controller buttons the hints show. */
enum class PadButton : std::uint8_t { A, B, X, Y, LB, RB, Start, Back };
/** @brief Horizontal alignment. */
enum class Align : std::uint8_t { Left, Center, Right };

/**
 * @brief Device resources of the CNA system look and the drawing primitives built on them. Sizes
 * are given at 1280 x 720 and multiplied by scale(), so the UI keeps its proportions.
 */
class Style {
public:
    /** @brief Creates fonts, icons and shapes for a device and back-buffer size.
     * @param device Device. @param width Back buffer width. @param height Back buffer height. */
    Style(Xna::Graphics::GraphicsDevice& device,int width,int height);
    /** @brief Releases the resources. */
    ~Style();
    Style(const Style&)=delete;
    Style& operator=(const Style&)=delete;

    /** @brief Whether this style was made for a device and size. @param device Device.
     * @param width Width. @param height Height. @return Matches. */
    [[nodiscard]] bool matches(const Xna::Graphics::GraphicsDevice& device,int width,int height) const;
    /** @brief UI scale relative to 1280 x 720. @return Scale. */
    [[nodiscard]] float scale() const { return scale_; }
    /** @brief A reference-pixel length at this scale. @param value Length at 1280 x 720. @return Pixels. */
    [[nodiscard]] float px(float value) const { return value*scale_; }
    /** @brief Back buffer width. @return Pixels. */
    [[nodiscard]] int width() const { return width_; }
    /** @brief Back buffer height. @return Pixels. */
    [[nodiscard]] int height() const { return height_; }

    /** @brief A text style's font. @param font Style. @return Font. */
    Xna::Graphics::SpriteFont& font(Font font);
    /** @brief Measures text. @param font Style. @param text UTF-8. @return Size. */
    Xna::Vector2 measure(Font font,const std::string& text);
    /** @brief Shortens text to a width with an ellipsis. @param font Style. @param text Text.
     * @param width Pixels. @return Text that fits. */
    std::string fit(Font font,const std::string& text,float width);
    /** @brief Breaks text into lines of at most a width (at spaces, or anywhere in a long word).
     * @param font Style. @param text Text. @param width Pixels. @return Lines. */
    std::vector<std::string> wrap(Font font,const std::string& text,float width);
    /** @brief Draws text. @param batch Batch. @param font Style. @param text Text.
     * @param position Top-left (or top-center/right per align). @param color Color. @param align Alignment. */
    void text(Xna::Graphics::SpriteBatch& batch,Font font,const std::string& text,Xna::Vector2 position,Xna::Color color,
        Align align=Align::Left);

    /** @brief Fills a rectangle. @param batch Batch. @param box Box. @param color Color. */
    void fill(Xna::Graphics::SpriteBatch& batch,const Box& box,Xna::Color color);
    /** @brief Fills a rounded rectangle. @param batch Batch. @param box Box. @param radius Corner radius, pixels.
     * @param color Color. */
    void rounded(Xna::Graphics::SpriteBatch& batch,const Box& box,float radius,Xna::Color color);
    /** @brief A soft shadow under a rounded rectangle. @param batch Batch. @param box Box.
     * @param radius Corner radius. @param spread Blur, pixels. @param color Color. */
    void shadow(Xna::Graphics::SpriteBatch& batch,const Box& box,float radius,float spread,Xna::Color color);
    /** @brief Fills a vertical gradient. @param batch Batch. @param box Box. @param top Top color.
     * @param bottom Bottom color. */
    void gradient(Xna::Graphics::SpriteBatch& batch,const Box& box,Xna::Color top,Xna::Color bottom);
    /** @brief Fills a disc. @param batch Batch. @param center Center. @param radius Radius. @param color Color. */
    void disc(Xna::Graphics::SpriteBatch& batch,Xna::Vector2 center,float radius,Xna::Color color);
    /** @brief Draws an icon. @param batch Batch. @param icon Icon. @param box Square. @param color Color. */
    void icon(Xna::Graphics::SpriteBatch& batch,Icon icon,const Box& box,Xna::Color color);
    /** @brief Draws a controller button glyph. @param batch Batch. @param button Button.
     * @param center Center. @param size Diameter. */
    void pad(Xna::Graphics::SpriteBatch& batch,PadButton button,Xna::Vector2 center,float size);
    /** @brief Draws a keyboard key cap with its label. @param batch Batch. @param label Key.
     * @param position Left, vertical center. @param height Cap height. @return Width drawn. */
    float key(Xna::Graphics::SpriteBatch& batch,const std::string& label,Xna::Vector2 position,float height);
    /** @brief A 1x1 white texture. @return Texture. */
    Xna::Graphics::Texture2D& white();

private:
    struct Resources;
    std::unique_ptr<Resources> resources_;
    const Xna::Graphics::GraphicsDevice* device_=nullptr;
    int width_=0,height_=0;
    float scale_=1.0f;
};
}
