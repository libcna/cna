// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/TitleContainer.hpp"
#include "System/IO/FileNotFoundException.hpp"
#include "System/IO/DirectoryNotFoundException.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/Uri.hpp"
#include "System/UriFormatException.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <cstdint>
#include <vector>

#include "CNA/Platform/CurrentPlatform.hpp"

#include "CNA/Internal/CaseInsensitivePath.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/TitleLocation.hpp"
#include "System/IO/FileStream.hpp"
#include "System/IO/MemoryStream.hpp"
#include "CNA/Internal/PathUtf8.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/IO/IOException.hpp"

namespace Microsoft::Xna::Framework
{
    namespace
    {
        // XNA4 TitleContainer.GetCleanPath removes components before validation.
        // std::filesystem::lexically_normal differs for empty/dot components before '..'.
        std::string CleanTitlePath(std::string path)
        {
            for (char& value : path) if (value == '\\') value = '/';
            for (std::size_t position = 0; (position = path.find("/./", position)) != std::string::npos; ++position)
                path.replace(position, 3, "/");
            while (path.starts_with("./")) path.erase(0, 2);
            while (path.ends_with("/."))
                path = path.size() <= 2 ? "/" : path.substr(0, path.size() - 2);
            const auto collapse = [&path](std::size_t position, std::size_t length)
            {
                const std::size_t previous = path.rfind('/', position - 1);
                const std::size_t start = previous == std::string::npos ? 0 : previous + 1;
                path.erase(start, position - start + length);
                return start > 1 ? start - 1 : std::size_t{1};
            };
            for (std::size_t position = 1; (position = path.find("/../", position)) != std::string::npos;)
                position = collapse(position, 4);
            if (path.ends_with("/..") && path.size() > 3)
                collapse(path.size() - 3, 3);
            if (path == ".") path.clear();
            return path;
        }

        bool IsInvalidTitlePath(const std::string& path)
        {
            return path.find_first_of(":*?\"<>|") != std::string::npos || path.starts_with('/') ||
                   path.starts_with("../") || path.find("/../") != std::string::npos ||
                   path.ends_with("/..") || path == "..";
        }
    }

    std::unique_ptr<System::IO::Stream> TitleContainer::OpenStream(const std::string& name)
    {
        if (name.empty()) throw System::ArgumentNullException("name");
        const std::string safeName = CleanTitlePath(name);
        if (IsInvalidTitlePath(safeName))
            throw System::ArgumentException("The title file name must be a valid relative path.");
        try
        {
            // XNA4 uses Framework4 Uri's 65520 UTF-16-unit limit, independently
            // measured for ASCII and surrogate pairs. The .NET11 reference lacks
            // this bound, so it belongs to the XNA contract, not Sharp's Uri type.
            if (CNA::Internal::PathFromUtf8(safeName).u16string().size() >= 65520)
                throw System::UriFormatException("The title URI exceeds the XNA4 length limit.");
            (void)System::Uri(safeName, System::UriKind::Relative);
        }
        catch (const std::exception&)
        {
            throw System::ArgumentException("The title file name must be a valid relative path.",
                                            std::current_exception());
        }
        try
        {
            const std::string realName = ResolveRealPath(safeName);

            CNA::Logger::Info("[TitleContainer] OpenStream requested: " + name);
            CNA::Logger::Info("[TitleContainer] Resolved path: " + realName);

            // Let FileStream validate the full UTF-8 name before any native open.
            // An existence precheck masks invalid names as missing files and can
            // alias an embedded-NUL prefix. Only genuine missing paths may fall
            // back to Android's packaged assets; other failures retain their cause.
#if defined(__ANDROID__)
            try
#endif
            {
                return std::make_unique<System::IO::FileStream>(realName, System::IO::FileMode::Open,
                                                               System::IO::FileAccess::Read);
            }

#if defined(__ANDROID__)
            catch (const System::IO::FileNotFoundException&) {}
            catch (const System::IO::DirectoryNotFoundException&) {}
            // Android assets are readable through the platform's file loader even when they are not
            // visible as normal files.
            // Try the relative name first, then the resolved name.
            const std::string androidAssetName = IsPathRooted(safeName) ? realName : safeName;

            std::vector<std::uint8_t> assetData;
            CNA::Platform::IPlatformFileSystem* fileSystem =
                CNA::Platform::GetCurrentPlatform().GetFileSystem();

            bool loaded = fileSystem->TryLoadFileIgnoringCase(androidAssetName, assetData);
            if (!loaded && androidAssetName != realName)
            {
                loaded = fileSystem->TryLoadFileIgnoringCase(realName, assetData);
            }

            if (loaded)
            {
                return std::make_unique<System::IO::MemoryStream>(
                    reinterpret_cast<SharpRuntime::bytecs*>(assetData.data()),
                    static_cast<SharpRuntime::intcs>(assetData.size()));
            }
            // XNA documents FileNotFoundException for a title file that is not there (it was a plain
            // runtime_error here).
            throw System::IO::FileNotFoundException("Could not find file '" + realName + "'.", realName);
#endif
        }
        catch (const System::IO::FileNotFoundException&)
        {
            throw;
        }
        catch (const System::IO::DirectoryNotFoundException&)
        {
            throw System::IO::FileNotFoundException("Could not find title file '" + safeName + "'.", safeName);
        }
        catch (const std::exception&)
        {
            // Genuine Windows XNA4 OpenStream IL_0076-0084 translates only the
            // two missing-path categories above; other errors retain their cause.
            // The available decompiled source also listed ArgumentException here,
            // but that branch is absent from the actual reference assembly.
            throw System::InvalidOperationException("Could not open title file '" + safeName + "'.",
                                                    std::current_exception());
        }
    }

