// SPDX-License-Identifier: MS-PL
#pragma once

#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <string>

namespace CNA::Content::Cnb
{
    // --- The floating-point representation contract (plans/plan_cnb.md `CNBF-H019`) -------------
    //
    // `docs/cnb-format.md` §2 defines `f32` and `f64` as IEEE-754 binary32 and binary64. That is a
    // promise about the BYTES IN THE FILE, so a platform that cannot produce them must fail to
    // build rather than silently emit a file no other CNA can read. Checked here, once, in the
    // header every part of the CNB primitive layer includes -- previously it was four scattered
    // `sizeof` assertions inside individual functions, which is neither the whole contract nor a
    // single place to read it.
    //
    // The three checks are not redundant; each rules out something the others do not:
    //
    //  * `is_iec559` + `digits` + `radix` pin the format to IEEE-754 binary32/binary64 rather than
    //    merely "some four-byte floating type".
    //  * `sizeof` pins the storage width, which `is_iec559` alone does not.
    //  * The `bit_cast` of a known constant is the one that catches the case the other two cannot:
    //    a host whose floating-point and integer object representations disagree in BYTE ORDER.
    //    `CnbByteWriter::WriteF32` bit-casts to `std::uint32_t` and then emits that integer
    //    little-endian, so on a mixed-endian host the file would receive the mantissa and exponent
    //    bytes reversed while every check above still passed. There is no standard trait for
    //    floating-point endianness; a `constexpr` bit-cast of `1.0f` is the portable way to ask.
    //
    // Note what is deliberately NOT asserted, and why:
    //
    //  * The host's own endianness. The container is byte-order independent by construction --
    //    integers are assembled from individual bytes -- so a big-endian host produces
    //    byte-identical output, and asserting little-endian would contradict §2 rather than
    //    enforce it.
    //  * Anything about floating-point ARITHMETIC. `is_iec559` is a weaker guarantee than it
    //    looks: GCC still reports it as true under `-ffast-math` (measured, not assumed; clang
    //    reports false). That is not a gap here, because CNB never computes with a serialized
    //    value -- it only bit-casts one -- and a build flag that relaxes arithmetic does not move
    //    a byte in the file. If CNB ever gains a computed field, this comment stops being true.
    static_assert(std::numeric_limits<float>::is_iec559,
                  "CNB requires an IEEE-754 float; this platform's float is not IEC 559.");
    static_assert(std::numeric_limits<float>::radix == 2 &&
                      std::numeric_limits<float>::digits == 24,
                  "CNB requires IEEE-754 binary32 for f32.");
    static_assert(sizeof(float) == 4, "CNB requires a 4-byte float for f32.");
    static_assert(std::bit_cast<std::uint32_t>(1.0f) == 0x3F800000u,
                  "CNB requires float and integer object representations to agree in byte order; "
                  "on this platform bit_cast<uint32_t>(1.0f) is not binary32's 0x3F800000.");

    static_assert(std::numeric_limits<double>::is_iec559,
                  "CNB requires an IEEE-754 double; this platform's double is not IEC 559.");
    static_assert(std::numeric_limits<double>::radix == 2 &&
                      std::numeric_limits<double>::digits == 53,
                  "CNB requires IEEE-754 binary64 for f64.");
    static_assert(sizeof(double) == 8, "CNB requires an 8-byte double for f64.");
    static_assert(std::bit_cast<std::uint64_t>(1.0) == 0x3FF0000000000000ull,
                  "CNB requires double and integer object representations to agree in byte order; "
                  "on this platform bit_cast<uint64_t>(1.0) is not binary64's 0x3FF0000000000000.");

    /**
     * @brief Byte-level constants of the `.cnb` container (plans/plan_cnb.md `CNBF-005`).
     *
     * The authoritative description of every field is `docs/cnb-format.md`; the values here and
     * that document are kept in step by `CnbSpecConformanceTests.cpp`.
     */
    namespace Format
    {
        /** @brief The four magic bytes at offset 0: `'C'`, `'N'`, `'B'`, `0x1A`. */
        inline constexpr std::array<std::uint8_t, 4> Magic{0x43u, 0x4Eu, 0x42u, 0x1Au};

        /** @brief Size of the fixed container header, in bytes. */
        inline constexpr std::uint32_t HeaderSize = 64u;

        /** @brief Size of one table-of-contents entry, in bytes. */
        inline constexpr std::uint32_t TocEntrySize = 48u;

        /** @brief Number of leading header bytes covered by the header checksum. */
        inline constexpr std::uint32_t HeaderChecksumCoverage = 44u;

        /** @brief Byte offset of the header checksum field. */
        inline constexpr std::uint32_t HeaderChecksumOffset = 44u;

        /** @brief Number of reserved, must-be-zero bytes at the end of the header. */
        inline constexpr std::uint32_t HeaderReservedSize = 16u;

        /** @brief Container major version this implementation reads and writes. */
        inline constexpr std::uint16_t ContainerMajor = 1u;

