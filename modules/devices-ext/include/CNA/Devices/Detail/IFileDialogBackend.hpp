// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <functional>
#include <string>
#include <vector>

#include "CNA/Devices/FileDialogFilter.hpp"

namespace CNA::Devices::Detail
{
    /**
     * @brief Callback type for a file dialog result — see `IFileDialogBackend`'s own
     * doc comment for why this is not a nested member of `FileDialog` itself.
     */
    using FileDialogResultCallback = std::function<void(const std::vector<std::string>& files)>;

    /**
     * @brief Native file-dialog backend interface (Task DEVICES-CNA-008).
     *
     * `FileDialog` calls through this interface instead of calling SDL3's
     * `SDL_Show*Dialog()` functions directly. This exists specifically because the
     * real backend (`PlatformFileDialogBackend`) has a genuinely uncontainable side
     * effect in an automated test: it launches a real, interactive, native OS
     * dialog (e.g. a real `zenity` process on Linux) that will hang indefinitely
     * waiting for a human, since nothing in an automated test run ever interacts
     * with it. Calling the real backend from a test is not just undesirable but
     * actively harmful (it was tried once during this class's own development and
     * left orphaned `zenity` processes running on the development machine's real
     * desktop session — see `plan_cna_devices.md` Task `DEVICES-CNA-008`'s
     * resolution note for the full account). Making the backend swappable via
     * `FileDialog::SetBackendForTesting()` lets tests exercise the wiring/logic
     * with a fake backend that never touches the real dialog subsystem.
     *
     * This is the same interface-swap pattern already established by
     * `Microsoft::Devices::Detail::IVibrateBackend`/`ICompassBackend`/
     * `IMotionBackend`.
     */
    class IFileDialogBackend
    {
    public:
        virtual ~IFileDialogBackend() = default;

        /** @brief See `FileDialog::ShowOpenFile()` for the full contract. */
        virtual void ShowOpenFile(
            FileDialogResultCallback onResult,
            const std::vector<FileDialogFilter>& filters,
            const std::string& defaultLocation,
            bool allowMultiple) = 0;

        /** @brief See `FileDialog::ShowSaveFile()` for the full contract. */
        virtual void ShowSaveFile(
            FileDialogResultCallback onResult,
            const std::vector<FileDialogFilter>& filters,
            const std::string& defaultLocation) = 0;

        /** @brief See `FileDialog::ShowOpenFolder()` for the full contract. */
        virtual void ShowOpenFolder(
            FileDialogResultCallback onResult,
            const std::string& defaultLocation,
            bool allowMultiple) = 0;
    };
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
