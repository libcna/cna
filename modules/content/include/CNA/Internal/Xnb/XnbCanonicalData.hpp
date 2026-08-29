// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Import/ImportedSound.hpp"
#include "CNA/Internal/Xnb/XnbHeader.hpp"
#include "CNA/Internal/Xnb/XnbReadLimits.hpp"
#include "Microsoft/Xna/Framework/Content/ContentReader.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

namespace CNA::Internal::Xnb
{
    /** @brief Shape of canonical texture bytes decoded from an XNB built-in reader. */
    enum class XnbTextureKind
    {
        /** @brief A single two-dimensional texture. */
        Texture2D,
        /** @brief A single three-dimensional volume texture. */
        Texture3D,
        /** @brief Six square cube-map faces. */
        TextureCube,
    };

    /** @brief Device-independent texture fields and level bytes from an XNB payload. */
    struct XnbTextureData
    {
        /** @brief Root or nested texture shape. */
        XnbTextureKind kind = XnbTextureKind::Texture2D;

        /** @brief Surface format declared by XNB after legacy-version mapping. */
        Microsoft::Xna::Framework::Graphics::SurfaceFormat surfaceFormat =
            Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color;

        /** @brief Width of mip level zero. */
        std::uint32_t width = 0u;

        /** @brief Height of mip level zero. */
        std::uint32_t height = 0u;

        /** @brief Depth of mip level zero, or one for 2D/cube textures. */
        std::uint32_t depth = 1u;

        /** @brief Face count, one for 2D/3D and six for a cube. */
        std::uint32_t faceCount = 1u;

        /** @brief Number of mip levels. */
        std::uint32_t mipCount = 1u;

        /** @brief Source platform byte used for platform-specific transfer rules. */
        char platform = '\0';

        /** @brief Raw XNB payloads ordered face-major and then mip-major. */
        std::vector<std::vector<std::uint8_t>> levels;
    };

    /** @brief Device-independent SpriteFont fields decoded from an XNB payload. */
    struct XnbSpriteFontData
    {
        /** @brief Nested glyph-atlas texture. */
        XnbTextureData atlas;

        /** @brief Glyph source rectangles. */
        std::vector<Microsoft::Xna::Framework::Rectangle> glyphs;

        /** @brief Glyph cropping rectangles. */
        std::vector<Microsoft::Xna::Framework::Rectangle> cropping;

        /** @brief Character map in serialized order. */
        std::vector<SharpRuntime::charcs> characters;

        /** @brief Vertical line spacing. */
        std::int32_t lineSpacing = 0;

        /** @brief Extra horizontal spacing. */
        float spacing = 0.0f;

        /** @brief Per-character left, width, and right kerning values. */
        std::vector<Microsoft::Xna::Framework::Vector3> kerning;

        /** @brief Optional fallback character. */
        std::optional<SharpRuntime::charcs> defaultCharacter;
    };

    /** @brief WAVEFORMATEX fields and sample payload decoded from an XNB SoundEffect. */
    struct XnbSoundEffectData
    {
        /** @brief Source platform byte governing WAVEFORMATEX byte order. */
        char platform = '\0';

        /** @brief WAVE format tag. */
        std::uint16_t formatTag = 0u;

        /** @brief Channel count. */
        std::uint16_t channels = 0u;

        /** @brief Sample rate in Hz. */
        std::uint32_t sampleRate = 0u;

        /** @brief Average encoded bytes per second. */
        std::uint32_t averageBytesPerSecond = 0u;

        /** @brief Encoded block alignment. */
        std::uint16_t blockAlign = 0u;

        /** @brief Nominal bits per sample. */
        std::uint16_t bitsPerSample = 0u;

        /** @brief WAVEFORMATEX extension bytes after cbSize. */
        std::vector<std::uint8_t> extensionData;

        /** @brief Encoded sample payload. */
        std::vector<std::uint8_t> samples;

        /** @brief First decoded sample frame in the loop. */
        std::int32_t loopStart = 0;

        /** @brief Number of decoded sample frames in the loop. */
        std::int32_t loopLength = 0;

        /** @brief Duration stored by the source pipeline, in milliseconds. */
        std::uint32_t storedDurationMs = 0u;
    };

    /** @brief Headless Song metadata carried by an XNB payload. */
    struct XnbSongData
    {
        /** @brief Authored path to the external streaming media. */
        std::string mediaPath;

        /** @brief Duration in milliseconds. */
        std::int32_t durationMs = 0;
    };

    /** @brief Headless Video metadata carried by an XNB payload. */
    struct XnbVideoData
    {
        /** @brief Authored path to the external streaming media. */
        std::string mediaPath;

        /** @brief Duration in milliseconds. */
        std::int32_t durationMs = 0;

        /** @brief Frame width. */
        std::int32_t width = 0;

        /** @brief Frame height. */
        std::int32_t height = 0;

        /** @brief Frames per second. */
        float framesPerSecond = 0.0f;

        /** @brief Serialized VideoSoundtrackType value. */
        std::int32_t soundtrackType = 0;
    };

    /** @brief Bounded canonical root values supported by native XNB transcoding. */
    using XnbCanonicalValue = std::variant<
        XnbTextureData,
        XnbSpriteFontData,
        XnbSoundEffectData,
        Microsoft::Xna::Framework::Curve,
        XnbSongData,
        XnbVideoData>;

    /** @brief Validated XNB container metadata plus its decoded built-in root value. */
    struct XnbCanonicalAsset
    {
        /** @brief Exact normalized root ContentTypeReader identity. */
        std::string rootReader;

        /** @brief Container platform byte. */
        char platform = '\0';

        /** @brief XNB container version. */
        int version = 0;

        /** @brief Container compression scheme. */
        XnbCompression compression = XnbCompression::None;

