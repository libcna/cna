// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"

namespace CNA::Content::Cnb
{
    /**
     * @brief Headless, deterministic import of source files into the codecs' own data types
     *        (plans/plan_cnb.md `CNBF-109`/`CNBF-110`).
     *
     * Everything here is **pure data**. A content compiler must not need a `GraphicsDevice`, an
     * audio device, a window or a renderer: it runs on a build machine, in a container, in CI. So
     * the import path is
     *
     * ```text
     * source bytes -> canonical CPU representation -> CnbTextureData / CnbSoundEffectData
     * ```
     *
     * and the CNB codecs take it from there. Nothing in this header constructs a runtime object,
     * reads pixels back from a GPU, or opens a mixer.
     *
     * Determinism is a requirement, not a happy accident: no clock, no randomness, no filesystem
     * enumeration order, no absolute paths in the output. Identical source bytes and options
     * produce identical `.cnb` bytes.
     */

    /** @brief Options for importing an image file as a `Texture2D`. */
    struct CnbImageImportOptions
    {
        /**
         * @brief RGB colour to make fully transparent, or `std::nullopt` for none.
         *
         * **Applied only when explicitly requested.** The `.cnj` `Texture2D` route applies a colour
         * key when the document asks for one, so compiling that document must too; a direct image
         * compile has no document to ask, and silently rewriting someone's pixels would be worse
         * than making them say so. Matching pixels keep their RGB and get an alpha of 0, which is
         * exactly what the runtime path does.
         */
        std::optional<std::array<std::uint8_t, 3>> colorKey;
    };

    /**
     * @brief Decodes an image file into a single-level `Rgba8` `Texture2D` description.
     *
     * Uses CNA's own shared image decoder, so a `.cnb` compiled here holds the same pixels the
     * runtime would have loaded from the same file — there is no second decoder to disagree with.
     *
     * @param imagePath Filesystem path to a PNG, JPEG or other format CNA's image loader decodes.
     * @param options   Import options; see CnbImageImportOptions.
     * @return The texture description, ready for EncodeTexture2DToCnb().
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the file cannot be read
     *         or decoded, or decodes to zero pixels.
     */
    [[nodiscard]] CnbTextureData ImportImageAsCnbTexture2D(
        const std::string& imagePath, const CnbImageImportOptions& options = {});

    /**
     * @brief Decodes a DDS cube map into a `TextureCube` description
     *        (plans/plan_cnb.md `CNBF-113`).
     *
     * Goes through `CNA::Internal::Graphics::DecodeDdsCube`, which is the **same** decoder
     * `TextureCube::DDSFromStreamEXT` uses — it was extracted out of that function precisely so a
     * headless compiler could reach it. There is no second DDS parser, and a compiled cube map
     * therefore holds the pixels the runtime would have produced from the same file.
     *
     * The result is `Rgba8`, not the original DXT blocks. That is not a shortcut: the runtime DDS
     * path already decompresses to RGBA8 on the CPU because CNA implements no compressed GPU
     * format end-to-end on any renderer, and texture schema 1's contract is the portable `Rgba8`
     * baseline. Storing the blocks would produce a file this build could not upload.
     *
     * @param ddsPath Filesystem path to the `.dds`.
     * @return The cube description, ready for EncodeTextureCubeToCnb().
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the file cannot be read.
     * @throws System::NotSupportedException, System::FormatException from the shared decoder, for
     *         a malformed, non-cube or unsupported DDS — the same exceptions, with the same
     *         meanings, the runtime raises.
     */
    [[nodiscard]] CnbTextureData ImportDdsAsCnbTextureCube(const std::string& ddsPath);

    /**
     * @brief Decodes DDS bytes already in memory into a `TextureCube` description.
     *
     * @param ddsBytes The complete file contents.
     * @param origin   Text naming the source in diagnostics, e.g. its path.
     * @return The cube description.
     * @throws As ImportDdsAsCnbTextureCube().
     */
    [[nodiscard]] CnbTextureData DecodeDdsAsCnbTextureCube(std::span<const std::uint8_t> ddsBytes,
                                                            const std::string& origin);

    /**
     * @brief Decodes a WAV file's bytes into a `SoundEffect` description.
     *
     * A **pure-data** RIFF/WAVE parser. CNA's runtime WAV path decodes through the mixer engine,
     * which needs an audio device; a content compiler cannot have one, so this reads the container
     * directly. It is deliberately narrow rather than a second general decoder: it accepts the
     * uncompressed PCM formats that convert to CNB's `Pcm16` **exactly**, and refuses everything
     * else by name instead of resampling or truncating someone's audio silently.
     *
     * Accepted: 16-bit PCM (stored as-is) and 8-bit unsigned PCM (widened exactly). Refused, by
     * name: 24-bit, 32-bit, IEEE float, ADPCM and any other tag — each of those would be a lossy
     * or lossy-adjacent conversion, which is an authoring decision rather than a compiler's.
     * `WAVE_FORMAT_EXTENSIBLE` is unwrapped only for a genuine `KSDATAFORMAT_SUBTYPE_*` GUID,
     * all sixteen bytes of it, so an unrelated GUID that merely begins with `01 00` is refused
     * rather than read as PCM.
     *
     * A `smpl` chunk's first loop entry becomes the sound's loop region, using the same rules the
     * runtime applies, so a looping WAV compiles to a looping `.cnb`. Every read of that entry is
     * bounded by the `smpl` chunk's own payload: a chunk declaring a loop it has no room for is
     * refused rather than completed from whatever follows it (plans/plan_cnb.md `CNBF-117`).
     *
     * Integers are decoded from their bytes rather than by copying into a host integer, so the
     * result does not depend on the machine's byte order; the RIFF form's declared length bounds
     * the chunk walk and must agree with the file; `fmt `'s redundant `blockAlign` and `byteRate`
     * must agree with the channel count, sample width and rate they duplicate; and an odd-length
     * chunk must carry its RIFF pad byte when anything follows it.
     *
     * @param wavBytes The complete file contents.
     * @param origin   Text naming the source in diagnostics, e.g. its path.
     * @return The sound description, ready for EncodeSoundEffectToCnb().
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException if the bytes are not a
     *         WAV, are truncated, declare an impossible format, or use an encoding this importer
     *         deliberately refuses.
     */
    [[nodiscard]] CnbSoundEffectData DecodeWavAsCnbSoundEffect(std::span<const std::uint8_t> wavBytes,
                                                                const std::string& origin);

    /**
     * @brief Reads and decodes a WAV file as a `SoundEffect` description.
     *
     * @param wavPath Filesystem path to the `.wav`.
     * @return The sound description.
     * @throws Microsoft::Xna::Framework::Content::ContentLoadException on any read or decode
     *         failure; see DecodeWavAsCnbSoundEffect().
     */
    [[nodiscard]] CnbSoundEffectData ImportWavAsCnbSoundEffect(const std::string& wavPath);
}
