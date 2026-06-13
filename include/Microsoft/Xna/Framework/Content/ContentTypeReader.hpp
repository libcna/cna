// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

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
        /** @brief Virtual destructor. */
        virtual ~ContentTypeReader() = default;

        /**
         * @brief Returns the file extensions this reader handles (e.g. {".png", ".jpg"}).
         *
         * When ContentManager::Load is called with an asset name that has no extension,
         * it iterates these extensions and tries each one until a file is found.
         * Return an empty vector if the reader always receives a full path.
         *
         * @return Vector of file extension strings.
         */
        [[nodiscard]] virtual std::vector<std::string> GetExtensions() const { return {}; }

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
