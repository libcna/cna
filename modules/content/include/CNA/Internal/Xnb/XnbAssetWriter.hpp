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
    /** @brief A complete `.xnb` image together with the reader its root object dispatched to. */
    struct XnbAssetWriteResult
    {
        /** @brief The complete `.xnb` file image. */
        std::vector<std::uint8_t> bytes;

        /**
         * @brief Canonical, assembly-free reader name the root object dispatched to.
         *
         * An `.xnb`'s compatibility identity, recorded from the write itself
         * (plans/plan_xnapipeline.md `XNAP-99`).
         */
        std::string rootReaderName;
    };

    /**
     * @brief Produces a complete `.xnb` file image and reports its root reader name.
     *
     * Identical to @ref WriteXnbAsset in every respect but the return type; a caller that has to
     * record the file's identity — a build manifest, an incremental check — needs the reader name
     * observed from the write rather than a second, hand-maintained copy of it.
     *
     * @tparam T The root type; a writer for it must be registered.
     * @param root The root value to serialize.
     * @param options Container-level configuration.
     * @param assetName Logical asset name used in diagnostics only; never serialized.
     * @param registry Frozen registry to resolve type writers from.
     * @return The file image and the root reader name.
     * @throws XnbWriteException under exactly the conditions @ref WriteXnbAsset throws.
     */
    template<typename T>
    [[nodiscard]] XnbAssetWriteResult WriteXnbAssetWithIdentity(
        const T& root, const XnbFileOptions& options = {}, const std::string& assetName = {},
        const XnbTypeWriterRegistry& registry = BuiltInXnbWriterRegistry())
    {
        XnbWriter writer(registry, options, assetName);
        XnbAssetWriteResult result;
        result.bytes = writer.WriteAsset(root);
        result.rootReaderName = writer.RootReaderName();
        return result;
    }

    template<typename T>
    void WriteXnbAssetFile(
        const std::filesystem::path& path, const T& root, const XnbFileOptions& options = {},
        const std::string& assetName = {},
        const XnbTypeWriterRegistry& registry = BuiltInXnbWriterRegistry())
    {
        WriteXnbFileBytes(path, WriteXnbAsset(root, options, assetName, registry));
    }

}
