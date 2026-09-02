// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Xnb/XnbAssetTypeWriters.hpp"

#include <algorithm>
#include <any>
#include <memory>
#include <utility>

#include "CNA/Content/Xnb/XnbBuiltInTypeWriters.hpp"
#include "CNA/Content/Xnb/XnbWriter.hpp"

namespace CNA::Content::Xnb
{
    using Cnb::CnbSongData;
    using Cnb::CnbSoundEffectData;
    using Cnb::CnbTextureData;
    using Cnb::CnbTextureFormat;
    using Cnb::CnbVideoData;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
    using Microsoft::Xna::Framework::Rectangle;
    using Microsoft::Xna::Framework::Vector3;

    namespace
    {
        /** @brief WAVE format tag for uncompressed PCM, the only encoding CNB schema 1 stores. */
        constexpr std::uint16_t WaveFormatPcm = 1u;

        /** @brief Bytes of a `WAVEFORMATEX` block with no extension. */
        constexpr std::uint32_t WaveFormatExSize = 16u;

        [[nodiscard]] const Cnb::CnbTextureRepresentation& SelectRepresentation(
            const XnbTextureAsset& asset)
        {
            if (asset.representation >= asset.data.representations.size())
            {
                throw XnbWriteException(
                    "XNB texture: representation " + std::to_string(asset.representation) +
                    " was requested, but the canonical texture holds " +
                    std::to_string(asset.data.representations.size()) + ".");
            }
            return asset.data.representations[asset.representation];
        }

        void ValidateTextureShape(const XnbTextureAsset& asset)
        {
            const CnbTextureData& data = asset.data;
            if (data.width == 0u || data.height == 0u || data.depth == 0u || data.mipCount == 0u)
            {
                throw XnbWriteException("XNB texture: every dimension and the mip count must be "
                                        "at least 1.");
            }
            switch (asset.shape)
            {
                case XnbTextureAsset::Shape::Texture2D:
                    if (data.faceCount != 1u || data.depth != 1u)
                    {
                        throw XnbWriteException(
                            "XNB Texture2D: expected one face and depth 1, but the canonical "
                            "texture declares " + std::to_string(data.faceCount) +
                            " faces and depth " + std::to_string(data.depth) + ".");
                    }
                    break;
                case XnbTextureAsset::Shape::Texture3D:
                    if (data.faceCount != 1u)
                    {
                        throw XnbWriteException(
                            "XNB Texture3D: expected one face, but the canonical texture declares " +
                            std::to_string(data.faceCount) + ".");
                    }
                    break;
                case XnbTextureAsset::Shape::TextureCube:
                    if (data.faceCount != Cnb::CnbTextureCubeFaceCount || data.depth != 1u)
                    {
                        throw XnbWriteException(
                            "XNB TextureCube: expected six faces and depth 1, but the canonical "
                            "texture declares " + std::to_string(data.faceCount) +
                            " faces and depth " + std::to_string(data.depth) + ".");
                    }
                    if (data.width != data.height)
                    {
                        throw XnbWriteException(
                            "XNB TextureCube: a cube face is square, but the canonical texture is " +
                            std::to_string(data.width) + "x" + std::to_string(data.height) + ".");
                    }
                    break;
            }
        }

        /**
         * @brief Writes one level payload: its `UInt32` byte count, then the bytes.
         *
         * The declared size is checked against the level's own dimensions first, so a canonical
         * texture whose payloads disagree with its header is refused here rather than producing a
         * file that decodes into the wrong pixels.
         */
        void WriteTextureLevel(XnbWriter& output, const CnbTextureData& data,
                               const CnbTextureFormat format,
                               const std::vector<std::uint8_t>& bytes, const std::uint32_t mip)
        {
            std::uint32_t width = 0u;
            std::uint32_t height = 0u;
            std::uint32_t depth = 0u;
            Cnb::CnbTextureLevelDimensions(data, mip, width, height, depth);
            const std::uint64_t expected =
                Cnb::CnbTextureLevelByteSize(format, width, height, depth);
            if (expected != bytes.size())
            {
                throw XnbWriteException(
                    "XNB texture: mip level " + std::to_string(mip) + " holds " +
                    std::to_string(bytes.size()) + " bytes, but " + std::to_string(width) + "x" +
                    std::to_string(height) + "x" + std::to_string(depth) + " in " +
                    Cnb::CnbTextureFormatToString(format) + " occupies " +
                    std::to_string(expected) + ".");
            }
            if (bytes.size() > static_cast<std::size_t>(output.Limits().maxPayloadBytes))
            {
                throw XnbWriteException(
                    "XNB texture: mip level " + std::to_string(mip) + " exceeds the maximum "
                    "payload size.");
            }
            output.WriteUInt32(static_cast<std::uint32_t>(bytes.size()));
            output.WriteBytes(bytes);
        }