        /** @brief Container minor version this implementation writes. */
        inline constexpr std::uint16_t ContainerMinor = 0u;

        /** @brief Byte offset at which CnbWriter always places the table of contents. */
        inline constexpr std::uint64_t DefaultTocOffset = HeaderSize;
    }

    /**
     * @brief A four-character chunk identifier, stored as a little-endian `u32` so its bytes read
     *        left-to-right in a hex dump.
     *
     * Every byte must be printable ASCII (`0x20`-`0x7E`). An identifier whose first byte is an
     * uppercase ASCII letter is reserved for CNA's own schemas; a game defining its own `.cnb`
     * schema uses an identifier starting with a lowercase letter.
     */
    struct CnbChunkId
    {
        /** @brief The packed identifier value, as stored in the file. */
        std::uint32_t value = 0u;

        /** @brief Equality comparison against another identifier. */
        [[nodiscard]] constexpr bool operator==(const CnbChunkId&) const = default;
    };

    /**
     * @brief Packs four characters into a CnbChunkId.
     *
     * @param a First character (lowest-addressed byte in the file).
     * @param b Second character.
     * @param c Third character.
     * @param d Fourth character.
     * @return The packed identifier.
     */
    [[nodiscard]] constexpr CnbChunkId MakeChunkId(char a, char b, char c, char d)
    {
        return CnbChunkId{static_cast<std::uint32_t>(static_cast<unsigned char>(a)) |
                          (static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 8) |
                          (static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 16) |
                          (static_cast<std::uint32_t>(static_cast<unsigned char>(d)) << 24)};
    }

    /**
     * @brief Renders a chunk identifier as its four characters, for diagnostics.
     *
     * Any byte outside printable ASCII is rendered as `?` so a corrupt identifier cannot inject
     * control characters into a log line.
     *
     * @param id The identifier to render.
     * @return A four-character string.
     */
    [[nodiscard]] std::string ChunkIdToString(CnbChunkId id);

    /**
     * @brief Whether every byte of @p id is printable ASCII, as the format requires.
     *
     * @param id The identifier to check.
     * @return True when the identifier is well-formed.
     */
    [[nodiscard]] bool IsWellFormedChunkId(CnbChunkId id);

    /** @brief Per-chunk flag bits stored in a table-of-contents entry's `flags` field. */
    namespace CnbChunkFlags
    {
        /** @brief No flags set. */
        inline constexpr std::uint32_t None = 0u;

        /**
         * @brief The chunk is mandatory: a reader that does not understand its identifier must
         *        refuse the whole file rather than skip it.
         */
        inline constexpr std::uint32_t Mandatory = 1u << 0;

        /** @brief Every flag bit this container version defines. Any other bit set is an error. */
        inline constexpr std::uint32_t KnownMask = Mandatory;
    }

    /**
     * @brief Per-chunk compression codec identifiers.
     *
     * **The numeric values are wire format and frozen.** Codec 2 is Zstandard in every `.cnb` ever
     * written, whether or not a given build implements it, so these numbers never move.
     *
     * Whether a build can actually *use* a codec is a separate, runtime question:
     * `IsCnbCompressionSupported()` answers it. Zstandard is implemented when CNA is built with
     * libzstd (`CNA_CNB_ZSTD`, `AUTO` by default); LZ4 and Deflate have identifiers and no
     * implementation. Compression is opt-in per chunk and off by default — see
     * `docs/cnb-compression-measurements.md` for why that default is measured rather than cautious.
     */
    enum class CnbCompression : std::uint32_t
    {
        /** @brief Stored uncompressed. Always available, and the default. */
        None = 0u,

        /** @brief LZ4. Identifier assigned; no implementation in this build. */
        Lz4 = 1u,

        /**
         * @brief Zstandard. Implemented when CNA is built with libzstd; ask
         *        `IsCnbCompressionSupported()` rather than assuming.
         */
        Zstd = 2u,

        /** @brief Deflate. Identifier assigned; no implementation in this build. */
        Deflate = 3u,

        /**
         * @brief Former name of Lz4, kept so existing source keeps compiling.
         * @deprecated Use CnbCompression::Lz4.
         */
        ReservedLz4 = Lz4,

        /**
         * @brief Former name of Zstd, from when no codec was implemented.
         * @deprecated Use CnbCompression::Zstd.
         */
        ReservedZstd = Zstd,

        /**
         * @brief Former name of Deflate, kept so existing source keeps compiling.
         * @deprecated Use CnbCompression::Deflate.
         */
        ReservedDeflate = Deflate,
    };

    /**
     * @brief Stable numeric identifiers for the asset a `.cnb` file holds
     *        (plans/plan_cnb.md decision `D6`).
     *
     * `0x00000001`-`0x3FFFFFFF` are CNA built-in types and are frozen once CNB v1 ships.
     * `0x40000000`-`0x7FFFFFFF` are reserved for future CNA use. `0x80000000`-`0xFFFFFFFF` belong
     * to games, which mint one with CnbAssetTypeIdFromName().
     */
    namespace CnbAssetTypeId
    {
        /** @brief Not a valid asset type; a file declaring it is rejected. */
        inline constexpr std::uint32_t Invalid = 0x00000000u;

