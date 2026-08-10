// SPDX-License-Identifier: MS-PL

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "CNA/CNAHelper.hpp"
#include "System/IO/Stream.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief Provides access to title content files. */
    class TitleContainer final
    {
    public:
        /** @brief Static-only class; not instantiable. */
        TitleContainer() = delete;

        /**
         * @brief Opens a title content file for reading.
         * @param name The relative or absolute path to the content file.
         * @return A unique pointer to the opened stream.
         */
        [[nodiscard]] static std::unique_ptr<System::IO::Stream> OpenStream(const std::string& name);

    private:
        using IntPtr = std::uintptr_t;

        [[nodiscard]] static std::string NormalizeFilePathSeparators(const std::string& name);
        [[nodiscard]] static bool IsPathRooted(const std::string& name);
        [[nodiscard]] static std::string CombineTitlePath(const std::string& name);
        [[nodiscard]] static std::string ResolveRealPath(const std::string& name);

        /**
         * @brief Reads a title content file into a newly allocated byte buffer (internal use).
         * @param name The path to the content file.
         * @param size Output parameter that receives the size of the allocated buffer.
         * @return A raw pointer to the allocated buffer; caller must release with FreePointer.
         */
        CNAEXT [[nodiscard]] static void* ReadToPointer(const std::string& name, IntPtr& size);

        /**
         * @brief Releases a pointer returned by ReadToPointer.
         * @param pointer The pointer to release.
         */
        CNAEXT static void FreePointer(void* pointer);
    };
}
