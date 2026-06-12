// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework
{
    /// Provides the base path used to resolve title content files.
    class TitleLocation final
    {
    public:
        /// Static-only class; not instantiable.
        TitleLocation() = delete;

        /// Gets the base directory for title content.
        [[nodiscard]] static const std::string& getPathProperty();

        /// Sets the base directory for title content. Useful for tests and custom launchers.
        NOXNA static void setPathProperty(const std::string& value);

        /// Gets the base directory for title content (matches XNA property name).
        [[nodiscard]] static const std::string& Path();

    private:
        static std::string path_;
        static bool initialized_;

        static void EnsureInitialized();
        [[nodiscard]] static std::string DetectBasePath();
    };
}
