// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Xnb/XnbAssetWriter.hpp"

#include <fstream>
#include <system_error>

#include "CNA/Internal/ContentPath.hpp"

namespace CNA::Internal::Xnb
{
    void WriteXnbFileBytes(const std::filesystem::path& path,
                           const std::vector<std::uint8_t>& bytes)
    {
        // Written to a sibling temporary and renamed, so a reader never observes a half-written
        // asset and a failed build never leaves a truncated file that looks valid enough to load.
        std::filesystem::path temporary = path;
        temporary += ".xnb-tmp";

        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                throw XnbWriteException(
                    "XNB: cannot create '" + ContentPathToUtf8(temporary) + "'.");
            }
            if (!bytes.empty())
            {
                stream.write(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size()));
            }
            stream.flush();
            if (!stream)
            {
                std::error_code ignored;
                std::filesystem::remove(temporary, ignored);
                throw XnbWriteException(
                    "XNB: cannot write '" + ContentPathToUtf8(temporary) + "'.");
            }
        }

        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (error)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw XnbWriteException(
                "XNB: cannot publish '" + ContentPathToUtf8(path) + "': " + error.message() + ".");
        }
    }
}
