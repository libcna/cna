#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#endif

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
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

    ContentManager::ContentManager()
    {
        RegisterBuiltinLoaders();
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
                return Graphics::Texture2D(path, gd);
            }
        };

        class SoundEffectTypeReader : public ContentTypeReader<Audio::SoundEffect>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".wav"};
            }

            Audio::SoundEffect Read(const std::string& path, ContentManager& /*cm*/) override
            {
                return Audio::SoundEffect(path);
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
                return {".shader.json"};
            }

            std::shared_ptr<Graphics::Effect> Read(const std::string& path, ContentManager& cm) override
            {
                // If path doesn't already end with .shader.json, append it.
                std::string jsonPath = path;
                const std::string ext = ".shader.json";
                if (jsonPath.size() < ext.size() ||
                    jsonPath.substr(jsonPath.size() - ext.size()) != ext)
                {
                    jsonPath += ext;
                }

                const std::string jsonText = ReadTextFile(jsonPath);

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
                return {".font.json"};
            }

            Graphics::SpriteFont Read(const std::string& path, ContentManager& cm) override
            {
                using Graphics::SpriteFont;
                using SharpRuntime::charcs;

                const std::string json = ReadTextFile(path);

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

        // ---------------------------------------------------------------------------
        // .model.json descriptor reader
        // ---------------------------------------------------------------------------

        class ModelTypeReader : public ContentTypeReader<Graphics::Model>
        {
        public:
            [[nodiscard]] std::vector<std::string> GetExtensions() const override
            {
                return {".model.json"};
            }

            Graphics::Model Read(const std::string& path, ContentManager& cm) override
            {
                namespace fs = std::filesystem;

                const std::string json = ReadTextFile(path);
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

                            const std::string meshName  = ExtractJsonStringField(mg, "name");
                            const std::string vertFile  = ExtractJsonStringField(mg, "vertices");
                            const std::string idxFile   = ExtractJsonStringField(mg, "indices");
                            const int         stride    = JsonInt(mg, "vertexStride", 16);
                            const std::string effectStr = ExtractJsonStringField(mg, "effect");

                            if (vertFile.empty() || idxFile.empty())
                                continue;

                            const auto vertBytes = ReadBinaryFile(
                                (fs::path(root) / vertFile).string());
                            const auto idxBytes  = ReadBinaryFile(
                                (fs::path(root) / idxFile).string());

                            if (stride <= 0) continue;
                            const int numVertices = static_cast<int>(vertBytes.size()) / stride;
                            const int numIndices  = static_cast<int>(idxBytes.size())
                                                    / static_cast<int>(sizeof(std::uint16_t));
                            const int primCount   = numIndices / 3;

                            auto vb = std::make_unique<Graphics::VertexBuffer>(device, numVertices);
                            if (stride == static_cast<int>(sizeof(Graphics::VertexPositionColor)))
                                vb->SetData(reinterpret_cast<const Graphics::VertexPositionColor*>(
                                    vertBytes.data()), numVertices);
                            else if (stride == static_cast<int>(
                                         sizeof(Graphics::VertexPositionNormalTexture)))
                                vb->SetData(reinterpret_cast<const Graphics::VertexPositionNormalTexture*>(
                                    vertBytes.data()), numVertices);
                            else if (stride == static_cast<int>(
                                         sizeof(Graphics::VertexPositionColorTexture)))
                                vb->SetData(reinterpret_cast<const Graphics::VertexPositionColorTexture*>(
                                    vertBytes.data()), numVertices);
                            else if (stride == static_cast<int>(sizeof(Graphics::VertexPositionTexture)))
                                vb->SetData(reinterpret_cast<const Graphics::VertexPositionTexture*>(
                                    vertBytes.data()), numVertices);

                            auto ib = std::make_unique<Graphics::IndexBuffer>(device, numIndices);
                            ib->SetData(reinterpret_cast<const std::uint16_t*>(
                                idxBytes.data()), numIndices);

                            auto part = std::make_unique<Graphics::ModelMeshPart>(
                                vb.get(), ib.get(), numVertices, primCount, 0, 0);
                            Graphics::ModelMeshPart* partPtr = part.get();

                            auto mesh = std::make_unique<Graphics::ModelMesh>(
                                &device, meshName.empty() ? "mesh" : meshName,
                                std::vector<Graphics::ModelMeshPart*>{partPtr});

                            // Load effect and register it in the mesh's effect collection.
                            std::shared_ptr<Graphics::Effect> fx;
                            if (effectStr.empty() || effectStr == "BasicEffect") {
                                fx = std::make_shared<Graphics::BasicEffect>(device);
                            } else {
                                fx = cm.Load<std::shared_ptr<Graphics::Effect>>(effectStr);
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

#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
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
        RegisterTypeReader<Audio::SoundEffect>(std::make_unique<SoundEffectTypeReader>());
        RegisterTypeReader<std::shared_ptr<Graphics::Effect>>(std::make_unique<EffectTypeReader>());
        RegisterTypeReader<Graphics::SpriteFont>(std::make_unique<SpriteFontTypeReader>());
        RegisterTypeReader<Graphics::Model>(std::make_unique<ModelTypeReader>());
        RegisterTypeReader<Media::Song>(std::make_unique<SongTypeReader>());
#if !defined(__EMSCRIPTEN__) && !defined(__ANDROID__)
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
} // namespace Microsoft::Xna::Framework::Content
