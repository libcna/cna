// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "System/IServiceProvider.hpp"
#include "CNA/Logger.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/CnjEnvelope.hpp"
#include "CNA/Internal/CnjSourceFile.hpp"
#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Json.hpp"
#include "CNA/Internal/PathContainment.hpp"
#include "CNA/Internal/Xnb/XnbTypeReaderTable.hpp"
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
#include "System/IO/FileStream.hpp"
#include "System/IO/MemoryStream.hpp"
// Must match CMakeLists.txt's CNA_FFMPEG_AVAILABLE condition (MINGW OR EMSCRIPTEN OR ANDROID) --
// VideoDecoder.cpp/VideoPlayer.cpp/Video.cpp are excluded from the build on all three, so
// Video::Video() has no definition to link against on any of them, not just Emscripten/Android.
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(__MINGW32__) && !defined(__MINGW32__)
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace Microsoft::Xna::Framework::Content
{
    // ---------------------------------------------------------------------------
    // Property accessors
    // ---------------------------------------------------------------------------

    const std::string& ContentManager::getRootDirectoryProperty() const
    {
        return rootDirectory_;
    }

    void ContentManager::setRootDirectoryProperty(const std::string& v)
    {
        rootDirectory_ = v;
    }

    void ContentManager::setRootDirectoryProperty(std::string&& v)
    {
        rootDirectory_ = std::move(v);
    }

    // ---------------------------------------------------------------------------
    // Constructor / lifecycle
    // ---------------------------------------------------------------------------

    ContentManager::ContentManager(System::IServiceProvider* serviceProvider)
        : serviceProvider_(serviceProvider)
    {
        RegisterBuiltinLoaders();
    }

    ContentManager::ContentManager(System::IServiceProvider* serviceProvider,
                                   const std::string& rootDirectory)
        : serviceProvider_(serviceProvider), rootDirectory_(rootDirectory)
    {
        RegisterBuiltinLoaders();
    }

    ContentManager::ContentManager()
    {
        RegisterBuiltinLoaders();
    }

    System::IServiceProvider* ContentManager::getServiceProviderProperty() const
    {
        return serviceProvider_;
    }

    void ContentManager::setGraphicsDevice(Graphics::GraphicsDevice& graphicsDevice)
    {
        graphicsDevice_ = &graphicsDevice;
    }

    Graphics::GraphicsDevice& ContentManager::getGraphicsDeviceInternal() const
    {
        if (graphicsDevice_ == nullptr)
        {
            throw ContentLoadException(
                "ContentManager: GraphicsDevice is not set. "
                "Call setGraphicsDevice() before loading GPU resources.");
        }
        return *graphicsDevice_;
    }

    void ContentManager::Dispose()
    {
        if (!disposed_)
        {
            Unload();
            disposed_ = true;
        }
    }

    void ContentManager::Unload()
    {
        loadedAssets_.clear();
        textureCache_.clear();
    }

    // ---------------------------------------------------------------------------
    // Content manifest (plan_xnb.md Phase B3: XNB-65/65A/66/67/61a)
    // ---------------------------------------------------------------------------

    std::vector<std::string> ContentManager::ScanXnbReaderNames(const std::filesystem::path& xnbPath) const
    {
        std::vector<std::string> names;
        try
        {
            std::ifstream file(xnbPath, std::ios::binary);
            if (!file.is_open())
            {
                return names;
            }
            std::ostringstream ss;
            ss << file.rdbuf();
            const std::string bytes = ss.str();
            if (bytes.size() < 10)
            {
                return names;
            }

            System::IO::MemoryStream headerStream(
                reinterpret_cast<const uint8_t*>(bytes.data()), static_cast<int32_t>(bytes.size()));
            System::IO::BinaryReader headerReader(&headerStream, true);
            const auto header = CNA::Internal::Xnb::ParseXnbHeader(headerReader, xnbPath.string());

            if (header.compression != CNA::Internal::Xnb::XnbCompression::None)
            {
                // XNB-61b (compressed-file inventory) is unblocked now that Phase D's decompressor
                // exists, but not yet picked up -- an empty inventory for this one entry is not a
                // scan failure, matching the same "not implemented yet" treatment for every
                // compression scheme (Lzx included), not just the ones CNA can't decode at all.
                return names;
            }

            System::IO::MemoryStream bodyStream(
                reinterpret_cast<const uint8_t*>(bytes.data()) + 10,
                static_cast<int32_t>(bytes.size()) - 10);
            System::IO::BinaryReader bodyReader(&bodyStream, true);
            const auto table = CNA::Internal::Xnb::ParseXnbTypeReaderTable(bodyReader, xnbPath.string());
            names.reserve(table.size());
            for (const auto& entry : table)
            {
                names.push_back(entry.normalizedName);
            }
        }
        catch (...)
        {
            // Best-effort: a malformed .xnb elsewhere in the content root must not abort the
            // whole manifest scan -- just leave this one entry's inventory empty.
            names.clear();
        }
        return names;
    }

    void ContentManager::RefreshContentManifest()
    {
        namespace fs = std::filesystem;
        std::unordered_map<std::string, ContentManifestEntry> entriesByBase;

        std::error_code ec;
        if (!fs::exists(rootDirectory_, ec) || ec)
        {
            contentManifest_.clear();
            contentManifestBuilt_ = true;
            return;
        }

        auto it = fs::recursive_directory_iterator(
            rootDirectory_, fs::directory_options::skip_permission_denied, ec);
        const auto end = fs::recursive_directory_iterator();
        for (; !ec && it != end; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || ec)
            {
                continue;
            }

            const fs::path& path = it->path();
            fs::path relative = fs::relative(path, rootDirectory_, ec);
            if (ec)
            {
                continue;
            }

            const std::string ext = path.extension().string();
            std::string relStr = relative.generic_string();
            const std::string base = relStr.substr(0, relStr.size() - ext.size());

            ContentManifestEntry& entry = entriesByBase[base];
            entry.relativePath = base;
            if (ext == ".xnb")
            {
                entry.hasXnb = true;
                entry.xnbReaderNames = ScanXnbReaderNames(path);
            }
            else if (ext == ".cnj")
            {
                entry.hasCnj = true;
            }
            else
            {
                entry.nativeExtensions.push_back(ext);
            }
        }

        contentManifest_.clear();
        contentManifest_.reserve(entriesByBase.size());
        for (auto& [base, entry] : entriesByBase)
        {
            contentManifest_.push_back(std::move(entry));
        }
        contentManifestBuilt_ = true;
    }

    const std::vector<ContentManifestEntry>& ContentManager::GetContentManifest()
    {
        if (!contentManifestBuilt_)
        {
            RefreshContentManifest();
        }
        return contentManifest_;
    }

    std::vector<ContentManifestReaderUsage> ContentManager::GetXnbReaderUsageSummary()
    {
        const auto& manifest = GetContentManifest();

        std::unordered_map<std::string, int> counts;
        for (const auto& entry : manifest)
        {
            for (const auto& readerName : entry.xnbReaderNames)
            {
                ++counts[readerName];
            }
        }

        std::vector<ContentManifestReaderUsage> result;
        result.reserve(counts.size());
        for (const auto& [readerName, count] : counts)
        {
            ContentManifestReaderUsage usage;
            usage.readerName = readerName;
            usage.fileCount = count;
            usage.isRegistered = ContentTypeReaderManager::IsRegistered(readerName);
            result.push_back(std::move(usage));
        }
        return result;
    }

    // ---------------------------------------------------------------------------
    // Path helpers
    // ---------------------------------------------------------------------------

    std::string ContentManager::BuildAssetPath(const std::string& assetName) const
    {
        if (assetName.empty())
        {
            return rootDirectory_;
        }
        if (rootDirectory_.empty())
        {
            return assetName;
        }
        namespace fs = std::filesystem;
        return (fs::path(rootDirectory_) / assetName).string();
    }

    std::string ContentManager::NormalizeKey(const std::string& assetName) const
    {
        std::string key = assetName;
        std::replace(key.begin(), key.end(), '\\', '/');
        std::transform(key.begin(), key.end(), key.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return key;
    }

    // ---------------------------------------------------------------------------
    // Built-in type readers
    // ---------------------------------------------------------------------------

    namespace
    {
        // Forward declaration -- defined below, alongside the other minimal JSON helpers used by
        // .cnj/.font.json-style readers. Texture2DTypeReader needs it for the .cnj sidecar path
        // (plan_cnj.md CNB-8), ahead of where it's textually defined.
        std::string ReadTextFile(const std::string& path);

        // Forward declaration -- defined below (originally added for the skeleton/clip binary
        // sidecars). plan_cnj.md CNB-43: Texture3DTypeReader needs it ahead of where it's
        // textually defined, same reason as ReadTextFile above.
        std::vector<std::uint8_t> ReadBinaryFile(const std::string& path);

        // Forward declaration -- defined below (originally added for AnimationClipTypeReader).
        // plan_cnj.md CNB-45: EffectTypeReader's stock-effect branch reuses it for
        // diffuseColor/emissiveColor/specularColor/environmentMapSpecular fields rather than
        // duplicating a third array-parsing helper.
        bool TryReadFloatArrayField(const CNA::Internal::JsonValue& obj, const char* field,
                                     std::size_t expectedCount, std::vector<float>& out,
                                     const std::string& path);

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
            if (!CNA::Internal::ValidateContainedPath(root, resolvedPath, false).ok)
            {
                return fs::path(resolvedPath).lexically_normal().string();
            }

            const fs::path rootPath =
                (root.empty() ? fs::path(".") : fs::path(root)).lexically_normal();
            return fs::path(resolvedPath).lexically_normal()
                .lexically_relative(rootPath)
                .generic_string();
        }

        std::string ResolveRootRelativeAssetName(
            const ContentManager& cm, const std::string& manifestPath,
            const char* field, const std::string& relativePath)
        {
            return ToContentManagerAssetName(
                cm.getRootDirectoryProperty(),
                ResolveRootRelativeSidecarPath(cm, manifestPath, field, relativePath));
        }

        // plan_cnj.md CNB-34: SpriteFont/Effect/Model .cnj documents are self-contained
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

        // Minimal "colorKey": [r, g, b] extractor for a Texture2D .cnj sidecar (CNB-8). Kept
        // local/self-contained rather than reusing JsonIntArray4 (a 4-element parser) below, since
        // colorKey is always exactly 3 components and duplicating this ~10-line scan is cheaper and
        // safer than coaxing a 4-element parser into stopping after 3.
        bool TryParseColorKeyRGB(const std::string& json, std::array<int, 3>& outRgb)
        {
            const std::string needle = "\"colorKey\"";
            auto pos = json.find(needle);
            if (pos == std::string::npos) return false;
            pos = json.find('[', pos + needle.size());
            if (pos == std::string::npos) return false;

            ++pos;
            for (int i = 0; i < 3; ++i)
            {
                while (pos < json.size() &&
                       !std::isdigit(static_cast<unsigned char>(json[pos])) && json[pos] != '-') ++pos;
                if (pos >= json.size()) return false;
                outRgb[static_cast<std::size_t>(i)] = std::stoi(json.substr(pos));
                while (pos < json.size() && json[pos] != ',' && json[pos] != ']') ++pos;
                if (pos < json.size() && json[pos] == ',') ++pos;
            }
            return true;
        }

        void ApplyColorKey(Graphics::Texture2D& texture, const std::array<int, 3>& colorKey)
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

        class Texture2DTypeReader : public LooseFileContentTypeReader<Graphics::Texture2D>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tga", ".tif", ".tiff", ".qoi"};
            }

            Graphics::Texture2D Read(const std::string& path, ContentManager& cm) override
            {
                Graphics::GraphicsDevice& gd = cm.getGraphicsDeviceInternal();

                if (std::filesystem::path(path).extension() == ".cnj")
                {
                    return ReadCnj(path, cm);
                }

                return Graphics::Texture2D(path, gd);
            }

        private:
            static Graphics::Texture2D ReadCnj(const std::string& path, ContentManager& cm)
            {
                const std::string json = ReadTextFile(path);
                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
                CNA::Internal::ValidateCnjEnvelope(envelope, "Texture2D", path);

                if (!envelope.hasSourceFile)
                {
                    throw ContentLoadException(
                        "ContentManager: Texture2D .cnj '" + path + "' has no 'sourceFile' field "
                        "(a self-contained, non-sourceFile Texture2D .cnj is not supported).");
                }

                const CNA::Internal::CnjSourceFileResult resolved =
                    CNA::Internal::ResolveCnjSourceFileSafely(
                        path, cm.getRootDirectoryProperty(), envelope.sourceFile);
                Graphics::Texture2D result = cm.Load<Graphics::Texture2D>(resolved.logicalName);

                std::array<int, 3> colorKey{};
                if (TryParseColorKeyRGB(json, colorKey))
                {
                    ApplyColorKey(result, colorKey);
                }

                return result;
            }
        };

        class TextureCubeTypeReader : public LooseFileContentTypeReader<Graphics::TextureCube>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".dds"};
            }

            Graphics::TextureCube Read(const std::string& path, ContentManager& cm) override
            {
                if (std::filesystem::path(path).extension() == ".cnj")
                {
                    return ReadCnj(path, cm);
                }

                Graphics::GraphicsDevice& gd = cm.getGraphicsDeviceInternal();
                System::IO::FileStream stream(path);
                return Graphics::TextureCube::DDSFromStreamEXT(gd, stream);
            }

        private:
            static Graphics::TextureCube ReadCnj(const std::string& path, ContentManager& cm)
            {
                const std::string json = ReadTextFile(path);
                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
                CNA::Internal::ValidateCnjEnvelope(envelope, "TextureCube", path);

                if (!envelope.hasSourceFile)
                {
                    throw ContentLoadException(
                        "ContentManager: TextureCube .cnj '" + path + "' has no 'sourceFile' "
                        "field (a self-contained, non-sourceFile TextureCube .cnj is not "
                        "supported).");
                }

                const CNA::Internal::CnjSourceFileResult resolved =
                    CNA::Internal::ResolveCnjSourceFileSafely(
                        path, cm.getRootDirectoryProperty(), envelope.sourceFile);
                return cm.Load<Graphics::TextureCube>(resolved.logicalName);
            }
        };

        // plan_cnj.md CNB-43: no native "3D texture" file format exists in CNA the way .dds serves
        // TextureCube (Texture3D has no FromStream/DDSFromStream equivalent) -- so unlike
        // TextureCubeTypeReader, this is self-contained JSON + a raw binary pixel-data sidecar
        // (mirrors Model's own vertex/index binary-sidecar convention), not a sourceFile delegation.
        // Single mip level only; deliberately skips the .xnb Texture3DReader's mip-chain/DXT
        // handling -- no real sample content needs either (plan_cnj.md CNB-43's own survey), and
        // .cnj content is hand-authored, so pre-compressed DXT data has no natural place to come
        // from here the way it does inside a real XNA content-pipeline build.
        class Texture3DTypeReader : public LooseFileContentTypeReader<std::shared_ptr<Graphics::Texture3D>>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".cnj"};
            }

            // Targets std::shared_ptr<Texture3D>, not a bare Texture3D: Texture3D is move-only
            // (copy deleted), and ContentManager's generic asset cache stores results in a
            // std::any, which requires CopyConstructible -- the same reason the .xnb-side
            // Texture3DReader/StockEffectContentTypeReaders already return shared_ptr instead of
            // a bare value.
            std::shared_ptr<Graphics::Texture3D> Read(const std::string& path, ContentManager& cm) override
            {
                using CNA::Internal::JsonValue;

                const std::string json = ReadTextFile(path);
                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
                CNA::Internal::ValidateCnjEnvelope(envelope, "Texture3D", path);
                RejectSourceFileForSelfContainedCnj(envelope, "Texture3D", path);

                const JsonValue root = CNA::Internal::ParseJson(json);
                const JsonValue* widthField  = root.FindMember("width");
                const JsonValue* heightField = root.FindMember("height");
                const JsonValue* depthField  = root.FindMember("depth");
                const JsonValue* dataField   = root.FindMember("data");

                if (widthField == nullptr || !widthField->IsNumber() ||
                    heightField == nullptr || !heightField->IsNumber() ||
                    depthField == nullptr || !depthField->IsNumber())
                {
                    throw ContentLoadException(
                        "Texture3D .cnj '" + path + "' is missing a numeric 'width'/'height'/'depth' field.");
                }
                if (dataField == nullptr || !dataField->IsString() || dataField->stringValue.empty())
                {
                    throw ContentLoadException(
                        "Texture3D .cnj '" + path + "' is missing a non-empty 'data' field naming a raw pixel sidecar.");
                }

                const int width  = static_cast<int>(widthField->numberValue);
                const int height = static_cast<int>(heightField->numberValue);
                const int depth  = static_cast<int>(depthField->numberValue);
                if (width <= 0 || height <= 0 || depth <= 0)
                {
                    throw ContentLoadException(
                        "Texture3D .cnj '" + path + "' has a non-positive 'width'/'height'/'depth'.");
                }

                const std::string dataPath = ResolveRootRelativeSidecarPath(
                    cm, path, "data", dataField->stringValue);
                const auto bytes = ReadBinaryFile(dataPath);
                const std::size_t expectedBytes =
                    static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                    static_cast<std::size_t>(depth) * 4;
                if (bytes.size() != expectedBytes)
                {
                    throw ContentLoadException(
                        "Texture3D .cnj '" + path + "': 'data' sidecar has " + std::to_string(bytes.size()) +
                        " bytes, expected " + std::to_string(expectedBytes) + " for " +
                        std::to_string(width) + "x" + std::to_string(height) + "x" + std::to_string(depth) + ".");
                }

                std::vector<Color> colors;
                colors.reserve(expectedBytes / 4);
                for (std::size_t i = 0; i < expectedBytes; i += 4)
                {
                    colors.emplace_back(bytes[i], bytes[i + 1], bytes[i + 2], bytes[i + 3]);
                }

                auto texture = std::make_shared<Graphics::Texture3D>(
                    cm.getGraphicsDeviceInternal(), width, height, depth, false, Graphics::SurfaceFormat::Color);
                texture->SetData(colors.data(), static_cast<int>(colors.size()));
                return texture;
            }
        };

        class SoundEffectTypeReader : public LooseFileContentTypeReader<Audio::SoundEffect>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".wav"};
            }

            Audio::SoundEffect Read(const std::string& path, ContentManager& cm) override
            {
                if (std::filesystem::path(path).extension() == ".cnj")
                {
                    return ReadCnj(path, cm);
                }

                return Audio::SoundEffect(path);
            }

        private:
            static Audio::SoundEffect ReadCnj(const std::string& path, ContentManager& cm)
            {
                const std::string json = ReadTextFile(path);
                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
                CNA::Internal::ValidateCnjEnvelope(envelope, "SoundEffect", path);

                if (!envelope.hasSourceFile)
                {
                    throw ContentLoadException(
                        "ContentManager: SoundEffect .cnj '" + path + "' has no 'sourceFile' "
                        "field (a self-contained, non-sourceFile SoundEffect .cnj is not "
                        "supported).");
                }

                const CNA::Internal::CnjSourceFileResult resolved =
                    CNA::Internal::ResolveCnjSourceFileSafely(
                        path, cm.getRootDirectoryProperty(), envelope.sourceFile);
                return cm.Load<Audio::SoundEffect>(resolved.logicalName);
            }
        };

        // ---------------------------------------------------------------------------

        std::string ReadTextFile(const std::string& path)
        {
            std::ifstream file(path);
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

        Microsoft::Xna::Framework::Graphics::CompareFunction ParseCompareFunctionEXT(
            const std::string& value, const std::string& path)
        {
            using Microsoft::Xna::Framework::Graphics::CompareFunction;
            if (value == "Always") { return CompareFunction::Always; }
            if (value == "Never") { return CompareFunction::Never; }
            if (value == "Less") { return CompareFunction::Less; }
            if (value == "LessEqual") { return CompareFunction::LessEqual; }
            if (value == "Equal") { return CompareFunction::Equal; }
            if (value == "GreaterEqual") { return CompareFunction::GreaterEqual; }
            if (value == "Greater") { return CompareFunction::Greater; }
            if (value == "NotEqual") { return CompareFunction::NotEqual; }
            throw ContentLoadException(
                "Effect .cnj '" + path + "' has an unrecognized CompareFunction '" + value + "'.");
        }

        Vector3 ReadVector3FieldEXT(const CNA::Internal::JsonValue& obj, const char* field,
                                     Vector3 def, const std::string& path)
        {
            std::vector<float> arr;
            if (!TryReadFloatArrayField(obj, field, 3, arr, path)) { return def; }
            return Vector3(arr[0], arr[1], arr[2]);
        }

        float ReadFloatFieldEXT(const CNA::Internal::JsonValue& obj, const char* field,
                                 float def, const std::string& path)
        {
            const CNA::Internal::JsonValue* v = obj.FindMember(field);
            if (v == nullptr) { return def; }
            if (!v->IsNumber())
            {
                throw ContentLoadException(
                    "Effect .cnj '" + path + "': '" + field + "' must be numeric.");
            }
            return static_cast<float>(v->numberValue);
        }

        bool ReadBoolFieldEXT(const CNA::Internal::JsonValue& obj, const char* field,
                               bool def, const std::string& path)
        {
            const CNA::Internal::JsonValue* v = obj.FindMember(field);
            if (v == nullptr) { return def; }
            if (v->type != CNA::Internal::JsonType::Boolean)
            {
                throw ContentLoadException(
                    "Effect .cnj '" + path + "': '" + field + "' must be a boolean.");
            }
            return v->boolValue;
        }

        // plan_cnj.md CNB-45: RegisterTypeReader<T>() allows exactly one reader per T, and this
        // class already owns std::shared_ptr<Effect> for the pre-existing custom-GLSL "Effect"
        // .cnj shape -- so the 5 stock effects (BasicEffect/AlphaTestEffect/DualTextureEffect/
        // EnvironmentMapEffect/SkinnedEffect) are dispatched from inside this same reader by
        // .cnj "type", rather than each getting its own registration (which would collide).
        // Field lists below are a direct JSON port of StockEffectContentTypeReaders.cpp's
        // already-FNA-verified per-effect field order.
        class EffectTypeReader : public LooseFileContentTypeReader<std::shared_ptr<Graphics::Effect>>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".cnj"};
            }

            std::shared_ptr<Graphics::Effect> Read(const std::string& path, ContentManager& cm) override
            {
                // If path doesn't already end with .cnj, append it.
                std::string jsonPath = path;
                const std::string ext = ".cnj";
                if (jsonPath.size() < ext.size() ||
                    jsonPath.substr(jsonPath.size() - ext.size()) != ext)
                {
                    jsonPath += ext;
                }

                const std::string jsonText = ReadTextFile(jsonPath);

                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(jsonText);
                CNA::Internal::ValidateCnjEnvelopeBaseline(envelope, jsonPath);

                // Confirm envelope.type is one of the 6 valid names *before* any other check --
                // otherwise a .cnj with both an unrecognized type and a 'sourceFile' field gets
                // the misleading "sourceFile not supported" message instead of the real problem
                // (an unrecognized 'type'). Found by adversarial review, 2026-07-17.
                const bool isStockEffect =
                    envelope.type == "BasicEffect" || envelope.type == "AlphaTestEffect" ||
                    envelope.type == "DualTextureEffect" || envelope.type == "EnvironmentMapEffect" ||
                    envelope.type == "SkinnedEffect";
                if (envelope.type != "Effect" && !isStockEffect)
                {
                    throw ContentLoadException(
                        "ContentManager: '" + jsonPath + "' has type '" + envelope.type + "', but an "
                        "Effect .cnj must be one of 'Effect', 'BasicEffect', 'AlphaTestEffect', "
                        "'DualTextureEffect', 'EnvironmentMapEffect', or 'SkinnedEffect'.");
                }

                RejectSourceFileForSelfContainedCnj(envelope, envelope.type, jsonPath);

                if (envelope.type == "Effect")
                {
                    return ReadCustomGlslEffect(jsonText, jsonPath, cm);
                }
                return ReadStockEffect(CNA::Internal::ParseJson(jsonText), envelope.type, jsonPath, cm);
            }

        private:
            static std::shared_ptr<Graphics::Effect> ReadCustomGlslEffect(
                const std::string& jsonText, const std::string& jsonPath, ContentManager& cm)
            {
                const std::string vertRel = ExtractJsonStringField(jsonText, "vertex");
                const std::string fragRel = ExtractJsonStringField(jsonText, "fragment");

                if (vertRel.empty() || fragRel.empty())
                {
                    throw ContentLoadException(
                        "ShaderEffect descriptor missing 'vertex' or 'fragment' field: " + jsonPath);
                }

                const std::string vertPath = ResolveRootRelativeSidecarPath(
                    cm, jsonPath, "vertex", vertRel);
                const std::string fragPath = ResolveRootRelativeSidecarPath(
                    cm, jsonPath, "fragment", fragRel);

                return std::make_shared<Graphics::ShaderEffect>(
                    cm.getGraphicsDeviceInternal(),
                    ReadTextFile(vertPath),
                    ReadTextFile(fragPath));
            }

            static std::optional<Graphics::Texture2D> LoadOptionalTexture2D(
                const CNA::Internal::JsonValue& root, const char* field,
                const std::string& jsonPath, ContentManager& cm)
            {
                const CNA::Internal::JsonValue* v = root.FindMember(field);
                if (v == nullptr) { return std::nullopt; }
                if (!v->IsString() || v->stringValue.empty())
                {
                    throw ContentLoadException(std::string("Effect .cnj: '") + field + "' must be a non-empty string.");
                }
                return cm.Load<Graphics::Texture2D>(ResolveRootRelativeAssetName(
                    cm, jsonPath, field, v->stringValue));
            }

            static std::shared_ptr<Graphics::Effect> ReadStockEffect(
                const CNA::Internal::JsonValue& root, const std::string& type,
                const std::string& jsonPath, ContentManager& cm)
            {
                using namespace Graphics;
                GraphicsDevice& gd = cm.getGraphicsDeviceInternal();

                if (type == "BasicEffect")
                {
                    auto effect = std::make_shared<BasicEffect>(gd);
                    if (auto tex = LoadOptionalTexture2D(root, "texture", jsonPath, cm))
                    {
                        effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*tex)));
                        effect->setTextureEnabledProperty(true);
                    }
                    effect->setDiffuseColorProperty(ReadVector3FieldEXT(root, "diffuseColor", Vector3::One, jsonPath));
                    effect->setEmissiveColorProperty(ReadVector3FieldEXT(root, "emissiveColor", Vector3::Zero, jsonPath));
                    effect->setSpecularColorProperty(ReadVector3FieldEXT(root, "specularColor", Vector3::One, jsonPath));
                    effect->setSpecularPowerProperty(ReadFloatFieldEXT(root, "specularPower", 16.0f, jsonPath));
                    effect->setAlphaProperty(ReadFloatFieldEXT(root, "alpha", 1.0f, jsonPath));
                    effect->VertexColorEnabled = ReadBoolFieldEXT(root, "vertexColorEnabled", false, jsonPath);
                    return effect;
                }
                if (type == "AlphaTestEffect")
                {
                    auto effect = std::make_shared<AlphaTestEffect>(gd);
                    if (auto tex = LoadOptionalTexture2D(root, "texture", jsonPath, cm))
                    {
                        effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*tex)));
                    }
                    const CNA::Internal::JsonValue* alphaFn = root.FindMember("alphaFunction");
                    effect->setAlphaFunctionProperty(
                        alphaFn != nullptr && alphaFn->IsString()
                            ? ParseCompareFunctionEXT(alphaFn->stringValue, jsonPath)
                            : CompareFunction::Greater);
                    effect->setReferenceAlphaProperty(
                        static_cast<int32_t>(ReadFloatFieldEXT(root, "referenceAlpha", 0.0f, jsonPath)));
                    effect->setDiffuseColorProperty(ReadVector3FieldEXT(root, "diffuseColor", Vector3::One, jsonPath));
                    effect->setAlphaProperty(ReadFloatFieldEXT(root, "alpha", 1.0f, jsonPath));
                    effect->setVertexColorEnabledProperty(ReadBoolFieldEXT(root, "vertexColorEnabled", false, jsonPath));
                    return effect;
                }
                if (type == "DualTextureEffect")
                {
                    auto effect = std::make_shared<DualTextureEffect>(gd);
                    if (auto tex = LoadOptionalTexture2D(root, "texture", jsonPath, cm))
                    {
                        effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*tex)));
                    }
                    if (auto tex2 = LoadOptionalTexture2D(root, "texture2", jsonPath, cm))
                    {
                        effect->SetOwnedTexture2(std::make_shared<Texture2D>(std::move(*tex2)));
                    }
                    effect->setDiffuseColorProperty(ReadVector3FieldEXT(root, "diffuseColor", Vector3::One, jsonPath));
                    effect->setAlphaProperty(ReadFloatFieldEXT(root, "alpha", 1.0f, jsonPath));
                    effect->setVertexColorEnabledProperty(ReadBoolFieldEXT(root, "vertexColorEnabled", false, jsonPath));
                    return effect;
                }
                if (type == "EnvironmentMapEffect")
                {
                    auto effect = std::make_shared<EnvironmentMapEffect>(gd);
                    if (auto tex = LoadOptionalTexture2D(root, "texture", jsonPath, cm))
                    {
                        effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*tex)));
                    }
                    if (const CNA::Internal::JsonValue* envMap = root.FindMember("environmentMap"))
                    {
                        if (!envMap->IsString() || envMap->stringValue.empty())
                        {
                            throw ContentLoadException(
                                "Effect .cnj '" + jsonPath + "': 'environmentMap' must be a non-empty string.");
                        }
                        Graphics::TextureCube cube = cm.Load<Graphics::TextureCube>(
                            ResolveRootRelativeAssetName(
                                cm, jsonPath, "environmentMap", envMap->stringValue));
                        effect->SetOwnedEnvironmentMap(std::make_shared<TextureCube>(std::move(cube)));
                    }
                    effect->setEnvironmentMapAmountProperty(ReadFloatFieldEXT(root, "environmentMapAmount", 1.0f, jsonPath));
                    effect->setEnvironmentMapSpecularProperty(ReadVector3FieldEXT(root, "environmentMapSpecular", Vector3::Zero, jsonPath));
                    effect->setFresnelFactorProperty(ReadFloatFieldEXT(root, "fresnelFactor", 1.0f, jsonPath));
                    effect->setDiffuseColorProperty(ReadVector3FieldEXT(root, "diffuseColor", Vector3::One, jsonPath));
                    effect->setEmissiveColorProperty(ReadVector3FieldEXT(root, "emissiveColor", Vector3::Zero, jsonPath));
                    effect->setAlphaProperty(ReadFloatFieldEXT(root, "alpha", 1.0f, jsonPath));
                    return effect;
                }
                // SkinnedEffect (the only remaining branch this method is ever called with).
                auto effect = std::make_shared<SkinnedEffect>(gd);
                if (auto tex = LoadOptionalTexture2D(root, "texture", jsonPath, cm))
                {
                    effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*tex)));
                }
                effect->setWeightsPerVertexProperty(
                    static_cast<int32_t>(ReadFloatFieldEXT(root, "weightsPerVertex", 4.0f, jsonPath)));
                effect->setDiffuseColorProperty(ReadVector3FieldEXT(root, "diffuseColor", Vector3::One, jsonPath));
                effect->setEmissiveColorProperty(ReadVector3FieldEXT(root, "emissiveColor", Vector3::Zero, jsonPath));
                effect->setSpecularColorProperty(ReadVector3FieldEXT(root, "specularColor", Vector3::One, jsonPath));
                effect->setSpecularPowerProperty(ReadFloatFieldEXT(root, "specularPower", 16.0f, jsonPath));
                effect->setAlphaProperty(ReadFloatFieldEXT(root, "alpha", 1.0f, jsonPath));
                return effect;
            }
        };

        // ---------------------------------------------------------------------------
        // Minimal JSON helpers for .font.json parsing (no external library)
        // ---------------------------------------------------------------------------

        static int JsonInt(const std::string& j, const std::string& key, int def = 0)
        {
            const std::string needle = "\"" + key + "\"";
            auto pos = j.find(needle);
            if (pos == std::string::npos) return def;
            pos = j.find(':', pos + needle.size());
            if (pos == std::string::npos) return def;
            ++pos;
            while (pos < j.size() && std::isspace(static_cast<unsigned char>(j[pos]))) ++pos;
            if (pos >= j.size()) return def;
            try { return std::stoi(j.substr(pos)); } catch (...) { return def; }
        }

        static bool JsonBool(const std::string& j, const std::string& key, bool def = false)
        {
            const std::string needle = "\"" + key + "\"";
            auto pos = j.find(needle);
            if (pos == std::string::npos) return def;
            pos = j.find(':', pos + needle.size());
            if (pos == std::string::npos) return def;
            ++pos;
            while (pos < j.size() && std::isspace(static_cast<unsigned char>(j[pos]))) ++pos;
            if (j.compare(pos, 4, "true") == 0) return true;
            if (j.compare(pos, 5, "false") == 0) return false;
            return def;
        }

        static float JsonFloat(const std::string& j, const std::string& key, float def = 0.0f)
        {
            const std::string needle = "\"" + key + "\"";
            auto pos = j.find(needle);
            if (pos == std::string::npos) return def;
            pos = j.find(':', pos + needle.size());
            if (pos == std::string::npos) return def;
            ++pos;
            while (pos < j.size() && std::isspace(static_cast<unsigned char>(j[pos]))) ++pos;
            if (pos >= j.size()) return def;
            try { return std::stof(j.substr(pos)); } catch (...) { return def; }
        }

        static std::size_t FindKeyArray(const std::string& j, const std::string& key)
        {
            const std::string needle = "\"" + key + "\"";
            auto pos = j.find(needle);
            if (pos == std::string::npos) return std::string::npos;
            return j.find('[', pos + needle.size());
        }

        static std::array<int, 4> JsonIntArray4(const std::string& j, std::size_t from)
        {
            std::array<int, 4> r{};
            if (from == std::string::npos) return r;
            std::size_t pos = from + 1;
            for (int i = 0; i < 4; ++i)
            {
                while (pos < j.size() &&
                       !std::isdigit(static_cast<unsigned char>(j[pos])) && j[pos] != '-') ++pos;
                if (pos >= j.size()) break;
                r[i] = std::stoi(j.substr(pos));
                while (pos < j.size() && j[pos] != ',' && j[pos] != ']') ++pos;
                if (pos < j.size() && j[pos] == ',') ++pos;
            }
            return r;
        }

        static std::array<float, 3> JsonFloatArray3(const std::string& j, std::size_t from)
        {
            std::array<float, 3> r{};
            if (from == std::string::npos) return r;
            std::size_t pos = from + 1;
            for (int i = 0; i < 3; ++i)
            {
                while (pos < j.size() &&
                       !std::isdigit(static_cast<unsigned char>(j[pos])) && j[pos] != '-') ++pos;
                if (pos >= j.size()) break;
                r[i] = std::stof(j.substr(pos));
                while (pos < j.size() && j[pos] != ',' && j[pos] != ']') ++pos;
                if (pos < j.size() && j[pos] == ',') ++pos;
            }
            return r;
        }

        // Morph target CLI/.cnj serialization: variable-length float array (mesh entry's own
        // "morphWeights" field, and each morph weight-track keyframe's own "weights" field) --
        // generalizes JsonFloatArray3's identical scan logic to an unbounded count, stopping at
        // the array's own closing bracket.
        static std::vector<float> JsonFloatArrayN(const std::string& j, std::size_t from)
        {
            std::vector<float> r;
            if (from == std::string::npos) return r;
            std::size_t pos = from + 1;
            while (pos < j.size() && j[pos] != ']')
            {
                while (pos < j.size() && j[pos] != ']' &&
                       !std::isdigit(static_cast<unsigned char>(j[pos])) && j[pos] != '-') ++pos;
                if (pos >= j.size() || j[pos] == ']') break;
                r.push_back(std::stof(j.substr(pos)));
                while (pos < j.size() && j[pos] != ',' && j[pos] != ']') ++pos;
                if (pos < j.size() && j[pos] == ',') ++pos;
            }
            return r;
        }

        class SpriteFontTypeReader : public LooseFileContentTypeReader<Graphics::SpriteFont>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".cnj"};
            }

            Graphics::SpriteFont Read(const std::string& path, ContentManager& cm) override
            {
                using Graphics::SpriteFont;
                using SharpRuntime::charcs;

                const std::string json = ReadTextFile(path);

                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
                CNA::Internal::ValidateCnjEnvelope(envelope, "SpriteFont", path);
                RejectSourceFileForSelfContainedCnj(envelope, "SpriteFont", path);

                const std::string textureName   = ExtractJsonStringField(json, "texture");
                const int         lineSpacing    = JsonInt(json, "lineSpacing");
                const float       spacing        = JsonFloat(json, "spacing");
                const std::string defCharStr     = ExtractJsonStringField(json, "defaultCharacter");

                if (textureName.empty())
                    throw ContentLoadException("SpriteFont descriptor missing 'texture' field: " + path);

                std::optional<charcs> defChar;
                if (!defCharStr.empty())
                    defChar = static_cast<charcs>(static_cast<unsigned char>(defCharStr[0]));

                // Atlas texture — loaded and cached via ContentManager so it stays alive.
                Graphics::Texture2D atlas = cm.Load<Graphics::Texture2D>(
                    ResolveRootRelativeAssetName(cm, path, "texture", textureName));

                std::vector<Rectangle>          glyphBounds;
                std::vector<Rectangle>          cropping;
                std::vector<charcs>             characters;
                std::vector<Vector3>            kerningData;

                // Parse "glyphs": [ { "char": N, "source":[x,y,w,h],
                //                     "crop":[x,y,w,h], "kerning":[l,a,r] }, ... ]
                const std::size_t glyphsKey = json.find("\"glyphs\"");
                if (glyphsKey != std::string::npos)
                {
                    const std::size_t arrStart = json.find('[', glyphsKey);
                    if (arrStart != std::string::npos)
                    {
                        std::size_t pos = arrStart + 1;
                        while (true)
                        {
                            const std::size_t objStart = json.find('{', pos);
                            if (objStart == std::string::npos) break;

                            int depth = 1;
                            std::size_t objEnd = objStart + 1;
                            while (objEnd < json.size() && depth > 0)
                            {
                                if      (json[objEnd] == '{') ++depth;
                                else if (json[objEnd] == '}') --depth;
                                ++objEnd;
                            }

                            const std::string g = json.substr(objStart, objEnd - objStart);

                            const int charCode = JsonInt(g, "char");
                            const auto src = JsonIntArray4(g,   FindKeyArray(g, "source"));
                            const auto crp = JsonIntArray4(g,   FindKeyArray(g, "crop"));
                            const auto krn = JsonFloatArray3(g, FindKeyArray(g, "kerning"));

                            characters.push_back(static_cast<charcs>(charCode));
                            glyphBounds.push_back({src[0], src[1], src[2], src[3]});
                            cropping.push_back({crp[0], crp[1], crp[2], crp[3]});
                            kerningData.push_back({krn[0], krn[1], krn[2]});

                            pos = objEnd;
                        }
                    }
                }

                return SpriteFont(
                    std::move(atlas),
                    std::move(glyphBounds),
                    std::move(cropping),
                    std::move(characters),
                    lineSpacing,
                    spacing,
                    std::move(kerningData),
                    defChar);
            }
        };

        std::vector<std::uint8_t> ReadBinaryFile(const std::string& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
                throw ContentLoadException("Cannot open binary file: " + path);
            return std::vector<std::uint8_t>(
                std::istreambuf_iterator<char>(file), {});
        }

        struct BinReaderEXT
        {
            const std::vector<std::uint8_t>& Data;
            std::size_t Pos = 0;

            /// Bytes left unread. Lets a reader tell "this optional trailing block is absent" from
            /// "this file is truncated" without guessing (plan_gltf.md GLTF-245).
            [[nodiscard]] std::size_t Remaining() const { return Pos <= Data.size() ? Data.size() - Pos : 0u; }

            template <typename T>
            T Read()
            {
                // Task 11.7: a truncated/corrupt .skeleton.bin/.clip.bin (or a header value like
                // boneCount/trackCount/keyCount inconsistent with the file's actual byte length)
                // previously caused a real out-of-bounds heap read (undefined behavior) here
                // instead of a clean, catchable error - this is the most serious memory-safety
                // finding in the whole Avatar content-loading path.
                if (Pos + sizeof(T) > Data.size())
                {
                    throw ContentLoadException(
                        "Truncated or corrupt binary content: attempted to read " + std::to_string(sizeof(T))
                            + " bytes at offset " + std::to_string(Pos) + ", but only "
                            + std::to_string(Data.size()) + " bytes are available");
                }
                T value{};
                std::memcpy(&value, Data.data() + Pos, sizeof(T));
                Pos += sizeof(T);
                return value;
            }

            Matrix ReadMatrix()
            {
                float m[16];
                for (float& f : m) { f = Read<float>(); }
                return Matrix(m[0],  m[1],  m[2],  m[3],
                              m[4],  m[5],  m[6],  m[7],
                              m[8],  m[9],  m[10], m[11],
                              m[12], m[13], m[14], m[15]);
            }
        };

        // Task 14.1: string-literal-aware - a brace/bracket embedded inside a JSON string value
        // (e.g. a part/clip name like "Weird{Name}", structurally possible from
        // convert_avatar.py's fully automated pipeline even if not currently produced) previously
        // miscounted depth, since this only ever tracked raw character occurrences with no
        // awareness of string-literal boundaries. Skips over "..." contents (respecting
        // backslash escapes, so \" doesn't end the string early) without counting brackets
        // inside them.
        std::size_t FindMatchingBracketEXT(const std::string& j, std::size_t openPos,
                                            char openCh, char closeCh)
        {
            int depth = 1;
            std::size_t pos = openPos + 1;
            bool inString = false;
            while (pos < j.size() && depth > 0)
            {
                const char c = j[pos];
                if (inString)
                {
                    if (c == '\\') { ++pos; } // skip the escaped character entirely
                    else if (c == '"') { inString = false; }
                }
                else if (c == '"') { inString = true; }
                else if (c == openCh) { ++depth; }
                else if (c == closeCh) { --depth; }
                ++pos;
            }
            return pos;
        }

        /**
         * @brief One entry of a Model .cnj's own "bones" array: a node of the imported scene graph.
         *
         * @note CNAEXT — not part of the XNA 4.0 API. plan_gltf.md GLTF-129 (Phase 5). `transform`
         * is the bone-LOCAL transform in XNA row-major order; world transforms are composed by
         * `Model::CopyAbsoluteBoneTransformsTo`, never stored.
         */
        struct CnjBoneEntry
        {
            std::string name;
            int parent = -1;
            Matrix transform = Matrix::getIdentityProperty();
        };

        // plan_gltf.md GLTF-129 (Phase 5): parses a Model .cnj's "bones" array into a flat,
        // parent-before-child node list. Returns empty when the field is absent, so a cnjVersion-1
        // file (no "bones" at all) and a name-only one both degrade to the previous single-Root
        // behavior rather than failing. Entry 0 is the root; its "parent" defaults to -1 and every
        // later entry's defaults to 0, so a hand-written file may omit the field for a flat graph.
        std::vector<CnjBoneEntry> ParseCnjBoneArrayEXT(const std::string& json)
        {
            std::vector<CnjBoneEntry> bones;
            const std::size_t key = json.find("\"bones\"");
            if (key == std::string::npos) { return bones; }
            const std::size_t arrayStart = json.find('[', key);
            if (arrayStart == std::string::npos) { return bones; }
            const std::size_t arrayEnd = FindMatchingBracketEXT(json, arrayStart, '[', ']');

            std::size_t pos = arrayStart + 1;
            while (pos < arrayEnd)
            {
                const std::size_t objectStart = json.find('{', pos);
                if (objectStart == std::string::npos || objectStart >= arrayEnd) { break; }
                const std::size_t objectEnd = FindMatchingBracketEXT(json, objectStart, '{', '}');
                const std::string entryJson = json.substr(objectStart, objectEnd - objectStart);
                pos = objectEnd;

                CnjBoneEntry entry;
                entry.name = ExtractJsonStringField(entryJson, "name");
                entry.parent = JsonInt(entryJson, "parent", bones.empty() ? -1 : 0);
                const std::size_t transformKey = entryJson.find("\"transform\"");
                if (transformKey != std::string::npos)
                {
                    const std::vector<float> m =
                        JsonFloatArrayN(entryJson, entryJson.find('[', transformKey));
                    if (m.size() == 16)
                    {
                        entry.transform = Matrix(m[0],  m[1],  m[2],  m[3],
                                                  m[4],  m[5],  m[6],  m[7],
                                                  m[8],  m[9],  m[10], m[11],
                                                  m[12], m[13], m[14], m[15]);
                    }
                }
                bones.push_back(std::move(entry));
            }
            return bones;
        }

        // Morph target CLI/.cnj serialization: extracts a single nested JSON object's substring
        // by key (e.g. a mesh entry's own "morphWeightTrack" field), bracket-depth-aware via
        // FindMatchingBracketEXT so a nested "keys" array's own braces don't truncate it early.
        std::string ExtractJsonObjectFieldEXT(const std::string& json, const std::string& key)
        {
            const std::size_t k = json.find("\"" + key + "\"");
            if (k == std::string::npos) { return {}; }
            const std::size_t os = json.find('{', k);
            if (os == std::string::npos) { return {}; }
            const std::size_t oe = FindMatchingBracketEXT(json, os, '{', '}');
            return json.substr(os, oe - os);
        }

        // Parses a flat JSON array of small objects bounded to the array's own closing
        // bracket (unlike the "meshes"/"glyphs" loops above, this schema has more than one
        // array key per file, so bounding matters — an unbounded scan would bleed into a
        // later array's objects).
        std::vector<std::string> ParseFlatObjectArrayEXT(const std::string& json, const std::string& key)
        {
            std::vector<std::string> result;
            const std::size_t k = json.find("\"" + key + "\"");
            if (k == std::string::npos) { return result; }
            const std::size_t a = json.find('[', k);
            if (a == std::string::npos) { return result; }
            const std::size_t arrEnd = FindMatchingBracketEXT(json, a, '[', ']');

            std::size_t pos = a + 1;
            while (true)
            {
                const std::size_t os = json.find('{', pos);
                if (os == std::string::npos || os >= arrEnd) { break; }
                const std::size_t oe = FindMatchingBracketEXT(json, os, '{', '}');
                result.push_back(json.substr(os, oe - os));
                pos = oe;
            }
            return result;
        }

        // Task 941: shared by SkinnedModelTypeReader (.skinnedmodel.json's "animations" section)
        // and ModelTypeReader (.model.json's own new "animations" section, added for real-Model
        // skeletal animation, Phase 77) -- both use the exact same .clip.bin binary format, so
        // this is the single, already-bug-fixed (Task 11.11) implementation both readers share,
        // rather than two copies that could drift out of sync.
        Graphics::AnimationClipEXT ReadAnimationClipFileEXT(const std::string& clipFilePath)
        {
            const auto clipBytes = ReadBinaryFile(clipFilePath);
            BinReaderEXT clipReader{clipBytes};

            Graphics::AnimationClipEXT clip;
            clip.Duration = System::TimeSpan::FromSeconds(clipReader.Read<double>());
            const int trackCount = clipReader.Read<std::int32_t>();
            clip.Tracks.reserve(static_cast<std::size_t>(trackCount));
            for (int t = 0; t < trackCount; ++t)
            {
                Graphics::BoneTrackEXT track;
                track.BoneIndex = clipReader.Read<std::int32_t>();
                const int keyCount = clipReader.Read<std::int32_t>();
                track.Keys.reserve(static_cast<std::size_t>(keyCount));
                for (int k = 0; k < keyCount; ++k)
                {
                    Graphics::KeyframeEXT key;
                    key.Time = System::TimeSpan::FromSeconds(clipReader.Read<double>());
                    // C++ does not guarantee left-to-right evaluation order for a single
                    // function call's arguments — reading each float into its own named
                    // local first (separate statements, strictly sequential) before
                    // constructing Vector3/Quaternion is required here, not stylistic — see
                    // Task 11.11's own regression (a keyframe's rotation bytes were read back
                    // scrambled under a right-to-left argument evaluation order).
                    const float tx = clipReader.Read<float>();
                    const float ty = clipReader.Read<float>();
                    const float tz = clipReader.Read<float>();
                    key.Translation = Vector3(tx, ty, tz);
                    const float qx = clipReader.Read<float>();
                    const float qy = clipReader.Read<float>();
                    const float qz = clipReader.Read<float>();
                    const float qw = clipReader.Read<float>();
                    key.Rotation = Quaternion(qx, qy, qz, qw);
                    const float sx = clipReader.Read<float>();
                    const float sy = clipReader.Read<float>();
                    const float sz = clipReader.Read<float>();
                    key.Scale = Vector3(sx, sy, sz);
                    track.Keys.push_back(key);
                }
                clip.Tracks.push_back(std::move(track));
            }
            return clip;
        }

        // plan_cnj.md CNB-48: an "animations" entry's "clip" field may name either a raw
        // .clip.bin binary blob (ReadAnimationClipFileEXT's original, still-supported shape) or
        // a standalone .cnj AnimationClip asset (Phase 10) -- letting multiple Models/
        // SkinnedModels share one clip (e.g. a common "Idle"/"Walk" library) instead of
        // duplicating the binary per model. Dispatched by extension. The .cnj branch goes
        // through ContentManager (real caching), so a previously containment-checked filesystem
        // path is re-expressed as a root-relative asset name when it is inside the content root;
        // an explicitly loaded outside-root bundle remains absolute. Callers resolve and validate
        // their own root-relative versus manifest-relative contract before entering this helper.
        Graphics::AnimationClipEXT ReadAnimationClipRefEXT(
            const std::string& resolvedClipPath, const std::string& root, ContentManager& cm)
        {
            namespace fs = std::filesystem;
            if (fs::path(resolvedClipPath).extension() == ".cnj")
            {
                return cm.Load<Graphics::AnimationClipEXT>(
                    ToContentManagerAssetName(root, resolvedClipPath));
            }
            return ReadAnimationClipFileEXT(resolvedClipPath);
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

        // plan_cnj.md CNB-40: a directly-loadable .cnj AnimationClip document, independent of
        // any specific Model -- cnj.md's own per-type conventions table documented this as a
        // natural, open-ended follow-up beyond plan_cnj.md's original 9 phases. Either inline
        // JSON keyframe/bone-transform data ("tracks"), or -- for large clips, to avoid bloating
        // JSON with thousands of matrices, exactly as cnj.md's table describes -- a reference to
        // an existing raw binary blob via "clipFile", read through the same shared
        // ReadAnimationClipFileEXT() helper ModelTypeReader's/SkinnedModelTypeReader's own
        // "animations" field already uses. Self-contained either way: like SpriteFont/Effect/
        // Model, "sourceFile" is rejected (CNB-34's capability matrix).
        class AnimationClipTypeReader : public LooseFileContentTypeReader<Graphics::AnimationClipEXT>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".cnj"};
            }

            Graphics::AnimationClipEXT Read(const std::string& path, ContentManager& cm) override
            {
                using CNA::Internal::JsonType;
                using CNA::Internal::JsonValue;

                const std::string json = ReadTextFile(path);

                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
                CNA::Internal::ValidateCnjEnvelope(envelope, "AnimationClip", path);
                RejectSourceFileForSelfContainedCnj(envelope, "AnimationClip", path);

                const JsonValue root = CNA::Internal::ParseJson(json);
                const JsonValue* clipFileField = root.FindMember("clipFile");
                const JsonValue* tracksField   = root.FindMember("tracks");

                if ((clipFileField != nullptr) == (tracksField != nullptr))
                {
                    throw ContentLoadException(
                        "AnimationClip .cnj '" + path +
                        "' must have exactly one of 'clipFile' or 'tracks'.");
                }

                if (clipFileField != nullptr)
                {
                    if (!clipFileField->IsString() || clipFileField->stringValue.empty())
                    {
                        throw ContentLoadException(
                            "AnimationClip .cnj '" + path + "' has a non-string or empty 'clipFile'.");
                    }
                    return ReadAnimationClipFileEXT(ResolveRootRelativeSidecarPath(
                        cm, path, "clipFile", clipFileField->stringValue));
                }

                const JsonValue* durationField = root.FindMember("duration");
                if (durationField == nullptr || !durationField->IsNumber())
                {
                    throw ContentLoadException(
                        "AnimationClip .cnj '" + path + "' is missing a numeric 'duration' field.");
                }

                Graphics::AnimationClipEXT clip;
                clip.Duration = System::TimeSpan::FromSeconds(durationField->numberValue);
                // plan_gltf.md GLTF-294: which index space this clip's track bone indices are in.
                // Absent means JointPalette, which is what every clip written before this could
                // only ever have been.
                if (const JsonValue* space = root.FindMember("targetSpace");
                    space != nullptr && space->IsString() && space->stringValue == "SceneNode")
                {
                    clip.TargetSpace = Graphics::ClipTargetSpaceEXT::SceneNode;
                }

                if (tracksField->type != JsonType::Array)
                {
                    throw ContentLoadException(
                        "AnimationClip .cnj '" + path + "' has a 'tracks' field that is not an array.");
                }

                std::vector<float> arr;
                for (const JsonValue& trackValue : tracksField->arrayValue)
                {
                    if (!trackValue.IsObject())
                    {
                        throw ContentLoadException(
                            "AnimationClip .cnj '" + path + "' has a non-object entry in 'tracks'.");
                    }

                    Graphics::BoneTrackEXT track;

                    const JsonValue* boneIndexField = trackValue.FindMember("boneIndex");
                    if (boneIndexField == nullptr || !boneIndexField->IsNumber())
                    {
                        throw ContentLoadException(
                            "AnimationClip .cnj '" + path + "' has a track missing a numeric 'boneIndex'.");
                    }
                    track.BoneIndex = static_cast<int>(boneIndexField->numberValue);

                    const JsonValue* keysField = trackValue.FindMember("keys");
                    if (keysField == nullptr || keysField->type != JsonType::Array)
                    {
                        throw ContentLoadException(
                            "AnimationClip .cnj '" + path + "' has a track missing a 'keys' array.");
                    }

                    track.Keys.reserve(keysField->arrayValue.size());
                    for (const JsonValue& keyValue : keysField->arrayValue)
                    {
                        if (!keyValue.IsObject())
                        {
                            throw ContentLoadException(
                                "AnimationClip .cnj '" + path + "' has a non-object entry in a track's 'keys'.");
                        }

                        Graphics::KeyframeEXT key;

                        const JsonValue* timeField = keyValue.FindMember("time");
                        if (timeField == nullptr || !timeField->IsNumber())
                        {
                            throw ContentLoadException(
                                "AnimationClip .cnj '" + path + "' has a keyframe missing a numeric 'time'.");
                        }
                        key.Time = System::TimeSpan::FromSeconds(timeField->numberValue);

                        if (TryReadFloatArrayField(keyValue, "translation", 3, arr, path))
                        {
                            key.Translation = Vector3(arr[0], arr[1], arr[2]);
                        }
                        if (TryReadFloatArrayField(keyValue, "rotation", 4, arr, path))
                        {
                            key.Rotation = Quaternion(arr[0], arr[1], arr[2], arr[3]);
                        }
                        if (TryReadFloatArrayField(keyValue, "scale", 3, arr, path))
                        {
                            key.Scale = Vector3(arr[0], arr[1], arr[2]);
                        }

                        track.Keys.push_back(key);
                    }

                    clip.Tracks.push_back(std::move(track));
                }

                return clip;
            }
        };

        Microsoft::Xna::Framework::CurveLoopType ParseCurveLoopTypeEXT(
            const std::string& value, const std::string& path)
        {
            using Microsoft::Xna::Framework::CurveLoopType;
            if (value.empty() || value == "Constant") { return CurveLoopType::Constant; }
            if (value == "Cycle") { return CurveLoopType::Cycle; }
            if (value == "CycleOffset") { return CurveLoopType::CycleOffset; }
            if (value == "Oscillate") { return CurveLoopType::Oscillate; }
            if (value == "Linear") { return CurveLoopType::Linear; }
            throw ContentLoadException(
                "Curve .cnj '" + path + "' has an unrecognized CurveLoopType '" + value + "'.");
        }

        Microsoft::Xna::Framework::CurveContinuity ParseCurveContinuityEXT(
            const std::string& value, const std::string& path)
        {
            using Microsoft::Xna::Framework::CurveContinuity;
            if (value.empty() || value == "Smooth") { return CurveContinuity::Smooth; }
            if (value == "Step") { return CurveContinuity::Step; }
            throw ContentLoadException(
                "Curve .cnj '" + path + "' has an unrecognized CurveContinuity '" + value + "'.");
        }

        // plan_cnj.md CNB-44: self-contained JSON port of CurveContentTypeReader.hpp's already-
        // FNA-verified field order/shape -- "preLoop"/"postLoop" (CurveLoopType names, default
        // "Constant" when omitted) + "keys" (position/value/tangentIn/tangentOut/continuity,
        // tangentIn/tangentOut/continuity default to 0.0/0.0/"Smooth" when omitted per key).
        class CurveTypeReader : public LooseFileContentTypeReader<Microsoft::Xna::Framework::Curve>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".cnj"};
            }

            Microsoft::Xna::Framework::Curve Read(const std::string& path, ContentManager& /*cm*/) override
            {
                using CNA::Internal::JsonType;
                using CNA::Internal::JsonValue;
                using Microsoft::Xna::Framework::Curve;
                using Microsoft::Xna::Framework::CurveKey;

                const std::string json = ReadTextFile(path);

                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
                CNA::Internal::ValidateCnjEnvelope(envelope, "Curve", path);
                RejectSourceFileForSelfContainedCnj(envelope, "Curve", path);

                const JsonValue root = CNA::Internal::ParseJson(json);

                Curve curve;

                if (const JsonValue* preLoop = root.FindMember("preLoop"))
                {
                    if (!preLoop->IsString())
                    {
                        throw ContentLoadException("Curve .cnj '" + path + "': 'preLoop' must be a string.");
                    }
                    curve.setPreLoopProperty(ParseCurveLoopTypeEXT(preLoop->stringValue, path));
                }
                if (const JsonValue* postLoop = root.FindMember("postLoop"))
                {
                    if (!postLoop->IsString())
                    {
                        throw ContentLoadException("Curve .cnj '" + path + "': 'postLoop' must be a string.");
                    }
                    curve.setPostLoopProperty(ParseCurveLoopTypeEXT(postLoop->stringValue, path));
                }

                const JsonValue* keysField = root.FindMember("keys");
                if (keysField == nullptr || keysField->type != JsonType::Array)
                {
                    throw ContentLoadException("Curve .cnj '" + path + "' is missing a 'keys' array.");
                }

                for (const JsonValue& keyValue : keysField->arrayValue)
                {
                    if (!keyValue.IsObject())
                    {
                        throw ContentLoadException(
                            "Curve .cnj '" + path + "' has a non-object entry in 'keys'.");
                    }

                    const JsonValue* positionField = keyValue.FindMember("position");
                    const JsonValue* valueField    = keyValue.FindMember("value");
                    if (positionField == nullptr || !positionField->IsNumber() ||
                        valueField == nullptr || !valueField->IsNumber())
                    {
                        throw ContentLoadException(
                            "Curve .cnj '" + path + "' has a key missing numeric 'position'/'value'.");
                    }

                    float tangentIn = 0.0f;
                    if (const JsonValue* t = keyValue.FindMember("tangentIn"))
                    {
                        if (!t->IsNumber())
                        {
                            throw ContentLoadException("Curve .cnj '" + path + "': 'tangentIn' must be numeric.");
                        }
                        tangentIn = static_cast<float>(t->numberValue);
                    }
                    float tangentOut = 0.0f;
                    if (const JsonValue* t = keyValue.FindMember("tangentOut"))
                    {
                        if (!t->IsNumber())
                        {
                            throw ContentLoadException("Curve .cnj '" + path + "': 'tangentOut' must be numeric.");
                        }
                        tangentOut = static_cast<float>(t->numberValue);
                    }
                    auto continuity = Microsoft::Xna::Framework::CurveContinuity::Smooth;
                    if (const JsonValue* c = keyValue.FindMember("continuity"))
                    {
                        if (!c->IsString())
                        {
                            throw ContentLoadException("Curve .cnj '" + path + "': 'continuity' must be a string.");
                        }
                        continuity = ParseCurveContinuityEXT(c->stringValue, path);
                    }

                    curve.getKeysProperty().Add(CurveKey(
                        static_cast<float>(positionField->numberValue),
                        static_cast<float>(valueField->numberValue),
                        tangentIn, tangentOut, continuity));
                }

                return curve;
            }
        };

        // ---------------------------------------------------------------------------
        // .model.json descriptor reader
        // ---------------------------------------------------------------------------

        // Owned resources shared by all copies of a Model returned by ModelTypeReader::Read()
        // (the .cnj JSON path) or ReadGltfModel() (CNB-70/71, Phase 13D's runtime glTF path) --
        // hoisted out of ModelTypeReader::Read() so both readers share one definition.
        struct ModelResources {
            std::vector<std::unique_ptr<Graphics::VertexBuffer>>  vbs;
            std::vector<std::unique_ptr<Graphics::IndexBuffer>>   ibs;
            std::vector<std::unique_ptr<Graphics::ModelBone>>     boneOwners;
            std::vector<std::unique_ptr<Graphics::ModelMesh>>     meshOwners;
            std::vector<std::unique_ptr<Graphics::ModelMeshPart>> partOwners;
            std::vector<std::shared_ptr<Graphics::Effect>>        effectOwners;
            std::vector<std::unique_ptr<Graphics::Texture2D>>     textureOwners;
            // Task 941: owns the skeleton/animation-clip data attached to the returned Model's
            // own Tag property. Null for a rigid, non-skinned model with no skeleton.
            std::unique_ptr<Graphics::SkinningData>               skinningData;
            // plan_gltf.md GLTF-294: owns the rigid (non-joint) animation clips attached to an
            // UNSKINNED model's Tag. Null for a skinned model, whose Tag carries the skeleton --
            // Tag holds one object, and that collision is a recorded limitation (GLTF-295).
            std::unique_ptr<Graphics::ModelAnimationsEXT>         modelAnimations;
            // CNB-64/65 (Phase 13B): owns the morph-target data attached to each morphed mesh
            // part's own Tag property (one entry per part that has morph targets, not per Model).
            std::vector<std::unique_ptr<Graphics::MorphTargetDataEXT>> morphOwners;
        };
        // plan_gltf.md GLTF-037. `std::vector<std::uint8_t>::data()` is only guaranteed to be
        // aligned for a byte, so casting it to `const std::uint32_t*` and dereferencing is a
        // misaligned load -- undefined behaviour by the standard, an outright fault on targets
        // without unaligned access, and exactly the class of finding GLTF-036's sanitizer job
        // exists to catch. In practice the allocator returns a suitably aligned block and the code
        // "worked", which is what let it stand.
        //
        // memcpy into a properly typed vector has no such requirement, the compiler lowers it to
        // the same loads on targets where they are legal, and it happens once per mesh part at
        // load time rather than per frame -- so there is no cost worth weighing against removing
        // undefined behaviour.
        template <typename TIndex>
        std::vector<TIndex> IndicesFromBytes(const std::vector<std::uint8_t>& bytes, int count)
        {
            std::vector<TIndex> indices(static_cast<std::size_t>(std::max(count, 0)));
            if (!indices.empty())
            {
                std::memcpy(indices.data(), bytes.data(), indices.size() * sizeof(TIndex));
            }
            return indices;
        }


        // Task 927: `stride` is always one of XNA's own "clean" (tightly packed, no vtable) sizes
        // -- 16/20/24/32/52/56 -- since every offline conversion tool (and, since CNB-70, the
        // runtime glTF reader) writes/produces that exact layout. Every CNA vertex struct below
        // publicly inherits the polymorphic IVertexType (a vtable pointer XNA's own C# interface
        // never carried), inflating its real sizeof() past that clean size -- comparing `stride`
        // against sizeof(...) directly, then reinterpret_cast-ing the tightly-packed bytes as an
        // array of those inflated structs, silently reads every vertex field from the wrong byte
        // offset (confirmed the true cause of a long-tracked "near-plane-clipping"/invisible-model
        // symptom family in ../cna-samples). Read each vertex's fields explicitly from the
        // buffer's own known-clean offsets instead, and construct real, normally-initialized C++
        // objects (the compiler resolves member layout correctly), then upload through the
        // existing typed SetData overload, which already packs into the correct compact GPU
        // layout. Stride 52/56 (GPU-skinned, with/without per-vertex Color -- CNB-67) already
        // match VertexBuffer's own compact skinned layout, so SetDataRaw() copies them straight
        // through with no C++ struct reinterpretation at all, avoiding the vtable-inflation risk
        // by construction rather than by careful offset arithmetic.
        std::unique_ptr<Graphics::VertexBuffer> BuildVertexBufferFromRawBytes(
            Graphics::GraphicsDevice& device, int stride, int numVertices,
            const std::vector<std::uint8_t>& vertBytes)
        {
            auto readF = [&](std::size_t off) {
                float v;
                std::memcpy(&v, vertBytes.data() + off, sizeof(float));
                return v;
            };
            auto readVec3 = [&](std::size_t off) {
                return Vector3(readF(off), readF(off + 4), readF(off + 8));
            };
            auto readVec2 = [&](std::size_t off) {
                return Vector2(readF(off), readF(off + 4));
            };
            auto readColor = [&](std::size_t off) {
                return Color(vertBytes[off], vertBytes[off + 1],
                             vertBytes[off + 2], vertBytes[off + 3]);
            };

            // Note: VertexPositionColorTexture's own declared `= default` default constructor is
            // implicitly deleted (its `Color Color` member has no zero-arg constructor) -- build
            // each vector via reserve()+emplace_back() rather than a sized constructor, so none
            // of the 4 struct-backed branches below ever needs default-construction.
            auto vb = std::make_unique<Graphics::VertexBuffer>(device, numVertices);
            if (stride == 16) {
                std::vector<Graphics::VertexPositionColor> verts;
                verts.reserve(static_cast<std::size_t>(numVertices));
                for (int i = 0; i < numVertices; ++i) {
                    const std::size_t o = static_cast<std::size_t>(i) * 16;
                    verts.emplace_back(readVec3(o), readColor(o + 12));
                }
                vb->SetData(verts.data(), numVertices);
            } else if (stride == 20) {
                std::vector<Graphics::VertexPositionTexture> verts;
                verts.reserve(static_cast<std::size_t>(numVertices));
                for (int i = 0; i < numVertices; ++i) {
                    const std::size_t o = static_cast<std::size_t>(i) * 20;
                    verts.emplace_back(readVec3(o), readVec2(o + 12));
                }
                vb->SetData(verts.data(), numVertices);
            } else if (stride == 24) {
                std::vector<Graphics::VertexPositionColorTexture> verts;
                verts.reserve(static_cast<std::size_t>(numVertices));
                for (int i = 0; i < numVertices; ++i) {
                    const std::size_t o = static_cast<std::size_t>(i) * 24;
                    verts.emplace_back(readVec3(o), readColor(o + 12), readVec2(o + 16));
                }
                vb->SetData(verts.data(), numVertices);
            } else if (stride == 32) {
                std::vector<Graphics::VertexPositionNormalTexture> verts;
                verts.reserve(static_cast<std::size_t>(numVertices));
                for (int i = 0; i < numVertices; ++i) {
                    const std::size_t o = static_cast<std::size_t>(i) * 32;
                    verts.emplace_back(readVec3(o), readVec3(o + 12), readVec2(o + 24));
                }
                vb->SetData(verts.data(), numVertices);
            } else if (stride == 48) {
                // plan_cnj.md CNB-57 (Phase 13A): VertexPositionNormalTangentTexture (PbrEffect)
                // -- raw byte upload for the same vtable-inflation reason as stride 52/56 above.
                vb->SetDataRaw(vertBytes.data(), numVertices, 48);
            } else if (stride == 52) {
                vb->SetDataRaw(vertBytes.data(), numVertices, 52);
            } else if (stride == 56) {
                vb->SetDataRaw(vertBytes.data(), numVertices, 56);
            } else if (stride == 68) {
                // PBR + skinning combo: VertexPositionNormalTangentTextureSkinned (SkinnedPbrEffect)
                // -- raw byte upload for the same vtable-inflation reason as stride 48/52/56 above.
                vb->SetDataRaw(vertBytes.data(), numVertices, 68);
            }
            else
            {
                // plan_gltf.md GLTF-157, the importer's half of it. There used to be no else: an
                // unlisted stride fell out of the chain and the freshly constructed, EMPTY
                // VertexBuffer was returned as though it had been filled. The mesh then drew from
                // whatever the buffer object happened to contain, which is the same class of
                // silent wrongness the renderer-side ApplyLayout fallback has.
                //
                // The canonical table is consulted rather than a second literal list, because a
                // second list is exactly what went stale in GLTF-278. If a stride is not in the
                // table no renderer can bind it either, so refusing here is refusing earlier, not
                // refusing more.
                const CNA::Internal::Graphics::InferredVertexLayout layout =
                    CNA::Internal::Graphics::InferredLayoutForStride(
                        stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
                throw ContentLoadException(
                    "Vertex stride " + std::to_string(stride) +
                    (layout.known
                         ? " is in the canonical layout table but has no upload path here, so the "
                           "vertex buffer would have been left empty."
                         : " is not in the canonical vertex layout table, so no renderer can bind "
                           "it and no upload path exists for it.") +
                    " Refusing rather than returning an empty vertex buffer (GLTF-157).");
            }
            return vb;
        }

        // CNB-97 (Phase 14H): applies up to 3 KHR_lights_punctual-derived directional lights
        // (see GltfImportCore::ExtractPunctualLightsEXT's own doc comment) to any effect
        // implementing IEffectLights -- a single dynamic_cast against the interface covers
        // BasicEffect/SkinnedEffect/PbrEffect/SkinnedPbrEffect uniformly, rather than one branch
        // per concrete type. A no-op for DualTextureEffect/AlphaTestEffect (neither implements
        // IEffectLights, matching real XNA's own lighting-less shape for both). Any DirectionalLight
        // slot beyond lights.size() is left at the effect's own default (disabled).
        void ApplyPunctualLightsEXT(Graphics::Effect& fx, const std::vector<CNA::Internal::GltfImport::LightOut>& lights)
        {
            auto* lit = dynamic_cast<Graphics::IEffectLights*>(&fx);
            if (!lit) { return; }

            // plan_gltf.md GLTF-215 fallback policy (CNAEXT). glTF does not require a scene to
            // declare any light, and most authored assets do not: lighting is normally the
            // viewer's business. That was harmless while an untextured mesh imported through
            // BasicEffect, whose own defaults are visible. Once GLTF-215 made metallic-roughness
            // the selection rule, the same mesh reaches PbrEffect, whose AmbientLightColor
            // defaults to (0,0,0) and whose three directional slots start disabled -- so a
            // light-less file would render black, which is a faithful reading of "no lights" and
            // a useless one for anybody importing a model to look at it.
            //
            // A file that declares no light at all therefore gets the effect's own
            // EnableDefaultLighting() rig -- the same three-light arrangement real XNA applies,
            // and the same one BasicEffect users already expect. This is a CNA import policy, not
            // a specification rule: it applies only when the file expresses no lighting intent,
            // so it can never override or dim an asset that authored its own lights.
            if (lights.empty())
            {
                lit->EnableDefaultLighting();
                return;
            }

            Graphics::DirectionalLight* slots[3] = {
                &lit->getDirectionalLight0Property(),
                &lit->getDirectionalLight1Property(),
                &lit->getDirectionalLight2Property(),
            };
            for (std::size_t i = 0; i < lights.size() && i < 3; ++i)
            {
                slots[i]->setDirectionProperty(lights[i].direction);
                slots[i]->setDiffuseColorProperty(lights[i].diffuseColor);
                slots[i]->setEnabledProperty(true);
            }
        }

        // plan_cnj.md CNB-70/71 (Phase 13D): loads a .gltf/.glb file directly into a real Model,
        // with no intermediate .cnj/binary sidecar files -- reuses the same
        // CNA::Internal::GltfImport::GltfImportCore parsing/skeleton/animation/mesh-extraction
        // functions tools/gltf_to_cnj's offline CLI tool calls (see that header's own docs for the
        // full behavior description: topological bone reorder, sparse-accessor-safe reads,
        // CUBICSPLINE Hermite evaluation, base-color/occlusion texture extraction, scene-scoped
        // mesh grouping, stride-20/24/32/52/56 selection).
        //
        // Unlike the offline tool (which can emit multiple .cnj Model outputs for a glTF file
        // combining several independently-skinned characters), a single Load<Model>() call
        // returns exactly one Model -- only the file's FIRST mesh group (CollectMeshGroups' own
        // order) is imported. A multi-character glTF file needs the offline gltf_to_cnj tool (or
        // splitting the source into separate files) to reach the other groups -- a documented
        // limitation, not a bug. unitScale is always 1.0 (no CLI-argument equivalent exists for
        // runtime loading); a source file not authored in meters needs the offline tool instead.
        Graphics::Model ReadGltfModel(const std::string& path, ContentManager& cm)
        {
            namespace fs = std::filesystem;
            using namespace CNA::Internal::GltfImport;

            cgltf_options parseOptions{};
            cgltf_data* data = nullptr;
            cgltf_result parseResult = cgltf_parse_file(&parseOptions, path.c_str(), &data);
            if (parseResult != cgltf_result_success)
            {
                throw ContentLoadException(
                    "Failed to parse glTF file '" + path + "' (cgltf error " +
                    std::to_string(static_cast<int>(parseResult)) + ").");
            }
            struct DataGuard { cgltf_data* d; ~DataGuard() { cgltf_free(d); } } guard{data};

            // plan_gltf.md GLTF-032/GLTF-198: refuse a file that names something outside its own
            // directory, BEFORE cgltf_load_buffers -- that call resolves external buffer URIs
            // itself and offers no hook to veto one, so the only place to stand is in front of it.
            try
            {
                ValidateExternalUriContainmentEXT(data, fs::path(path).parent_path());
            }
            catch (const std::exception& e)
            {
                throw ContentLoadException(
                    "glTF file '" + path + "' was rejected: " + e.what());
            }

            parseResult = cgltf_load_buffers(&parseOptions, data, path.c_str());
            if (parseResult != cgltf_result_success)
            {
                throw ContentLoadException("Failed to load buffers for glTF file '" + path + "'.");
            }

            if (data->asset.version && std::string(data->asset.version) != "2.0")
            {
                throw ContentLoadException(
                    "Unsupported glTF asset.version '" + std::string(data->asset.version) +
                    "' in '" + path + "' -- only glTF 2.0 is supported.");
            }

            // plan_gltf.md GLTF-021..GLTF-024: structural validation, extensionsRequired
            // enforcement and an ignored-extension report, before a single byte is decoded. A
            // failure here is a rejection rather than a warning because every constraint
            // cgltf_validate checks is one whose violation would read outside the file's buffers.
            {
                std::vector<std::string> validationWarnings;
                try
                {
                    ValidateGltfEXT(data, path, validationWarnings);
                }
                catch (const std::exception& e)
                {
                    throw ContentLoadException(e.what());
                }
                for (const std::string& warning : validationWarnings)
                {
                    CNA::Logger::Warn(warning);
                }
            }

            // plan_gltf.md GLTF-113/GLTF-114 (Phase 5): the default scene's node graph, flattened
            // parent-before-child with composed world transforms. Every ModelBone below mirrors one
            // of these nodes, so a mesh is placed by its node exactly as glTF specifies rather than
            // landing at the origin in mesh-local space.
            const SceneGraphOut sceneGraph = BuildSceneGraph(data);
            const std::vector<MeshGroup> groups = CollectMeshGroups(data, sceneGraph);
            if (groups.empty())
            {
                throw ContentLoadException("glTF file '" + path + "' contains no mesh instances to import.");
            }
            // plan_gltf.md GLTF-137. This used to be `groups.front()`, and every other group was
            // dropped without a word: CollectMeshGroups makes one group per distinct skin (plus one
            // for the unskinned meshes), so a file with a character AND a prop lost whichever of
            // the two did not come first in the node array. Every group is imported now.
            //
            // One limit survives and is REPORTED rather than hidden: `Model::Tag` holds a single
            // `SkinningData`, so only one skeleton can reach the application. A second skin's
            // meshes are therefore not imported -- posing them with another rig's palette would be
            // a fresh silent corruption in place of the old silent drop, which is the same
            // reasoning `GLTF-295` records for rigid clips on a skinned model. The unskinned group
            // is never affected: it has no skeleton to collide with.
            std::size_t skinnedGroupIndex = groups.size();
            for (std::size_t i = 0; i < groups.size(); ++i)
            {
                if (groups[i].skin != nullptr) { skinnedGroupIndex = i; break; }
            }
            const bool hasSkin = skinnedGroupIndex < groups.size();

            // CNB-97 (Phase 14H): KHR_lights_punctual, approximated as up to 3 directional lights
            // (see ExtractPunctualLightsEXT's own doc comment) -- applied to every mesh part's
            // effect below via ApplyPunctualLightsEXT, for whichever ones implement IEffectLights.
            LightReportEXT lightReport;
            const std::vector<LightOut> punctualLights = ExtractPunctualLightsEXT(data, lightReport);
            // plan_gltf.md GLTF-326: the approximation is documented, but it was not visible.
            if (lightReport.droppedLightCount > 0)
            {
                CNA::Logger::Warn(
                    "glTF file '" + path + "': the scene declares " +
                    std::to_string(punctualLights.size() + lightReport.droppedLightCount) +
                    " lights; XNA's stock effects bind three, so " +
                    std::to_string(lightReport.droppedLightCount) + " were dropped (GLTF-326).");
            }
            if (lightReport.approximatedPointLightCount > 0 ||
                lightReport.approximatedSpotLightCount > 0)
            {
                CNA::Logger::Warn(
                    "glTF file '" + path + "': " +
                    std::to_string(lightReport.approximatedPointLightCount) + " point and " +
                    std::to_string(lightReport.approximatedSpotLightCount) +
                    " spot light(s) were approximated as directional lights aimed at the scene "
                    "origin -- no CNA stock effect shader has a point or spot term, and a spot's "
                    "cone is lost entirely (GLTF-326).");
            }
            if (lightReport.clampedIntensityLightCount > 0)
            {
                CNA::Logger::Warn(
                    "glTF file '" + path + "': " +
                    std::to_string(lightReport.clampedIntensityLightCount) +
                    " light(s) had color * intensity above 1 and were clamped (worst channel " +
                    std::to_string(lightReport.worstPreClampChannelEXT) +
                    "). glTF intensity is photometric and unbounded; DiffuseColor is a [0,1] "
                    "colour, so a bright light imports as white (GLTF-326).");
            }

            SkeletonResult skeleton;
            if (hasSkin)
            {
                const MeshGroup& group = groups[skinnedGroupIndex];
                // plan_gltf.md GLTF-245/GLTF-247: the skeleton needs two things the skin alone
                // cannot supply -- the joints' full scene ancestry, and the world transform of the
                // node instancing the skinned mesh, which glTF requires to be cancelled. A skin
                // referenced by several nodes resolves to the first placement in this group, the
                // same documented simplification ExtractMorphWeightTrack already makes.
                Matrix meshNodeWorld = Matrix::getIdentityProperty();
                for (const MeshInstanceOut& placement : group.instances)
                {
                    if (placement.skinned) { meshNodeWorld = placement.worldTransform; break; }
                }
                skeleton = BuildSkeleton(group.skin, sceneGraph, meshNodeWorld, 1.0f);
            }

            Graphics::GraphicsDevice& device = cm.getGraphicsDeviceInternal();
            const fs::path gltfDir = fs::path(path).parent_path();

            auto res = std::make_shared<ModelResources>();
            std::vector<Graphics::ModelBone*> boneRawPtrs;
            std::vector<Graphics::ModelMesh*> meshRawPtrs;

            // One ModelBone per scene node, in the graph's own parent-before-child order, so a
            // bone's index equals its SceneGraphOut index and Model::CopyAbsoluteBoneTransformsTo
            // composes the same world transforms BuildSceneGraph computed. Bone 0 is the synthetic
            // identity root.
            for (const SceneNodeOut& node : sceneGraph.nodes)
            {
                auto bone = std::make_unique<Graphics::ModelBone>(
                    static_cast<int>(boneRawPtrs.size()), node.name);
                bone->setTransformProperty(node.localTransform);
                boneRawPtrs.push_back(bone.get());
                res->boneOwners.push_back(std::move(bone));
            }
            for (std::size_t i = 1; i < sceneGraph.nodes.size(); ++i)
            {
                const int parent = sceneGraph.nodes[i].parentIndex;
                boneRawPtrs[static_cast<std::size_t>(parent < 0 ? 0 : parent)]->AddChild(boneRawPtrs[i]);
            }
            if (hasSkin)
            {
                auto skinningData = std::make_unique<Graphics::SkinningData>();
                const int boneCount = static_cast<int>(skeleton.bones.size());
                skinningData->BoneCount = boneCount;
                skinningData->SkeletonHierarchy.resize(static_cast<std::size_t>(boneCount));
                skinningData->BindPose.resize(static_cast<std::size_t>(boneCount));
                skinningData->InverseBindPose.resize(static_cast<std::size_t>(boneCount));
                skinningData->SkeletonRootPrefix.resize(static_cast<std::size_t>(boneCount));
                for (int i = 0; i < boneCount; ++i)
                {
                    const auto ui = static_cast<std::size_t>(i);
                    skinningData->SkeletonHierarchy[ui] = skeleton.bones[ui].parentIndex;
                    skinningData->BindPose[ui] = skeleton.bones[ui].bindPoseLocal;
                    skinningData->InverseBindPose[ui] = skeleton.bones[ui].inverseBindGlobal;
                    skinningData->SkeletonRootPrefix[ui] = skeleton.bones[ui].parentWorldPrefix;
                }
                // plan_gltf.md GLTF-249: the declared rig root, carried so an application can find
                // it. Read nowhere in the transform arithmetic, by design -- see §15.1.1.
                skinningData->SkeletonRootNodeIndexEXT = skeleton.declaredSkeletonRootNodeIndex;
                skinningData->SkeletonRootNameEXT = skeleton.declaredSkeletonRootName;

                std::vector<std::string> warnings;
                const std::vector<ClipOut> clips = ExtractClips(data, skeleton, 1.0f, warnings);
                for (const ClipOut& clip : clips)
                {
                    Graphics::AnimationClipEXT outClip;
                    outClip.Duration = System::TimeSpan::FromSeconds(clip.duration);
                    outClip.Tracks.reserve(clip.tracks.size());
                    for (const TrackOut& track : clip.tracks)
                    {
                        Graphics::BoneTrackEXT outTrack;
                        outTrack.BoneIndex = track.boneIndex;
                        outTrack.Keys.reserve(track.keys.size());
                        for (const KeyframeOut& k : track.keys)
                        {
                            Graphics::KeyframeEXT key;
                            key.Time = System::TimeSpan::FromSeconds(k.time);
                            key.Translation = k.translation;
                            key.Rotation = k.rotation;
                            key.Scale = k.scale;
                            outTrack.Keys.push_back(key);
                        }
                        outClip.Tracks.push_back(std::move(outTrack));
                    }
                    skinningData->AnimationClips[clip.name] = std::move(outClip);
                }

                res->skinningData = std::move(skinningData);
            }
            else
            {
                // plan_gltf.md GLTF-294: rigid (non-joint) node animation. Only for an unskinned
                // model -- a skinned one's Tag already carries the skeleton, and that collision is
                // a recorded limitation rather than something to resolve silently (GLTF-295).
                std::vector<std::string> clipWarnings;
                const std::vector<ClipOut> rigidClips =
                    ExtractSceneNodeClips(data, sceneGraph, 1.0f, clipWarnings);
                for (const std::string& warning : clipWarnings) { CNA::Logger::Warn(warning); }
                if (!rigidClips.empty())
                {
                    auto animations = std::make_unique<Graphics::ModelAnimationsEXT>();
                    for (const ClipOut& clip : rigidClips)
                    {
                        Graphics::AnimationClipEXT outClip;
                        outClip.Duration = System::TimeSpan::FromSeconds(clip.duration);
                        outClip.TargetSpace = Graphics::ClipTargetSpaceEXT::SceneNode;
                        outClip.Tracks.reserve(clip.tracks.size());
                        for (const TrackOut& track : clip.tracks)
                        {
                            Graphics::BoneTrackEXT outTrack;
                            outTrack.BoneIndex = track.boneIndex;
                            outTrack.Keys.reserve(track.keys.size());
                            for (const KeyframeOut& key : track.keys)
                            {
                                Graphics::KeyframeEXT outKey;
                                outKey.Time = System::TimeSpan::FromSeconds(key.time);
                                outKey.Translation = key.translation;
                                outKey.Rotation = key.rotation;
                                outKey.Scale = key.scale;
                                outTrack.Keys.push_back(outKey);
                            }
                            outClip.Tracks.push_back(std::move(outTrack));
                        }
                        animations->Clips[clip.name] = std::move(outClip);
                    }
                    res->modelAnimations = std::move(animations);
                }
            }

            // Textures are decoded straight from the extracted in-memory bytes via MemoryStream --
            // no temporary files, unlike the offline CLI tool. Cached by cgltf_image* so a texture
            // shared by several primitives is only decoded once per Load<Model>() call.
            std::unordered_map<const cgltf_image*, Graphics::Texture2D*> textureCache;
            auto loadTexture = [&](const cgltf_image* image) -> Graphics::Texture2D*
            {
                if (!image) { return nullptr; }
                auto cached = textureCache.find(image);
                if (cached != textureCache.end()) { return cached->second; }
                auto extracted = ExtractImage(image, gltfDir);
                if (!extracted) { return nullptr; }
                System::IO::MemoryStream ms(extracted->bytes.data(),
                                             static_cast<std::int32_t>(extracted->bytes.size()));
                auto tex = std::make_unique<Graphics::Texture2D>(
                    Graphics::Texture2D::FromStream(device, ms));
                Graphics::Texture2D* texPtr = tex.get();
                res->textureOwners.push_back(std::move(tex));
                textureCache[image] = texPtr;
                return texPtr;
            };

            // CNB-88 (Phase 14E): DualTextureEffect's own occlusion-as-lightmap blend expects
            // "0.5 = neutral", not glTF's own real "1.0 = fully visible" occlusion convention --
            // decode/halve/re-encode before loading (see RemapOcclusionImageForDualTextureEXT's
            // own doc comment). A separate cache from textureCache above: the SAME cgltf_image*
            // could in principle also be referenced, unmodified, by a different primitive's
            // PbrEffect::OcclusionMap elsewhere in this same file.
            std::unordered_map<const cgltf_image*, Graphics::Texture2D*> remappedOcclusionCache;
            auto loadOcclusionTextureForDualTextureEXT = [&](const cgltf_image* image) -> Graphics::Texture2D*
            {
                if (!image) { return nullptr; }
                auto cached = remappedOcclusionCache.find(image);
                if (cached != remappedOcclusionCache.end()) { return cached->second; }
                auto extracted = ExtractImage(image, gltfDir);
                if (!extracted) { return nullptr; }
                auto remapped = RemapOcclusionImageForDualTextureEXT(*extracted);
                const ExtractedImage& toLoad = remapped ? *remapped : *extracted;
                System::IO::MemoryStream ms(toLoad.bytes.data(),
                                             static_cast<std::int32_t>(toLoad.bytes.size()));
                auto tex = std::make_unique<Graphics::Texture2D>(
                    Graphics::Texture2D::FromStream(device, ms));
                Graphics::Texture2D* texPtr = tex.get();
                res->textureOwners.push_back(std::move(tex));
                remappedOcclusionCache[image] = texPtr;
                return texPtr;
            };

            // GLTF-137: every group's instances, each paired with the skeleton that poses it --
            // decided once, here, instead of being re-derived inside the loop. A group whose skin
            // is not the one on Model::Tag contributes nothing and says so.
            struct ImportableInstance
            {
                const MeshInstanceOut* instance;
                const SkeletonResult* skeleton;
            };
            std::vector<ImportableInstance> importable;
            for (std::size_t gi = 0; gi < groups.size(); ++gi)
            {
                const MeshGroup& g = groups[gi];
                if (g.skin != nullptr && gi != skinnedGroupIndex)
                {
                    CNA::Logger::Warn(
                        "glTF file '" + path + "': skin '" +
                        std::string(g.skin->name ? g.skin->name : "<unnamed>") + "' drives " +
                        std::to_string(g.instances.size()) +
                        " mesh instance(s) that are not imported -- Model::Tag carries one "
                        "SkinningData and another skin already owns it (GLTF-137).");
                    continue;
                }
                const SkeletonResult* groupSkeleton = (g.skin != nullptr) ? &skeleton : nullptr;
                for (const MeshInstanceOut& placement : g.instances)
                {
                    importable.push_back({&placement, groupSkeleton});
                }
            }

            int meshCounter = 0;
            for (const ImportableInstance& entry : importable)
            {
                const MeshInstanceOut& instance = *entry.instance;
                const cgltf_mesh* mesh = instance.mesh;
                // plan_gltf.md GLTF-116/GLTF-117: a mirroring placement. §3.7.4 asks for the
                // winding to be reversed at draw time; CNA carries the fact rather than applying
                // it, for the same reason GLTF-231 carries `doubleSided` -- the cull mode is
                // per-draw device state an XNA application owns, and reversing the shared index
                // buffer instead would break the unmirrored placements of the same mesh.
                if (instance.mirroredEXT)
                {
                    CNA::Logger::Warn(
                        "glTF file '" + path + "': node '" +
                        (instance.node != nullptr && instance.node->name != nullptr
                             ? instance.node->name : "<unnamed>") +
                        "' places its mesh with a mirroring transform (negative world "
                        "determinant). CNA does not reverse the triangle winding for it, so this "
                        "placement's front faces are back-facing under the default cull mode "
                        "(GLTF-116, a documented limit).");
                }
                for (cgltf_size p = 0; p < mesh->primitives_count; ++p)
                {
                    const std::string partName = mesh->name
                        ? (std::string(mesh->name) + (mesh->primitives_count > 1 ? "_" + std::to_string(p) : ""))
                        : ("mesh" + std::to_string(meshCounter));
                    MeshOut meshOut = ExtractMesh(data, mesh->primitives[p], partName, entry.skeleton, 1.0f);

                    const int numVertices = meshOut.stride > 0
                        ? static_cast<int>(meshOut.vertexBytes.size()) / meshOut.stride : 0;
                    auto vb = BuildVertexBufferFromRawBytes(device, meshOut.stride, numVertices, meshOut.vertexBytes);

                    const int indexSize = meshOut.use32BitIndices
                        ? static_cast<int>(sizeof(std::uint32_t)) : static_cast<int>(sizeof(std::uint16_t));
                    const int numIndices = static_cast<int>(meshOut.indexBytes.size()) / indexSize;
                    // plan_gltf.md GLTF-078: the count follows the part's own topology. It is
                    // still numIndices/3 for a triangle list -- which every imported part is
                    // today, since a strip or fan was already converted to one (GLTF-072).
                    const int primCount = PrimitiveCountForTopology(meshOut.topology,
                                                                    static_cast<std::size_t>(numIndices));

                    auto ib = std::make_unique<Graphics::IndexBuffer>(
                        device,
                        meshOut.use32BitIndices ? Graphics::IndexElementSize::ThirtyTwoBits
                                                : Graphics::IndexElementSize::SixteenBits,
                        numIndices, Graphics::BufferUsage::None);
                    if (meshOut.use32BitIndices) {
                        const std::vector<std::uint32_t> indices =
                            IndicesFromBytes<std::uint32_t>(meshOut.indexBytes, numIndices);
                        ib->SetData(indices.data(), numIndices);
                    } else {
                        const std::vector<std::uint16_t> indices =
                            IndicesFromBytes<std::uint16_t>(meshOut.indexBytes, numIndices);
                        ib->SetData(indices.data(), numIndices);
                    }

                    auto part = std::make_unique<Graphics::ModelMeshPart>(
                        vb.get(), ib.get(), numVertices, primCount, 0, 0);
                    // plan_gltf.md GLTF-073: the topology travels to the draw rather than being
                    // assumed there.
                    // plan_gltf.md GLTF-241: a vertex-coloured primitive whose material is
                    // metallic-roughness cannot be imported as PBR, and says so rather than
                    // arriving quietly as a BasicEffect with its material gone.
                    if (!meshOut.unsupportedMaterialModelEXT.empty())
                    {
                        CNA::Logger::Warn(
                            "glTF file '" + path + "': primitive '" + meshOut.name +
                            "' carries COLOR_0 and a " + meshOut.unsupportedMaterialModelEXT +
                            " material. CNA has no vertex-coloured PBR vertex layout, so it is "
                            "imported through BasicEffect with its vertex colours; the material's "
                            "factors and maps are not applied (GLTF-241).");
                    }
                    // plan_gltf.md GLTF-200/GLTF-350: a map whose pixels are in a format CNA has no
                    // decoder for. The file said the texture exists; without this the model simply
                    // drew untextured and nothing anywhere said why.
                    for (const std::string& unsupported : meshOut.unsupportedTextureSourcesEXT)
                    {
                        CNA::Logger::Warn(
                            "glTF file '" + path + "': primitive '" + meshOut.name +
                            "' has a texture CNA cannot read -- " + unsupported +
                            ". That map is not applied; the primitive draws as though it had none "
                            "(GLTF-200).");
                    }
                    // plan_gltf.md GLTF-339: transmission approximated as alpha blending. Always
                    // reported -- an approximation nobody is told about is indistinguishable from
                    // a bug, which is exactly how the opaque-glass defect presented.
                    if (meshOut.transmissionApproximatedEXT)
                    {
                        CNA::Logger::Warn(
                            "glTF file '" + path + "': primitive '" + meshOut.name +
                            "' declares KHR_materials_transmission (factor " +
                            std::to_string(meshOut.transmissionFactorEXT) +
                            "). CNA has no transmission pass, so it is approximated as alpha "
                            "blending with alpha = 1 - factor. This is NOT physical: no "
                            "refraction, no roughness blur, tinted glass darkens rather than "
                            "tints what is behind it, and specular fades with the alpha "
                            "(GLTF-339)." +
                            (meshOut.transmissionHasTextureEXT
                                 ? " The material also declares a transmission texture, which has "
                                   "nowhere to go in this approximation -- the whole surface uses "
                                   "the single factor."
                                 : ""));
                    }
                    // plan_gltf.md GLTF-184/GLTF-336: one UV channel means one bakeable
                    // transform, so every other map's is silently the base colour's.
                    if (!meshOut.unbakedTextureTransformsEXT.empty())
                    {
                        std::string maps;
                        for (const std::string& map : meshOut.unbakedTextureTransformsEXT)
                        {
                            if (!maps.empty()) { maps += ", "; }
                            maps += map;
                        }
                        CNA::Logger::Warn(
                            "glTF file '" + path + "': primitive '" + meshOut.name +
                            "' declares a KHR_texture_transform on " + maps +
                            " that differs from the base colour's. CNA bakes exactly one transform "
                            "into its single UV channel, so those maps are sampled with the base "
                            "colour's coordinates instead of their own (GLTF-184).");
                    }
                    // plan_gltf.md GLTF-173: normals CNA derived rather than the file authoring
                    // them. Only reported when the derivation had to approximate -- a faceted mesh
                    // whose author already split its edges gets exact flat normals, and saying so
                    // on every such import would be noise nobody reads.
                    if (meshOut.smoothedNormalVertexCountEXT > 0)
                    {
                        CNA::Logger::Warn(
                            "glTF file '" + path + "': primitive '" + meshOut.name +
                            "' authors no NORMAL, so normals were computed per §3.7.2.1 -- but " +
                            std::to_string(meshOut.smoothedNormalVertexCountEXT) +
                            " vertex/vertices are shared between faces of different orientation. "
                            "Flat shading would duplicate those vertices once per face, which this "
                            "importer does not do, so they received the area-weighted average "
                            "instead and that edge will look smooth rather than sharp (GLTF-173).");
                    }
                    // plan_gltf.md GLTF-095/GLTF-257: influence sets past the first. The dropped
                    // share is what says whether it matters -- a fifth influence weighted 0.002 is
                    // exporter noise and one weighted 0.4 is a visibly different pose.
                    if (meshOut.extraInfluenceSetsEXT > 0)
                    {
                        CNA::Logger::Warn(
                            "glTF file '" + path + "': primitive '" + meshOut.name + "' authors " +
                            std::to_string(meshOut.extraInfluenceSetsEXT + 1) +
                            " joint influence sets. XNA's BlendIndices/BlendWeight carry exactly "
                            "four influences, so only the first set is imported; up to " +
                            std::to_string(meshOut.worstDroppedInfluenceEXT * 100.0f) +
                            "% of a vertex's influence was dropped. The retained weights are "
                            "renormalised, so the skin is coarser, not collapsed (GLTF-257).");
                    }
                    // plan_gltf.md GLTF-256: joint weights that did not sum to 1 were renormalised.
                    // Never silent -- a sum far from 1 is a broken file, not exporter quantisation,
                    // and the deviation is what tells the two apart.
                    if (meshOut.renormalisedWeightVertexCountEXT > 0)
                    {
                        CNA::Logger::Warn(
                            "glTF file '" + path + "': primitive '" + meshOut.name + "' had " +
                            std::to_string(meshOut.renormalisedWeightVertexCountEXT) +
                            " vertex/vertices whose joint weights did not sum to 1 (worst "
                            "deviation " + std::to_string(meshOut.worstWeightSumDeviationEXT) +
                            "); they were renormalised. Left as authored they would have applied a "
                            "fraction of each vertex's transform, dragging it toward the origin "
                            "(GLTF-256).");
                    }
                    if (meshOut.zeroWeightVertexCountEXT > 0)
                    {
                        CNA::Logger::Warn(
                            "glTF file '" + path + "': primitive '" + meshOut.name + "' has " +
                            std::to_string(meshOut.zeroWeightVertexCountEXT) +
                            " vertex/vertices whose joint weights sum to zero. They are left "
                            "unweighted rather than assigned to an arbitrary joint (GLTF-256).");
                    }
                    // plan_gltf.md GLTF-086: an authored tangent basis with nowhere to live.
                    if (meshOut.droppedTangentForStrideEXT)
                    {
                        CNA::Logger::Warn(
                            "glTF file '" + path + "': primitive '" + meshOut.name +
                            "' authors TANGENT, but only the PBR vertex layouts (strides 48 and 68) "
                            "have a tangent slot and this primitive uses stride " +
                            std::to_string(meshOut.stride) +
                            ", so the authored tangent basis is discarded (GLTF-086).");
                    }
                    // plan_gltf.md GLTF-181: CNA's PBR effects sample every map from ONE shared UV
                    // channel. A material whose maps disagree renders some of them from the wrong
                    // set -- detected since CNB-97 but, until now, reported only by the offline
                    // tool, so the runtime path was silently wrong on exactly the same file.
                    if (meshOut.pbrUv2Mismatch)
                    {
                        CNA::Logger::Warn(
                            "glTF file '" + path + "': primitive '" + meshOut.name +
                            "' has material maps referencing different TEXCOORD sets. CNA samples "
                            "every map from one shared UV channel, so the maps that disagree are "
                            "sampled from the wrong set (GLTF-181, a documented limit).");
                    }
                    // plan_gltf.md GLTF-079: an index count that is not a whole number of
                    // primitives. The tail was dropped rather than drawn past the end of the run,
                    // and saying so is the difference between a diagnosable export bug and a model
                    // that is quietly missing a face.
                    if (meshOut.droppedIncompleteIndicesEXT != 0)
                    {
                        CNA::Logger::Warn(
                            "glTF file '" + path + "': primitive '" + meshOut.name + "' declares " +
                            std::to_string(meshOut.droppedIncompleteIndicesEXT) +
                            " index/indices more than form a whole primitive for its mode, so that "
                            "incomplete tail was dropped (GLTF-079).");
                    }
                    if (meshOut.droppedNormalForStrideEXT)
                    {
                        CNA::Logger::Warn(
                            "glTF file '" + path + "': primitive '" + meshOut.name +
                            "' authors NORMAL, but the vertex layout chosen for it (stride " +
                            std::to_string(meshOut.stride) + ") has no normal slot, so the "
                            "normals are discarded and the primitive cannot be lit (GLTF-241).");
                    }
                    part->setPrimitiveTypeEXTProperty(PrimitiveTypeForTopology(meshOut.topology));
                    // plan_gltf.md GLTF-202/GLTF-203: the file's own sampler state, per texture
                    // slot. Without this every imported texture drew with whatever the device
                    // happened to have -- LinearWrap -- so a CLAMP_TO_EDGE asset with UVs outside
                    // [0,1] tiled instead of clamping.
                    for (std::size_t slot = 0; slot < meshOut.samplers.size(); ++slot)
                    {
                        const SamplerOut& sampler = meshOut.samplers[slot];
                        Graphics::SamplerState state;
                        state.setFilterProperty(sampler.filter);
                        state.setAddressUProperty(sampler.addressU);
                        state.setAddressVProperty(sampler.addressV);
                        part->setSamplerStateEXTProperty(static_cast<int>(slot), state);
                    }
                    Graphics::ModelMeshPart* partPtr = part.get();

                    // CNB-64/65 (Phase 13B): morph targets, attached to this part's own real XNA
                    // Tag property (see MorphTargetEXT.hpp's own doc comment). Weight animation
                    // (ExtractMorphWeightTrack) is independent of skinning -- checked regardless
                    // of hasSkin, since a mesh can have morph targets and no skin at all.
                    if (!meshOut.morphPositionDeltas.empty())
                    {
                        const std::size_t targetCount = meshOut.morphPositionDeltas.size();
                        auto morph = std::make_unique<Graphics::MorphTargetDataEXT>();
                        morph->BaseVertexBytes = meshOut.vertexBytes;
                        morph->Stride = meshOut.stride;
                        morph->PositionDeltas.reserve(targetCount);
                        morph->NormalDeltas.reserve(targetCount);
                        morph->TangentDeltas.reserve(targetCount);
                        for (std::size_t t = 0; t < targetCount; ++t)
                        {
                            morph->PositionDeltas.push_back(meshOut.morphPositionDeltas[t]);
                            morph->NormalDeltas.push_back(meshOut.morphNormalDeltas[t]);
                            // plan_gltf.md GLTF-279: without these a morphed PBR surface kept its
                            // rest-pose tangent basis, so normal mapping lit the deformed surface
                            // with the undeformed basis.
                            morph->TangentDeltas.push_back(meshOut.morphTangentDeltas[t]);
                        }
                        // GLTF-281: the instancing node's own weights win over the mesh's.
                        morph->Weights = GetMeshDefaultWeights(mesh, targetCount, instance.node);
                        if (auto weightTrack = ExtractMorphWeightTrack(data, mesh, targetCount))
                        {
                            morph->WeightTrack.Keys.reserve(weightTrack->keys.size());
                            for (const MorphWeightKeyframeOut& k : weightTrack->keys)
                            {
                                Graphics::MorphWeightKeyframeEXT key;
                                key.Time = System::TimeSpan::FromSeconds(k.time);
                                key.Weights = k.weights;
                                key.InTangent = k.inTangent;
                                key.OutTangent = k.outTangent;
                                morph->WeightTrack.Keys.push_back(std::move(key));
                            }
                            morph->WeightTrack.StepInterpolation = weightTrack->stepInterpolation;
                            morph->WeightTrack.CubicSpline = weightTrack->cubicSpline;
                        }
                        partPtr->setTagProperty(morph.get());
                        // glTF's "mesh.weights" is the default/initial blend state, not
                        // necessarily all-zero -- apply it now so the uploaded vertex buffer
                        // reflects the file author's own intended default pose, not always the
                        // raw (zero-weight) base pose. SetMorphWeightsEXT re-reads Tag, so this
                        // must run after setTagProperty() above.
                        const bool hasNonZeroDefault = std::any_of(
                            morph->Weights.begin(), morph->Weights.end(),
                            [](float w) { return w != 0.0f; });
                        if (hasNonZeroDefault)
                        {
                            Graphics::SetMorphWeightsEXT(*partPtr, morph->Weights);
                        }
                        res->morphOwners.push_back(std::move(morph));
                    }

                    const std::string& meshName = meshOut.name;
                    auto meshObj = std::make_unique<Graphics::ModelMesh>(
                        &device, meshName.empty() ? "mesh" : meshName,
                        std::vector<Graphics::ModelMeshPart*>{partPtr});

                    // plan_gltf.md GLTF-114: the mesh is parented to the bone of the node that
                    // instantiates it, so Model::Draw composes the glTF world transform for free.
                    // A skinned instance is parented to the identity root instead: glTF requires a
                    // skinned mesh's own node transform to be ignored, because its joints already
                    // place the geometry. Completing that rule -- the inverse(meshNodeWorld) term
                    // and the joint ancestry BuildSkeleton still drops -- is GLTF-245/247/260, so
                    // this is deliberately the conservative half, not a claim that skinning works.
                    const std::size_t parentBoneIndex = instance.skinned
                        ? 0u : static_cast<std::size_t>(instance.sceneNodeIndex);
                    meshObj->setParentBoneProperty(boneRawPtrs[parentBoneIndex]);

                    std::shared_ptr<Graphics::Effect> fx;
                    // PBR + skinning combo: SkinnedPbrEffect is a separate class from both
                    // SkinnedEffect and PbrEffect (see that header's own doc comment), so it must
                    // be checked before either single-flag branch below.
                    if (meshOut.skinned && meshOut.usePbr) { fx = std::make_shared<Graphics::SkinnedPbrEffect>(device); }
                    else if (meshOut.skinned) { fx = std::make_shared<Graphics::SkinnedEffect>(device); }
                    else if (meshOut.usePbr) { fx = std::make_shared<Graphics::PbrEffect>(device); }
                    else if (meshOut.useDualTexture) { fx = std::make_shared<Graphics::DualTextureEffect>(device); }
                    else { fx = std::make_shared<Graphics::BasicEffect>(device); }

                    if (Graphics::Texture2D* tex = loadTexture(meshOut.baseColorImage))
                    {
                        if (auto* basicFx = dynamic_cast<Graphics::BasicEffect*>(fx.get())) {
                            basicFx->setTextureProperty(tex);
                            basicFx->setTextureEnabledProperty(true);
                        } else if (auto* skinnedFx = dynamic_cast<Graphics::SkinnedEffect*>(fx.get())) {
                            skinnedFx->setTextureProperty(tex);
                        } else if (auto* dualFx = dynamic_cast<Graphics::DualTextureEffect*>(fx.get())) {
                            dualFx->setTextureProperty(tex);
                        } else if (auto* pbrFx = dynamic_cast<Graphics::PbrEffect*>(fx.get())) {
                            pbrFx->setTextureProperty(tex);
                        } else if (auto* skinnedPbrFx = dynamic_cast<Graphics::SkinnedPbrEffect*>(fx.get())) {
                            skinnedPbrFx->setTextureProperty(tex);
                        }
                    }

                    if (meshOut.useDualTexture)
                    {
                        if (Graphics::Texture2D* tex2 = loadOcclusionTextureForDualTextureEXT(meshOut.occlusionImage))
                        {
                            if (auto* dualFx = dynamic_cast<Graphics::DualTextureEffect*>(fx.get())) {
                                dualFx->setTexture2Property(tex2);
                            }
                        }
                    }

                    if (meshOut.usePbr)
                    {
                        if (auto* pbrFx = dynamic_cast<Graphics::PbrEffect*>(fx.get()))
                        {
                            if (Graphics::Texture2D* normalTex = loadTexture(meshOut.normalImage))
                                pbrFx->setNormalMapProperty(normalTex);
                            if (Graphics::Texture2D* mrTex = loadTexture(meshOut.metallicRoughnessImage))
                                pbrFx->setMetallicRoughnessMapProperty(mrTex);
                            if (Graphics::Texture2D* emissiveTex = loadTexture(meshOut.emissiveImage))
                                pbrFx->setEmissiveMapProperty(emissiveTex);
                            // MeshOut::occlusionImage is shared with useDualTexture's own
                            // Texture2 approximation (CNB-72/73) -- the two are mutually
                            // exclusive per-mesh (useDualTexture is forced false whenever usePbr
                            // is true), so reusing the same field here is unambiguous.
                            if (Graphics::Texture2D* occlusionTex = loadTexture(meshOut.occlusionImage))
                                pbrFx->setOcclusionMapProperty(occlusionTex);
                            pbrFx->setMetallicFactorProperty(meshOut.metallicFactor);
                            pbrFx->setRoughnessFactorProperty(meshOut.roughnessFactor);
                            pbrFx->setEmissiveFactorProperty(meshOut.emissiveFactor);
                            // plan_gltf.md GLTF-224/GLTF-225: never read before, so a material that
                            // dialled its normal map down to a subtle 0.2 got the full 1.0 instead.
                            pbrFx->setNormalScaleEXTProperty(meshOut.normalScale);
                            pbrFx->setOcclusionStrengthEXTProperty(meshOut.occlusionStrength);
                            // plan_gltf.md GLTF-216: baseColorFactor multiplies the base-colour
                            // texture, or stands alone when there is none. Never read before.
                            pbrFx->setDiffuseColorProperty(Vector3(meshOut.baseColorFactor.X,
                                                                    meshOut.baseColorFactor.Y,
                                                                    meshOut.baseColorFactor.Z));
                            pbrFx->setAlphaProperty(meshOut.baseColorFactor.W);
                            // plan_gltf.md GLTF-228/GLTF-229/GLTF-231.
                            pbrFx->setAlphaModeEXTProperty(meshOut.alphaMode);
                            pbrFx->setAlphaCutoffEXTProperty(meshOut.alphaCutoff);
                            pbrFx->setDoubleSidedEXTProperty(meshOut.doubleSided);
                        }
                        else if (auto* skinnedPbrFx = dynamic_cast<Graphics::SkinnedPbrEffect*>(fx.get()))
                        {
                            if (Graphics::Texture2D* normalTex = loadTexture(meshOut.normalImage))
                                skinnedPbrFx->setNormalMapProperty(normalTex);
                            if (Graphics::Texture2D* mrTex = loadTexture(meshOut.metallicRoughnessImage))
                                skinnedPbrFx->setMetallicRoughnessMapProperty(mrTex);
                            if (Graphics::Texture2D* emissiveTex = loadTexture(meshOut.emissiveImage))
                                skinnedPbrFx->setEmissiveMapProperty(emissiveTex);
                            if (Graphics::Texture2D* occlusionTex = loadTexture(meshOut.occlusionImage))
                                skinnedPbrFx->setOcclusionMapProperty(occlusionTex);
                            skinnedPbrFx->setMetallicFactorProperty(meshOut.metallicFactor);
                            skinnedPbrFx->setRoughnessFactorProperty(meshOut.roughnessFactor);
                            skinnedPbrFx->setEmissiveFactorProperty(meshOut.emissiveFactor);
                            // plan_gltf.md GLTF-224/GLTF-225: never read before, so a material that
                            // dialled its normal map down to a subtle 0.2 got the full 1.0 instead.
                            skinnedPbrFx->setNormalScaleEXTProperty(meshOut.normalScale);
                            skinnedPbrFx->setOcclusionStrengthEXTProperty(meshOut.occlusionStrength);
                            // GLTF-216, on the skinned twin: the same base colour, so a skinned
                            // and an unskinned primitive of one material shade alike.
                            skinnedPbrFx->setDiffuseColorProperty(Vector3(meshOut.baseColorFactor.X,
                                                                          meshOut.baseColorFactor.Y,
                                                                          meshOut.baseColorFactor.Z));
                            skinnedPbrFx->setAlphaProperty(meshOut.baseColorFactor.W);
                            skinnedPbrFx->setAlphaModeEXTProperty(meshOut.alphaMode);
                            skinnedPbrFx->setAlphaCutoffEXTProperty(meshOut.alphaCutoff);
                            skinnedPbrFx->setDoubleSidedEXTProperty(meshOut.doubleSided);
                        }
                    }

                    if (meshOut.colored)
                    {
                        if (auto* basicFx = dynamic_cast<Graphics::BasicEffect*>(fx.get())) {
                            basicFx->VertexColorEnabled = true;
                        } else if (auto* skinnedFx = dynamic_cast<Graphics::SkinnedEffect*>(fx.get())) {
                            skinnedFx->VertexColorEnabled = true;
                        }
                    }

                    ApplyPunctualLightsEXT(*fx, punctualLights);

                    partPtr->setEffectProperty(fx.get());
                    res->effectOwners.push_back(std::move(fx));

                    meshRawPtrs.push_back(meshObj.get());
                    res->vbs.push_back(std::move(vb));
                    res->ibs.push_back(std::move(ib));
                    res->partOwners.push_back(std::move(part));
                    res->meshOwners.push_back(std::move(meshObj));
                    ++meshCounter;
                }
            }

            if (meshRawPtrs.empty())
            {
                throw ContentLoadException("glTF file '" + path + "' contains no mesh primitives to import.");
            }

            Graphics::Model model(&device, std::move(boneRawPtrs), std::move(meshRawPtrs));
            model.setOwnedResources(res);
            // plan_gltf.md GLTF-294: an unskinned model's Tag carries its rigid clips instead.
            if (res->skinningData)      { model.setTagProperty(res->skinningData.get()); }
            else if (res->modelAnimations) { model.setTagProperty(res->modelAnimations.get()); }
            // plan_gltf.md GLTF-262: a skinned effect's palette defaults to identity matrices,
            // which means "every joint matrix is the identity" -- not "no skinning". Drawn that
            // way the mesh is posed in joint space and glTF's own inverse(meshNodeWorld)
            // cancellation never applies, so a model nobody has animated yet renders wrong rather
            // than merely still. Posing the bind pose here makes a freshly loaded model drawable;
            // any later SetBoneTransforms simply overwrites it.
            if (res->skinningData)
            {
                Graphics::ApplyBindPoseBoneTransformsEXT(model, *res->skinningData);
            }
            // plan_gltf.md GLTF-317 … GLTF-321: the file's own cameras. Projection built from the
            // source's own parameters here rather than at use time, so an application never has to
            // reimplement glTF's infinite-far-plane case to draw what the author framed.
            {
                std::vector<Graphics::ModelCameraEXT> cameras;
                for (const CameraOut& camera : ExtractCamerasEXT(data, sceneGraph, 1.0f))
                {
                    Graphics::ModelCameraEXT out;
                    out.Name = camera.name;
                    out.SceneNodeIndex = camera.sceneNodeIndex;
                    out.WorldTransform = camera.worldTransform;
                    out.IsPerspective = camera.perspective;
                    if (camera.perspective)
                    {
                        // §3.10.3: an absent aspectRatio means "use the viewport's". The importer
                        // has no viewport, so 1 is used and the application is expected to rebuild
                        // the projection if it cares -- which it can, since yfov and znear are
                        // recoverable from the matrix and the node transform is carried separately.
                        const float aspect = camera.aspectRatio > 0.0f ? camera.aspectRatio : 1.0f;
                        out.HasInfiniteFarPlane = (camera.zfar <= 0.0f);
                        out.Projection = out.HasInfiniteFarPlane
                            ? Graphics::CreateInfinitePerspectiveFieldOfViewEXT(
                                  camera.yfov, aspect, camera.znear)
                            : Matrix::CreatePerspectiveFieldOfView(
                                  camera.yfov, aspect, camera.znear, camera.zfar);
                    }
                    else
                    {
                        // §3.10.2: xmag/ymag are HALF extents, so the orthographic volume is twice
                        // each. Halving them here would be the classic silent factor-of-two.
                        out.Projection = Matrix::CreateOrthographic(
                            camera.xmag * 2.0f, camera.ymag * 2.0f, camera.znear, camera.zfar);
                    }
                    cameras.push_back(std::move(out));
                }
                model.setCamerasEXTProperty(std::move(cameras));
            }
            return model;
        }

        class ModelTypeReader : public LooseFileContentTypeReader<Graphics::Model>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                // CNB-70/71 (Phase 13D): .gltf/.glb are tried after .cnj (ResolveAssetPath's own
                // "always try .cnj first" rule), so an asset with both a .cnj sidecar and a
                // same-named .gltf/.glb file still resolves to the .cnj -- matching cnj.md's
                // established "sidecar always wins" convention for every other native format.
                return {".cnj", ".gltf", ".glb"};
            }

            Graphics::Model Read(const std::string& path, ContentManager& cm) override
            {
                namespace fs = std::filesystem;

                // CNB-70/71 (Phase 13D): a .gltf/.glb path is parsed directly, with no .cnj/binary
                // sidecar files at all -- see ReadGltfModel()'s own doc comment.
                const std::string ext = fs::path(path).extension().string();
                if (ext == ".gltf" || ext == ".glb")
                {
                    return ReadGltfModel(path, cm);
                }

                const std::string json = ReadTextFile(path);

                const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
                // plan_gltf.md GLTF-129: Model is the one type with a version 2 -- it adds the
                // "bones" hierarchy and the per-mesh "parentBone" index. Every other type still
                // accepts version 1 only, so an unknown future version stays a hard error there.
                CNA::Internal::ValidateCnjEnvelope(envelope, "Model", path, /*maxVersion=*/2);
                RejectSourceFileForSelfContainedCnj(envelope, "Model", path);

                const std::string root = cm.getRootDirectoryProperty();
                Graphics::GraphicsDevice& device = cm.getGraphicsDeviceInternal();

                // Owned resources shared by all copies of the returned Model (ModelResources is
                // hoisted to file scope, shared with ReadGltfModel()).
                auto res = std::make_shared<ModelResources>();

                std::vector<Graphics::ModelBone*> boneRawPtrs;
                std::vector<Graphics::ModelMesh*> meshRawPtrs;

                // plan_gltf.md GLTF-129/GLTF-130 (Phase 5): a cnjVersion-2 Model .cnj carries the
                // whole node graph in "bones" (parent-before-child, index 0 the identity root) and
                // each mesh names its own "parentBone" index into it. Rebuilding that tree here is
                // what makes the offline .cnj path place geometry identically to the runtime
                // .gltf path, rather than collapsing every part onto an identity root.
                //
                // Backward compatible with cnjVersion 1: a file whose "bones" is absent, or holds
                // only a root name, yields exactly the previous single-Root shape, and the per-mesh
                // fallback below still gives each mesh its own named child bone.
                const std::vector<CnjBoneEntry> cnjBones = ParseCnjBoneArrayEXT(json);
                const bool hasBoneHierarchy = cnjBones.size() > 1;
                {
                    std::string rootName = cnjBones.empty() ? std::string("Root") : cnjBones.front().name;
                    if (rootName.empty()) { rootName = "Root"; }
                    auto bone = std::make_unique<Graphics::ModelBone>(0, std::move(rootName));
                    boneRawPtrs.push_back(bone.get());
                    res->boneOwners.push_back(std::move(bone));
                }
                for (std::size_t b = 1; b < cnjBones.size(); ++b)
                {
                    const CnjBoneEntry& entry = cnjBones[b];
                    auto bone = std::make_unique<Graphics::ModelBone>(
                        static_cast<int>(boneRawPtrs.size()),
                        entry.name.empty() ? ("Node" + std::to_string(b)) : entry.name);
                    bone->setTransformProperty(entry.transform);
                    boneRawPtrs.push_back(bone.get());
                    res->boneOwners.push_back(std::move(bone));
                }
                for (std::size_t b = 1; b < cnjBones.size(); ++b)
                {
                    const int parent = cnjBones[b].parent;
                    if (parent < 0 || static_cast<std::size_t>(parent) >= boneRawPtrs.size())
                    {
                        throw ContentLoadException(
                            "Model .cnj bone " + std::to_string(b) + " has an out-of-range parent index ("
                                + std::to_string(parent) + "): " + path);
                    }
                    boneRawPtrs[static_cast<std::size_t>(parent)]->AddChild(boneRawPtrs[b]);
                }
                // Task 937: root_ is always bones_.bones_[0] once the model is constructed below
                // (see Model::Model), so the just-pushed root bone stays at a stable, known index
                // regardless of how many per-mesh child bones get appended after it.
                Graphics::ModelBone* rootBone = boneRawPtrs.front();

                // Skeletal animation (Task 941, Phase 77) — an optional "skeleton" field names a
                // .skeleton.bin file (byte-for-byte the same format SkinnedModelTypeReader's own
                // "skeleton" field already reads, see Task 939's own design decision); when
                // present, an optional "animations" field (same {name, clip} shape and .clip.bin
                // format as SkinnedModelTypeReader's own "animations" field) supplies the
                // clips AnimationPlayer will play back. The result is attached to the returned
                // Model's own Tag property below, mirroring the real XNA Skinned Model Sample's
                // own convention (real XNA's Model has no dedicated skinning-data property).
                const std::string skeletonRel = ExtractJsonStringField(json, "skeleton");
                if (!skeletonRel.empty())
                {
                    const std::string skeletonPath = ResolveRootRelativeSidecarPath(
                        cm, path, "skeleton", skeletonRel);
                    const auto skelBytes = ReadBinaryFile(skeletonPath);
                    BinReaderEXT skelReader{skelBytes};
                    const int boneCount = skelReader.Read<std::int32_t>();
                    // Mirrors SkinnedModelTypeReader's own identical bone-count sanity check
                    // (Task 11.8) — a corrupt/negative file value must not reach a std::vector
                    // resize.
                    constexpr int kMaxSaneBoneCount = 100000;
                    if (boneCount < 0 || boneCount > kMaxSaneBoneCount)
                    {
                        throw ContentLoadException(
                            "Model skeleton has an invalid bone count (" + std::to_string(boneCount)
                                + "): " + path);
                    }

                    auto skinningData = std::make_unique<Graphics::SkinningData>();
                    skinningData->BoneCount = boneCount;
                    skinningData->SkeletonHierarchy.resize(static_cast<std::size_t>(boneCount));
                    for (int i = 0; i < boneCount; ++i)
                        skinningData->SkeletonHierarchy[static_cast<std::size_t>(i)] = skelReader.Read<std::int32_t>();
                    skinningData->BindPose.resize(static_cast<std::size_t>(boneCount));
                    for (int i = 0; i < boneCount; ++i)
                        skinningData->BindPose[static_cast<std::size_t>(i)] = skelReader.ReadMatrix();
                    skinningData->InverseBindPose.resize(static_cast<std::size_t>(boneCount));
                    for (int i = 0; i < boneCount; ++i)
                        skinningData->InverseBindPose[static_cast<std::size_t>(i)] = skelReader.ReadMatrix();

                    // plan_gltf.md GLTF-245/GLTF-247: an optional third matrix block, the per-root
                    // prefix carrying the joints' scene ancestry and the skinned mesh node's
                    // cancellation. Appended after the original two blocks precisely so a sidecar
                    // written before it still loads: when the bytes are absent the array is left
                    // empty, which AnimationPlayer reads as all-identity -- the previous behaviour.
                    if (skelReader.Remaining() >= static_cast<std::size_t>(boneCount) * 64u)
                    {
                        skinningData->SkeletonRootPrefix.resize(static_cast<std::size_t>(boneCount));
                        for (int i = 0; i < boneCount; ++i)
                            skinningData->SkeletonRootPrefix[static_cast<std::size_t>(i)] = skelReader.ReadMatrix();
                    }

                    for (const std::string& ag : ParseFlatObjectArrayEXT(json, "animations"))
                    {
                        const std::string name     = ExtractJsonStringField(ag, "name");
                        const std::string clipFile = ExtractJsonStringField(ag, "clip");
                        if (name.empty() || clipFile.empty()) { continue; }
                        const std::string clipPath = ResolveRootRelativeSidecarPath(
                            cm, path, "clip", clipFile);
                        skinningData->AnimationClips[name] =
                            ReadAnimationClipRefEXT(clipPath, root, cm);
                    }

                    res->skinningData = std::move(skinningData);
                }
                else
                {
                    // plan_gltf.md GLTF-294: a .cnj with no skeleton may still carry rigid
                    // (non-joint) clips, whose track indices are Model::Bones indices. Before
                    // this, the "animations" array was read only inside the skeleton branch, so an
                    // unskinned model's clips were parsed by nobody -- the reader half of D6.
                    auto animations = std::make_unique<Graphics::ModelAnimationsEXT>();
                    for (const std::string& ag : ParseFlatObjectArrayEXT(json, "animations"))
                    {
                        const std::string name     = ExtractJsonStringField(ag, "name");
                        const std::string clipFile = ExtractJsonStringField(ag, "clip");
                        if (name.empty() || clipFile.empty()) { continue; }
                        Graphics::AnimationClipEXT clip = ReadAnimationClipRefEXT(
                            ResolveRootRelativeSidecarPath(cm, path, "clip", clipFile), root, cm);
                        if (clip.TargetSpace != Graphics::ClipTargetSpaceEXT::SceneNode)
                        {
                            // A palette clip on a model with no skeleton has nothing to index.
                            // Saying so beats attaching it somewhere it would pose wrong bones.
                            CNA::Logger::Warn(
                                "Model '" + path + "' has no skeleton but its clip '" + name +
                                "' targets a joint palette -- skipped.");
                            continue;
                        }
                        animations->Clips[name] = std::move(clip);
                    }
                    if (!animations->Clips.empty()) { res->modelAnimations = std::move(animations); }
                }

                // CNB-97 (Phase 14H): KHR_lights_punctual, written by gltf_to_cnj.cpp as a
                // top-level "lights" array (see that file's own doc comment) -- applied to every
                // mesh part's effect below via ApplyPunctualLightsEXT.
                std::vector<CNA::Internal::GltfImport::LightOut> punctualLights;
                for (const std::string& lg : ParseFlatObjectArrayEXT(json, "lights"))
                {
                    CNA::Internal::GltfImport::LightOut light;
                    const auto dir = JsonFloatArray3(lg, FindKeyArray(lg, "direction"));
                    const auto diff = JsonFloatArray3(lg, FindKeyArray(lg, "diffuseColor"));
                    light.direction = Vector3(dir[0], dir[1], dir[2]);
                    light.diffuseColor = Vector3(diff[0], diff[1], diff[2]);
                    punctualLights.push_back(light);
                }

                // Meshes
                const std::size_t mk = json.find("\"meshes\"");
                if (mk != std::string::npos) {
                    const std::size_t ma = json.find('[', mk);
                    if (ma != std::string::npos) {
                        std::size_t pos = ma + 1;
                        while (true) {
                            const std::size_t os = json.find('{', pos);
                            if (os == std::string::npos) break;
                            int depth = 1;
                            std::size_t oe = os + 1;
                            while (oe < json.size() && depth > 0) {
                                if (json[oe] == '{') ++depth;
                                else if (json[oe] == '}') --depth;
                                ++oe;
                            }
                            const std::string mg = json.substr(os, oe - os);
                            pos = oe;

                            const std::string meshName   = ExtractJsonStringField(mg, "name");
                            const std::string vertFile   = ExtractJsonStringField(mg, "vertices");
                            const std::string idxFile    = ExtractJsonStringField(mg, "indices");
                            const int         stride     = JsonInt(mg, "vertexStride", 16);
                            const std::string effectStr  = ExtractJsonStringField(mg, "effect");
                            const std::string textureFile = ExtractJsonStringField(mg, "texture");
                            const std::string texture2File = ExtractJsonStringField(mg, "texture2");
                            const bool vertexColorEnabled = JsonBool(mg, "vertexColorEnabled", false);
                            // CNB-59 (Phase 13A): PbrEffect's own 4 maps + factor values.
                            const std::string normalMapFile = ExtractJsonStringField(mg, "normalMap");
                            const std::string metallicRoughnessMapFile = ExtractJsonStringField(mg, "metallicRoughnessMap");
                            const std::string emissiveMapFile = ExtractJsonStringField(mg, "emissiveMap");
                            const std::string occlusionMapFile = ExtractJsonStringField(mg, "occlusionMap");
                            const float metallicFactor  = JsonFloat(mg, "metallicFactor", 1.0f);
                            const float roughnessFactor = JsonFloat(mg, "roughnessFactor", 1.0f);
                            const auto emissiveFactorArr = JsonFloatArray3(mg, FindKeyArray(mg, "emissiveFactor"));
                            // plan_gltf.md GLTF-228/GLTF-229/GLTF-231. Absent from a .cnj written
                            // before them, whose defaults are glTF's own -- so an older asset loads
                            // as the opaque, single-sided material it could only ever have been.
                            const auto alphaMode = CNA::Internal::GltfImport::AlphaModeEXTFromName(
                                ExtractJsonStringField(mg, "alphaMode"));
                            const float alphaCutoff = JsonFloat(mg, "alphaCutoff", 0.5f);
                            const bool doubleSided = JsonBool(mg, "doubleSided", false);
                            // Morph target CLI/.cnj serialization: "morphTargets" is the binary
                            // sidecar path (BuildMorphBytes' own format, see gltf_to_cnj.cpp),
                            // "morphWeights" the default blend weights, and "morphWeightTrack"
                            // (optional) the weight animation track.
                            const std::string morphTargetsFile = ExtractJsonStringField(mg, "morphTargets");
                            const std::vector<float> morphWeightsField =
                                JsonFloatArrayN(mg, FindKeyArray(mg, "morphWeights"));
                            const std::string morphWeightTrackJson = ExtractJsonObjectFieldEXT(mg, "morphWeightTrack");

                            if (vertFile.empty() || idxFile.empty())
                                continue;

                            const std::string vertPath = ResolveRootRelativeSidecarPath(
                                cm, path, "vertices", vertFile);
                            const std::string idxPath = ResolveRootRelativeSidecarPath(
                                cm, path, "indices", idxFile);
                            std::optional<std::string> morphTargetsPath;
                            if (!morphTargetsFile.empty())
                            {
                                morphTargetsPath = ResolveRootRelativeSidecarPath(
                                    cm, path, "morphTargets", morphTargetsFile);
                            }

                            const auto vertBytes = ReadBinaryFile(vertPath);
                            const auto idxBytes  = ReadBinaryFile(idxPath);

                            if (stride <= 0) continue;
                            const int numVertices = static_cast<int>(vertBytes.size()) / stride;
                            // Task 931: real XNA's stock ModelProcessor auto-selects 32-bit
                            // indices (IndexElementSize.ThirtyTwoBits) once a mesh exceeds 65535
                            // vertices -- mirror that selection here rather than hardcoding
                            // 16-bit, which silently mis-decoded the index buffer (wrong element
                            // count, wrong byte offsets) for any larger mesh.
                            const bool use32BitIndices = numVertices > 65535;
                            const int  indexSize  = use32BitIndices
                                                        ? static_cast<int>(sizeof(std::uint32_t))
                                                        : static_cast<int>(sizeof(std::uint16_t));
                            const int numIndices  = static_cast<int>(idxBytes.size()) / indexSize;
                            // plan_gltf.md GLTF-073/GLTF-078: the part's own topology, defaulting
                            // to TRIANGLES for a .cnj written before the field existed -- which
                            // could only ever have held a triangle list anyway.
                            const auto topology =
                                CNA::Internal::GltfImport::PrimitiveTopologyFromName(
                                    ExtractJsonStringField(mg, "primitiveTopology"));
                            const int primCount =
                                CNA::Internal::GltfImport::PrimitiveCountForTopology(
                                    topology, static_cast<std::size_t>(numIndices));

                            auto vb = BuildVertexBufferFromRawBytes(device, stride, numVertices, vertBytes);

                            auto ib = std::make_unique<Graphics::IndexBuffer>(
                                device,
                                use32BitIndices ? Graphics::IndexElementSize::ThirtyTwoBits
                                                : Graphics::IndexElementSize::SixteenBits,
                                numIndices, Graphics::BufferUsage::None);
                            if (use32BitIndices) {
                                const std::vector<std::uint32_t> indices =
                                    IndicesFromBytes<std::uint32_t>(idxBytes, numIndices);
                                ib->SetData(indices.data(), numIndices);
                            } else {
                                const std::vector<std::uint16_t> indices =
                                    IndicesFromBytes<std::uint16_t>(idxBytes, numIndices);
                                ib->SetData(indices.data(), numIndices);
                            }

                            auto part = std::make_unique<Graphics::ModelMeshPart>(
                                vb.get(), ib.get(), numVertices, primCount, 0, 0);
                            part->setPrimitiveTypeEXTProperty(
                                CNA::Internal::GltfImport::PrimitiveTypeForTopology(topology));
                            Graphics::ModelMeshPart* partPtr = part.get();

                            // Morph target CLI/.cnj serialization: read BuildMorphBytes' own
                            // binary sidecar format back and attach the result to this part's own
                            // real XNA Tag property, mirroring ReadGltfModel()'s identical
                            // MorphTargetDataEXT wiring for the runtime glTF path (see
                            // MorphTargetEXT.hpp's own doc comments).
                            if (morphTargetsPath.has_value())
                            {
                                const auto morphBytes = ReadBinaryFile(*morphTargetsPath);
                                BinReaderEXT morphReader{morphBytes};
                                const int targetCount = morphReader.Read<std::int32_t>();
                                constexpr int kMaxSaneTargetCount = 100000;
                                if (targetCount < 0 || targetCount > kMaxSaneTargetCount)
                                {
                                    throw ContentLoadException(
                                        "Model mesh has an invalid morph target count ("
                                            + std::to_string(targetCount) + "): " + path);
                                }

                                auto morph = std::make_unique<Graphics::MorphTargetDataEXT>();
                                morph->BaseVertexBytes = vertBytes;
                                morph->Stride = stride;
                                morph->PositionDeltas.reserve(static_cast<std::size_t>(targetCount));
                                morph->NormalDeltas.reserve(static_cast<std::size_t>(targetCount));
                                for (int t = 0; t < targetCount; ++t)
                                {
                                    const int vertexCount = morphReader.Read<std::int32_t>();
                                    std::vector<Vector3> positions;
                                    positions.reserve(static_cast<std::size_t>(vertexCount));
                                    for (int v = 0; v < vertexCount; ++v)
                                    {
                                        const float x = morphReader.Read<float>();
                                        const float y = morphReader.Read<float>();
                                        const float z = morphReader.Read<float>();
                                        positions.emplace_back(x, y, z);
                                    }
                                    morph->PositionDeltas.push_back(std::move(positions));

                                    std::vector<Vector3> normals;
                                    if (morphReader.Read<std::int32_t>() != 0)
                                    {
                                        normals.reserve(static_cast<std::size_t>(vertexCount));
                                        for (int v = 0; v < vertexCount; ++v)
                                        {
                                            const float x = morphReader.Read<float>();
                                            const float y = morphReader.Read<float>();
                                            const float z = morphReader.Read<float>();
                                            normals.emplace_back(x, y, z);
                                        }
                                    }
                                    morph->NormalDeltas.push_back(std::move(normals));
                                }

                                morph->Weights = !morphWeightsField.empty()
                                    ? morphWeightsField
                                    : std::vector<float>(static_cast<std::size_t>(targetCount), 0.0f);

                                if (!morphWeightTrackJson.empty())
                                {
                                    morph->WeightTrack.StepInterpolation =
                                        JsonBool(morphWeightTrackJson, "stepInterpolation", false);
                                    morph->WeightTrack.CubicSpline =
                                        JsonBool(morphWeightTrackJson, "cubicSpline", false);
                                    for (const std::string& kg : ParseFlatObjectArrayEXT(morphWeightTrackJson, "keys"))
                                    {
                                        Graphics::MorphWeightKeyframeEXT key;
                                        key.Time = System::TimeSpan::FromSeconds(JsonFloat(kg, "time", 0.0f));
                                        key.Weights = JsonFloatArrayN(kg, FindKeyArray(kg, "weights"));
                                        key.InTangent = JsonFloatArrayN(kg, FindKeyArray(kg, "inTangent"));
                                        key.OutTangent = JsonFloatArrayN(kg, FindKeyArray(kg, "outTangent"));
                                        morph->WeightTrack.Keys.push_back(std::move(key));
                                    }
                                }

                                partPtr->setTagProperty(morph.get());
                                // glTF's "mesh.weights" is the default/initial blend state, not
                                // necessarily all-zero -- apply it now so the uploaded vertex
                                // buffer reflects the file author's own intended default pose, not
                                // always the raw zero-weight base (mirrors ReadGltfModel()'s
                                // identical logic).
                                const bool hasNonZeroDefault = std::any_of(
                                    morph->Weights.begin(), morph->Weights.end(),
                                    [](float w) { return w != 0.0f; });
                                if (hasNonZeroDefault)
                                {
                                    Graphics::SetMorphWeightsEXT(*partPtr, morph->Weights);
                                }
                                res->morphOwners.push_back(std::move(morph));
                            }

                            auto mesh = std::make_unique<Graphics::ModelMesh>(
                                &device, meshName.empty() ? "mesh" : meshName,
                                std::vector<Graphics::ModelMeshPart*>{partPtr});

                            // Task 937: give this mesh its own real ModelBone (a child of the
                            // model's Root, named after the mesh) instead of leaving ParentBone
                            // null -- unblocks samples whose own game code looks up a named bone
                            // per rigid part (e.g. SplitScreen/TankOnAHeightMap's wheel/turret/
                            // cannon/hatch bone lookups) via Model.Bones["PartName"]. Mesh names
                            // in every currently-known .model.json asset already match the bone
                            // names real ported game code expects.
                            // plan_gltf.md GLTF-114/GLTF-129: a cnjVersion-2 file already carries a
                            // real bone per scene node, so the mesh is attached to the one its own
                            // "parentBone" names -- that is what makes the offline path place
                            // geometry where glTF says, instead of at the identity root.
                            //
                            // Without a hierarchy (cnjVersion 1, and every hand-written .model.json
                            // asset) the previous behavior is preserved exactly: give this mesh its
                            // own real ModelBone (a child of Root, named after the mesh) rather than
                            // leaving ParentBone null -- Task 937, which unblocked samples whose own
                            // game code looks up a named bone per rigid part (e.g. SplitScreen/
                            // TankOnAHeightMap's wheel/turret/cannon/hatch lookups) via
                            // Model.Bones["PartName"].
                            if (hasBoneHierarchy)
                            {
                                const int parentBone = JsonInt(mg, "parentBone", 0);
                                if (parentBone < 0 || static_cast<std::size_t>(parentBone) >= boneRawPtrs.size())
                                {
                                    throw ContentLoadException(
                                        "Model mesh names an out-of-range parentBone index ("
                                            + std::to_string(parentBone) + "): " + path);
                                }
                                mesh->setParentBoneProperty(boneRawPtrs[static_cast<std::size_t>(parentBone)]);
                            }
                            else
                            {
                                auto meshBone = std::make_unique<Graphics::ModelBone>(
                                    static_cast<int>(boneRawPtrs.size()),
                                    meshName.empty() ? "mesh" : meshName);
                                rootBone->AddChild(meshBone.get());
                                mesh->setParentBoneProperty(meshBone.get());
                                boneRawPtrs.push_back(meshBone.get());
                                res->boneOwners.push_back(std::move(meshBone));
                            }

                            // Load effect and register it in the mesh's effect collection.
                            std::shared_ptr<Graphics::Effect> fx;
                            if (effectStr.empty() || effectStr == "BasicEffect") {
                                fx = std::make_shared<Graphics::BasicEffect>(device);
                            } else if (effectStr == "SkinnedEffect") {
                                // Task 941 (Phase 77): the real Skinned Model Sample's own
                                // effect for GPU-skinned meshes -- AnimationPlayer's own
                                // GetSkinTransforms() feeds SkinnedEffect::SetBoneTransforms()
                                // directly (see Task 942), not through this reader.
                                fx = std::make_shared<Graphics::SkinnedEffect>(device);
                            } else if (effectStr == "DualTextureEffect") {
                                // CNB-73: real XNA's two-layer multitexturing effect -- always
                                // samples both texture slots (no TextureEnabled toggle), so its
                                // vertex buffer must be the stride-20 VertexPositionTexture shape
                                // (no Normal in between location0/location1, see ApplyLayout's
                                // stride==20 case) rather than stride-32.
                                fx = std::make_shared<Graphics::DualTextureEffect>(device);
                            } else if (effectStr == "PbrEffect") {
                                // CNB-56/58 (Phase 13A): CNAEXT metallic-roughness PBR effect --
                                // its vertex buffer must be the stride-48
                                // VertexPositionNormalTangentTexture shape (Position+Normal+
                                // Tangent+TextureCoordinate), never stride-32.
                                fx = std::make_shared<Graphics::PbrEffect>(device);
                            } else if (effectStr == "SkinnedPbrEffect") {
                                // PBR + skinning combo: PbrEffect's own BRDF applied to a
                                // GPU-skinned mesh -- its vertex buffer must be the stride-68
                                // VertexPositionNormalTangentTextureSkinned shape. Bone transforms
                                // are fed by AnimationPlayer::GetSkinTransforms() at draw time,
                                // same as SkinnedEffect above -- not through this reader.
                                fx = std::make_shared<Graphics::SkinnedPbrEffect>(device);
                            } else {
                                fx = cm.Load<std::shared_ptr<Graphics::Effect>>(
                                    ResolveRootRelativeAssetName(
                                        cm, path, "effect", effectStr));
                            }

                            // Task 932: bind a per-mesh diffuse texture, if the descriptor names
                            // one -- mirrors SkinnedModelTypeReader's own already-working
                            // per-part texture loading. BasicEffect needs TextureEnabled
                            // explicitly turned on; SkinnedEffect's real XNA shader is always
                            // textured (no such toggle exists on it). A custom effect loaded
                            // via "effect" has no standard texture slot to bind through here.
                            if (!textureFile.empty()) {
                                auto tex = std::make_unique<Graphics::Texture2D>(
                                    cm.Load<Graphics::Texture2D>(
                                        ResolveRootRelativeAssetName(
                                            cm, path, "texture", textureFile)));
                                if (auto* basicFx = dynamic_cast<Graphics::BasicEffect*>(fx.get())) {
                                    basicFx->setTextureProperty(tex.get());
                                    basicFx->setTextureEnabledProperty(true);
                                    res->textureOwners.push_back(std::move(tex));
                                } else if (auto* skinnedFx = dynamic_cast<Graphics::SkinnedEffect*>(fx.get())) {
                                    skinnedFx->setTextureProperty(tex.get());
                                    res->textureOwners.push_back(std::move(tex));
                                } else if (auto* dualFx = dynamic_cast<Graphics::DualTextureEffect*>(fx.get())) {
                                    dualFx->setTextureProperty(tex.get());
                                    res->textureOwners.push_back(std::move(tex));
                                } else if (auto* pbrFx = dynamic_cast<Graphics::PbrEffect*>(fx.get())) {
                                    pbrFx->setTextureProperty(tex.get());
                                    res->textureOwners.push_back(std::move(tex));
                                } else if (auto* skinnedPbrFx = dynamic_cast<Graphics::SkinnedPbrEffect*>(fx.get())) {
                                    skinnedPbrFx->setTextureProperty(tex.get());
                                    res->textureOwners.push_back(std::move(tex));
                                }
                            }

                            // CNB-73: the second (layer-1) texture slot -- only meaningful for
                            // DualTextureEffect, which is the only stock effect with a Texture2
                            // parameter.
                            if (!texture2File.empty()) {
                                if (auto* dualFx = dynamic_cast<Graphics::DualTextureEffect*>(fx.get())) {
                                    auto tex2 = std::make_unique<Graphics::Texture2D>(
                                        cm.Load<Graphics::Texture2D>(
                                            ResolveRootRelativeAssetName(
                                                cm, path, "texture2", texture2File)));
                                    dualFx->setTexture2Property(tex2.get());
                                    res->textureOwners.push_back(std::move(tex2));
                                }
                            }

                            // CNB-59 (Phase 13A): PbrEffect's own 4 maps + factor values.
                            if (auto* pbrFx = dynamic_cast<Graphics::PbrEffect*>(fx.get())) {
                                auto loadPbrMap = [&](const std::string& file,
                                                      const char* field) -> Graphics::Texture2D* {
                                    if (file.empty()) { return nullptr; }
                                    auto tex = std::make_unique<Graphics::Texture2D>(
                                        cm.Load<Graphics::Texture2D>(
                                            ResolveRootRelativeAssetName(
                                                cm, path, field, file)));
                                    Graphics::Texture2D* texPtr = tex.get();
                                    res->textureOwners.push_back(std::move(tex));
                                    return texPtr;
                                };
                                if (Graphics::Texture2D* t = loadPbrMap(normalMapFile, "normalMap"))
                                    pbrFx->setNormalMapProperty(t);
                                if (Graphics::Texture2D* t = loadPbrMap(
                                        metallicRoughnessMapFile, "metallicRoughnessMap"))
                                    pbrFx->setMetallicRoughnessMapProperty(t);
                                if (Graphics::Texture2D* t = loadPbrMap(emissiveMapFile, "emissiveMap"))
                                    pbrFx->setEmissiveMapProperty(t);
                                if (Graphics::Texture2D* t = loadPbrMap(occlusionMapFile, "occlusionMap"))
                                    pbrFx->setOcclusionMapProperty(t);
                                pbrFx->setMetallicFactorProperty(metallicFactor);
                                pbrFx->setRoughnessFactorProperty(roughnessFactor);
                                pbrFx->setEmissiveFactorProperty(Vector3(
                                    emissiveFactorArr[0], emissiveFactorArr[1], emissiveFactorArr[2]));
                                pbrFx->setAlphaModeEXTProperty(alphaMode);
                                pbrFx->setAlphaCutoffEXTProperty(alphaCutoff);
                                pbrFx->setDoubleSidedEXTProperty(doubleSided);
                            } else if (auto* skinnedPbrFx = dynamic_cast<Graphics::SkinnedPbrEffect*>(fx.get())) {
                                auto loadPbrMap = [&](const std::string& file,
                                                      const char* field) -> Graphics::Texture2D* {
                                    if (file.empty()) { return nullptr; }
                                    auto tex = std::make_unique<Graphics::Texture2D>(
                                        cm.Load<Graphics::Texture2D>(
                                            ResolveRootRelativeAssetName(
                                                cm, path, field, file)));
                                    Graphics::Texture2D* texPtr = tex.get();
                                    res->textureOwners.push_back(std::move(tex));
                                    return texPtr;
                                };
                                if (Graphics::Texture2D* t = loadPbrMap(normalMapFile, "normalMap"))
                                    skinnedPbrFx->setNormalMapProperty(t);
                                if (Graphics::Texture2D* t = loadPbrMap(
                                        metallicRoughnessMapFile, "metallicRoughnessMap"))
                                    skinnedPbrFx->setMetallicRoughnessMapProperty(t);
                                if (Graphics::Texture2D* t = loadPbrMap(emissiveMapFile, "emissiveMap"))
                                    skinnedPbrFx->setEmissiveMapProperty(t);
                                if (Graphics::Texture2D* t = loadPbrMap(occlusionMapFile, "occlusionMap"))
                                    skinnedPbrFx->setOcclusionMapProperty(t);
                                skinnedPbrFx->setMetallicFactorProperty(metallicFactor);
                                skinnedPbrFx->setRoughnessFactorProperty(roughnessFactor);
                                skinnedPbrFx->setEmissiveFactorProperty(Vector3(
                                    emissiveFactorArr[0], emissiveFactorArr[1], emissiveFactorArr[2]));
                                skinnedPbrFx->setAlphaModeEXTProperty(alphaMode);
                                skinnedPbrFx->setAlphaCutoffEXTProperty(alphaCutoff);
                                skinnedPbrFx->setDoubleSidedEXTProperty(doubleSided);
                            }

                            // Task 1115 / CNB-67 (Phase 13C): a "vertexStride": 24
                            // (VertexPositionColorTexture) or 56 (skinned + Color) mesh may set
                            // "vertexColorEnabled" to actually light the per-vertex color data it
                            // already uploads -- both BasicEffect and SkinnedEffect default
                            // VertexColorEnabled to false, so without this the color bytes are
                            // present in the vertex buffer but the shader ignores them.
                            // SkinnedEffect's VertexColorEnabled is a CNAEXT addition (real XNA's
                            // SkinnedEffect has no such property at all).
                            if (vertexColorEnabled) {
                                if (auto* basicFx = dynamic_cast<Graphics::BasicEffect*>(fx.get())) {
                                    basicFx->VertexColorEnabled = true;
                                } else if (auto* skinnedFx = dynamic_cast<Graphics::SkinnedEffect*>(fx.get())) {
                                    skinnedFx->VertexColorEnabled = true;
                                }
                            }

                            ApplyPunctualLightsEXT(*fx, punctualLights);

                            partPtr->setEffectProperty(fx.get());
                            res->effectOwners.push_back(std::move(fx));

                            meshRawPtrs.push_back(mesh.get());
                            res->vbs.push_back(std::move(vb));
                            res->ibs.push_back(std::move(ib));
                            res->partOwners.push_back(std::move(part));
                            res->meshOwners.push_back(std::move(mesh));
                        }
                    }
                }

                Graphics::Model model(&device,
                                      std::move(boneRawPtrs),
                                      std::move(meshRawPtrs));
                model.setOwnedResources(res);
                // Task 941 (Phase 77): attach the skeleton/animation-clip data (if any) to the
                // real XNA Model.Tag property, mirroring the Skinned Model Sample's own
                // convention -- game code retrieves it via
                // static_cast<Graphics::SkinningData*>(model.getTagProperty()). Left null (Tag's
                // own default) for a rigid, non-skinned .model.json with no "skeleton" field.
                // plan_gltf.md GLTF-294: an unskinned model's Tag carries its rigid clips instead.
                if (res->skinningData)      { model.setTagProperty(res->skinningData.get()); }
                else if (res->modelAnimations) { model.setTagProperty(res->modelAnimations.get()); }
                // plan_gltf.md GLTF-262, exactly as on the .gltf path above: an unposed skinned
                // model is not merely unanimated, it is wrong.
                if (res->skinningData)
                {
                    Graphics::ApplyBindPoseBoneTransformsEXT(model, *res->skinningData);
                }
                return model;
            }
        };

        // ---------------------------------------------------------------------------
        // .skinnedmodel.json descriptor reader
        // CNAEXT — loads a GPU-skinned mesh + skeleton + animation clips for the real-rendering
        // Avatar extension (see AvatarRenderer::EnableRealRenderingEXT). Not part of the XNA
        // 4.0 content pipeline.
        // ---------------------------------------------------------------------------

        class SkinnedModelTypeReader
            : public LooseFileContentTypeReader<std::shared_ptr<Graphics::SkinnedModelEXT>>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".skinnedmodel.json"};
            }

            std::shared_ptr<Graphics::SkinnedModelEXT> Read(const std::string& path,
                                                             ContentManager& cm) override
            {
                const std::string json = ReadTextFile(path);
                const std::string root = cm.getRootDirectoryProperty();
                // Every path the manifest references (skeleton/vertices/indices/texture/clip) is
                // relative to the manifest's own directory, not the content root — so a bundle
                // like Content/avatar/male/ is self-contained and relocatable without rewriting
                // any of its internal paths.
                Graphics::GraphicsDevice& device = cm.getGraphicsDeviceInternal();

                auto model = std::make_shared<Graphics::SkinnedModelEXT>();

                // --- Skeleton ---
                const std::string skeletonRel = ExtractJsonStringField(json, "skeleton");
                if (skeletonRel.empty())
                {
                    throw ContentLoadException(
                        "SkinnedModel descriptor missing 'skeleton' field: " + path);
                }

                const std::string skeletonPath = ResolveManifestRelativeSidecarPath(
                    cm, path, "skeleton", skeletonRel);
                const auto skelBytes = ReadBinaryFile(skeletonPath);
                BinReaderEXT skelReader{skelBytes};
                const int boneCount = skelReader.Read<std::int32_t>();
                // Task 11.8: boneCount is a raw int32_t from file content with no validation
                // before being cast to std::size_t and used to .resize() 3 vectors below - a
                // negative value wraps to a huge std::size_t (static_cast<std::size_t>(-1) is
                // SIZE_MAX), and even a merely-large-but-positive corrupt value could attempt a
                // huge, wasteful allocation before Task 11.7's own per-Read() bounds check would
                // ever get a chance to reject it. Reject both cleanly instead of risking
                // std::length_error/std::bad_alloc (the wrong exception type for this project) or
                // a crash. kMaxSaneBoneCount is a generous, arbitrary ceiling - real content uses
                // 19 (avatar) or a handful more per wardrobe piece; nothing plausible ever
                // approaches five figures.
                constexpr int kMaxSaneBoneCount = 100000;
                if (boneCount < 0 || boneCount > kMaxSaneBoneCount)
                {
                    throw ContentLoadException(
                        "SkinnedModel skeleton has an invalid bone count (" + std::to_string(boneCount)
                            + "): " + path);
                }
                model->BoneCount = boneCount;
                model->ParentBoneIndices.resize(static_cast<std::size_t>(boneCount));
                for (int i = 0; i < boneCount; ++i)
                {
                    model->ParentBoneIndices[static_cast<std::size_t>(i)] = skelReader.Read<std::int32_t>();
                }
                model->BindPoseLocal.resize(static_cast<std::size_t>(boneCount));
                for (int i = 0; i < boneCount; ++i)
                {
                    model->BindPoseLocal[static_cast<std::size_t>(i)] = skelReader.ReadMatrix();
                }
                model->InverseBindPoseGlobal.resize(static_cast<std::size_t>(boneCount));
                for (int i = 0; i < boneCount; ++i)
                {
                    model->InverseBindPoseGlobal[static_cast<std::size_t>(i)] = skelReader.ReadMatrix();
                }

                // --- Parts ---
                for (const std::string& pg : ParseFlatObjectArrayEXT(json, "parts"))
                {
                    const std::string name     = ExtractJsonStringField(pg, "name");
                    const std::string vertFile = ExtractJsonStringField(pg, "vertices");
                    const std::string idxFile  = ExtractJsonStringField(pg, "indices");
                    const int stride           = JsonInt(pg, "vertexStride", 52);
                    const std::string texFile  = ExtractJsonStringField(pg, "texture");

                    if (vertFile.empty() || idxFile.empty()) { continue; }
                    if (stride <= 0) { continue; }

                    const std::string vertPath = ResolveManifestRelativeSidecarPath(
                        cm, path, "vertices", vertFile);
                    const std::string idxPath = ResolveManifestRelativeSidecarPath(
                        cm, path, "indices", idxFile);
                    std::optional<std::string> texturePath;
                    if (!texFile.empty())
                    {
                        texturePath = ResolveManifestRelativeSidecarPath(
                            cm, path, "texture", texFile);
                    }

                    const auto vertBytes = ReadBinaryFile(vertPath);
                    const auto idxBytes  = ReadBinaryFile(idxPath);

                    // Task 11.9: numVertices/numIndices below used to truncate silently if the
                    // byte counts weren't exact multiples of stride/sizeof(uint16_t), and index
                    // values were never checked to reference an in-range vertex - malformed/
                    // corrupted part data could produce an index buffer referencing out-of-range
                    // vertices with no validation anywhere in this path.
                    if (vertBytes.size() % static_cast<std::size_t>(stride) != 0)
                    {
                        throw ContentLoadException(
                            "SkinnedModel part '" + name + "' vertex data size (" + std::to_string(vertBytes.size())
                                + ") is not a multiple of its vertexStride (" + std::to_string(stride) + "): " + path);
                    }
                    if (idxBytes.size() % sizeof(std::uint16_t) != 0)
                    {
                        throw ContentLoadException(
                            "SkinnedModel part '" + name + "' index data size (" + std::to_string(idxBytes.size())
                                + ") is not a multiple of " + std::to_string(sizeof(std::uint16_t)) + ": " + path);
                    }

                    const int numVertices = static_cast<int>(vertBytes.size()) / stride;
                    const int numIndices  = static_cast<int>(idxBytes.size())
                                            / static_cast<int>(sizeof(std::uint16_t));
                    const int primCount   = numIndices / 3;

                    const std::vector<std::uint16_t> indexData =
                        IndicesFromBytes<std::uint16_t>(idxBytes, numIndices);
                    for (int i = 0; i < numIndices; ++i)
                    {
                        if (static_cast<int>(indexData[i]) >= numVertices)
                        {
                            throw ContentLoadException(
                                "SkinnedModel part '" + name + "' index " + std::to_string(i)
                                    + " references vertex " + std::to_string(indexData[i]) + ", but only "
                                    + std::to_string(numVertices) + " vertices exist: " + path);
                        }
                    }

                    auto vb = std::make_unique<Graphics::VertexBuffer>(device, numVertices);
                    vb->SetDataRaw(vertBytes.data(), numVertices, stride);

                    auto ib = std::make_unique<Graphics::IndexBuffer>(device, numIndices);
                    ib->SetData(indexData.data(), numIndices);

                    auto part = std::make_unique<Graphics::ModelMeshPart>(
                        vb.get(), ib.get(), numVertices, primCount, 0, 0);

                    Graphics::Texture2D texture;
                    if (texturePath.has_value())
                    {
                        texture = cm.Load<Graphics::Texture2D>(
                            ToContentManagerAssetName(root, *texturePath));
                    }

                    model->AddPartEXT(name, std::move(vb), std::move(ib), std::move(part),
                                       std::move(texture));
                }

                // --- Animations ---
                for (const std::string& ag : ParseFlatObjectArrayEXT(json, "animations"))
                {
                    const std::string name     = ExtractJsonStringField(ag, "name");
                    const std::string clipFile = ExtractJsonStringField(ag, "clip");
                    if (name.empty() || clipFile.empty()) { continue; }
                    const std::string clipPath = ResolveManifestRelativeSidecarPath(
                        cm, path, "clip", clipFile);

                    // Task 941: extracted into the shared ReadAnimationClipFileEXT() helper
                    // (also used by ModelTypeReader's own new .model.json "animations" support).
                    // plan_cnj.md CNB-48: ReadAnimationClipRefEXT additionally lets "clip" name a
                    // standalone, shareable .cnj AnimationClip asset instead of only a raw
                    // .clip.bin blob.
                    model->Clips[name] = ReadAnimationClipRefEXT(clipPath, root, cm);
                }

                return model;
            }
        };

        class SongTypeReader : public LooseFileContentTypeReader<Media::Song>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".mp3", ".ogg", ".wav", ".flac", ".opus", ".aac", ".wma"};
            }

            Media::Song Read(const std::string& path, ContentManager& /*cm*/) override
            {
                const std::string name =
                    std::filesystem::path(path).stem().string();
                return Media::Song(path, name);
            }
        };

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(__MINGW32__)
        class VideoTypeReader : public LooseFileContentTypeReader<Media::Video>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".mp4", ".ogv", ".webm", ".mkv", ".avi", ".mov"};
            }

            Media::Video Read(const std::string& path, ContentManager& cm) override
            {
                return Media::Video(path, &cm.getGraphicsDeviceInternal());
            }
        };