    void* TitleContainer::ReadToPointer(const std::string& name, IntPtr& size)
    {
        const std::string safeName = NormalizeFilePathSeparators(name);
        const std::string realName = ResolveRealPath(safeName);

        size = 0;

        std::ifstream file(CNA::Internal::PathFromUtf8(realName), std::ios::binary | std::ios::ate);
        if (file)
        {
            const std::streamsize length = file.tellg();
            if (length < 0)
            {
                throw System::IO::IOException("[TitleContainer] Failed to determine file size: " + realName);
            }

            file.seekg(0, std::ios::beg);

            void* buffer = std::malloc(static_cast<std::size_t>(length));
            if (buffer == nullptr && length > 0)
            {
                throw std::bad_alloc();
            }

            if (length > 0 && !file.read(static_cast<char*>(buffer), length))
            {
                std::free(buffer);
                throw System::IO::IOException("[TitleContainer] Failed to read file: " + realName);
            }

            size = static_cast<IntPtr>(length);
            return buffer;
        }

#if defined(__ANDROID__)
        const std::string androidAssetName = IsPathRooted(safeName) ? realName : safeName;

        std::vector<std::uint8_t> assetData;
        CNA::Platform::IPlatformFileSystem* fileSystem =
            CNA::Platform::GetCurrentPlatform().GetFileSystem();

        bool loaded = fileSystem->TryLoadFileIgnoringCase(androidAssetName, assetData);
        if (!loaded && androidAssetName != realName)
        {
            loaded = fileSystem->TryLoadFileIgnoringCase(realName, assetData);
        }

        if (loaded)
        {
            // The caller owns the returned block and frees it with std::free, so the platform's
            // vector cannot simply be released into it -- the copy is the same one the previous
            // implementation made out of its loader's own buffer.
            void* buffer = std::malloc(assetData.size());
            if (buffer == nullptr && !assetData.empty())
            {
                throw std::bad_alloc();
            }

            if (!assetData.empty())
            {
                std::memcpy(buffer, assetData.data(), assetData.size());
            }

            size = static_cast<IntPtr>(assetData.size());
            return buffer;
        }
#endif

        throw System::IO::FileNotFoundException("Could not find file '" + realName + "'.", realName);
    }

    void TitleContainer::FreePointer(void* pointer)
    {
        std::free(pointer);
    }

    std::string TitleContainer::NormalizeFilePathSeparators(const std::string& name)
    {
        std::string result = name;
        for (char& ch : result)
        {
            if (ch == '\\')
            {
                ch = '/';
            }
        }
        return result;
    }

    bool TitleContainer::IsPathRooted(const std::string& name)
    {
        if (name.empty())
        {
            return false;
        }

        if (name[0] == '/')
        {
            return true;
        }

#if defined(_WIN32)
        if (name.size() >= 3 &&
            ((name[0] >= 'A' && name[0] <= 'Z') || (name[0] >= 'a' && name[0] <= 'z')) &&
            name[1] == ':' &&
            (name[2] == '/' || name[2] == '\\'))
        {
            return true;
        }
#endif

        return false;
    }

    std::string TitleContainer::CombineTitlePath(const std::string& name)
    {
        const std::filesystem::path base =
            CNA::Internal::PathFromUtf8(TitleLocation::getPathProperty());
        return CNA::Internal::PathToGenericUtf8(
            (base / CNA::Internal::PathFromUtf8(name)).lexically_normal());
    }

    std::string TitleContainer::ResolveRealPath(const std::string& name)
    {
        if (IsPathRooted(name))
        {
            return CNA::Internal::ResolveExistingXnaPath(CNA::Internal::PathToGenericUtf8(
                CNA::Internal::PathFromUtf8(name).lexically_normal()));
        }

#if defined(__ANDROID__)
        // A relative title path names an AssetManager entry on Android, not a child of the
        // process working directory. Preserve that logical name so the platform loader below
        // can resolve it; host case-folding would otherwise enumerate the sandboxed `/`.
        return CNA::Internal::NormalizeXnaPathSeparators(name);
#else
        return CNA::Internal::ResolveExistingXnaPath(CombineTitlePath(name));
#endif
    }
}
