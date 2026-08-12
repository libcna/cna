// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include "CNA/Devices/Detail/IFileDialogBackend.hpp"

namespace CNA::Devices::Detail
{
    /**
     * @brief Real `IFileDialogBackend` implementation, forwarding to the platform's dialog
     * service. The default backend `FileDialog` uses outside of tests.
     */
    class PlatformFileDialogBackend final : public IFileDialogBackend
    {
    public:
        /**
         * @brief Shows a file-open dialog.
         * @param onResult Called once with the selected files, or empty on cancel.
         * @param filters The file type filters.
         * @param defaultLocation The directory to start in, or empty.
         * @param allowMultiple Whether more than one file may be selected.
         */
        void ShowOpenFile(
            FileDialogResultCallback onResult,
            const std::vector<FileDialogFilter>& filters,
            const std::string& defaultLocation,
            bool allowMultiple) override;

        /**
         * @brief Shows a file-save dialog.
         * @param onResult Called once with the chosen file, or empty on cancel.
         * @param filters The file type filters.
         * @param defaultLocation The directory or file name to start with, or empty.
         */
        void ShowSaveFile(
            FileDialogResultCallback onResult,
            const std::vector<FileDialogFilter>& filters,
            const std::string& defaultLocation) override;

        /**
         * @brief Shows a folder-selection dialog.
         * @param onResult Called once with the chosen folders, or empty on cancel.
         * @param defaultLocation The directory to start in, or empty.
         * @param allowMultiple Whether more than one folder may be selected.
         */
        void ShowOpenFolder(
            FileDialogResultCallback onResult,
            const std::string& defaultLocation,
            bool allowMultiple) override;
    };
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