#endif

    } // anonymous namespace

    // ---------------------------------------------------------------------------

    void ContentManager::RegisterBuiltinLoaders()
    {
        RegisterTypeReader<Graphics::Texture2D>(std::make_unique<Texture2DTypeReader>());
        RegisterTypeReader<Graphics::TextureCube>(std::make_unique<TextureCubeTypeReader>());
        RegisterTypeReader<std::shared_ptr<Graphics::Texture3D>>(std::make_unique<Texture3DTypeReader>());
        RegisterTypeReader<Audio::SoundEffect>(std::make_unique<SoundEffectTypeReader>());
        RegisterTypeReader<std::shared_ptr<Graphics::Effect>>(std::make_unique<EffectTypeReader>());
        RegisterTypeReader<Graphics::SpriteFont>(std::make_unique<SpriteFontTypeReader>());
        RegisterTypeReader<Graphics::Model>(std::make_unique<ModelTypeReader>());
        RegisterTypeReader<Graphics::AnimationClipEXT>(std::make_unique<AnimationClipTypeReader>());
        RegisterTypeReader<Microsoft::Xna::Framework::Curve>(std::make_unique<CurveTypeReader>());
        RegisterTypeReader<std::shared_ptr<Graphics::SkinnedModelEXT>>(
            std::make_unique<SkinnedModelTypeReader>());
        RegisterTypeReader<Media::Song>(std::make_unique<SongTypeReader>());
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__) && !defined(__MINGW32__)
        RegisterTypeReader<Media::Video>(std::make_unique<VideoTypeReader>());
