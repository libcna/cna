// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Cnb/CnbFormat.hpp"

#include <stdexcept>

#include "CNA/Content/Cnb/CnbByteReader.hpp"

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

    std::string CnbLogicalNameProblem(std::string_view logicalName)
    {
        // The order matters only for which message a caller sees first; every one of these is a
        // refusal. Kept as one function so a reader, a writer and a media codec cannot drift apart
        // about what a legal reference looks like (plans/plan_cnb.md CNBF-115).
        if (logicalName.empty()) { return "is empty"; }
        if (!CnbByteReader::IsWellFormedUtf8(logicalName))
        {
            return "is not well-formed UTF-8";
        }
        if (logicalName.find('\\') != std::string_view::npos)
        {
            return "contains a backslash; .cnb logical names use '/' only";
        }
        if (logicalName.front() == '/') { return "is an absolute path"; }
        if (logicalName.size() >= 2u && logicalName[1] == ':')
        {
            return "is a drive-qualified absolute path";
        }
        for (std::size_t start = 0u; start <= logicalName.size();)
        {
            const std::size_t slash = logicalName.find('/', start);
            const std::size_t end = slash == std::string_view::npos ? logicalName.size() : slash;
            if (logicalName.substr(start, end - start) == "..")
            {
                return "contains a '..' segment";
            }
            if (slash == std::string_view::npos) { break; }
            start = slash + 1u;
        }
        return {};
    }

}
