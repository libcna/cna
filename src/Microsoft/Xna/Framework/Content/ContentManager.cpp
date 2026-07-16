// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "System/IServiceProvider.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/CnbEnvelope.hpp"
#include "CNA/Internal/CnbSourceFile.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#include "System/IO/FileStream.hpp"
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
        // .cnb/.font.json-style readers. Texture2DTypeReader needs it for the .cnb sidecar path
        // (plan_cnb.md CNB-8), ahead of where it's textually defined.
        std::string ReadTextFile(const std::string& path);

        // plan_cnb.md CNB-34: SpriteFont/Effect/Model .cnb documents are self-contained
        // descriptors -- unlike Texture2D/SoundEffect/TextureCube, they have no meaning for a
        // "sourceFile" field. Reject it explicitly with a clear error instead of silently
        // ignoring it (the previous behavior) or letting some future field-parsing change
        // accidentally half-honor it.
        void RejectSourceFileForSelfContainedCnb(const CNA::Internal::CnbEnvelope& envelope,
                                                  const std::string& typeName, const std::string& path)
        {
            if (envelope.hasSourceFile)
            {
                throw ContentLoadException(
                    "ContentManager: " + typeName + " .cnb '" + path + "' has a 'sourceFile' "
                    "field, but " + typeName + " .cnb documents are self-contained and do not "
                    "support 'sourceFile'.");
            }
        }

        // Minimal "colorKey": [r, g, b] extractor for a Texture2D .cnb sidecar (CNB-8). Kept
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

        class Texture2DTypeReader : public ContentTypeReader<Graphics::Texture2D>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tga", ".tif", ".tiff", ".qoi"};
            }

            Graphics::Texture2D Read(const std::string& path, ContentManager& cm) override
            {
                Graphics::GraphicsDevice& gd = cm.getGraphicsDeviceInternal();

                if (std::filesystem::path(path).extension() == ".cnb")
                {
                    return ReadCnb(path, cm);
                }

                return Graphics::Texture2D(path, gd);
            }

        private:
            static Graphics::Texture2D ReadCnb(const std::string& path, ContentManager& cm)
            {
                const std::string json = ReadTextFile(path);
                const CNA::Internal::CnbEnvelope envelope = CNA::Internal::ParseCnbEnvelope(json);
                CNA::Internal::ValidateCnbEnvelope(envelope, "Texture2D", path);

                if (!envelope.hasSourceFile)
                {
                    throw ContentLoadException(
                        "ContentManager: Texture2D .cnb '" + path + "' has no 'sourceFile' field "
                        "(a self-contained, non-sourceFile Texture2D .cnb is not supported).");
                }

                const CNA::Internal::CnbSourceFileResult resolved =
                    CNA::Internal::ResolveCnbSourceFileSafely(
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

        class TextureCubeTypeReader : public ContentTypeReader<Graphics::TextureCube>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".dds"};
            }

            Graphics::TextureCube Read(const std::string& path, ContentManager& cm) override
            {
                if (std::filesystem::path(path).extension() == ".cnb")
                {
                    return ReadCnb(path, cm);
                }

                Graphics::GraphicsDevice& gd = cm.getGraphicsDeviceInternal();
                System::IO::FileStream stream(path);
                return Graphics::TextureCube::DDSFromStreamEXT(gd, stream);
            }

        private:
            static Graphics::TextureCube ReadCnb(const std::string& path, ContentManager& cm)
            {
                const std::string json = ReadTextFile(path);
                const CNA::Internal::CnbEnvelope envelope = CNA::Internal::ParseCnbEnvelope(json);
                CNA::Internal::ValidateCnbEnvelope(envelope, "TextureCube", path);

                if (!envelope.hasSourceFile)
                {
                    throw ContentLoadException(
                        "ContentManager: TextureCube .cnb '" + path + "' has no 'sourceFile' "
                        "field (a self-contained, non-sourceFile TextureCube .cnb is not "
                        "supported).");
                }

                const CNA::Internal::CnbSourceFileResult resolved =
                    CNA::Internal::ResolveCnbSourceFileSafely(
                        path, cm.getRootDirectoryProperty(), envelope.sourceFile);
                return cm.Load<Graphics::TextureCube>(resolved.logicalName);
            }
        };

        class SoundEffectTypeReader : public ContentTypeReader<Audio::SoundEffect>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".wav"};
            }

            Audio::SoundEffect Read(const std::string& path, ContentManager& cm) override
            {
                if (std::filesystem::path(path).extension() == ".cnb")
                {
                    return ReadCnb(path, cm);
                }

                return Audio::SoundEffect(path);
            }

        private:
            static Audio::SoundEffect ReadCnb(const std::string& path, ContentManager& cm)
            {
                const std::string json = ReadTextFile(path);
                const CNA::Internal::CnbEnvelope envelope = CNA::Internal::ParseCnbEnvelope(json);
                CNA::Internal::ValidateCnbEnvelope(envelope, "SoundEffect", path);

                if (!envelope.hasSourceFile)
                {
                    throw ContentLoadException(
                        "ContentManager: SoundEffect .cnb '" + path + "' has no 'sourceFile' "
                        "field (a self-contained, non-sourceFile SoundEffect .cnb is not "
                        "supported).");
                }

                const CNA::Internal::CnbSourceFileResult resolved =
                    CNA::Internal::ResolveCnbSourceFileSafely(
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

        class EffectTypeReader : public ContentTypeReader<std::shared_ptr<Graphics::Effect>>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".cnb"};
            }

            std::shared_ptr<Graphics::Effect> Read(const std::string& path, ContentManager& cm) override
            {
                // If path doesn't already end with .cnb, append it.
                std::string jsonPath = path;
                const std::string ext = ".cnb";
                if (jsonPath.size() < ext.size() ||
                    jsonPath.substr(jsonPath.size() - ext.size()) != ext)
                {
                    jsonPath += ext;
                }

                const std::string jsonText = ReadTextFile(jsonPath);

                const CNA::Internal::CnbEnvelope envelope = CNA::Internal::ParseCnbEnvelope(jsonText);
                CNA::Internal::ValidateCnbEnvelope(envelope, "Effect", jsonPath);
                RejectSourceFileForSelfContainedCnb(envelope, "Effect", jsonPath);

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

        class SpriteFontTypeReader : public ContentTypeReader<Graphics::SpriteFont>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".cnb"};
            }

            Graphics::SpriteFont Read(const std::string& path, ContentManager& cm) override
            {
                using Graphics::SpriteFont;
                using SharpRuntime::charcs;

                const std::string json = ReadTextFile(path);

                const CNA::Internal::CnbEnvelope envelope = CNA::Internal::ParseCnbEnvelope(json);
                CNA::Internal::ValidateCnbEnvelope(envelope, "SpriteFont", path);
                RejectSourceFileForSelfContainedCnb(envelope, "SpriteFont", path);

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

        static std::vector<std::uint8_t> ReadBinaryFile(const std::string& path)
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

        // ---------------------------------------------------------------------------
        // .model.json descriptor reader
        // ---------------------------------------------------------------------------

        class ModelTypeReader : public ContentTypeReader<Graphics::Model>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".cnb"};
            }

            Graphics::Model Read(const std::string& path, ContentManager& cm) override
            {
                namespace fs = std::filesystem;

                const std::string json = ReadTextFile(path);

                const CNA::Internal::CnbEnvelope envelope = CNA::Internal::ParseCnbEnvelope(json);
                CNA::Internal::ValidateCnbEnvelope(envelope, "Model", path);
                RejectSourceFileForSelfContainedCnb(envelope, "Model", path);

                const std::string root = cm.getRootDirectoryProperty();
                Graphics::GraphicsDevice& device = cm.getGraphicsDeviceInternal();

                // Owned resources shared by all copies of the returned Model.
                struct ModelResources {
                    std::vector<std::unique_ptr<Graphics::VertexBuffer>>  vbs;
                    std::vector<std::unique_ptr<Graphics::IndexBuffer>>   ibs;
                    std::vector<std::unique_ptr<Graphics::ModelBone>>     boneOwners;
                    std::vector<std::unique_ptr<Graphics::ModelMesh>>     meshOwners;
                    std::vector<std::unique_ptr<Graphics::ModelMeshPart>> partOwners;
                    std::vector<std::shared_ptr<Graphics::Effect>>        effectOwners;
                    std::vector<std::unique_ptr<Graphics::Texture2D>>     textureOwners;
                    // Task 941: owns the skeleton/animation-clip data attached to the returned
                    // Model's own Tag property (see the "Skeletal animation" section below) --
                    // null for a rigid, non-skinned .model.json with no "skeleton" field.
                    std::unique_ptr<Graphics::SkinningData>               skinningData;
                };
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
                            ReadAnimationClipFileEXT((fs::path(root) / clipFile).string());
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

                            // Task 927: `stride` is always one of XNA's own "clean" (tightly
                            // packed, no vtable) sizes -- 16/20/24/32 -- since every offline
                            // conversion tool writes that exact layout. Every CNA vertex struct
                            // below publicly inherits the polymorphic IVertexType (a vtable
                            // pointer XNA's own C# interface never carried), inflating its real
                            // sizeof() past that clean size -- comparing `stride` against
                            // sizeof(...) directly, then reinterpret_cast-ing the tightly-packed
                            // file bytes as an array of those inflated structs, silently read
                            // every vertex field from the wrong byte offset (confirmed the true
                            // cause of a long-tracked "near-plane-clipping"/invisible-model
                            // symptom family in ../cna-samples). Read each vertex's fields
                            // explicitly from the file's own known-clean offsets instead, and
                            // construct real, normally-initialized C++ objects (the compiler
                            // resolves member layout correctly), then upload through the existing
                            // typed SetData overload, which already packs into the correct
                            // compact GPU layout.
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

                            // Note: VertexPositionColorTexture's own declared `= default` default
                            // constructor is implicitly deleted (its `Color Color` member has no
                            // zero-arg constructor) -- build each vector via reserve()+
                            // emplace_back() rather than a sized constructor, so none of the 4
                            // branches below ever needs default-construction.
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
                                // Task 941 (Phase 77): GPU-skinned vertex (VertexPositionNormal
                                // TextureSkinned) -- pos+normal+texcoord+blendweight+blendindices,
                                // already exactly the compact GPU layout VertexBuffer's own
                                // skinned SetData path expects (matches
                                // SkinnedModelTypeReader's own identical stride-52 handling in
                                // this same file). SetDataRaw() copies these bytes straight
                                // through with no C++ struct reinterpretation at all, so this
                                // branch has no analogue of Task 927's own vtable-inflation risk.
                                vb->SetDataRaw(vertBytes.data(), numVertices, 52);
                            }

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
            : public ContentTypeReader<std::shared_ptr<Graphics::SkinnedModelEXT>>
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
                    model->Clips[name] = ReadAnimationClipFileEXT((manifestDir / clipFile).string());
                }

                return model;
            }
        };

        class SongTypeReader : public ContentTypeReader<Media::Song>
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
        class VideoTypeReader : public ContentTypeReader<Media::Video>
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
        RegisterTypeReader<Audio::SoundEffect>(std::make_unique<SoundEffectTypeReader>());
        RegisterTypeReader<std::shared_ptr<Graphics::Effect>>(std::make_unique<EffectTypeReader>());
        RegisterTypeReader<Graphics::SpriteFont>(std::make_unique<SpriteFontTypeReader>());
        RegisterTypeReader<Graphics::Model>(std::make_unique<ModelTypeReader>());
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

        // Load fresh from disk.
        auto readerIt = typeReaders_.find(std::type_index(typeid(Graphics::Texture2D)));
        if (readerIt == typeReaders_.end())
            throw ContentLoadException(
                std::string("ContentManager::Load<Texture2D>(): No reader registered, asset '")
                + assetName + "'.");

        auto* readerPtr = std::any_cast<
            std::shared_ptr<ContentTypeReader<Graphics::Texture2D>>>(&readerIt->second);
        if (!readerPtr || !*readerPtr)
            throw ContentLoadException(
                std::string("ContentManager::Load<Texture2D>(): Reader is null, asset '")
                + assetName + "'.");

        ContentTypeReader<Graphics::Texture2D>& reader = **readerPtr;
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

        auto readerIt = typeReaders_.find(std::type_index(typeid(Audio::SoundEffect)));
        if (readerIt == typeReaders_.end())
            throw ContentLoadException(
                std::string("ContentManager::Load<T>(): No reader registered for type, asset '")
                + assetName + "'.");

        auto* readerPtr = std::any_cast<
            std::shared_ptr<ContentTypeReader<Audio::SoundEffect>>>(&readerIt->second);
        if (!readerPtr || !*readerPtr)
            throw ContentLoadException(
                std::string("ContentManager::Load<T>(): Reader is null for asset '")
                + assetName + "'.");

        ContentTypeReader<Audio::SoundEffect>& reader = **readerPtr;
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

        auto readerIt = typeReaders_.find(std::type_index(typeid(Graphics::TextureCube)));
        if (readerIt == typeReaders_.end())
            throw ContentLoadException(
                std::string("ContentManager::Load<T>(): No reader registered for type, asset '")
                + assetName + "'.");

        auto* readerPtr = std::any_cast<
            std::shared_ptr<ContentTypeReader<Graphics::TextureCube>>>(&readerIt->second);
        if (!readerPtr || !*readerPtr)
            throw ContentLoadException(
                std::string("ContentManager::Load<T>(): Reader is null for asset '")
                + assetName + "'.");

        ContentTypeReader<Graphics::TextureCube>& reader = **readerPtr;
        const std::string resolvedPath = ResolveAssetPath(assetName, reader);

        return reader.Read(resolvedPath, *this);
    }
} // namespace Microsoft::Xna::Framework::Content
