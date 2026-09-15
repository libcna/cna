// SPDX-License-Identifier: MS-PL

#pragma once

#include "CNA/CNAHelper.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/EventArgs.hpp"

#include <vector>

namespace Microsoft::Xna::Framework
{
    /**
     * @brief Event arguments of `GameWindow::FileDropEXT`: the files of one drop.
     *
     * @note CNAEXT — CNA extension, not XNA API. XNA had no drag and drop; the shape is
     * MonoGame's `FileDropEventArgs`, so a game ported from there finds what it expects.
     */
    CNAEXT class FileDropEventArgsEXT : public System::EventArgs
    {
    public:
        /**
         * @brief Creates the arguments for one drop.
         *
         * @param files The dropped files' local paths, UTF-8, in the order they were dropped.
         */
        explicit FileDropEventArgsEXT(std::vector<SharpRuntime::String> files);

        /**
         * @brief Gets the dropped files.
         *
         * @return Their local paths, UTF-8, in the order they were dropped; never empty.
         */
        [[nodiscard]] const std::vector<SharpRuntime::String>& getFilesProperty() const;

    private:
        std::vector<SharpRuntime::String> files_;
    };
}
