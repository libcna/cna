// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include "CNA/Devices/Detail/IFileDialogBackend.hpp"

namespace CNA::Devices::Detail
{
    /**
     * @brief Real `IFileDialogBackend` implementation calling SDL3's
     * `SDL_ShowOpenFileDialog()`/`SDL_ShowSaveFileDialog()`/`SDL_ShowOpenFolderDialog()`
     * directly. The default backend `FileDialog` uses outside of tests.
     */
    class SdlFileDialogBackend final : public IFileDialogBackend
    {
    public:
        void ShowOpenFile(
            FileDialogResultCallback onResult,
            const std::vector<FileDialogFilter>& filters,
            const std::string& defaultLocation,
            bool allowMultiple) override;

        void ShowSaveFile(
            FileDialogResultCallback onResult,
            const std::vector<FileDialogFilter>& filters,
            const std::string& defaultLocation) override;

        void ShowOpenFolder(
            FileDialogResultCallback onResult,
            const std::string& defaultLocation,
            bool allowMultiple) override;
    };
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
