// SPDX-License-Identifier: MS-PL

#include "CNA/C/sprite_font.h"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiOutput.hpp"

#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::AddOwnedGraphicsResourceFor;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetOwnedTexture2D;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::IsCanonicalBool;
using CNA::C::Detail::IsVersionedStructure;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::RemoveOwnedGraphicsResourceFor;
using CNA::C::Detail::SpriteFontResource;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::Texture2DResource;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ValidateVersionedStructure;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::SpriteFont;

[[nodiscard]] CNA_Result GetSpriteFont(
    const CNA_Handle handle,
    std::shared_ptr<SpriteFontResource>* const outSpriteFont)
{
    return LookupHandle(handle, ObjectKind::SpriteFont, outSpriteFont);
}

} // namespace

CNA_Result cna_sprite_font_create(
    const CNA_SpriteFontCreateInfo* const createInfo,
    CNA_Handle* const outSpriteFont)
{
    return CallWithExceptionBarrier([&]() {
        if (outSpriteFont == nullptr) {
            return InvalidArgument("The SpriteFont output handle is null.");
        }
        *outSpriteFont = CNA_INVALID_HANDLE;
        if (!IsVersionedStructure(createInfo) || !std::isfinite(createInfo->spacing) ||
            !IsCanonicalBool(createInfo->has_default_character) || createInfo->reserved[0] != 0U ||
            createInfo->reserved[1] != 0U || createInfo->reserved[2] != 0U ||
            createInfo->reserved[3] != 0U || createInfo->reserved[4] != 0U) {
            return InvalidArgument("The SpriteFont creation configuration is invalid.");
        }

        std::size_t ignoredBytes = 0U;
        if (const CNA_Result result = CheckedElementByteCount(
                createInfo->glyphs,
                createInfo->glyph_count,
                sizeof(CNA_SpriteFontGlyph),
                &ignoredBytes);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The SpriteFont glyph array is invalid.");
        }
        std::shared_ptr<Texture2DResource> texture;
        if (const CNA_Result result = GetOwnedTexture2D(createInfo->texture, &texture);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }

        std::vector<Rectangle> glyphBounds;
        std::vector<Rectangle> cropping;
        std::vector<SharpRuntime::charcs> characters;
        std::vector<Vector3> kerning;
        if (createInfo->glyph_count > glyphBounds.max_size()) {
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The SpriteFont glyph count exceeds the native collection range.");
        }
        const std::size_t count = static_cast<std::size_t>(createInfo->glyph_count);
        glyphBounds.reserve(count);
        cropping.reserve(count);
        characters.reserve(count);
        kerning.reserve(count);
        for (std::size_t index = 0U; index < count; ++index) {
            const CNA_SpriteFontGlyph& glyph = createInfo->glyphs[index];
            if (glyph.struct_size < sizeof(CNA_SpriteFontGlyph) ||
                glyph.struct_version != StructureVersion || glyph.reserved != 0U ||
                !std::isfinite(glyph.kerning.x) || !std::isfinite(glyph.kerning.y) ||
                !std::isfinite(glyph.kerning.z)) {
                return InvalidArgument("A SpriteFont glyph descriptor is invalid.");
            }
            glyphBounds.emplace_back(
                glyph.glyph_bounds.x,
                glyph.glyph_bounds.y,
                glyph.glyph_bounds.width,
                glyph.glyph_bounds.height);
            cropping.emplace_back(
                glyph.cropping.x,
                glyph.cropping.y,
                glyph.cropping.width,
                glyph.cropping.height);
            characters.push_back(static_cast<SharpRuntime::charcs>(glyph.character));
            kerning.emplace_back(glyph.kerning.x, glyph.kerning.y, glyph.kerning.z);
        }

        const std::optional<SharpRuntime::charcs> defaultCharacter =
            createInfo->has_default_character == CNA_TRUE
            ? std::optional<SharpRuntime::charcs>(
                static_cast<SharpRuntime::charcs>(createInfo->default_character))
            : std::nullopt;
        const auto nativeFont = std::make_shared<SpriteFont>(
            *texture->value,
            std::move(glyphBounds),
            std::move(cropping),
            std::move(characters),
            createInfo->line_spacing,
            createInfo->spacing,
            std::move(kerning),
            defaultCharacter);
        const auto resource = std::make_shared<SpriteFontResource>(
            SpriteFontResource{nativeFont, texture, texture->parentGame});
        if (const CNA_Result result = CreateHandle(ObjectKind::SpriteFont, resource, outSpriteFont);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (texture->activeFontReferenceCount == std::numeric_limits<uint64_t>::max()) {
            static_cast<void>(GetRuntimeHandles().Release(*outSpriteFont));
            *outSpriteFont = CNA_INVALID_HANDLE;
            return Fail(
                CNA_RESULT_OVERFLOW,
                CNA_ERROR_CATEGORY_RANGE,
                "The SpriteFont texture reference count overflowed.");
        }
        ++texture->activeFontReferenceCount;
        AddOwnedGraphicsResourceFor(texture->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_font_get_info(
    const CNA_Handle spriteFontHandle,
    CNA_SpriteFontInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() {
        if (const CNA_Result result =
                ValidateVersionedStructure(outInfo, "The SpriteFont output structure is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<SpriteFontResource> spriteFont;
        if (const CNA_Result result = GetSpriteFont(spriteFontHandle, &spriteFont);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::optional<SharpRuntime::charcs> defaultCharacter =
            spriteFont->value->getDefaultCharacterProperty();
        *outInfo = CNA_SpriteFontInfo{
            .struct_size = sizeof(CNA_SpriteFontInfo),
            .struct_version = StructureVersion,
            .character_count = spriteFont->value->getCharactersProperty().size(),
            .line_spacing = spriteFont->value->getLineSpacingProperty(),
            .spacing = spriteFont->value->getSpacingProperty(),
            .default_character = defaultCharacter.has_value()
                ? static_cast<CNA_Char16>(defaultCharacter.value())
                : static_cast<CNA_Char16>(0U),
            .has_default_character = ToCBool(defaultCharacter.has_value()),
            .reserved = {0U, 0U, 0U, 0U, 0U}};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_font_copy_characters(
    const CNA_Handle spriteFontHandle,
    CNA_Char16* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The SpriteFont character output buffer is invalid.");
        }
        std::shared_ptr<SpriteFontResource> spriteFont;
        if (const CNA_Result result = GetSpriteFont(spriteFontHandle, &spriteFont);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<SharpRuntime::charcs>& characters =
            spriteFont->value->getCharactersProperty();
        *outCount = characters.size();
        if (capacity < characters.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The SpriteFont character output buffer is too small.");
        }
        for (std::size_t index = 0U; index < characters.size(); ++index) {
            destination[index] = static_cast<CNA_Char16>(characters[index]);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_font_copy_glyphs(
    const CNA_Handle spriteFontHandle,
    CNA_SpriteFontGlyph* const destination,
    const uint64_t capacity,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() {
        if (outCount == nullptr || (destination == nullptr && capacity != 0U)) {
            return InvalidArgument("The SpriteFont glyph output buffer is invalid.");
        }
        std::shared_ptr<SpriteFontResource> spriteFont;
        if (const CNA_Result result = GetSpriteFont(spriteFontHandle, &spriteFont);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const SpriteFont& font = *spriteFont->value;
        const std::vector<SharpRuntime::charcs>& characters = font.getCharactersProperty();
        const std::vector<Rectangle>& bounds = font.getGlyphBoundsEXT();
        const std::vector<Rectangle>& cropping = font.getCroppingEXT();
        const std::vector<Vector3>& kerning = font.getKerningEXT();
        *outCount = characters.size();
        // The constructor enforces one entry per character in all four tables, so disagreement
        // here would mean the font was built by something that bypassed it. Refusing beats
        // reading past the shorter table.
        if (bounds.size() != characters.size() || cropping.size() != characters.size() ||
            kerning.size() != characters.size()) {
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The SpriteFont glyph tables disagree on their length.");
        }
        if (capacity < characters.size()) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The SpriteFont glyph output buffer is too small.");
        }
        for (std::size_t index = 0U; index < characters.size(); ++index) {
            CNA_SpriteFontGlyph& glyph = destination[index];
            glyph.struct_size = static_cast<uint32_t>(sizeof(CNA_SpriteFontGlyph));
            glyph.struct_version = StructureVersion;
            glyph.glyph_bounds.x = bounds[index].X;
            glyph.glyph_bounds.y = bounds[index].Y;
            glyph.glyph_bounds.width = bounds[index].Width;
            glyph.glyph_bounds.height = bounds[index].Height;
            glyph.cropping.x = cropping[index].X;
            glyph.cropping.y = cropping[index].Y;
            glyph.cropping.width = cropping[index].Width;
            glyph.cropping.height = cropping[index].Height;
            glyph.character = static_cast<CNA_Char16>(characters[index]);
            glyph.reserved = UINT16_C(0);
            glyph.kerning.x = kerning[index].X;
            glyph.kerning.y = kerning[index].Y;
            glyph.kerning.z = kerning[index].Z;
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_font_measure_utf8(
    const CNA_Handle spriteFontHandle,
    const CNA_StringView text,
    CNA_Vector2* const outSize)
{
    return CallWithExceptionBarrier([&]() {
        if (outSize == nullptr) {
            return InvalidArgument("The SpriteFont measurement output is null.");
        }
        std::string textCopy;
        if (const CNA_Result result = CopyStringView(text, false, &textCopy);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The SpriteFont measurement text is not valid UTF-8.");
        }
        std::shared_ptr<SpriteFontResource> spriteFont;
        if (const CNA_Result result = GetSpriteFont(spriteFontHandle, &spriteFont);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const Vector2 size = spriteFont->value->MeasureString(textCopy);
        *outSize = CNA_Vector2{size.X, size.Y};
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_font_destroy(const CNA_Handle spriteFontHandle)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<SpriteFontResource> spriteFont;
        if (const CNA_Result result = GetSpriteFont(spriteFontHandle, &spriteFont);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result releaseResult = ReleaseHandle(spriteFontHandle);
            releaseResult != CNA_RESULT_SUCCESS) {
            return releaseResult;
        }
        if (spriteFont->texture->activeFontReferenceCount != 0U) {
            --spriteFont->texture->activeFontReferenceCount;
        }
        RemoveOwnedGraphicsResourceFor(spriteFont->parentGame);
        return CNA_RESULT_SUCCESS;
    });
}

namespace CNA::C::Detail {

CNA_Result GetOwnedSpriteFont(
    const CNA_Handle handle,
    std::shared_ptr<SpriteFontResource>* const outSpriteFont)
{
    return LookupHandle(handle, ObjectKind::SpriteFont, outSpriteFont);
}

} // namespace CNA::C::Detail

// The mechanically regular routes of this family, generated from modules/c-api/spec/SpriteFont.toml.
#include "generated/SpriteFont.inc"
