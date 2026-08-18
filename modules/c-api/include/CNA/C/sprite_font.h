// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_SPRITE_FONT_H
#define CNA_C_SPRITE_FONT_H

#include "CNA/C/graphics.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Fully qualified native type name represented by a SpriteFont handle. */
#define CNA_SPRITE_FONT_TYPE_NAME "Microsoft.Xna.Framework.Graphics.SpriteFont"

/** @brief One UTF-16 code unit matching the native XNA `char` representation. */
typedef uint16_t CNA_Char16;

/** @brief Defines one glyph in a caller-built SpriteFont atlas. */
typedef struct CNA_SpriteFontGlyph {
    /** @brief Size of this array element in bytes; version one requires exact current size. */
    uint32_t struct_size;
    /** @brief Version of this array element. */
    uint32_t struct_version;
    /** @brief Source rectangle inside the atlas texture. */
    CNA_Rectangle glyph_bounds;
    /** @brief Per-glyph cropping/offset rectangle. */
    CNA_Rectangle cropping;
    /** @brief UTF-16 character mapped to this glyph. */
    CNA_Char16 character;
    /** @brief Reserved for future use; must be zero. */
    uint16_t reserved;
    /** @brief Left bearing, glyph width and right bearing. */
    CNA_Vector3 kerning;
} CNA_SpriteFontGlyph;

/** @brief Configures an owned SpriteFont built from an existing texture and glyph table. */
typedef struct CNA_SpriteFontCreateInfo {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief Owned Texture2D or RenderTarget2D handle retained by the font. */
    CNA_Handle texture;
    /** @brief Caller-owned exact-stride glyph array copied during creation. */
    const CNA_SpriteFontGlyph* glyphs;
    /** @brief Number of entries in @ref glyphs. */
    uint64_t glyph_count;
    /** @brief Vertical distance between consecutive text baselines. */
    int32_t line_spacing;
    /** @brief Extra horizontal spacing between characters. Must be finite. */
    float spacing;
    /** @brief Optional fallback UTF-16 character. */
    CNA_Char16 default_character;
    /** @brief Whether @ref default_character has a value. */
    CNA_Bool has_default_character;
    /** @brief Reserved bytes; must be zero. */
    uint8_t reserved[5];
} CNA_SpriteFontCreateInfo;

/** @brief Point-in-time SpriteFont properties. */
typedef struct CNA_SpriteFontInfo {
    /** @brief Size of this structure in bytes. */
    uint32_t struct_size;
    /** @brief Version of this structure. */
    uint32_t struct_version;
    /** @brief Number of supported UTF-16 characters. */
    uint64_t character_count;
    /** @brief Vertical distance between consecutive text baselines. */
    int32_t line_spacing;
    /** @brief Extra horizontal spacing between characters. */
    float spacing;
    /** @brief Optional fallback UTF-16 character. */
    CNA_Char16 default_character;
    /** @brief Whether @ref default_character has a value. */
    CNA_Bool has_default_character;
    /** @brief Reserved bytes; returned as zero. */
    uint8_t reserved[5];
} CNA_SpriteFontInfo;

/**
 * @brief Creates an owned game-child SpriteFont from a texture and complete glyph table.
 *
 * @param create_info Versioned texture, glyph and layout configuration.
 * @param out_sprite_font Receives an owned SpriteFont handle on success.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread/memory failure.
 *
 * The glyph table is copied. The source texture remains owned by the caller and cannot be
 * destroyed until this SpriteFont is destroyed.
 */
CNA_C_API CNA_Result cna_sprite_font_create(
    const CNA_SpriteFontCreateInfo* create_info,
    CNA_Handle* out_sprite_font);

/**
 * @brief Gets mutable layout properties and the character count.
 *
 * @param sprite_font Owned SpriteFont handle.
 * @param out_info Caller-initialized versioned output structure.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sprite_font_get_info(
    CNA_Handle sprite_font,
    CNA_SpriteFontInfo* out_info);

/**
 * @brief Copies the complete supported UTF-16 character collection.
 *
 * @param sprite_font Owned SpriteFont handle.
 * @param destination Caller-owned output array, or null only when @p capacity is zero.
 * @param capacity Capacity in UTF-16 code units.
 * @param out_count Receives the exact required element count.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL`, or a documented failure. No partial
 * array is written.
 */
