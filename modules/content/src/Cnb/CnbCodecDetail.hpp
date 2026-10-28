// SPDX-License-Identifier: MS-PL
#pragma once
// What each .cnb codec's reading half (Cnb*Codec.cpp) shares with its writing half
// (Cnb*Encode.cpp). The two are separate translation units so that loading a .cnb asset does not
// link CnbWriter (docs/link-size-architecture.md); the rules both directions enforce live once,
// in the reading half, which every writer also links.

#include <cstdint>
#include <string>

#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelV2Codec.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"

namespace CNA::Content::Cnb::SoundEffectCodecDetail
{
    /** @brief The canonical .NET name a .cnb sound records. */
    inline constexpr const char* kCanonicalName = "Microsoft.Xna.Framework.Audio.SoundEffect";

    /** @brief Sample data alignment, matching the texture payloads (CNBF-108). */
    inline constexpr std::uint32_t kPayloadAlignment = 16u;

    /**
     * @brief The rules both directions share.
     * @param data The sound.
     * @param schemaVersion The schema the file declares, or the writer emits.
     * @param reader The decoding reader, so a diagnostic names the file; null when encoding.
     */
    void ValidateSound(const CnbSoundEffectData& data, std::uint32_t schemaVersion,
                       CnbByteReader* reader);
}

namespace CNA::Content::Cnb::TextureCodecDetail
{
    // The canonical .NET names recorded in CMET. For a built-in type these are diagnostic rather
    // than load-bearing (docs/cnb-format.md §7), but they are what cnb_info prints.
    /** @brief Texture2D's canonical name. */
    inline constexpr const char* kTexture2DName = "Microsoft.Xna.Framework.Graphics.Texture2D";
    /** @brief TextureCube's canonical name. */
    inline constexpr const char* kTextureCubeName = "Microsoft.Xna.Framework.Graphics.TextureCube";
    /** @brief Texture3D's canonical name. */
    inline constexpr const char* kTexture3DName = "Microsoft.Xna.Framework.Graphics.Texture3D";

    /// The shape rules an asset type imposes on the shared texture layout. Keeping them in one
    /// struct is what lets 2D, cube and 3D share a codec instead of three near-copies.
    struct TextureShape
    {
        /** @brief The asset type. */
        std::uint32_t assetTypeId;
        /** @brief Its canonical name. */
        const char* canonicalTypeName;
        /** @brief The name diagnostics use. */
        const char* label;
        /** @brief Faces a texture of this type has. */
        std::uint32_t requiredFaceCount;
        /** @brief Whether the depth must be 1. */
        bool requireDepthOne;
        /** @brief Whether a face must be square. */
        bool requireSquare;
    };

    /** @brief A Texture2D's shape. */
    inline constexpr TextureShape kTexture2D{CnbAssetTypeId::Texture2D, kTexture2DName,
                                             "Texture2D", 1u, true, false};
    /** @brief A TextureCube's shape. */
    inline constexpr TextureShape kTextureCube{CnbAssetTypeId::TextureCube, kTextureCubeName,
                                               "TextureCube", CnbTextureCubeFaceCount, true, true};
    /** @brief A Texture3D's shape. */
    inline constexpr TextureShape kTexture3D{CnbAssetTypeId::Texture3D, kTexture3DName,
                                             "Texture3D", 1u, false, false};

    /**
     * @brief Bytes of one mip level of one face in @p format.
     * @param data The texture.
     * @param format The representation's format.
     * @param level The mip level.
     * @return Its size.
     */
    std::uint64_t LevelBytes(const CnbTextureData& data, CnbTextureFormat format,
                             std::uint32_t level);
}

namespace CNA::Content::Cnb::SpriteFontCodecDetail
{
    /** @brief The canonical .NET name a .cnb font records. */
    inline constexpr const char* kCanonicalName = "Microsoft.Xna.Framework.Graphics.SpriteFont";
    /** @brief The name its diagnostics, and its embedded atlas's, use. */
    inline constexpr const char* kLabel = "SpriteFont";

    /**
     * @brief The invariants SpriteFont's own constructor requires, checked in both directions.
     * @param data The font.
     */
    void ValidateFont(const CnbSpriteFontData& data);
}

namespace CNA::Content::Cnb::MediaCodecDetail
{
    /** @brief The largest VideoSoundtrackType value (Music, Dialog, MusicAndDialog). */
    inline constexpr std::uint32_t kMaxSoundtrackType = 2u;