        /** @brief The `Texture2D`/`Texture3D`/`TextureCube` writer, closed over one shape. */
        class TextureXnbTypeWriter final : public XnbTypeWriterT<XnbTextureAsset>
        {
        public:
            explicit TextureXnbTypeWriter(const XnbTextureAsset::Shape shape) : shape_(shape) {}

            [[nodiscard]] std::string TargetTypeName() const override
            {
                return XnbTextureTypeName(shape_);
            }

            [[nodiscard]] std::string RuntimeReaderName() const override
            {
                // Every texture reader lives in Microsoft.Xna.Framework.Graphics, so its name
                // must carry that assembly for a real XNA runtime's Type.GetType() to find it.
                switch (shape_)
                {
                    case XnbTextureAsset::Shape::Texture2D:
                        return XnbQualifiedReaderName(
                            "Microsoft.Xna.Framework.Content.Texture2DReader",
                            XnaGraphicsAssembly);
                    case XnbTextureAsset::Shape::Texture3D:
                        return XnbQualifiedReaderName(
                            "Microsoft.Xna.Framework.Content.Texture3DReader",
                            XnaGraphicsAssembly);
                    case XnbTextureAsset::Shape::TextureCube:
                        return XnbQualifiedReaderName(
                            "Microsoft.Xna.Framework.Content.TextureCubeReader",
                            XnaGraphicsAssembly);
                }
                return XnbQualifiedReaderName("Microsoft.Xna.Framework.Content.Texture2DReader",
                                              XnaGraphicsAssembly);
            }

            [[nodiscard]] bool IsValueType() const override { return false; }

            void Write(XnbWriter& output, const XnbTextureAsset& asset) const override
            {
                if (asset.shape != shape_)
                {
                    throw XnbWriteException(
                        "'" + TargetTypeName() + "' received a texture declared as a different "
                        "shape.");
                }
                ValidateTextureShape(asset);

                const Cnb::CnbTextureRepresentation& representation = SelectRepresentation(asset);
                const CnbTextureData& data = asset.data;
                const std::size_t expectedLevels =
                    static_cast<std::size_t>(data.faceCount) * data.mipCount;
                if (representation.levels.size() != expectedLevels)
                {
                    throw XnbWriteException(
                        "XNB texture: the selected representation holds " +
                        std::to_string(representation.levels.size()) + " level payloads, but " +
                        std::to_string(data.faceCount) + " faces x " +
                        std::to_string(data.mipCount) + " mips needs " +
                        std::to_string(expectedLevels) + ".");
                }

                output.WriteInt32(
                    XnbSurfaceFormatValue(XnbSurfaceFormatFor(representation.format)));
                output.WriteUInt32(data.width);
                if (shape_ != XnbTextureAsset::Shape::TextureCube)
                {
                    // A cube declares one square size rather than a width and a height.
                    output.WriteUInt32(data.height);
                }
                if (shape_ == XnbTextureAsset::Shape::Texture3D)
                {
                    output.WriteUInt32(data.depth);
                }
                output.WriteUInt32(data.mipCount);

                // Level payloads are face-major then mip, which is exactly the canonical order and
                // exactly the order a cube's six faces appear in the format.
                for (std::size_t level = 0u; level < representation.levels.size(); ++level)
                {
                    WriteTextureLevel(output, data, representation.format,
                                      representation.levels[level],
                                      static_cast<std::uint32_t>(level % data.mipCount));
                }
            }

        private:
            XnbTextureAsset::Shape shape_;
        };

        /** @brief The `SpriteFont` writer. */
        class SpriteFontXnbTypeWriter final : public XnbTypeWriterT<XnbSpriteFontAsset>
        {
        public:
            [[nodiscard]] std::string TargetTypeName() const override
            {
                return XnbTypeKey<XnbSpriteFontAsset>::Name();
            }

            [[nodiscard]] std::string RuntimeReaderName() const override
            {
                return XnbQualifiedReaderName(
                    "Microsoft.Xna.Framework.Content.SpriteFontReader", XnaGraphicsAssembly);
            }

            [[nodiscard]] bool IsValueType() const override { return false; }

