// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>
#include <vector>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Content
{
    /**
     * @brief CNAEXT introspection record for one logical asset found by a `ContentManager`
     *        content-root scan (XNB-65/XNB-66; docs/xnb-content-pipeline-support.md).
     *
     * Not part of the XNA 4.0 API -- a CNA-specific tooling/diagnostic surface (asset
     * validators, an editor's "what's in Content" view). One entry per logical asset name
     * (the relative path with its final extension stripped), aggregating every file found
     * under that name.
     */
    struct CNAEXT ContentManifestEntry
    {
        /** @brief Logical asset name relative to the content root, forward-slash separated, no extension. */
        std::string relativePath;

        /** @brief True if `<relativePath>.xnb` exists. */
        bool hasXnb = false;

        /** @brief True if `<relativePath>.cnj` exists. */
        bool hasCnj = false;

        /** @brief Every other extension found for this name (e.g. `{".png"}`), in scan order. */
        std::vector<std::string> nativeExtensions;

        /**
         * @brief Canonical `.xnb` type-reader names referenced by `<relativePath>.xnb`'s
         *        type-reader table (XNB-61a/XNB-61b), if @ref hasXnb and the file is well-formed
         *        enough to decode its header/table. LZX- and LZ4-compressed files are decoded
         *        during the scan. A malformed file is not a scan failure; its inventory is empty.
         */
        std::vector<std::string> xnbReaderNames;
    };

    /**
     * @brief CNAEXT aggregate view of every `.xnb` reader name referenced across an entire
     *        content-manifest scan (XNB-67).
     */
    struct CNAEXT ContentManifestReaderUsage
    {
        /** @brief The canonical `.xnb` reader name (see NormalizeXnbTypeReaderName()). */
        std::string readerName;

        /** @brief How many `.xnb` files in the scanned content root reference this reader name. */
        int fileCount = 0;

        /** @brief True if `ContentTypeReaderManager` currently has a factory registered for this name. */
        bool isRegistered = false;
    };
}
