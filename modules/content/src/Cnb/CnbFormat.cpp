// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbFormat.hpp"

#include <stdexcept>

namespace CNA::Content::Cnb
{
    namespace
    {
        [[nodiscard]] bool IsPrintableAscii(std::uint8_t byte)
        {
            return byte >= 0x20u && byte <= 0x7Eu;
        }

        [[nodiscard]] std::string ToHex32(std::uint32_t value)
        {
            static constexpr char kDigits[] = "0123456789ABCDEF";
            std::string out = "0x";
            for (int shift = 28; shift >= 0; shift -= 4)
            {
                out.push_back(kDigits[(value >> shift) & 0xFu]);
            }
            return out;
        }
    }

    std::string ChunkIdToString(CnbChunkId id)
    {
        std::string out;
        out.reserve(4);
        for (int shift = 0; shift < 32; shift += 8)
        {
            const auto byte = static_cast<std::uint8_t>((id.value >> shift) & 0xFFu);
            out.push_back(IsPrintableAscii(byte) ? static_cast<char>(byte) : '?');
        }
        return out;
    }

    bool IsWellFormedChunkId(CnbChunkId id)
    {
        for (int shift = 0; shift < 32; shift += 8)
        {
            if (!IsPrintableAscii(static_cast<std::uint8_t>((id.value >> shift) & 0xFFu)))
            {
                return false;
            }
        }
        return true;
    }

    std::uint32_t CnbAssetTypeIdFromName(const std::string& name)
    {
        if (name.empty())
        {
            throw std::invalid_argument("CnbAssetTypeIdFromName(): name must not be empty.");
        }

        // FNV-1a, 32-bit. Chosen for being tiny, dependency-free and fully specified, so the same
        // name mints the same identifier in the compiler, the runtime and any third-party tool.
        std::uint32_t hash = 2166136261u;
        for (const char c : name)
        {
            hash ^= static_cast<std::uint32_t>(static_cast<unsigned char>(c));
            hash *= 16777619u;
        }
        return hash | CnbAssetTypeId::CustomRangeFirst;
    }

    std::string AssetTypeIdToString(std::uint32_t assetTypeId)
    {
        switch (assetTypeId)
        {
            case CnbAssetTypeId::Invalid:       return "Invalid";
            case CnbAssetTypeId::Texture2D:     return "Texture2D";
            case CnbAssetTypeId::Texture3D:     return "Texture3D";
            case CnbAssetTypeId::TextureCube:   return "TextureCube";
            case CnbAssetTypeId::SpriteFont:    return "SpriteFont";
            case CnbAssetTypeId::Model:         return "Model";
            case CnbAssetTypeId::AnimationClip: return "AnimationClip";
            case CnbAssetTypeId::Curve:         return "Curve";
            case CnbAssetTypeId::SoundEffect:   return "SoundEffect";
            case CnbAssetTypeId::Song:          return "Song";
            case CnbAssetTypeId::Video:         return "Video";
            case CnbAssetTypeId::Effect:        return "Effect";
            default: break;
        }
        return (IsCustomAssetTypeId(assetTypeId) ? "custom type " : "unknown type ") +
               ToHex32(assetTypeId);
    }
}
