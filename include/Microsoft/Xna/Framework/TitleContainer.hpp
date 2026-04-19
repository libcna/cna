#pragma once

#include <memory>
#include <string>

#include "System/IO/Stream.hpp"

namespace Microsoft::Xna::Framework
{
    /**
     * @brief Provides access to title content files.
     *
     * This class is a lightweight C++ emulation of XNA TitleContainer.
     *
     * @note Status: PARTIAL
     */
    class TitleContainer
    {
    public:
        TitleContainer() = delete;
        ~TitleContainer() = delete;

        /**
         * @brief Opens a content file for reading.
         *
         * @param path Relative or absolute path to the content file.
         * @return Opened stream.
         *
         * @note Status: IMPLEMENTED
         */
        [[nodiscard]] static std::unique_ptr<System::IO::Stream> OpenStream(const std::string& path);
    };
}