#pragma once

#include <string>

namespace Microsoft::Xna::Framework::Content
{
    class ContentManager; // forward declaration

    /**
     * @brief Abstract base for type-specific asset loaders used by ContentManager.
     *
     * @tparam T The asset type this reader produces.
     */
    template <typename T>
    class ContentTypeReader
    {
    public:
        virtual ~ContentTypeReader() = default;

        /**
         * @brief Reads and constructs an asset of type T.
         *
         * @param path Full filesystem path to the asset file (assembled by ContentManager).
         * @param cm   Reference back to the content manager for recursive loading.
         * @return Loaded asset instance.
         */
        virtual T Read(const std::string& path, ContentManager& cm) = 0;
    };
}
