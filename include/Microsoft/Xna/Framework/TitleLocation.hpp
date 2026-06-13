// SPDX-License-Identifier: MS-PL

#pragma once

#include <string>

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief Provides the base path used to resolve title content files. */
    class TitleLocation final
    {
    public:
        /** @brief Static-only class; not instantiable. */
        TitleLocation() = delete;

        /**
         * @brief Gets the base directory for title content.
         * @return A const reference to the base content path string.
         */
        [[nodiscard]] static const std::string& getPathProperty();

        /**
         * @brief Sets the base directory for title content. Useful for tests and custom launchers.
         * @param value The new base content path.
         */
        NOXNA static void setPathProperty(const std::string& value);

        /**
         * @brief Gets the base directory for title content (matches XNA property name).
         * @return A const reference to the base content path string.
         */
        [[nodiscard]] static const std::string& Path();

    private:
        static std::string path_;
        static bool initialized_;

        static void EnsureInitialized();
        [[nodiscard]] static std::string DetectBasePath();
    };
}
