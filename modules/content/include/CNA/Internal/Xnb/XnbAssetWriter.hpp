// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "CNA/Internal/Xnb/XnbBuiltInWriters.hpp"
#include "CNA/Internal/Xnb/XnbFileOptions.hpp"
#include "CNA/Internal/Xnb/XnbTypeWriter.hpp"
#include "CNA/Internal/Xnb/XnbWriter.hpp"

namespace CNA::Internal::Xnb
{
    /**
     * @brief One-call entry point producing a complete `.xnb` file image
     *        (plans/plan_xnapipeline.md `XNAP-16`).
     *
     * Everything the file needs is decided here: the type-reader table is interned in first-use
     * order as the graph is written, shared resources are serialized after the root object, and
     * the ten-byte header is prepended once the payload's final length is known. The result is a
     * pure function of its arguments — no clock, no random source, no host paths, no locale — so
     * the same inputs always produce the same bytes.
     *
     * @tparam T The root type; a writer for it must be registered.
     * @param root The root value to serialize.
     * @param options Container-level configuration.
     * @param assetName Logical asset name used in diagnostics only; never serialized.
     * @param registry Frozen registry to resolve type writers from.
     * @return The complete `.xnb` file image.
     * @throws XnbWriteException for an unregistered type, an exceeded limit, an invalid option
     *         combination, or a value the format cannot express.
     */
    template<typename T>
    [[nodiscard]] std::vector<std::uint8_t> WriteXnbAsset(
        const T& root, const XnbFileOptions& options = {}, const std::string& assetName = {},
        const XnbTypeWriterRegistry& registry = BuiltInXnbWriterRegistry())
    {
        XnbWriter writer(registry, options, assetName);
        return writer.WriteAsset(root);
    }

    /**
     * @brief Writes a complete `.xnb` file to disk through a temporary file and an atomic rename.
     *
     * @tparam T The root type; a writer for it must be registered.
     * @param path Destination path; its parent directory must already exist.
     * @param root The root value to serialize.
     * @param options Container-level configuration.
     * @param assetName Logical asset name used in diagnostics only.
     * @param registry Frozen registry to resolve type writers from.
     * @throws XnbWriteException for a serialization refusal or a filesystem failure.
     */
    template<typename T>
    void WriteXnbAssetFile(
        const std::filesystem::path& path, const T& root, const XnbFileOptions& options = {},
        const std::string& assetName = {},
        const XnbTypeWriterRegistry& registry = BuiltInXnbWriterRegistry())
    {
        WriteXnbFileBytes(path, WriteXnbAsset(root, options, assetName, registry));
    }

    /**
     * @brief Writes complete `.xnb` bytes to disk through a temporary file and an atomic rename.
     *
     * Separated from the templates above so the filesystem handling exists once, in one
     * translation unit, rather than being instantiated per root type.
     *
     * @param path Destination path; its parent directory must already exist.
     * @param bytes The complete file image.
     * @throws XnbWriteException if the file cannot be created, written or renamed.
     */
    void WriteXnbFileBytes(const std::filesystem::path& path,
                           const std::vector<std::uint8_t>& bytes);
}
