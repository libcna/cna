// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Content
{
    class ContentManager; // forward declaration

    /**
     * @brief CNAEXT abstract base for type-specific loose-file asset loaders used by
     *        ContentManager's `.cnj`/native-extension loading scheme (see cnj.md).
     *
     * Renamed 2026-07-16 from `ContentTypeReader<T>` (which this class was originally called)
     * to free that name for the real, binary-protocol-shaped `Microsoft.Xna.Framework.Content.
     * ContentTypeReader`/`ContentTypeReader<T>` FNA API class (`Read(ContentReader&, T)`,
     * see plans/plan_xnb.md XNB-14) -- this class's shape (`Read(const std::string& path,
     * ContentManager&)`) is a CNA-original design for loose-file/`.cnj` loading, not an XNA-faithful
     * port, so it does not belong under the real API's name.
     *
     * @tparam T The asset type this reader produces.
     */
    template <typename T>
    class CNAEXT LooseFileContentTypeReader
    {
    public:
        /** @brief Virtual destructor. */
        virtual ~LooseFileContentTypeReader() = default;

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