#endif
    }

} // namespace Microsoft::Xna::Framework::Content

// ---------------------------------------------------------------------------
// Explicit specialisation: weak-cache for Texture2D
// ---------------------------------------------------------------------------
// Textures are NOT stored in loadedAssets_ (strong cache). Instead only weak
// references are kept. When the last external Texture2D copy is destroyed its
// GPU renderer is freed immediately, preventing per-world RAM growth caused by
// world-specific background textures accumulating in the cache.
// ---------------------------------------------------------------------------

namespace Microsoft::Xna::Framework::Content
{
    template<>
    Graphics::Texture2D ContentManager::Load<Graphics::Texture2D>(const std::string& assetName)
    {
        if (disposed_)
            throw std::runtime_error("ContentManager has been disposed.");

        const std::string key = NormalizeKey(assetName);
        log::Debug(std::string("Loading texture: ") + assetName);

        auto cacheIt = textureCache_.find(key);
        if (cacheIt != textureCache_.end())
        {
            auto rendererSp   = cacheIt->second.renderer.lock();
            auto cpuPixelsSp = cacheIt->second.cpuPixels.lock(); // may be null when context recovery disabled
            if (rendererSp)
            {
                // Reuse the existing GPU renderer — no reload from disk needed.
                const int w = rendererSp->GetWidth();
                const int h = rendererSp->GetHeight();
                return Graphics::Texture2D::ReconstructFromCache(
                    getGraphicsDeviceInternal(),
                    w, h,
                    cacheIt->second.fmt,
                    cacheIt->second.levelCount,
                    std::move(rendererSp),
                    std::move(cpuPixelsSp));
            }
            // Renderer expired — remove stale entry and fall through to reload.
            textureCache_.erase(cacheIt);
        }

        // .xnb always wins first (cnj.md's "Core rule", 2026-07-16 decision) -- same reasoning
        // as the generic Load<T>() template; this specialization needs its own copy since it
        // doesn't call that template body at all (weak-cache semantics require a bespoke
        // implementation here).
        const std::string xnbCandidate = BuildAssetPath(assetName) + ".xnb";
        if (std::filesystem::exists(xnbCandidate))
        {
            Graphics::Texture2D result = LoadXnbAsset<Graphics::Texture2D>(xnbCandidate, assetName);

            WeakTextureEntry entry;
            entry.renderer    = result.GetRendererWeak();
            entry.cpuPixels  = result.GetCpuPixelsWeak();
            entry.fmt        = result.getFormatProperty();
            entry.levelCount = result.getLevelCountProperty();
            textureCache_[key] = std::move(entry);

            return result;
        }

        // Load fresh from disk.
        auto readerIt = typeReaders_.find(std::type_index(typeid(Graphics::Texture2D)));
        if (readerIt == typeReaders_.end())
            throw ContentLoadException(
                std::string("ContentManager::Load<Texture2D>(): No reader registered, asset '")
                + assetName + "'.");

        auto* readerPtr = std::any_cast<
            std::shared_ptr<LooseFileContentTypeReader<Graphics::Texture2D>>>(&readerIt->second);
        if (!readerPtr || !*readerPtr)
            throw ContentLoadException(
                std::string("ContentManager::Load<Texture2D>(): Reader is null, asset '")
                + assetName + "'.");

        LooseFileContentTypeReader<Graphics::Texture2D>& reader = **readerPtr;
        const std::string resolvedPath = ResolveAssetPath(assetName, reader);

        Graphics::Texture2D result = reader.Read(resolvedPath, *this);

        // Cache weak references so the GPU renderer is freed as soon as the
        // caller drops all its Texture2D copies.
        WeakTextureEntry entry;
        entry.renderer    = result.GetRendererWeak();
        entry.cpuPixels  = result.GetCpuPixelsWeak();
        entry.fmt        = result.getFormatProperty();
        entry.levelCount = result.getLevelCountProperty();
        textureCache_[key] = std::move(entry);

        return result;
    }

