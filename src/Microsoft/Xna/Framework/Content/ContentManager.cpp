#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"

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
        return RootDirectory_;
    }

    void ContentManager::setRootDirectoryProperty(const std::string& v)
    {
        RootDirectory_ = v;
    }

    // ---------------------------------------------------------------------------
    // Constructor / lifecycle
    // ---------------------------------------------------------------------------

    ContentManager::ContentManager()
    {
        RegisterBuiltinEffectLoader();
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
        Unload();
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
            return getRootDirectoryProperty();
        }

        if (getRootDirectoryProperty().empty())
        {
            return assetName;
        }

        namespace fs = std::filesystem;
        return (fs::path(getRootDirectoryProperty()) / assetName).string();
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
    // Built-in Effect loader
    // ---------------------------------------------------------------------------

    namespace
    {
        /**
         * @brief Minimal JSON string-value extractor.
         *
         * Finds the value of a simple string field in a flat JSON object:
         *   { "key": "value" }
         *
         * @param json  Full JSON text.
         * @param key   Field name to look up.
         * @return Field value, or empty string if not found.
         */
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

        class EffectTypeReader : public ContentTypeReader<std::shared_ptr<Graphics::Effect>>
        {
        public:
            std::shared_ptr<Graphics::Effect> Read(const std::string& path, ContentManager& cm) override
            {
                // Resolve .shader.json path: use path as-is if it ends with .shader.json,
                // otherwise append the extension.
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

                const std::string vertSrc = ReadTextFile(vertPath);
                const std::string fragSrc = ReadTextFile(fragPath);

                return std::make_shared<Graphics::ShaderEffect>(
                    cm.getGraphicsDeviceInternal(), vertSrc, fragSrc);
            }
        };
    } // anonymous namespace

    void ContentManager::RegisterBuiltinEffectLoader()
    {
        RegisterTypeReader<std::shared_ptr<Graphics::Effect>>(
            std::make_unique<EffectTypeReader>());
    }
}