            void Write(XnbWriter& output, const XnbSpriteFontAsset& asset) const override
            {
                const Cnb::CnbSpriteFontData& font = asset.data;
                const std::size_t glyphs = font.glyphBounds.size();
                if (font.cropping.size() != glyphs || font.characters.size() != glyphs ||
                    font.kerning.size() != glyphs)
                {
                    throw XnbWriteException(
                        "XNB SpriteFont: the glyph, cropping, character and kerning lists must be "
                        "the same length (" + std::to_string(glyphs) + ", " +
                        std::to_string(font.cropping.size()) + ", " +
                        std::to_string(font.characters.size()) + ", " +
                        std::to_string(font.kerning.size()) + ").");
                }
                if (!std::is_sorted(font.characters.begin(), font.characters.end()) ||
                    std::adjacent_find(font.characters.begin(), font.characters.end()) !=
                        font.characters.end())
                {
                    throw XnbWriteException(
                        "XNB SpriteFont: the character map must be strictly ascending, because "
                        "the runtime binary-searches it.");
                }
                if (font.defaultCharacter.has_value() &&
                    !std::binary_search(font.characters.begin(), font.characters.end(),
                                        *font.defaultCharacter))
                {
                    throw XnbWriteException(
                        "XNB SpriteFont: the default character is not one of the font's own "
                        "characters.");
                }

                XnbTextureAsset atlas;
                atlas.shape = XnbTextureAsset::Shape::Texture2D;
                atlas.data = font.atlas;
                atlas.representation = asset.representation;
                output.WriteObject(XnbTextureTypeName(XnbTextureAsset::Shape::Texture2D),
                                   std::any(atlas));

                WriteRectangleList(output, font.glyphBounds);
                WriteRectangleList(output, font.cropping);

                std::vector<std::any> characters;
                characters.reserve(glyphs);
                for (const SharpRuntime::charcs character : font.characters)
                {
                    characters.emplace_back(static_cast<char16_t>(character));
                }
                output.WriteObject(XnbListTypeName(XnbTypeKey<char16_t>::Name()),
                                   std::any(XnbBoxedList{XnbTypeKey<char16_t>::Name(),
                                                         std::move(characters)}));

                output.WriteInt32(font.lineSpacing);
                output.WriteSingle(font.spacing);

                std::vector<std::any> kerning;
                kerning.reserve(glyphs);
                for (const Vector3& value : font.kerning) { kerning.emplace_back(value); }
                output.WriteObject(XnbListTypeName(XnbTypeKey<Vector3>::Name()),
                                   std::any(XnbBoxedList{XnbTypeKey<Vector3>::Name(),
                                                         std::move(kerning)}));

                // The default character is a raw Nullable<Char>, not a dispatched object: XNA
                // writes Nullable<T> inline because it is a value type.
                XnbBoxedNullable defaultCharacter;
                defaultCharacter.valueTypeName = XnbTypeKey<char16_t>::Name();
                if (font.defaultCharacter.has_value())
                {
                    defaultCharacter.value =
                        std::any(static_cast<char16_t>(*font.defaultCharacter));
                }
                output.WriteRawObject(XnbNullableTypeName(XnbTypeKey<char16_t>::Name()),
                                      std::any(defaultCharacter));
            }

        private:
            static void WriteRectangleList(XnbWriter& output,
                                           const std::vector<Rectangle>& rectangles)
            {
                std::vector<std::any> boxed;
                boxed.reserve(rectangles.size());
                for (const Rectangle& rectangle : rectangles) { boxed.emplace_back(rectangle); }
                output.WriteObject(XnbListTypeName(XnbTypeKey<Rectangle>::Name()),
                                   std::any(XnbBoxedList{XnbTypeKey<Rectangle>::Name(),
                                                         std::move(boxed)}));
            }
        };

        /** @brief The `SoundEffect` writer. */
        class SoundEffectXnbTypeWriter final : public XnbTypeWriterT<CnbSoundEffectData>
        {
        public:
            [[nodiscard]] std::string TargetTypeName() const override
            {
                return XnbTypeKey<CnbSoundEffectData>::Name();
            }

            [[nodiscard]] std::string RuntimeReaderName() const override
            {
                return "Microsoft.Xna.Framework.Content.SoundEffectReader";
            }

            [[nodiscard]] bool IsValueType() const override { return false; }