CNA_C_API CNA_Result cna_sprite_font_copy_characters(
    CNA_Handle sprite_font,
    CNA_Char16* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Copies the complete glyph table this SpriteFont was built from.
 *
 * @param sprite_font Owned SpriteFont handle.
 * @param destination Array of @p capacity `CNA_SpriteFontGlyph` elements, or null only when
 *        @p capacity is zero. Every element is written by the call, `struct_size` and
 *        `struct_version` included, so the caller need not pre-initialize them.
 * @param capacity Number of elements available at @p destination.
 * @param out_count Always receives the glyph count, which equals
 *        `CNA_SpriteFontInfo::character_count`.
 * @return `CNA_RESULT_SUCCESS`, `CNA_RESULT_BUFFER_TOO_SMALL` with **no partial write**, or a
 *         documented argument/handle/thread failure.
 *
 * The inverse of `cna_sprite_font_create`: the array this returns is exactly the array that
 * constructor accepts, in the same order, so a font can be read back and rebuilt without loss.
 * Element `i` describes the character `cna_sprite_font_copy_characters` returns at index `i`.
 *
 * This exists because measuring is not drawing. `cna_sprite_font_measure_utf8` answers the size of
 * a whole string, which is enough to lay out a text box and not enough to place a glyph: a
 * consumer implementing `SpriteBatch.DrawString` above this ABI needs each glyph's atlas
 * rectangle, its cropping offset and its three kerning values, and without them a native-owned
 * font could be measured and never drawn. The atlas texture completing the picture is the handle
 * the caller passed to `cna_sprite_font_create`, or the one
 * `cna_content_manager_load_sprite_font` reports.
 */
CNA_C_API CNA_Result cna_sprite_font_copy_glyphs(
    CNA_Handle sprite_font,
    CNA_SpriteFontGlyph* destination,
    uint64_t capacity,
    uint64_t* out_count);

/**
 * @brief Sets or clears the fallback character.
 *
 * @param sprite_font Owned SpriteFont handle.
 * @param has_value `CNA_TRUE` to set @p value or `CNA_FALSE` to clear the fallback.
 * @param value UTF-16 fallback character; ignored when @p has_value is false.
 * @return `CNA_RESULT_SUCCESS` or `CNA_RESULT_INVALID_ARGUMENT` when the value is not in the font.
 */
CNA_C_API CNA_Result cna_sprite_font_set_default_character(
    CNA_Handle sprite_font,
    CNA_Bool has_value,
    CNA_Char16 value);

/**
 * @brief Sets the vertical baseline spacing.
 *
 * @param sprite_font Owned SpriteFont handle.
 * @param line_spacing New line spacing in pixels.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_sprite_font_set_line_spacing(
    CNA_Handle sprite_font,
    int32_t line_spacing);

/**
 * @brief Sets the extra horizontal character spacing.
 *
 * @param sprite_font Owned SpriteFont handle.
 * @param spacing New finite spacing in pixels.
 * @return `CNA_RESULT_SUCCESS` or a documented argument/handle/thread failure.
 */
CNA_C_API CNA_Result cna_sprite_font_set_spacing(
    CNA_Handle sprite_font,
    float spacing);

/**
 * @brief Measures a length-delimited UTF-8 string with the font.
 *
 * @param sprite_font Owned SpriteFont handle.
 * @param text Valid UTF-8 bytes; embedded U+0000 is preserved.
 * @param out_size Receives measured width and height in pixels.
 * @return `CNA_RESULT_SUCCESS` or a documented text/argument/handle/thread failure.
 *
 * This one route maps both native String and StringBuilder overloads because their observable
 * behavior is identical after StringBuilder materializes its string.
 */
CNA_C_API CNA_Result cna_sprite_font_measure_utf8(
    CNA_Handle sprite_font,
    CNA_StringView text,
    CNA_Vector2* out_size);

/**
 * @brief Disposes and releases an owned SpriteFont.
 *
 * @param sprite_font Owned SpriteFont handle.
 * @return `CNA_RESULT_SUCCESS` or a documented handle/thread failure.
 */
CNA_C_API CNA_Result cna_sprite_font_destroy(CNA_Handle sprite_font);

#ifdef __cplusplus
}
#endif

#endif
