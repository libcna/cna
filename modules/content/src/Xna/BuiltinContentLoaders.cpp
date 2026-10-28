// SPDX-License-Identifier: MS-PL
// What the built-in content loaders share: reading files, resolving sidecar paths, .cnj checks and
// the .cnb texture representation choice. Declared in BuiltinContentLoaders.hpp.
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "CNA/Internal/CaseInsensitivePath.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/ContentRoot.hpp"
#include "CNA/Internal/TitlePath.hpp"
#include "CNA/Internal/GltfImport/CgltfFileCallbacks.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "System/IServiceProvider.hpp"
#include "CNA/Logger.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelV2Codec.hpp"
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Internal/CnjCanonicalRead.hpp"
#include "CNA/Internal/CnjEnvelope.hpp"
#include "CNA/Internal/CnjMorphSidecarEXT.hpp"
#include "CNA/Internal/CnjSourceFile.hpp"
#include "CNA/Internal/Graphics/ModelMaterialVariantsEXT.hpp"
#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Json.hpp"
#include "CNA/Internal/PathContainment.hpp"
#include "CNA/Internal/Xnb/XnbTypeReaderTable.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IGraphicsDeviceService.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectLights.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "System/IO/FileStream.hpp"
#include "System/IO/MemoryStream.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <limits>
#include <filesystem>
#include <optional>
#include <fstream>
#include <unordered_map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include "BuiltinContentLoaders.hpp"

namespace Microsoft::Xna::Framework::Content
{
    namespace detail
    {
        std::string RequireContainedSidecarPath(
            const CNA::Internal::ContainedPathResult& result,
            const std::string& manifestPath, const char* field)
        {
            if (!result.ok)
            {
                throw ContentLoadException(
                    "ContentManager: manifest '" + manifestPath + "' field '" + field +
                    "' must be a non-empty relative path contained within its authorized "
                    "content root or explicit external bundle.");
            }
            return result.resolvedPath;
        }

        std::string ResolveRootRelativeSidecarPath(
            const ContentManager& cm, const std::string& manifestPath,
            const char* field, const std::string& relativePath)
        {
            const std::string& root = cm.getRootDirectoryProperty();
            return RequireContainedSidecarPath(
                CNA::Internal::ResolveContainedPath(root, relativePath), manifestPath, field);
        }

        std::string ResolveManifestRelativeSidecarPath(
            const ContentManager& cm, const std::string& manifestPath,
            const char* field, const std::string& relativePath)
        {
            return RequireContainedSidecarPath(
                CNA::Internal::ResolveContainedPathRelativeToFile(
                    cm.getRootDirectoryProperty(), manifestPath, relativePath),
                manifestPath, field);
        }

        // ContentManager::Load() accepts a root-relative logical name and, by established
        // contract, an explicit absolute outside-root asset. Keep in-root cache identity in the
        // usual root-relative form; preserve an explicitly external bundle as an absolute path.
        std::string ToContentManagerAssetName(const std::string& root,
                                              const std::string& resolvedPath)
        {
            namespace fs = std::filesystem;
            // A cache key and a name that is handed back to Load<T>(), so both exits are
            // generic-form UTF-8 -- .string() would additionally have spelled the separators backslash
            // on Windows, which no other producer of this key does.
            const fs::path resolved = CNA::Internal::PathFromUtf8(resolvedPath).lexically_normal();
            if (!CNA::Internal::ValidateContainedPath(root, resolvedPath, false).ok)
            {
                return CNA::Internal::PathToGenericUtf8(resolved);
            }

            const fs::path rootPath =
                (root.empty() ? fs::path(".") : CNA::Internal::PathFromUtf8(root))
                    .lexically_normal();
            return CNA::Internal::PathToGenericUtf8(resolved.lexically_relative(rootPath));
        }

