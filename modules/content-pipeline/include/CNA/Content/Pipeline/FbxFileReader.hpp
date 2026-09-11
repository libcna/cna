// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <exception>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace CNA::Content::Pipeline
{
    /**
     * @brief One property of an FBX node: a scalar, a string, or an array of numbers.
     *
     * The binary encoding distinguishes more types than this does (signed widths, raw blobs), and
     * the text encoding distinguishes none. What every consumer of an FBX actually asks a property
     * is "what number is this", "what text is this" or "what array is this", so that is what the
     * reader answers; the exact width a file stored a value in is not a thing the object model
     * carries into the pipeline.
     */
    using FbxProperty = std::variant<double, std::string, std::vector<double>>;

    /** @brief One node of an FBX document: a name, its properties, and the nodes inside it. */
    struct FbxNode
    {
        /** @brief The node's name, `Model`, `Vertices`, `Properties70` and so on. */
        std::string name;

        /** @brief The node's own properties, in order. */
        std::vector<FbxProperty> properties;

        /** @brief The nodes nested inside this one, in order. */
        std::vector<FbxNode> children;

        /**
         * @brief The first child of a name, or null.
         *
         * @param childName The name to look for.
         * @return The child, or null when there is none.
         */
        [[nodiscard]] const FbxNode* Find(const std::string& childName) const;

        /**
         * @brief One property as a number, or a fallback.
         *
         * @param index Which property.
         * @param fallback What to answer when the property is absent or not a number.
         * @return The number.
         */
        [[nodiscard]] double Number(std::size_t index, double fallback = 0.0) const;

        /**
         * @brief One property as text, or a fallback.
         *
         * @param index Which property.
         * @param fallback What to answer when the property is absent or not text.
         * @return The text.
         */
        [[nodiscard]] std::string Text(std::size_t index, const std::string& fallback = {}) const;

        /**
         * @brief The node's numbers, whether it stored them as an array or one by one.
         *
         * The text encoding writes a long list as loose numbers and the binary one as an array,
         * and a consumer asking for a mesh's vertices means the same thing either way.
         *
         * @return The numbers, in order.
         */
        [[nodiscard]] std::vector<double> Numbers() const;
    };

    /** @brief A parsed FBX document. */
    struct FbxFile
    {
        /** @brief The version the file declares: 6100, 7400, 7500 and so on. */
        std::uint32_t version = 0u;

        /** @brief Whether the file was the binary encoding. */
        bool binary = false;

        /** @brief The document's top-level nodes. */
        std::vector<FbxNode> nodes;

        /**
         * @brief The first top-level node of a name, or null.
         *
         * @param name The name to look for.
         * @return The node, or null.
         */
        [[nodiscard]] const FbxNode* Find(const std::string& name) const;
    };

    /** @brief Why an FBX file could not be read. */
    enum class FbxFileError
    {
        /** @brief The file held no bytes, or nothing a loader could initialize from. */
        CannotInitialize,
        /** @brief The bytes are not an FBX document at all. */
        NotFbx,
        /** @brief The document is malformed past its header. */
        ParseError,
        /** @brief The document needs something this build does not have, such as zlib. */
        Unsupported,
    };

    /** @brief An FBX file that could not be read, carrying which kind of failure it was. */
    class FbxFileException : public std::exception
    {
    public:
        /**
         * @brief Creates the failure.
         *
         * @param error Which kind of failure it was.
         * @param detail A sentence naming what was wrong.
         */
        FbxFileException(FbxFileError error, std::string detail);

        /** @brief Which kind of failure it was. */
        [[nodiscard]] FbxFileError Error() const noexcept;

        /** @brief A sentence naming what was wrong. */
        [[nodiscard]] const char* what() const noexcept override;

    private:
        FbxFileError error_;
        std::string detail_;
    };

    /** @brief Ceilings an FBX file may not exceed, so a malformed one cannot exhaust this host. */
    struct FbxFileLimits
    {
        /** @brief Largest file this reader will read, in bytes. */
        std::size_t maximumBytes = 512u * 1024u * 1024u;
        /** @brief Deepest nesting of nodes. */
        std::size_t maximumDepth = 64u;
        /** @brief Most nodes one document may hold. */
        std::size_t maximumNodes = 4000000u;
        /** @brief Most numbers one array may hold. */
        std::size_t maximumArrayLength = 64u * 1024u * 1024u;
        /** @brief Largest an array may decompress to, in bytes. */
        std::size_t maximumDecompressedBytes = 512u * 1024u * 1024u;
        /** @brief Longest name or string, in bytes. */
        std::size_t maximumStringLength = 1024u * 1024u;
    };

    /**
     * @brief Reads an FBX document into its node tree.
     *
     * Both encodings are read: the text one, which is what FBX 6.1 files are and what XNA's own
     * SDK reads, and the binary record stream every current exporter writes. Reading the binary
     * one is deliberately beyond what XNA does -- its FBX SDK 2011.3.1 refuses version 7400 and
     * above, which the oracle records -- and is a recorded divergence rather than an accident:
     * refusing a file a user's tool just wrote, to match a bundled SDK's age, would serve nobody.
     *
     * @param bytes The complete file.
     * @param limits Ceilings a malformed file may not exceed.
     * @return The parsed document.
     * @throws FbxFileException naming which kind of failure it was.
     */
    [[nodiscard]] FbxFile ReadFbxFile(std::span<const std::uint8_t> bytes,
                                      const FbxFileLimits& limits = {});
}