            void Write(XnbWriter& output, const CnbSoundEffectData& sound) const override
            {
                if (sound.format != Cnb::CnbAudioFormat::Pcm16)
                {
                    throw XnbWriteException(
                        "XNB SoundEffect: only PCM16 samples can be written; " +
                        Cnb::CnbAudioFormatToString(sound.format) +
                        " would need an encoder CNA does not have.");
                }
                if (sound.channels == 0u || sound.channels > 2u)
                {
                    throw XnbWriteException(
                        "XNB SoundEffect: expected 1 or 2 channels, not " +
                        std::to_string(sound.channels) + ".");
                }
                if (sound.sampleRate == 0u)
                {
                    throw XnbWriteException("XNB SoundEffect: the sample rate must be nonzero.");
                }

                const auto blockAlign = static_cast<std::uint16_t>(sound.channels * 2u);
                const std::int64_t expectedBytes = XnbCheckedMultiply(
                    {static_cast<std::int64_t>(sound.frameCount), blockAlign}, "XNB SoundEffect");
                if (static_cast<std::size_t>(expectedBytes) != sound.samples.size())
                {
                    throw XnbWriteException(
                        "XNB SoundEffect: " + std::to_string(sound.frameCount) + " frames of " +
                        std::to_string(blockAlign) + " bytes needs " +
                        std::to_string(expectedBytes) + " sample bytes, but " +
                        std::to_string(sound.samples.size()) + " are present.");
                }

                const std::int64_t averageBytesPerSecond = XnbCheckedMultiply(
                    {static_cast<std::int64_t>(sound.sampleRate), blockAlign},
                    "XNB SoundEffect");
                const std::int64_t loopStartBytes = XnbCheckedMultiply(
                    {static_cast<std::int64_t>(sound.loopStart), blockAlign}, "XNB SoundEffect");
                const std::int64_t loopLengthBytes = XnbCheckedMultiply(
                    {static_cast<std::int64_t>(sound.loopLength), blockAlign}, "XNB SoundEffect");
                if (loopStartBytes > expectedBytes ||
                    XnbCheckedAdd(loopStartBytes, loopLengthBytes, "XNB SoundEffect") >
                        expectedBytes)
                {
                    throw XnbWriteException(
                        "XNB SoundEffect: the loop region runs past the end of the samples.");
                }

                // A WAVEFORMATEX block, little-endian, exactly as a desktop XNB stores it.
                output.WriteUInt32(WaveFormatExSize);
                output.WriteUInt16(WaveFormatPcm);
                output.WriteUInt16(static_cast<std::uint16_t>(sound.channels));
                output.WriteUInt32(sound.sampleRate);
                output.WriteUInt32(static_cast<std::uint32_t>(averageBytesPerSecond));
                output.WriteUInt16(blockAlign);
                output.WriteUInt16(16u);

                output.WriteUInt32(static_cast<std::uint32_t>(sound.samples.size()));
                output.WriteBytes(sound.samples);

                output.WriteInt32(static_cast<std::int32_t>(loopStartBytes));
                output.WriteInt32(static_cast<std::int32_t>(loopLengthBytes));

                const std::int64_t durationMs =
                    sound.sampleRate == 0u
                        ? 0
                        : (static_cast<std::int64_t>(sound.frameCount) * 1000) / sound.sampleRate;
                output.WriteInt32(static_cast<std::int32_t>(durationMs));
            }
        };

        /** @brief The `Song` writer: a streaming file name plus a dispatched duration. */
        class SongXnbTypeWriter final : public XnbTypeWriterT<CnbSongData>
        {
        public:
            [[nodiscard]] std::string TargetTypeName() const override
            {
                return XnbTypeKey<CnbSongData>::Name();
            }

            [[nodiscard]] std::string RuntimeReaderName() const override
            {
                return "Microsoft.Xna.Framework.Content.SongReader";
            }

            [[nodiscard]] bool IsValueType() const override { return false; }

            void Write(XnbWriter& output, const CnbSongData& song) const override
            {
                if (song.streamReference.empty())
                {
                    throw XnbWriteException(
                        "XNB Song: the streaming file name must not be empty; a Song is always an "
                        "external media reference.");
                }
                // The duration is `Object: Int32`: dispatched through the file's own type table,
                // which is why a real Song .xnb names Int32Reader there.
                output.WriteString(song.streamReference);
                output.WriteObject(XnbTypeKey<std::int32_t>::Name(),
                                   std::any(static_cast<std::int32_t>(song.durationMs)));
            }
        };

        /** @brief The `Video` writer. */
        class VideoXnbTypeWriter final : public XnbTypeWriterT<CnbVideoData>
        {
        public:
            [[nodiscard]] std::string TargetTypeName() const override
            {
                return XnbTypeKey<CnbVideoData>::Name();
            }

