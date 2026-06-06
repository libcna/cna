#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
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

    } // anonymous namespace

    // ---------------------------------------------------------------------------

    void ContentManager::RegisterBuiltinLoaders()
    {
        RegisterTypeReader<Graphics::Texture2D>(std::make_unique<Texture2DTypeReader>());
        RegisterTypeReader<Audio::SoundEffect>(std::make_unique<SoundEffectTypeReader>());
        RegisterTypeReader<std::shared_ptr<Graphics::Effect>>(std::make_unique<EffectTypeReader>());
    }

} // namespace Microsoft::Xna::Framework::Content
