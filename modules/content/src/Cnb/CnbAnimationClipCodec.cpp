// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"

#include <cmath>
#include <limits>

#include "CNA/Content/Cnb/CnbArithmetic.hpp"
#include "CNA/Content/Cnb/CnbByteReader.hpp"
#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;
using Microsoft::Xna::Framework::Graphics::BoneTrackEXT;
using Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT;
using Microsoft::Xna::Framework::Graphics::KeyframeEXT;

namespace CNA::Content::Cnb
{
    namespace
    {
        /// Frozen by the file format, same reasoning as the Curve enumerators.
        constexpr std::uint32_t kMaxClipTargetSpace = 1u; // JointPalette..SceneNode

        /// `System::TimeSpan::FromSeconds` throws System::ArgumentException/OverflowException for a
        /// NaN or out-of-range value. Those are perfectly good exceptions but they are not what the
        /// content subsystem's callers catch, and a malformed file must surface as a
        /// ContentLoadException naming the file. So the range is checked here, before the value can
        /// reach TimeSpan at all.
        constexpr double kTicksPerSecond = 10'000'000.0;
        const double kMaxRepresentableSeconds =
            static_cast<double>(std::numeric_limits<std::int64_t>::max()) / kTicksPerSecond;

    }

    double ReadCnbSeconds(CnbByteReader& reader, const char* what)
    {
        const double seconds = reader.ReadF64();
        if (!std::isfinite(seconds) || seconds < -kMaxRepresentableSeconds ||
            seconds > kMaxRepresentableSeconds)
        {
            reader.Fail(std::string(what) + " is " + std::to_string(seconds) +
                        " seconds, which is not representable as a System::TimeSpan.");
        }
        return seconds;
    }

    void WriteCnbKeyframe(CnbByteWriter& writer, const KeyframeEXT& key)
    {
        writer.WriteF64(key.Time.getTotalSecondsProperty());
        writer.WriteF32(key.Translation.X);
        writer.WriteF32(key.Translation.Y);
        writer.WriteF32(key.Translation.Z);
        writer.WriteF32(key.Rotation.X);
        writer.WriteF32(key.Rotation.Y);
        writer.WriteF32(key.Rotation.Z);
        writer.WriteF32(key.Rotation.W);
        writer.WriteF32(key.Scale.X);
        writer.WriteF32(key.Scale.Y);
        writer.WriteF32(key.Scale.Z);
    }

    KeyframeEXT ReadCnbKeyframe(CnbByteReader& reader)
    {
        KeyframeEXT key;
        key.Time = System::TimeSpan::FromSeconds(ReadCnbSeconds(reader, "a keyframe time"));
        // Each component into its own named local before the constructor call: C++ does not
        // sequence a call's arguments, and the .clip.bin reader this mirrors was once bitten by
        // exactly that (a keyframe's rotation read back scrambled under right-to-left evaluation).
        const float tx = reader.ReadF32();
        const float ty = reader.ReadF32();
        const float tz = reader.ReadF32();
        key.Translation = Vector3(tx, ty, tz);
        const float qx = reader.ReadF32();
        const float qy = reader.ReadF32();
        const float qz = reader.ReadF32();
        const float qw = reader.ReadF32();
        key.Rotation = Quaternion(qx, qy, qz, qw);
        const float sx = reader.ReadF32();
        const float sy = reader.ReadF32();
        const float sz = reader.ReadF32();
        key.Scale = Vector3(sx, sy, sz);
        return key;
    }

    std::vector<std::uint8_t> EncodeAnimationClipToCnb(const AnimationClipEXT& clip,
                                                        const std::string& contentName)
    {
        const std::size_t trackCount = clip.Tracks.size();
        std::uint64_t totalKeys = 0;
        for (const BoneTrackEXT& track : clip.Tracks)
        {
            totalKeys += track.Keys.size();
        }
        const auto maxU32 = static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max());
        if (trackCount > maxU32 || totalKeys > maxU32)
        {
            throw ContentLoadException("CNB AnimationClip: track or key count is not representable.");
        }

        CnbByteWriter header;
        header.WriteF64(clip.Duration.getTotalSecondsProperty());
        header.WriteU32(static_cast<std::uint32_t>(clip.TargetSpace));
        header.WriteU32(static_cast<std::uint32_t>(trackCount));
        header.WriteU32(static_cast<std::uint32_t>(totalKeys));

        CnbByteWriter tracks;
        CnbByteWriter keys;
        std::uint32_t firstKey = 0;
        for (const BoneTrackEXT& track : clip.Tracks)
        {
            tracks.WriteI32(track.BoneIndex);
            tracks.WriteU32(firstKey);
            tracks.WriteU32(static_cast<std::uint32_t>(track.Keys.size()));
            firstKey += static_cast<std::uint32_t>(track.Keys.size());

            for (const KeyframeEXT& key : track.Keys) { WriteCnbKeyframe(keys, key); }
        }