    // See the declaration in ContentManager.hpp for why this type is not cached: SoundEffect
    // is move-only (T-3G), so there is no way to hand back "the same loaded instance" to a
    // second caller anyway -- reader.Read() below already does a fresh decode per call, which
    // is exactly what a cache MISS did before this specialisation existed.
    template<>
    Audio::SoundEffect ContentManager::Load<Audio::SoundEffect>(const std::string& assetName)
    {
        if (disposed_)
            throw std::runtime_error("ContentManager has been disposed.");

        log::Debug(std::string("Loading asset: ") + assetName);

        // .xnb always wins first (cnj.md's "Core rule") -- same reasoning as Load<Texture2D>'s
        // own specialisation; this one needs its own copy too since move-only types skip the
        // generic Load<T>() template's any-cache body entirely.
        const std::string xnbCandidate = BuildAssetPath(assetName) + ".xnb";
        if (std::filesystem::exists(xnbCandidate))
        {
            return LoadXnbAsset<Audio::SoundEffect>(xnbCandidate, assetName);
        }

        auto readerIt = typeReaders_.find(std::type_index(typeid(Audio::SoundEffect)));
        if (readerIt == typeReaders_.end())
            throw ContentLoadException(
                std::string("ContentManager::Load<T>(): No reader registered for type, asset '")
                + assetName + "'.");

        auto* readerPtr = std::any_cast<
            std::shared_ptr<LooseFileContentTypeReader<Audio::SoundEffect>>>(&readerIt->second);
        if (!readerPtr || !*readerPtr)
            throw ContentLoadException(
                std::string("ContentManager::Load<T>(): Reader is null for asset '")
                + assetName + "'.");

        LooseFileContentTypeReader<Audio::SoundEffect>& reader = **readerPtr;
        const std::string resolvedPath = ResolveAssetPath(assetName, reader);

        return reader.Read(resolvedPath, *this);
    }

