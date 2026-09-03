// SPDX-License-Identifier: MS-PL

#include "TitleContentBytes.hpp"

#include "CNA/Internal/CaseInsensitivePath.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "System/IO/FileNotFoundException.hpp"

namespace CNA::Internal::Audio
{
    std::vector<std::uint8_t> ReadTitleContentBytes(
        const std::string& filename)
    {
        std::vector<std::uint8_t> bytes;
        const std::string logicalName =
            CNA::Internal::NormalizeXnaPathSeparators(filename);
        if (!CNA::Platform::GetCurrentPlatform().GetFileSystem()
                 ->TryLoadFileIgnoringCase(logicalName, bytes))
        {
            throw System::IO::FileNotFoundException(
                "Could not find file '" + filename + "'.", filename);
        }
        return bytes;
    }
}
