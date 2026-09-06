// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace CNA::Content::Pipeline
{
    /**
     * @brief One data object out of a DirectX `.x` file, with its data flattened.
     *
     * A `.x` file is a tree of typed objects. Their layouts are declared by `template` blocks the
     * file may carry, but every template this pipeline needs is one of the standard ones whose
     * layout is fixed, so the reader does not interpret templates: it reads each object's data as
     * the sequence of numbers and strings it literally contains, and the layer above -- which
     * knows what a `Mesh` or a `SkinWeights` holds -- reads that sequence positionally.
     *
     * This is the whole of the format handling. What a `.x` object *means* to XNA is
     * `XImporter`'s, and is measured rather than assumed
     * (`tests/reference/xna40/model/model-import-oracle.json`).
     */
    struct DirectXFileObject
    {
        /** @brief The template name, `Frame`, `Mesh`, `MeshNormals` and so on. */
        std::string type;

        /** @brief The object's own name, or empty when it has none. */
        std::string name;

        /** @brief Every number the object holds, in the order it holds them. */
        std::vector<double> numbers;

        /** @brief Every string the object holds, in order. */
        std::vector<std::string> strings;

        /** @brief The objects nested inside this one, in order. */
        std::vector<DirectXFileObject> children;

        /** @brief Names this object references by `{ Name }` rather than nesting. */
        std::vector<std::string> references;
    };

    /** @brief A parsed `.x` file: its top-level objects, templates already dropped. */
    struct DirectXFile
    {
        /** @brief The four format characters after `xof 0303`: `txt`, `bin`, `tzip` or `bzip`. */
        std::string encoding;

        /** @brief The top-level data objects. */
        std::vector<DirectXFileObject> objects;
    };

    /**
     * @brief Why a `.x` file could not be read, in the vocabulary XNA's own message uses.
     *
     * The genuine importer appends the D3DX code its reader answered, and the code says which
     * kind of failure it was (measured, `model-import-oracle.json` cases `x/empty.x`,
     * `x/not_x.x`, `x/bad_version.x`, `x/truncated.x`).
     */
    enum class DirectXFileError
    {
        /** @brief The file held no bytes at all: `D3DXFERR_BADFILE`. */
        BadFile,
        /** @brief The file does not begin with `xof`: `D3DXFERR_BADFILETYPE`. */
        BadFileType,
        /** @brief The version or encoding is not one this reader knows: `D3DXFERR_BADFILEVERSION`. */
        BadFileVersion,
        /** @brief The tokens are malformed or the file ends inside an object: `D3DXFERR_PARSEERROR`. */
        ParseError,
    };

    /** @brief A `.x` file that could not be read, carrying which kind of failure it was. */
    class DirectXFileException : public std::exception
    {
    public:
        /**
         * @brief Creates the failure.
         *
         * @param error Which kind of failure it was.
         * @param detail A sentence naming what was wrong, for a build log.
         */
        DirectXFileException(DirectXFileError error, std::string detail);

        /** @brief Which kind of failure it was. */
        [[nodiscard]] DirectXFileError Error() const noexcept;

        /** @brief The D3DX code name XNA's message carries for this failure. */
        [[nodiscard]] const char* CodeName() const noexcept;

        /** @brief A sentence naming what was wrong. */
        [[nodiscard]] const char* what() const noexcept override;

    private:
        DirectXFileError error_;
        std::string detail_;
    };

    /** @brief Ceilings a `.x` file may not exceed, so a malformed one cannot exhaust this host. */
    struct DirectXFileLimits
    {
        /** @brief Largest file this reader will read, in bytes. */
        std::size_t maximumBytes = 256u * 1024u * 1024u;

        /** @brief Deepest nesting of data objects. */
        std::size_t maximumDepth = 64u;

        /** @brief Most data objects one file may hold, nested ones included. */
        std::size_t maximumObjects = 1000000u;

        /** @brief Most numbers one object may hold. */
        std::size_t maximumNumbersPerObject = 64u * 1024u * 1024u;

        /** @brief Longest name or string, in bytes. */
        std::size_t maximumStringLength = 65536u;
    };

    /**
     * @brief Reads a DirectX `.x` file into its object tree.
     *
     * Both encodings the format defines for uncompressed data are read -- the text one and the
     * binary token stream -- because they are the same object model and a reader that took only
     * one would leave half of what XNA accepts unreadable. The two compressed encodings (`tzip`
     * and `bzip`) are refused by name rather than silently mis-read.
     *
     * @param bytes The complete file.
     * @param limits Ceilings a malformed file may not exceed.
     * @return The parsed file.
     * @throws DirectXFileException naming which kind of failure it was.
     */
    [[nodiscard]] DirectXFile ReadDirectXFile(std::span<const std::uint8_t> bytes,
                                              const DirectXFileLimits& limits = {});
}
