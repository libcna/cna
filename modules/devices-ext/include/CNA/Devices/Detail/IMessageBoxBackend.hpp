// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <string>
#include <vector>

#include "CNA/Devices/MessageBoxType.hpp"

namespace CNA::Devices::Detail
{
    /**
     * @brief Native message-box backend interface (Task DEVICES-CNA-011).
     *
     * `MessageBox` calls through this interface rather than reaching the platform's dialog
     * service directly. This exists for
     * the same reason `IFileDialogBackend` does: the real backend
     * (`PlatformMessageBoxBackend`) pops a genuine, interactive, modal native OS dialog
     * that blocks the calling thread until a human closes it — calling it from an
     * automated test would hang the test run indefinitely, the same class of
     * incident already hit once during `FileDialog`'s own development (see
     * `plan_cna_devices.md` Task `DEVICES-CNA-008`'s resolution note). Making the
     * backend swappable via `MessageBox::SetBackendForTesting()` lets tests exercise
     * the wiring/logic with a fake backend that never shows a real dialog.
     *
     * Unlike `IFileDialogBackend`, both methods here return synchronously — a message box blocks
     * the calling thread until the user responds, so there is no async callback or
     * result-lifetime concern to model.
     */
    class IMessageBoxBackend
    {
    public:
        virtual ~IMessageBoxBackend() = default;

        /** @brief See `MessageBox::ShowSimple()` for the full contract. */
        virtual void ShowSimple(MessageBoxType type, const std::string& title, const std::string& message) = 0;

        /**
         * @brief See `MessageBox::Show()` for the full contract.
         * @return The index into `buttonLabels` of the button the user clicked.
         */
        virtual int Show(
            MessageBoxType type,
            const std::string& title,
            const std::string& message,
            const std::vector<std::string>& buttonLabels) = 0;
    };
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