        std::string ResolveRootRelativeAssetName(
            const ContentManager& cm, const std::string& manifestPath,
            const char* field, const std::string& relativePath)
        {
            return ToContentManagerAssetName(
                cm.getRootDirectoryProperty(),
                ResolveRootRelativeSidecarPath(cm, manifestPath, field, relativePath));
        }

        // XNA resolves a reference from the referring asset's own directory
        // (ContentReader.GetPathToReference). A .cnb names it relative to the root it was compiled
        // against and records its own name there, so the name it is loaded by, minus that compiled
        // name, is where the build sits below this manager's root (CNA-DEMOS-ISSUE-020).
        std::string CnbReferenceNameEXT(const CNA::Content::Cnb::CnbDocument& document,
                                        const std::string& assetName,
                                        const std::string& reference)
        {
            const std::string& compiled = document.Metadata().contentName;
            if (compiled.empty() || reference.empty()) { return reference; }
            std::string loaded = assetName;
            std::replace(loaded.begin(), loaded.end(), '\\', '/');
            if (loaded.ends_with(".cnb") && !compiled.ends_with(".cnb"))
            {
                loaded.resize(loaded.size() - 4);
            }
            if (loaded.size() <= compiled.size() || !loaded.ends_with(compiled) ||
                loaded[loaded.size() - compiled.size() - 1] != '/')
            {
                return reference;
            }
            return loaded.substr(0, loaded.size() - compiled.size()) + reference;
        }

        // plans/plan_cnj.md CNB-34: SpriteFont/Effect/Model .cnj documents are self-contained
        // descriptors -- unlike Texture2D/SoundEffect/TextureCube, they have no meaning for a
        // "sourceFile" field. Reject it explicitly with a clear error instead of silently
        // ignoring it (the previous behavior) or letting some future field-parsing change
        // accidentally half-honor it.
        void RejectSourceFileForSelfContainedCnj(const CNA::Internal::CnjEnvelope& envelope,
                                                  const std::string& typeName, const std::string& path)
        {
            if (envelope.hasSourceFile)
            {
                throw ContentLoadException(
                    "ContentManager: " + typeName + " .cnj '" + path + "' has a 'sourceFile' "
                    "field, but " + typeName + " .cnj documents are self-contained and do not "
                    "support 'sourceFile'.");
            }
        }

        // Applies a colour key already read by CNA::Internal::ReadCnjColorKey
        // (CNBF-118). The reading of the member itself lives there, shared with the .cnj -> .cnb
        // compiler, so the two routes cannot disagree about which pixels a document keys out.
        void ApplyColorKey(Graphics::Texture2D& texture, const std::array<std::uint8_t, 3>& colorKey)
        {
            const int width = texture.getWidthProperty();
            const int height = texture.getHeightProperty();
            const int count = width * height;
            if (count <= 0) return;

            std::vector<Color> pixels(static_cast<std::size_t>(count), Color(0, 0, 0, 0));
            texture.GetData(pixels.data(), count);

            const auto keyR = static_cast<SharpRuntime::bytecs>(colorKey[0]);
            const auto keyG = static_cast<SharpRuntime::bytecs>(colorKey[1]);
            const auto keyB = static_cast<SharpRuntime::bytecs>(colorKey[2]);


            for (auto& pixel : pixels)
            {
                if (pixel.getRProperty() == keyR && pixel.getGProperty() == keyG &&
                    pixel.getBProperty() == keyB)
                {
                    pixel = Color(keyR, keyG, keyB, static_cast<SharpRuntime::bytecs>(0));
                }
            }

            texture.SetData(pixels.data(), count);
        }

        std::string ReadTextFile(const std::string& path)
        {
            std::ifstream file(CNA::Internal::PathFromUtf8(path));
            if (!file.is_open())
            {
                throw ContentLoadException("Cannot open file: " + path);
            }
            std::ostringstream ss;
            ss << file.rdbuf();
            return ss.str();
        }