    // Task 934: TextureCube is move-only (CNAEXT, copy constructor deleted -- unlike Texture2D,
    // which supports reference-counted renderer sharing via its own weak-cache specialisation
    // above), so it cannot be held in the generic strong (std::any-based) cache either. Mirrors
    // SoundEffect's own identical not-cached specialisation immediately above: reader.Read()
    // already does a fresh decode per call, which is exactly what a cache MISS did before this
    // specialisation existed.
    template<>
    Graphics::TextureCube ContentManager::Load<Graphics::TextureCube>(const std::string& assetName)
    {
        if (disposed_)
            throw std::runtime_error("ContentManager has been disposed.");

        log::Debug(std::string("Loading asset: ") + assetName);

        // .xnb always wins first (cnj.md's "Core rule") -- same reasoning as Load<Texture2D>'s and
        // Load<SoundEffect>'s own specialisations above; this one needs its own copy too since
        // move-only types skip the generic Load<T>() template's any-cache body entirely
        // (plan_xnb.md XNB-25).
        const std::string xnbCandidate = BuildAssetPath(assetName) + ".xnb";
        if (std::filesystem::exists(xnbCandidate))
        {
            return LoadXnbAsset<Graphics::TextureCube>(xnbCandidate, assetName);
        }

        auto readerIt = typeReaders_.find(std::type_index(typeid(Graphics::TextureCube)));
        if (readerIt == typeReaders_.end())
            throw ContentLoadException(
                std::string("ContentManager::Load<T>(): No reader registered for type, asset '")
                + assetName + "'.");

        auto* readerPtr = std::any_cast<
            std::shared_ptr<LooseFileContentTypeReader<Graphics::TextureCube>>>(&readerIt->second);
        if (!readerPtr || !*readerPtr)
            throw ContentLoadException(
                std::string("ContentManager::Load<T>(): Reader is null for asset '")
                + assetName + "'.");

        LooseFileContentTypeReader<Graphics::TextureCube>& reader = **readerPtr;
        const std::string resolvedPath = ResolveAssetPath(assetName, reader);

        return reader.Read(resolvedPath, *this);
    }
} // namespace Microsoft::Xna::Framework::Content