        /** @brief Canonical CPU value selected by rootReader. */
        XnbCanonicalValue value;
    };

    /**
     * @brief Reads and validates one Texture2D payload without constructing a GraphicsDevice object.
     *
     * @param input Content reader positioned at the first Texture2D field.
     * @param maximumDimension Optional caller-owned target limit checked before level bytes.
     * @return Canonical source format, dimensions, mip count, and raw level bytes.
     */
    [[nodiscard]] XnbTextureData DecodeTexture2DXnbData(
        Microsoft::Xna::Framework::Content::ContentReader& input,
        std::uint32_t maximumDimension = std::numeric_limits<std::uint32_t>::max());

    /**
     * @brief Reads and validates one Texture3D payload without constructing a GPU resource.
     *
     * @param input Content reader positioned at the first Texture3D field.
     * @return Canonical volume dimensions, mip count, and raw level bytes.
     */
    [[nodiscard]] XnbTextureData DecodeTexture3DXnbData(
        Microsoft::Xna::Framework::Content::ContentReader& input);

    /**
     * @brief Reads and validates one TextureCube payload without constructing a GPU resource.
     *
     * @param input Content reader positioned at the first TextureCube field.
     * @return Canonical cube dimensions and face-major raw level bytes.
     */
    [[nodiscard]] XnbTextureData DecodeTextureCubeXnbData(
        Microsoft::Xna::Framework::Content::ContentReader& input);

    /**
     * @brief Reads a SpriteFont payload, including its nested built-in Texture2D and list readers.
     *
     * @param input Initialized content reader positioned at the first SpriteFont field.
     * @param maximumTextureDimension Optional target limit for the nested atlas.
     * @return Canonical font and atlas CPU data.
     */
    [[nodiscard]] XnbSpriteFontData DecodeSpriteFontXnbData(
        Microsoft::Xna::Framework::Content::ContentReader& input,
        std::uint32_t maximumTextureDimension =
            std::numeric_limits<std::uint32_t>::max());

    /**
     * @brief Reads a SoundEffect payload into neutral WAVEFORMATEX and sample data.
     *
     * @param input Content reader positioned at the first SoundEffect field.
     * @return Parsed audio fields without constructing a SoundEffect or opening an audio device.
     */
    [[nodiscard]] XnbSoundEffectData DecodeSoundEffectXnbData(
        Microsoft::Xna::Framework::Content::ContentReader& input);

    /**
     * @brief Reads Curve fields into the existing CPU Curve value.
     *
     * @param input Content reader positioned at the first Curve field.
     * @param existing Existing Curve for the runtime reload path, or no value for a fresh curve.
     * @return Decoded Curve semantics.
     */
    [[nodiscard]] Microsoft::Xna::Framework::Curve DecodeCurveXnbData(
        Microsoft::Xna::Framework::Content::ContentReader& input,
        std::optional<Microsoft::Xna::Framework::Curve> existing = std::nullopt);

    /**
     * @brief Reads Song path and duration fields without resolving or opening media.
     *
     * @param input Content reader positioned at the first Song field.
     * @return Authored media path and duration.
     */
    [[nodiscard]] XnbSongData DecodeSongXnbData(
        Microsoft::Xna::Framework::Content::ContentReader& input);

    /**
     * @brief Reads Video path and metadata fields without constructing playback objects.
     *
     * @param input Content reader positioned at the first Video field.
     * @param objectReferences Whether fields use FNA's real ReadObject reader references. The
     *        false compatibility mode preserves CNA's established field-only runtime reader.
     * @return Authored media path and video metadata.
     */
    [[nodiscard]] XnbVideoData DecodeVideoXnbData(
        Microsoft::Xna::Framework::Content::ContentReader& input,
        bool objectReferences = false);

    /**
     * @brief Converts a supported XNB texture into CNB schema-1 Rgba8 CPU data.
     *
     * @param source Validated XNB texture data.
     * @param allowXboxPayload Preserves the historical runtime reader's best-effort treatment of
     *        Xbox payload bytes; the pipeline leaves this false because it cannot prove swizzling.
     * @return Rgba8 levels preserving the texture shape and mip/face order.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException for a source format the
     *         frozen native schema cannot represent without changing observable semantics.
     */
    [[nodiscard]] CNA::Content::Cnb::CnbTextureData ConvertXnbTextureToCnbRgba8(
        const XnbTextureData& source, bool allowXboxPayload = false);

    /**
     * @brief Decodes supported XNB SoundEffect formats to source-oriented PCM for the pipeline.
     *
     * @param source Parsed XNB WAVEFORMATEX and sample data.
     * @param origin Asset identity used in diagnostics.
     * @param allowXboxPayload Preserves the historical runtime reader's best-effort treatment of
     *        Xbox sample bytes; the pipeline leaves this false because byte order is not proven.
     * @return Signed PCM16 or unsigned PCM8 data accepted by SoundEffectProcessor.
     */
    [[nodiscard]] CNA::Content::Import::ImportedSound ConvertXnbSoundToImportedSound(
        const XnbSoundEffectData& source, const std::string& origin,
        bool allowXboxPayload = false);

    /**
     * @brief Validates an XNB container and decodes one supported built-in root headlessly.
     *
     * @param path Native path to the XNB source.
     * @param limits Bounds for file, table, collection, and decompressed allocations.
     * @return Container metadata and canonical root data.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException for malformed containers,
     *         unsupported compression, shared-resource graphs, or unsupported/custom roots.
     */
    [[nodiscard]] XnbCanonicalAsset DecodeXnbCanonicalAsset(
        const std::filesystem::path& path,
        const XnbReadLimits& limits = DefaultXnbReadLimits());
}
