// SPDX-License-Identifier: MS-PL

#include "CNA/C/sprite_font.h"
#include "CnaCApiGraphicsDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

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

using CNA::C::Detail::AddOwnedGraphicsResource;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CheckedElementByteCount;
using CNA::C::Detail::CopyStringView;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetOwnedTexture2D;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::SpriteFontResource;
using CNA::C::Detail::RemoveOwnedGraphicsResource;
using CNA::C::Detail::Texture2DResource;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::SpriteFont;

constexpr uint32_t StructureVersion = UINT32_C(1);

[[nodiscard]] bool IsBool(const CNA_Bool value) noexcept
{
    return value == CNA_FALSE || value == CNA_TRUE;
}

[[nodiscard]] CNA_Result InvalidArgument(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result GetSpriteFont(
    const CNA_Handle handle,
    std::shared_ptr<SpriteFontResource>* const outSpriteFont)
{
    const CNA_Result result = GetRuntimeHandles().Get(
        handle, ObjectKind::SpriteFont, outSpriteFont);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned SpriteFont handle is invalid for this call.");
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
        if (createInfo == nullptr ||
            createInfo->struct_size < sizeof(CNA_SpriteFontCreateInfo) ||
            createInfo->struct_version != StructureVersion || !std::isfinite(createInfo->spacing) ||
            !IsBool(createInfo->has_default_character) || createInfo->reserved[0] != 0U ||
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
            if (glyph.struct_size != sizeof(CNA_SpriteFontGlyph) ||
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
        const CNA_Result result = GetRuntimeHandles().Create(
            ObjectKind::SpriteFont, resource, outSpriteFont);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned SpriteFont handle could not be created.");
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
        AddOwnedGraphicsResource();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_font_get_info(
    const CNA_Handle spriteFontHandle,
    CNA_SpriteFontInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_SpriteFontInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidArgument("The SpriteFont output structure is invalid.");
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
            .has_default_character = defaultCharacter.has_value() ? CNA_TRUE : CNA_FALSE,
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

CNA_Result cna_sprite_font_set_default_character(
    const CNA_Handle spriteFontHandle,
    const CNA_Bool hasValue,
    const CNA_Char16 value)
{
    return CallWithExceptionBarrier([&]() {
        if (!IsBool(hasValue)) {
            return InvalidArgument("The SpriteFont default-character presence flag is invalid.");
        }
        std::shared_ptr<SpriteFontResource> spriteFont;
        if (const CNA_Result result = GetSpriteFont(spriteFontHandle, &spriteFont);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        spriteFont->value->setDefaultCharacterProperty(
            hasValue == CNA_TRUE
                ? std::optional<SharpRuntime::charcs>(static_cast<SharpRuntime::charcs>(value))
                : std::nullopt);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_font_set_line_spacing(
    const CNA_Handle spriteFontHandle,
    const int32_t lineSpacing)
{
    return CallWithExceptionBarrier([&]() {
        std::shared_ptr<SpriteFontResource> spriteFont;
        if (const CNA_Result result = GetSpriteFont(spriteFontHandle, &spriteFont);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        spriteFont->value->setLineSpacingProperty(lineSpacing);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_sprite_font_set_spacing(
    const CNA_Handle spriteFontHandle,
    const float spacing)
{
    return CallWithExceptionBarrier([&]() {
        if (!std::isfinite(spacing)) {
            return InvalidArgument("The SpriteFont spacing must be finite.");
        }
        std::shared_ptr<SpriteFontResource> spriteFont;
        if (const CNA_Result result = GetSpriteFont(spriteFontHandle, &spriteFont);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        spriteFont->value->setSpacingProperty(spacing);
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
        const CNA_Result releaseResult = GetRuntimeHandles().Release(spriteFontHandle);
        if (releaseResult != CNA_RESULT_SUCCESS) {
            return Fail(
                releaseResult,
                ErrorCategoryForResult(releaseResult),
                "The owned SpriteFont handle could not be released.");
        }
        if (spriteFont->texture->activeFontReferenceCount != 0U) {
            --spriteFont->texture->activeFontReferenceCount;
        }
        RemoveOwnedGraphicsResource();
        return CNA_RESULT_SUCCESS;
    });
}

namespace CNA::C::Detail {

CNA_Result GetOwnedSpriteFont(
    const CNA_Handle handle,
    std::shared_ptr<SpriteFontResource>* const outSpriteFont)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::SpriteFont, outSpriteFont);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The SpriteFont handle is invalid for this call.");
}

} // namespace CNA::C::Detail