        std::string ExtractJsonStringField(const std::string& json, const std::string& key)
        {
            const std::string needle = "\"" + key + "\"";
            auto pos = json.find(needle);
            if (pos == std::string::npos) return {};

            pos = json.find(':', pos + needle.size());
            if (pos == std::string::npos) return {};

            pos = json.find('"', pos + 1);
            if (pos == std::string::npos) return {};

            auto end = json.find('"', pos + 1);
            if (end == std::string::npos) return {};

            return json.substr(pos + 1, end - pos - 1);
        }

        std::vector<std::uint8_t> ReadBinaryFile(const std::string& path)
        {
            std::ifstream file(CNA::Internal::PathFromUtf8(path), std::ios::binary);
            if (!file.is_open())
                throw ContentLoadException("Cannot open binary file: " + path);
            return std::vector<std::uint8_t>(
                std::istreambuf_iterator<char>(file), {});
        }

        // Reads a fixed-length JSON numeric array field (e.g. "translation": [x,y,z]) from a
        // JSON object, or returns false leaving `out` untouched if the field is absent --
        // AnimationClipTypeReader's keyframes then keep KeyframeEXT's own default member
        // initializers (Rotation = Identity, Scale = {1,1,1}) instead of requiring every field
        // on every key.
        bool TryReadFloatArrayField(const CNA::Internal::JsonValue& obj, const char* field,
                                     std::size_t expectedCount, std::vector<float>& out,
                                     const std::string& path)
        {
            const CNA::Internal::JsonValue* v = obj.FindMember(field);
            if (v == nullptr) { return false; }
            if (v->type != CNA::Internal::JsonType::Array || v->arrayValue.size() != expectedCount)
            {
                throw ContentLoadException(
                    "AnimationClip .cnj '" + path + "': '" + field + "' must be a " +
                    std::to_string(expectedCount) + "-element numeric array.");
            }
            out.clear();
            out.reserve(expectedCount);
            for (const auto& element : v->arrayValue)
            {
                if (!element.IsNumber())
                {
                    throw ContentLoadException(
                        "AnimationClip .cnj '" + path + "': '" + field + "' has a non-number element.");
                }
                out.push_back(static_cast<float>(element.numberValue));
            }
            return true;
        }

        /// The formats this build can hand to Texture2D/TextureCube/Texture3D::SetData today.
        bool CanUploadCnbTextureFormatEXT(CNA::Content::Cnb::CnbTextureFormat format)
        {
            return format == CNA::Content::Cnb::CnbTextureFormat::Rgba8;
        }

        const CNA::Content::Cnb::CnbTextureRepresentation& RequireUploadableRepresentationEXT(
            const CNA::Content::Cnb::CnbTextureData& data, const std::string& assetName)
        {
            const std::size_t index = CNA::Content::Cnb::SelectCnbTextureRepresentation(
                data, CanUploadCnbTextureFormatEXT);
            if (index >= data.representations.size())
            {
                std::string offered;
                for (const auto& representation : data.representations)
                {
                    if (!offered.empty()) { offered += ", "; }
                    offered += CNA::Content::Cnb::CnbTextureFormatToString(representation.format);
                }
                throw ContentLoadException(
                    "The .cnb texture '" + assetName + "' offers only " + offered +
                    ", and this build can upload none of them. CNB texture schema 1 encodes Rgba8; "
                    "see docs/cnb-format.md §16.");
            }
            return data.representations[index];
        }

        /// Widens one level's R,G,B,A bytes into the Color array SetData takes. Written out rather
        /// than reinterpret_cast so it does not depend on Color's packed layout.
        std::vector<Color> ColorsFromRgba8EXT(const std::vector<std::uint8_t>& rgba)
        {
            std::vector<Color> colors(rgba.size() / 4u);
            for (std::size_t i = 0; i < colors.size(); ++i)
            {
                colors[i] = Color(rgba[i * 4u], rgba[i * 4u + 1u], rgba[i * 4u + 2u],
                                  rgba[i * 4u + 3u]);
            }
            return colors;
        }
    }
}