        /** @brief `Microsoft::Xna::Framework::Graphics::Texture2D`. Implemented, schema version 1. */
        inline constexpr std::uint32_t Texture2D = 0x00000001u;

        /** @brief `Microsoft::Xna::Framework::Graphics::Texture3D`. Implemented, schema version 1. */
        inline constexpr std::uint32_t Texture3D = 0x00000002u;

        /** @brief `Microsoft::Xna::Framework::Graphics::TextureCube`. Implemented, schema version 1. */
        inline constexpr std::uint32_t TextureCube = 0x00000003u;

        /** @brief `Microsoft::Xna::Framework::Graphics::SpriteFont`. Implemented, schema version 1; embeds its atlas. */
        inline constexpr std::uint32_t SpriteFont = 0x00000004u;

        /** @brief `Microsoft::Xna::Framework::Graphics::Model`. Implemented, schema version 1. */
        inline constexpr std::uint32_t Model = 0x00000005u;

        /** @brief `Microsoft::Xna::Framework::Graphics::AnimationClipEXT`. Implemented, schema version 1. */
        inline constexpr std::uint32_t AnimationClip = 0x00000006u;

        /** @brief `Microsoft::Xna::Framework::Curve`. Implemented, schema version 1. */
        inline constexpr std::uint32_t Curve = 0x00000007u;

        /** @brief `Microsoft::Xna::Framework::Audio::SoundEffect`. Implemented, schema version 1. */
        inline constexpr std::uint32_t SoundEffect = 0x00000008u;

        /** @brief `Microsoft::Xna::Framework::Media::Song`. Implemented, schema version 1; metadata plus a streaming reference. */
        inline constexpr std::uint32_t Song = 0x00000009u;

        /** @brief `Microsoft::Xna::Framework::Media::Video`. Implemented, schema version 1; metadata plus a streaming reference. */
        inline constexpr std::uint32_t Video = 0x0000000Au;

        /**
         * @brief `Microsoft::Xna::Framework::Graphics::Effect`. Identifier reserved; **no schema, by
         *        design**.
         *
         * The only built-in identifier without one. CNA has many renderers, so a `.cnb` carrying a
         * single API's shader bytecode would be useless on the others; this waits for the FX/shader
         * pipeline and renderer abstraction to settle rather than for someone to find time.
         */
        inline constexpr std::uint32_t Effect = 0x0000000Bu;

        /** @brief Lowest identifier reserved for future CNA use. */
        inline constexpr std::uint32_t ReservedRangeFirst = 0x40000000u;

        /** @brief Lowest identifier available to game-defined asset types. */
        inline constexpr std::uint32_t CustomRangeFirst = 0x80000000u;
    }

    /**
     * @brief Mints the custom asset type identifier for a game-defined type name.
     *
     * The identifier is `FNV-1a-32(name) | 0x80000000`, i.e. 31 usable bits. Collisions are
     * therefore possible in principle; `CnbLoaderRegistry` refuses to register two different
     * names that produce the same identifier, and the optional `CMET` chunk carries the type name
     * so a loader can report a mismatch rather than silently decode the wrong asset.
     *
     * @param name UTF-8 type name, e.g. `"MyGame.Level"`. Must not be empty.
     * @return The custom asset type identifier for @p name.
     * @throws std::invalid_argument if @p name is empty.
     */
    [[nodiscard]] std::uint32_t CnbAssetTypeIdFromName(const std::string& name);

    /**
     * @brief Whether @p assetTypeId lies in the game-defined custom range.
     *
     * @param assetTypeId The identifier to classify.
     * @return True when the identifier is `>= CnbAssetTypeId::CustomRangeFirst`.
     */
    [[nodiscard]] constexpr bool IsCustomAssetTypeId(std::uint32_t assetTypeId)
    {
        return assetTypeId >= CnbAssetTypeId::CustomRangeFirst;
    }

    /**
     * @brief Renders an asset type identifier as a human-readable name for diagnostics.
     *
     * @param assetTypeId The identifier to render.
     * @return The built-in type's name, or a hexadecimal rendering for a custom/unknown one.
     */
    [[nodiscard]] std::string AssetTypeIdToString(std::uint32_t assetTypeId);

    /** @brief Chunk identifiers the container itself defines, independent of any asset schema. */
    namespace CnbContainerChunk
    {
        /**
         * @brief `CMET` -- the asset's canonical type name and its source content name.
         *
         * Optional for a built-in asset type, **required for a custom one**, whose canonical name
         * is checked against the registered one before dispatch. See CnbMetadata.
         */
        inline constexpr CnbChunkId Metadata = MakeChunkId('C', 'M', 'E', 'T');

        /** @brief `XREF` -- optional table of external assets this file refers to by logical name. */
        inline constexpr CnbChunkId ExternalReferences = MakeChunkId('X', 'R', 'E', 'F');
    }
}
