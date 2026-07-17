// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "System/IServiceProvider.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/CnjEnvelope.hpp"
#include "CNA/Internal/CnjSourceFile.hpp"
#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Json.hpp"
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
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
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
                namespace fs = std::filesystem;
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

                const auto bytes = ReadBinaryFile((fs::path(cm.getRootDirectoryProperty()) / dataField->stringValue).string());
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

                namespace fs = std::filesystem;
                const std::string root = cm.getRootDirectoryProperty();
                const std::string vertPath = (fs::path(root) / vertRel).string();
                const std::string fragPath = (fs::path(root) / fragRel).string();

                return std::make_shared<Graphics::ShaderEffect>(
                    cm.getGraphicsDeviceInternal(),
                    ReadTextFile(vertPath),
                    ReadTextFile(fragPath));
            }

            static std::optional<Graphics::Texture2D> LoadOptionalTexture2D(
                const CNA::Internal::JsonValue& root, const char* field, ContentManager& cm)
            {
                const CNA::Internal::JsonValue* v = root.FindMember(field);
                if (v == nullptr) { return std::nullopt; }
                if (!v->IsString() || v->stringValue.empty())
                {
                    throw ContentLoadException(std::string("Effect .cnj: '") + field + "' must be a non-empty string.");
                }
                return cm.Load<Graphics::Texture2D>(v->stringValue);
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
                    if (auto tex = LoadOptionalTexture2D(root, "texture", cm))
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
                    if (auto tex = LoadOptionalTexture2D(root, "texture", cm))
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
                    if (auto tex = LoadOptionalTexture2D(root, "texture", cm))
                    {
                        effect->SetOwnedTexture(std::make_shared<Texture2D>(std::move(*tex)));
                    }
                    if (auto tex2 = LoadOptionalTexture2D(root, "texture2", cm))
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
                    if (auto tex = LoadOptionalTexture2D(root, "texture", cm))
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
                        Graphics::TextureCube cube = cm.Load<Graphics::TextureCube>(envMap->stringValue);
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
                if (auto tex = LoadOptionalTexture2D(root, "texture", cm))
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
                Graphics::Texture2D atlas = cm.Load<Graphics::Texture2D>(textureName);

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
        // through ContentManager (real caching), so @p baseDir must be re-expressed relative to
        // @p root first -- cm.Load<T>() always resolves relative to the content root, not
        // @p baseDir, mirroring SkinnedModelTypeReader's own existing texRootRelative pattern for
        // textures. The binary branch is unaffected by that distinction (it never goes through
        // ContentManager) and keeps resolving directly against @p baseDir, exactly as before this
        // task.
        Graphics::AnimationClipEXT ReadAnimationClipRefEXT(
            const std::string& clipFile, const std::filesystem::path& baseDir,
            const std::string& root, ContentManager& cm)
        {
            namespace fs = std::filesystem;
            if (fs::path(clipFile).extension() == ".cnj")
            {
                const std::string rootRelative = fs::relative(baseDir / clipFile, root).string();
                return cm.Load<Graphics::AnimationClipEXT>(rootRelative);
            }
            return ReadAnimationClipFileEXT((baseDir / clipFile).string());
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
                namespace fs = std::filesystem;
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
                    return ReadAnimationClipFileEXT(
                        (fs::path(cm.getRootDirectoryProperty()) / clipFileField->stringValue).string());
                }

                const JsonValue* durationField = root.FindMember("duration");
                if (durationField == nullptr || !durationField->IsNumber())
                {
                    throw ContentLoadException(
                        "AnimationClip .cnj '" + path + "' is missing a numeric 'duration' field.");
                }

                Graphics::AnimationClipEXT clip;
                clip.Duration = System::TimeSpan::FromSeconds(durationField->numberValue);

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
            // CNB-64/65 (Phase 13B): owns the morph-target data attached to each morphed mesh
            // part's own Tag property (one entry per part that has morph targets, not per Model).
            std::vector<std::unique_ptr<Graphics::MorphTargetDataEXT>> morphOwners;
        };

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
            } else if (stride == 52) {
                vb->SetDataRaw(vertBytes.data(), numVertices, 52);
            } else if (stride == 56) {
                vb->SetDataRaw(vertBytes.data(), numVertices, 56);
            }
            return vb;
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

            const std::vector<MeshGroup> groups = CollectMeshGroups(data);
            if (groups.empty())
            {
                throw ContentLoadException("glTF file '" + path + "' contains no mesh instances to import.");
            }
            const MeshGroup& group = groups.front();

            const bool hasSkin = group.skin != nullptr;
            SkeletonResult skeleton;
            if (hasSkin) { skeleton = BuildSkeleton(group.skin, 1.0f); }

            Graphics::GraphicsDevice& device = cm.getGraphicsDeviceInternal();
            const fs::path gltfDir = fs::path(path).parent_path();

            auto res = std::make_shared<ModelResources>();
            std::vector<Graphics::ModelBone*> boneRawPtrs;
            std::vector<Graphics::ModelMesh*> meshRawPtrs;

            {
                auto bone = std::make_unique<Graphics::ModelBone>(0, std::string("Root"));
                boneRawPtrs.push_back(bone.get());
                res->boneOwners.push_back(std::move(bone));
            }
            Graphics::ModelBone* rootBone = boneRawPtrs.front();

            if (hasSkin)
            {
                auto skinningData = std::make_unique<Graphics::SkinningData>();
                const int boneCount = static_cast<int>(skeleton.bones.size());
                skinningData->BoneCount = boneCount;
                skinningData->SkeletonHierarchy.resize(static_cast<std::size_t>(boneCount));
                skinningData->BindPose.resize(static_cast<std::size_t>(boneCount));
                skinningData->InverseBindPose.resize(static_cast<std::size_t>(boneCount));
                for (int i = 0; i < boneCount; ++i)
                {
                    const auto ui = static_cast<std::size_t>(i);
                    skinningData->SkeletonHierarchy[ui] = skeleton.bones[ui].parentIndex;
                    skinningData->BindPose[ui] = skeleton.bones[ui].bindPoseLocal;
                    skinningData->InverseBindPose[ui] = skeleton.bones[ui].inverseBindGlobal;
                }

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

            int meshCounter = 0;
            for (const cgltf_mesh* mesh : group.meshes)
            {
                for (cgltf_size p = 0; p < mesh->primitives_count; ++p)
                {
                    const std::string partName = mesh->name
                        ? (std::string(mesh->name) + (mesh->primitives_count > 1 ? "_" + std::to_string(p) : ""))
                        : ("mesh" + std::to_string(meshCounter));
                    MeshOut meshOut = ExtractMesh(mesh->primitives[p], partName, hasSkin ? &skeleton : nullptr, 1.0f);

                    const int numVertices = meshOut.stride > 0
                        ? static_cast<int>(meshOut.vertexBytes.size()) / meshOut.stride : 0;
                    auto vb = BuildVertexBufferFromRawBytes(device, meshOut.stride, numVertices, meshOut.vertexBytes);

                    const int indexSize = meshOut.use32BitIndices
                        ? static_cast<int>(sizeof(std::uint32_t)) : static_cast<int>(sizeof(std::uint16_t));
                    const int numIndices = static_cast<int>(meshOut.indexBytes.size()) / indexSize;
                    const int primCount = numIndices / 3;

                    auto ib = std::make_unique<Graphics::IndexBuffer>(
                        device,
                        meshOut.use32BitIndices ? Graphics::IndexElementSize::ThirtyTwoBits
                                                : Graphics::IndexElementSize::SixteenBits,
                        numIndices, Graphics::BufferUsage::None);
                    if (meshOut.use32BitIndices) {
                        ib->SetData(reinterpret_cast<const std::uint32_t*>(meshOut.indexBytes.data()), numIndices);
                    } else {
                        ib->SetData(reinterpret_cast<const std::uint16_t*>(meshOut.indexBytes.data()), numIndices);
                    }

                    auto part = std::make_unique<Graphics::ModelMeshPart>(
                        vb.get(), ib.get(), numVertices, primCount, 0, 0);
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
                        for (std::size_t t = 0; t < targetCount; ++t)
                        {
                            morph->PositionDeltas.push_back(meshOut.morphPositionDeltas[t]);
                            morph->NormalDeltas.push_back(meshOut.morphNormalDeltas[t]);
                        }
                        morph->Weights = GetMeshDefaultWeights(mesh, targetCount);
                        if (auto weightTrack = ExtractMorphWeightTrack(data, mesh, targetCount))
                        {
                            morph->WeightTrack.Keys.reserve(weightTrack->keys.size());
                            for (const MorphWeightKeyframeOut& k : weightTrack->keys)
                            {
                                Graphics::MorphWeightKeyframeEXT key;
                                key.Time = System::TimeSpan::FromSeconds(k.time);
                                key.Weights = k.weights;
                                morph->WeightTrack.Keys.push_back(std::move(key));
                            }
                            morph->WeightTrack.StepInterpolation = weightTrack->stepInterpolation;
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

                    auto meshBone = std::make_unique<Graphics::ModelBone>(
                        static_cast<int>(boneRawPtrs.size()), meshName.empty() ? "mesh" : meshName);
                    rootBone->AddChild(meshBone.get());
                    meshObj->setParentBoneProperty(meshBone.get());
                    boneRawPtrs.push_back(meshBone.get());
                    res->boneOwners.push_back(std::move(meshBone));

                    std::shared_ptr<Graphics::Effect> fx;
                    if (meshOut.skinned) { fx = std::make_shared<Graphics::SkinnedEffect>(device); }
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
                        }
                    }

                    if (meshOut.useDualTexture)
                    {
                        if (Graphics::Texture2D* tex2 = loadTexture(meshOut.occlusionImage))
                        {
                            if (auto* dualFx = dynamic_cast<Graphics::DualTextureEffect*>(fx.get())) {
                                dualFx->setTexture2Property(tex2);
                            }
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
            model.setTagProperty(res->skinningData.get());
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
                CNA::Internal::ValidateCnjEnvelope(envelope, "Model", path);
                RejectSourceFileForSelfContainedCnj(envelope, "Model", path);

                const std::string root = cm.getRootDirectoryProperty();
                Graphics::GraphicsDevice& device = cm.getGraphicsDeviceInternal();

                // Owned resources shared by all copies of the returned Model (ModelResources is
                // hoisted to file scope, shared with ReadGltfModel()).
                auto res = std::make_shared<ModelResources>();

                std::vector<Graphics::ModelBone*> boneRawPtrs;
                std::vector<Graphics::ModelMesh*> meshRawPtrs;

                // Root bone
                {
                    std::string rootName = "Root";
                    const std::size_t bk = json.find("\"bones\"");
                    if (bk != std::string::npos) {
                        const std::size_t ba = json.find('[', bk);
                        if (ba != std::string::npos) {
                            const std::size_t bo = json.find('{', ba);
                            if (bo != std::string::npos) {
                                int d = 1; std::size_t e = bo + 1;
                                while (e < json.size() && d > 0) {
                                    if (json[e] == '{') ++d;
                                    else if (json[e] == '}') --d;
                                    ++e;
                                }
                                const std::string bg = json.substr(bo, e - bo);
                                const std::string n  = ExtractJsonStringField(bg, "name");
                                if (!n.empty()) rootName = n;
                            }
                        }
                    }
                    auto bone = std::make_unique<Graphics::ModelBone>(0, std::move(rootName));
                    boneRawPtrs.push_back(bone.get());
                    res->boneOwners.push_back(std::move(bone));
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
                    const auto skelBytes = ReadBinaryFile((fs::path(root) / skeletonRel).string());
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

                    for (const std::string& ag : ParseFlatObjectArrayEXT(json, "animations"))
                    {
                        const std::string name     = ExtractJsonStringField(ag, "name");
                        const std::string clipFile = ExtractJsonStringField(ag, "clip");
                        if (name.empty() || clipFile.empty()) { continue; }
                        skinningData->AnimationClips[name] =
                            ReadAnimationClipRefEXT(clipFile, fs::path(root), root, cm);
                    }

                    res->skinningData = std::move(skinningData);
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

                            if (vertFile.empty() || idxFile.empty())
                                continue;

                            const auto vertBytes = ReadBinaryFile(
                                (fs::path(root) / vertFile).string());
                            const auto idxBytes  = ReadBinaryFile(
                                (fs::path(root) / idxFile).string());

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
                            const int primCount   = numIndices / 3;

                            auto vb = BuildVertexBufferFromRawBytes(device, stride, numVertices, vertBytes);

                            auto ib = std::make_unique<Graphics::IndexBuffer>(
                                device,
                                use32BitIndices ? Graphics::IndexElementSize::ThirtyTwoBits
                                                : Graphics::IndexElementSize::SixteenBits,
                                numIndices, Graphics::BufferUsage::None);
                            if (use32BitIndices) {
                                ib->SetData(reinterpret_cast<const std::uint32_t*>(
                                    idxBytes.data()), numIndices);
                            } else {
                                ib->SetData(reinterpret_cast<const std::uint16_t*>(
                                    idxBytes.data()), numIndices);
                            }

                            auto part = std::make_unique<Graphics::ModelMeshPart>(
                                vb.get(), ib.get(), numVertices, primCount, 0, 0);
                            Graphics::ModelMeshPart* partPtr = part.get();

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
                            auto meshBone = std::make_unique<Graphics::ModelBone>(
                                static_cast<int>(boneRawPtrs.size()),
                                meshName.empty() ? "mesh" : meshName);
                            rootBone->AddChild(meshBone.get());
                            mesh->setParentBoneProperty(meshBone.get());
                            boneRawPtrs.push_back(meshBone.get());
                            res->boneOwners.push_back(std::move(meshBone));

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
                            } else {
                                fx = cm.Load<std::shared_ptr<Graphics::Effect>>(effectStr);
                            }

                            // Task 932: bind a per-mesh diffuse texture, if the descriptor names
                            // one -- mirrors SkinnedModelTypeReader's own already-working
                            // per-part texture loading. BasicEffect needs TextureEnabled
                            // explicitly turned on; SkinnedEffect's real XNA shader is always
                            // textured (no such toggle exists on it). A custom effect loaded
                            // via "effect" has no standard texture slot to bind through here.
                            if (!textureFile.empty()) {
                                auto tex = std::make_unique<Graphics::Texture2D>(
                                    cm.Load<Graphics::Texture2D>(textureFile));
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
                                }
                            }

                            // CNB-73: the second (layer-1) texture slot -- only meaningful for
                            // DualTextureEffect, which is the only stock effect with a Texture2
                            // parameter.
                            if (!texture2File.empty()) {
                                if (auto* dualFx = dynamic_cast<Graphics::DualTextureEffect*>(fx.get())) {
                                    auto tex2 = std::make_unique<Graphics::Texture2D>(
                                        cm.Load<Graphics::Texture2D>(texture2File));
                                    dualFx->setTexture2Property(tex2.get());
                                    res->textureOwners.push_back(std::move(tex2));
                                }
                            }

                            // Task 1115 / CNB-67 (Phase 13C): a "vertexStride": 24
                            // (VertexPositionColorTexture) or 56 (skinned + Color) mesh may set
                            // "vertexColorEnabled" to actually light the per-vertex color data it
                            // already uploads -- both BasicEffect and SkinnedEffect default
                            // VertexColorEnabled to false, so without this the color bytes are
                            // present in the vertex buffer but the shader ignores them.
                            // SkinnedEffect's VertexColorEnabled is a NOXNA addition (real XNA's
                            // SkinnedEffect has no such property at all).
                            if (vertexColorEnabled) {
                                if (auto* basicFx = dynamic_cast<Graphics::BasicEffect*>(fx.get())) {
                                    basicFx->VertexColorEnabled = true;
                                } else if (auto* skinnedFx = dynamic_cast<Graphics::SkinnedEffect*>(fx.get())) {
                                    skinnedFx->VertexColorEnabled = true;
                                }
                            }

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
                model.setTagProperty(res->skinningData.get());
                return model;
            }
        };

        // ---------------------------------------------------------------------------
        // .skinnedmodel.json descriptor reader
        // NOXNA — loads a GPU-skinned mesh + skeleton + animation clips for the real-rendering
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
                namespace fs = std::filesystem;

                const std::string json = ReadTextFile(path);
                const std::string root = cm.getRootDirectoryProperty();
                // Every path the manifest references (skeleton/vertices/indices/texture/clip) is
                // relative to the manifest's own directory, not the content root — so a bundle
                // like Content/avatar/male/ is self-contained and relocatable without rewriting
                // any of its internal paths.
                const fs::path manifestDir = fs::path(path).parent_path();
                Graphics::GraphicsDevice& device = cm.getGraphicsDeviceInternal();

                auto model = std::make_shared<Graphics::SkinnedModelEXT>();

                // --- Skeleton ---
                const std::string skeletonRel = ExtractJsonStringField(json, "skeleton");
                if (skeletonRel.empty())
                {
                    throw ContentLoadException(
                        "SkinnedModel descriptor missing 'skeleton' field: " + path);
                }

                const auto skelBytes = ReadBinaryFile((manifestDir / skeletonRel).string());
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

                    const auto vertBytes = ReadBinaryFile((manifestDir / vertFile).string());
                    const auto idxBytes  = ReadBinaryFile((manifestDir / idxFile).string());

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

                    const auto* indexData = reinterpret_cast<const std::uint16_t*>(idxBytes.data());
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
                    ib->SetData(indexData, numIndices);

                    auto part = std::make_unique<Graphics::ModelMeshPart>(
                        vb.get(), ib.get(), numVertices, primCount, 0, 0);

                    Graphics::Texture2D texture;
                    if (!texFile.empty())
                    {
                        // cm.Load<T>() always resolves its argument relative to the content
                        // root, not the manifest's directory — re-express texFile (manifest-
                        // relative, like every other path here) as root-relative first.
                        const std::string texRootRelative = fs::relative(manifestDir / texFile, root).string();
                        texture = cm.Load<Graphics::Texture2D>(texRootRelative);
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

                    // Task 941: extracted into the shared ReadAnimationClipFileEXT() helper
                    // (also used by ModelTypeReader's own new .model.json "animations" support).
                    // plan_cnj.md CNB-48: ReadAnimationClipRefEXT additionally lets "clip" name a
                    // standalone, shareable .cnj AnimationClip asset instead of only a raw
                    // .clip.bin blob.
                    model->Clips[name] = ReadAnimationClipRefEXT(clipFile, manifestDir, root, cm);
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
// GPU backend is freed immediately, preventing per-world RAM growth caused by
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
            auto backendSp   = cacheIt->second.backend.lock();
            auto cpuPixelsSp = cacheIt->second.cpuPixels.lock(); // may be null when context recovery disabled
            if (backendSp)
            {
                // Reuse the existing GPU backend — no reload from disk needed.
                const int w = backendSp->GetWidth();
                const int h = backendSp->GetHeight();
                return Graphics::Texture2D::ReconstructFromCache(
                    getGraphicsDeviceInternal(),
                    w, h,
                    cacheIt->second.fmt,
                    cacheIt->second.levelCount,
                    std::move(backendSp),
                    std::move(cpuPixelsSp));
            }
            // Backend expired — remove stale entry and fall through to reload.
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
            entry.backend    = result.GetBackendWeak();
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

        // Cache weak references so the GPU backend is freed as soon as the
        // caller drops all its Texture2D copies.
        WeakTextureEntry entry;
        entry.backend    = result.GetBackendWeak();
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

    // Task 934: TextureCube is move-only (NOXNA, copy constructor deleted -- unlike Texture2D,
    // which supports reference-counted backend sharing via its own weak-cache specialisation
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
