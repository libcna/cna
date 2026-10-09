// SPDX-License-Identifier: MS-PL

#include "TitleContentBytes.hpp"

#include "CNA/Internal/CaseInsensitivePath.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "CNA/Internal/TitlePath.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "System/IO/FileNotFoundException.hpp"

#include <filesystem>
#include <optional>

namespace CNA::Internal::Audio
{
    std::vector<std::uint8_t> ReadTitleContentBytes(
        const std::string& filename)
    {
        std::vector<std::uint8_t> bytes;
        const std::string logicalName =
            CNA::Internal::NormalizeXnaPathSeparators(filename);
        CNA::Platform::IPlatformFileSystem* fileSystem =
            CNA::Platform::GetCurrentPlatform().GetFileSystem();
#if !defined(__ANDROID__)
        // XNA's AudioEngine, WaveBank and SoundBank open their file through TitleContainer, so a
        // relative name is under TitleLocation.Path, not the working directory (FNA reads it with
        // TitleContainer.ReadToPointer too). A game started from anywhere else -- Finder, another
        // shell directory -- drew all of its ContentManager content and found none of its XACT
        // files, and played silently (AM4-321). The working directory stays the fallback, as it
        // is for ContentManager's root. Android keeps SDL's packaged-asset namespace.
        std::string title = CNA::Internal::TryGetTitlePath().value_or(std::string{});
        if (title.empty())
        {
            title = fileSystem->GetBasePath();
        }
        const std::optional<std::filesystem::path> relative =
            CNA::Internal::TryPathFromUtf8(logicalName);
        const std::optional<std::filesystem::path> titleDirectory =
            CNA::Internal::TryPathFromUtf8(title);
        if (!title.empty() && relative && titleDirectory && !relative->is_absolute() &&
            !relative->has_root_name() &&
            fileSystem->TryLoadFileIgnoringCase(
                CNA::Internal::PathToGenericUtf8((*titleDirectory / *relative).lexically_normal()),
                bytes))
        {
            return bytes;
        }
#endif
        if (!fileSystem->TryLoadFileIgnoringCase(logicalName, bytes))
        {
            throw System::IO::FileNotFoundException(
                "Could not find file '" + filename + "'.", filename);
        }
        return bytes;
    }
}
