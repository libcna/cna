#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "System/IO/Stream.hpp"

namespace Microsoft::Xna::Framework
{
    /// Provides access to title content files.
    class TitleContainer final
    {
    public:
        using IntPtr = std::uintptr_t;

        TitleContainer() = delete;

        /// Opens a title content file for reading.
        [[nodiscard]] static std::unique_ptr<System::IO::Stream> OpenStream(const std::string& name);

        /// Reads a title content file into a newly allocated byte buffer.
        ///
        /// The returned pointer must be released with FreePointer().
        [[nodiscard]] static void* ReadToPointer(const std::string& name, IntPtr& size);

        /// Releases a pointer returned by ReadToPointer().
        static void FreePointer(void* pointer);

    private:
        [[nodiscard]] static std::string NormalizeFilePathSeparators(const std::string& name);
        [[nodiscard]] static bool IsPathRooted(const std::string& name);
        [[nodiscard]] static std::string CombineTitlePath(const std::string& name);
        [[nodiscard]] static std::string ResolveRealPath(const std::string& name);
    };
}
