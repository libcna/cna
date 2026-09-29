// SPDX-License-Identifier: MS-PL
#pragma once
// The CNA system typeface and icon family: original monoline skeletons (lines, elliptical arcs,
// dots, filled discs and polygons) drawn with round caps and rasterized by distance, so one set
// of shapes gives smooth text and icons at any size. No font or icon file is involved.
#include <cstdint>
#include <string_view>
#include <vector>

namespace CNA::Internal::GamerServices::GuideUi {
/** @brief A point in skeleton units (1000 per em, y up, baseline 0). */
struct StrokePoint { float x=0, y=0; };

/** @brief A parsed skeleton: open polylines drawn with the stroke width, and filled shapes. */
struct StrokeShape {
    /** @brief Polylines (arcs already sampled). */
    std::vector<std::vector<StrokePoint>> lines;
    /** @brief Filled closed polygons (discs sampled). */
    std::vector<std::vector<StrokePoint>> fills;
};

/** @brief Parses a skeleton: `L x y x y ...` a polyline, `A cx cy rx ry a0 a1` an elliptical arc
 * (degrees, 0 = +x, 90 = +y, sampled from a0 to a1 either way), `D x y` a dot, `C cx cy r` a
 * filled disc, `P x y x y ...` a filled polygon; commands separated by `;`.
 * @param text Skeleton. @return Shape. */
StrokeShape parseStrokes(std::string_view text);

/** @brief Coverage of a shape on a pixel grid. */
struct StrokeImage {
    /** @brief Width in pixels. */
    int width=0;
    /** @brief Height in pixels. */
    int height=0;
    /** @brief Coverage 0..1 per pixel, rows top to bottom. */
    std::vector<float> alpha;
};

/** @brief Rasterizes a shape: pixel (x, y) samples skeleton point (originX + (x + 0.5) / scale,
 * originY - (y + 0.5) / scale); lines are covered within `radius` units (round caps and joins),
 * fills inside their outline; edges are anti-aliased over one pixel.
 * @param shape Shape. @param scale Pixels per unit. @param radius Half the stroke width, units.
 * @param originX Unit x of the left edge. @param originY Unit y of the top edge.
 * @param width Pixels. @param height Pixels. @return Coverage. */
StrokeImage rasterizeStrokes(const StrokeShape& shape,float scale,float radius,float originX,float originY,int width,int height);

/** @brief One glyph of the CNA system typeface. */
struct Glyph {
    /** @brief Character. */
    char16_t character=0;
    /** @brief Advance, units. */
    float advance=0;
    /** @brief Skeleton. */
    std::string_view strokes;
};
/** @brief Every glyph (printable ASCII and a few symbols). @return Glyphs. */
const std::vector<Glyph>& systemGlyphs();

/** @brief The CNA system icons. */
enum class Icon : std::uint8_t {
    Person, People, Party, Message, Invite, Trophy, Controller, Store, Settings, Star, StarOutline, Check, Cross, Plus,
    ChevronRight, ChevronLeft, Lock, Globe, Moon, Busy, Crown, Speaker, Clock, Search, PersonAdd, SignOut, Pencil,
    Download, Info, Leaderboard, Home, Warning, Error, Question, Figure, Drop, Face, Eye, Mouth, Hair, Beard, Shirt, Trousers, Shoe,
    Glasses, Hat, Dice, Count
};
/** @brief An icon's skeleton, drawn in a 1000 x 1000 box (y up). @param icon Icon. @return Skeleton. */
std::string_view iconStrokes(Icon icon);
}