    /// Largest duration either schema may carry, in milliseconds (`CNBF-119`).
    ///
    /// The wire field is a `u32` and `Song`'s and `Video`'s constructors both take an `intcs`, so
    /// a duration above `INT32_MAX` is not a long song -- it is a negative one once it reaches the
    /// runtime, and every `Duration`/`PlayPosition` comparison downstream then reads backwards.
    /// Bounded at both boundaries, because a file arrives from somewhere else as often as it is
    /// produced here. INT32_MAX ms is about 24.8 days, so nothing real is refused.
    inline constexpr std::uint32_t kMaxDurationMs = 0x7FFFFFFFu;

    /**
     * @brief Throws the codec's ContentLoadException.
     * @param label "Song" or "Video".
     * @param what What is wrong.
     */
    [[noreturn]] void Fail(const char* label, const std::string& what);
}

// The model codec (schema 1): its writer is CnbModelCodec.cpp, its reader CnbModelDecode.cpp.
namespace CNA::Content::Cnb::ModelCodecDetail
{
    inline constexpr std::uint32_t kHeaderFlagHasBoneHierarchy = 1u << 0;
    inline constexpr std::uint32_t kHeaderFlagGltfLightingPolicy = 1u << 1;
    inline constexpr std::uint32_t kKnownHeaderFlags =
        kHeaderFlagHasBoneHierarchy | kHeaderFlagGltfLightingPolicy;
    inline constexpr std::uint32_t kPartFlagVertexColorEnabled = 1u << 0;
    inline constexpr std::uint32_t kPartFlagUnlit = 1u << 1;
    inline constexpr std::uint32_t kMaterialFlagDoubleSided = 1u << 0;
    inline constexpr std::uint32_t kSkeletonFlagHasRootPrefix = 1u << 0;
    inline constexpr std::uint32_t kMorphFlagRecomputeFlatNormals = 1u << 0;
    inline constexpr std::uint32_t kMorphTargetHasPositions = 1u << 0;
    inline constexpr std::uint32_t kMorphTargetHasNormals = 1u << 1;
    inline constexpr std::uint32_t kMorphTargetHasTangents = 1u << 2;
    inline constexpr std::uint32_t kMorphTrackStepInterpolation = 1u << 0;
    inline constexpr std::uint32_t kMorphTrackCubicSpline = 1u << 1;

    /// Every vertex layout CNA's importer produces is far below this; the bound exists so a
    /// corrupt stride cannot turn into an enormous per-vertex allocation before anything else
    /// gets a chance to reject it.
    inline constexpr std::uint32_t kMaxVertexStride = 4096u;

    /// Schema-level ceilings on the two counts whose in-memory footprint is many times their
    /// encoded size. The container's generic array limit alone would let a 64 MB chunk of
    /// four-byte presence words expand into a gigabyte of CnbMorphTarget objects, which is
    /// technically bounded and still not something a content loader should attempt. Both
    /// numbers match the ceilings ContentManager's own .cnj morph/skeleton readers already
    /// apply, so nothing a .cnj can express is refused here.
    inline constexpr std::uint32_t kMaxMorphTargets = 100000u;
    inline constexpr std::uint32_t kMaxMorphWeightKeys = 1000000u;

    /// The smallest number of bytes one morph weight key can occupy: an f64 time plus three
    /// zero-length stream counts. Passing the true minimum to ReadCount is what makes its fit
    /// check meaningful for a variable-length record.
    inline constexpr std::uint32_t kMinMorphWeightKeyBytes = 20u;

    /**
     * @brief The primitive count a topology and index count imply.
     * @param topology The part's primitive topology.
     * @param indexCount Its index count.
     * @return The primitive count.
     */
    [[nodiscard]] std::uint64_t DerivePrimitiveCount(std::uint32_t topology,
                                                     std::uint64_t indexCount);
}

// The model codec (schema 2): its writer is CnbModelV2Codec.cpp, its reader CnbModelV2Decode.cpp.
namespace CNA::Content::Cnb::ModelV2CodecDetail
{
    /**
     * @brief Throws the codec's ContentLoadException.
     * @param detail What is wrong.
     */
    [[noreturn]] void Fail(const std::string& detail);

    /**
     * @brief The model invariants both directions enforce.
     * @param model The model.
     */
    void ValidateModel(const CnbModelV2Data& model);
}