        CnbWriter writer(CnbAssetTypeId::AnimationClip, CnbAnimationClipSchemaVersion);
        writer.SetMetadata("Microsoft.Xna.Framework.Graphics.AnimationClipEXT", contentName);
        writer.AddChunk(CnbAnimationClipChunk::Header, header.Take(), CnbChunkFlags::Mandatory, 8u);
        writer.AddChunk(CnbAnimationClipChunk::Tracks, tracks.Take(), CnbChunkFlags::Mandatory, 4u);
        writer.AddChunk(CnbAnimationClipChunk::Keys, keys.Take(), CnbChunkFlags::Mandatory, 8u);
        return writer.Build();
    }

    AnimationClipEXT DecodeAnimationClipFromCnb(const CnbDocument& document)
    {
        document.RequireAsset(CnbAssetTypeId::AnimationClip, CnbAnimationClipSchemaVersion);
        const CnbChunkId known[] = {CnbAnimationClipChunk::Header, CnbAnimationClipChunk::Tracks,
                                    CnbAnimationClipChunk::Keys};
        document.RequireMandatoryChunksUnderstood(known);

        CnbByteReader header =
            document.OpenChunk(document.RequireSingle(CnbAnimationClipChunk::Header));
        AnimationClipEXT clip;
        clip.Duration = System::TimeSpan::FromSeconds(ReadCnbSeconds(header, "the clip duration"));
        const std::uint32_t targetSpace = header.ReadU32();
        if (targetSpace > kMaxClipTargetSpace)
        {
            header.Fail("target space is " + std::to_string(targetSpace) +
                        ", which is not a ClipTargetSpaceEXT value (0-1).");
        }
        clip.TargetSpace = static_cast<ClipTargetSpaceEXT>(targetSpace);
        const std::uint32_t trackCount = header.ReadU32();
        const std::uint32_t totalKeyCount = header.ReadU32();
        header.RequireExhausted();

        if (trackCount > document.Limits().maxArrayElementCount ||
            totalKeyCount > document.Limits().maxArrayElementCount)
        {
            header.Fail("declares more tracks or keyframes than the configured limit allows.");
        }

        CnbByteReader trackReader =
            document.OpenChunk(document.RequireSingle(CnbAnimationClipChunk::Tracks));
        const std::uint64_t expectedTrackBytes =
            static_cast<std::uint64_t>(trackCount) * CnbAnimationTrackStride;
        if (trackReader.Size() != expectedTrackBytes)
        {
            trackReader.Fail("holds " + std::to_string(trackReader.Size()) +
                             " byte(s) but the header declares " + std::to_string(trackCount) +
                             " track(s), which need " + std::to_string(expectedTrackBytes) +
                             " byte(s).");
        }

        CnbByteReader keyReader =
            document.OpenChunk(document.RequireSingle(CnbAnimationClipChunk::Keys));
        const std::uint64_t expectedKeyBytes =
            static_cast<std::uint64_t>(totalKeyCount) * CnbAnimationKeyStride;
        if (keyReader.Size() != expectedKeyBytes)
        {
            keyReader.Fail("holds " + std::to_string(keyReader.Size()) +
                           " byte(s) but the header declares " + std::to_string(totalKeyCount) +
                           " keyframe(s), which need " + std::to_string(expectedKeyBytes) +
                           " byte(s).");
        }

        struct TrackRange
        {
            std::int32_t boneIndex = -1;
            std::uint32_t firstKey = 0;
            std::uint32_t keyCount = 0;
        };
        std::vector<TrackRange> ranges;
        ranges.reserve(trackCount);
        for (std::uint32_t t = 0; t < trackCount; ++t)
        {
            TrackRange range;
            range.boneIndex = trackReader.ReadI32();
            range.firstKey = trackReader.ReadU32();
            range.keyCount = trackReader.ReadU32();

            const std::uint64_t end = CheckedAdd(range.firstKey, range.keyCount,
                                                 trackReader.Context() + " track " +
                                                     std::to_string(t));
            if (end > totalKeyCount)
            {
                trackReader.Fail("track " + std::to_string(t) + " names keyframes [" +
                                 std::to_string(range.firstKey) + ", " + std::to_string(end) +
                                 ") but the clip only has " + std::to_string(totalKeyCount) + ".");
            }
            ranges.push_back(range);
        }
        trackReader.RequireExhausted();

        // Every keyframe is decoded once, in file order, into a flat array; the tracks then take
        // their slices out of it. A track range may legitimately overlap another's -- two tracks
        // sharing an identical key run is a valid, if unusual, encoding -- so nothing here assumes
        // the ranges partition the array.
        std::vector<KeyframeEXT> allKeys;
        allKeys.reserve(totalKeyCount);
        for (std::uint32_t k = 0; k < totalKeyCount; ++k)
        {
            allKeys.push_back(ReadCnbKeyframe(keyReader));
        }
        keyReader.RequireExhausted();

        clip.Tracks.reserve(trackCount);
        for (const TrackRange& range : ranges)
        {
            BoneTrackEXT track;
            track.BoneIndex = range.boneIndex;
            track.Keys.assign(allKeys.begin() + static_cast<std::ptrdiff_t>(range.firstKey),
                              allKeys.begin() + static_cast<std::ptrdiff_t>(range.firstKey) +
                                  static_cast<std::ptrdiff_t>(range.keyCount));
            clip.Tracks.push_back(std::move(track));
        }

        return clip;
    }
}