            [[nodiscard]] std::string RuntimeReaderName() const override
            {
                return "Microsoft.Xna.Framework.Content.VideoReader";
            }

            [[nodiscard]] bool IsValueType() const override { return false; }

            void Write(XnbWriter& output, const CnbVideoData& video) const override
            {
                if (video.streamReference.empty())
                {
                    throw XnbWriteException(
                        "XNB Video: the streaming file name must not be empty; a Video is always "
                        "an external media reference.");
                }
                if (video.width == 0u || video.height == 0u)
                {
                    throw XnbWriteException("XNB Video: the frame size must be nonzero.");
                }
                // Every Video field is a dispatched object, streaming file name included, so the
                // file's type table names StringReader, Int32Reader and SingleReader.
                const auto writeInt32 = [&output](const std::uint32_t value)
                {
                    output.WriteObject(XnbTypeKey<std::int32_t>::Name(),
                                       std::any(static_cast<std::int32_t>(value)));
                };
                output.WriteObject(XnbTypeKey<std::string>::Name(),
                                   std::any(video.streamReference));
                writeInt32(video.durationMs);
                writeInt32(video.width);
                writeInt32(video.height);
                output.WriteObject(XnbTypeKey<float>::Name(), std::any(video.framesPerSecond));
                writeInt32(video.soundtrackType);
            }
        };
    }

    std::int32_t XnbSurfaceFormatValue(const SurfaceFormat format)
    {
        const auto value = static_cast<int>(format);
        if (value < 0 || value > XnbMaxSurfaceFormatValue)
        {
            throw XnbWriteException(
                "XNB texture: SurfaceFormat value " + std::to_string(value) +
                " is a CNA extension with no XNA 4.0 identity, so no XNA reader could interpret "
                "it.");
        }
        return value;
    }

    SurfaceFormat XnbSurfaceFormatFor(const CnbTextureFormat format)
    {
        SurfaceFormat surface{};
        try
        {
            surface = Cnb::CnbTextureFormatToSurfaceFormat(format);
        }
        catch (const std::exception& error)
        {
            throw XnbWriteException(
                "XNB texture: " + Cnb::CnbTextureFormatToString(format) +
                " cannot be written: " + error.what());
        }
        if (static_cast<int>(surface) > XnbMaxSurfaceFormatValue)
        {
            throw XnbWriteException(
                "XNB texture: " + Cnb::CnbTextureFormatToString(format) +
                " maps to a CNA-only SurfaceFormat with no XNA 4.0 identity, so it cannot be "
                "written to an .xnb file.");
        }
        return surface;
    }

    std::string XnbTextureTypeName(const XnbTextureAsset::Shape shape)
    {
        switch (shape)
        {
            case XnbTextureAsset::Shape::Texture2D:
                return "Microsoft.Xna.Framework.Graphics.Texture2D";
            case XnbTextureAsset::Shape::Texture3D:
                return "Microsoft.Xna.Framework.Graphics.Texture3D";
            case XnbTextureAsset::Shape::TextureCube:
                return "Microsoft.Xna.Framework.Graphics.TextureCube";
        }
        return "Microsoft.Xna.Framework.Graphics.Texture2D";
    }

    void RegisterXnbAssetTypeWriters(XnbTypeWriterRegistry& registry)
    {
        registry.Register(
            std::make_shared<const TextureXnbTypeWriter>(XnbTextureAsset::Shape::Texture2D));
        registry.Register(
            std::make_shared<const TextureXnbTypeWriter>(XnbTextureAsset::Shape::Texture3D));
        registry.Register(
            std::make_shared<const TextureXnbTypeWriter>(XnbTextureAsset::Shape::TextureCube));

        // SpriteFont's payload is three closed generic lists and one closed nullable; the file's
        // type table has to resolve all of them, so they are registered with it rather than left
        // to the caller.
        RegisterXnbListWriter(registry, XnbTypeKey<Rectangle>::Name());
        RegisterXnbListWriter(registry, XnbTypeKey<char16_t>::Name());
        RegisterXnbListWriter(registry, XnbTypeKey<Vector3>::Name());
        RegisterXnbNullableWriter(registry, XnbTypeKey<char16_t>::Name());
        registry.Register(std::make_shared<const SpriteFontXnbTypeWriter>());

        registry.Register(std::make_shared<const SoundEffectXnbTypeWriter>());
        registry.Register(std::make_shared<const SongXnbTypeWriter>());
        registry.Register(std::make_shared<const VideoXnbTypeWriter>());
    }
}
