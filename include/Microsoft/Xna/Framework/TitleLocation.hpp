#pragma once

#include <string>

namespace Microsoft::Xna::Framework
{
    /// Provides the base path used to resolve title content files.
    class TitleLocation final
    {
    public:
        TitleLocation() = delete;

        /// Gets the base directory for title content.
        [[nodiscard]] static const std::string& getPathProperty();

        /// Sets the base directory for title content. Useful for tests and custom launchers.
        static void setPathProperty(const std::string& value);

        /// XNA-style property helper for code that expects TitleLocation::Path().
        [[nodiscard]] static const std::string& Path();

    private:
        static std::string path_;
        static bool initialized_;

        static void EnsureInitialized();
        [[nodiscard]] static std::string DetectBasePath();
    };
}
