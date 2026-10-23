// SPDX-License-Identifier: MS-PL
#pragma once
// What the built-in content loaders (Content*Loaders.cpp) share, defined in
// BuiltinContentLoaders.cpp. Private to the content module's sources.
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Internal/CnjEnvelope.hpp"
#include "CNA/Internal/Json.hpp"
#include "CNA/Internal/PathContainment.hpp"
#include "CNA/Internal/Xnb/CurveContentTypeReader.hpp"
#include "CNA/Internal/Xnb/EffectContentTypeReader.hpp"
#include "CNA/Internal/Xnb/EffectMaterialContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/ModelContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/SongContentTypeReader.hpp"
#include "CNA/Internal/Xnb/SoundEffectContentTypeReader.hpp"
#include "CNA/Internal/Xnb/SpriteFontContentTypeReader.hpp"
#include "CNA/Internal/Xnb/StockEffectContentTypeReaders.hpp"
#include "CNA/Internal/Xnb/Texture2DContentTypeReader.hpp"
#include "CNA/Internal/Xnb/Texture3DContentTypeReader.hpp"
#include "CNA/Internal/Xnb/TextureCubeContentTypeReader.hpp"
#include "CNA/Internal/Xnb/VideoContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics { class Texture2D; }

namespace Microsoft::Xna::Framework::Content::detail
{
    /** @brief The path a sidecar field names, refused when it escapes its root. */
    std::string RequireContainedSidecarPath(const CNA::Internal::ContainedPathResult& result,
                                            const std::string& manifestPath, const char* field);
    /** @brief A sidecar path relative to the content root. */
    std::string ResolveRootRelativeSidecarPath(const ContentManager& cm,
                                               const std::string& manifestPath,
                                               const char* field,
                                               const std::string& relativePath);
    /** @brief A sidecar path relative to the manifest that names it. */
    std::string ResolveManifestRelativeSidecarPath(const ContentManager& cm,
                                                   const std::string& manifestPath,
                                                   const char* field,
                                                   const std::string& relativePath);
    /** @brief The logical asset name of a resolved path under a content root. */
    std::string ToContentManagerAssetName(const std::string& root,
                                          const std::string& resolvedPath);
    /** @brief A content-root-relative field as a logical asset name. */
    std::string ResolveRootRelativeAssetName(const ContentManager& cm,
                                             const std::string& manifestPath,
                                             const char* field,
                                             const std::string& relativePath);
    /** @brief A compiled reference of @p document as named below this manager's root. */
    std::string CnbReferenceNameEXT(const CNA::Content::Cnb::CnbDocument& document,
                                    const std::string& assetName, const std::string& reference);
    /** @brief Refuses a "sourceFile" on a .cnj whose type is self-contained. */
    void RejectSourceFileForSelfContainedCnj(const CNA::Internal::CnjEnvelope& envelope,
                                             const std::string& typeName,
                                             const std::string& path);
    /** @brief Makes every pixel of a colour transparent. */
    void ApplyColorKey(Graphics::Texture2D& texture, const std::array<std::uint8_t, 3>& colorKey);
    /** @brief A whole text file. */
    std::string ReadTextFile(const std::string& path);
    /** @brief A string field of a flat JSON object, or empty. */
    std::string ExtractJsonStringField(const std::string& json, const std::string& key);
    /** @brief A whole binary file. */
    std::vector<std::uint8_t> ReadBinaryFile(const std::string& path);
    /** @brief A fixed-length numeric array field; false (out untouched) when absent. */
    bool TryReadFloatArrayField(const CNA::Internal::JsonValue& obj, const char* field,
                                std::size_t expectedCount, std::vector<float>& out,
                                const std::string& path);
    /** @brief Whether this build can upload a .cnb texture representation. */
    bool CanUploadCnbTextureFormatEXT(CNA::Content::Cnb::CnbTextureFormat format);
    /** @brief The first .cnb texture representation this build can upload. */
    const CNA::Content::Cnb::CnbTextureRepresentation& RequireUploadableRepresentationEXT(
        const CNA::Content::Cnb::CnbTextureData& data, const std::string& assetName);
    /** @brief One level's RGBA bytes as Colors. */
    std::vector<Color> ColorsFromRgba8EXT(const std::vector<std::uint8_t>& rgba);
}
