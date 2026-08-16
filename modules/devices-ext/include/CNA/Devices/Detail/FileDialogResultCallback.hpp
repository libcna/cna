// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <functional>
#include <string>
#include <vector>

namespace CNA::Devices::Detail
{
    /**
     * @brief Callback type for a file dialog result.
     *
     * In `Detail` rather than nested inside `FileDialog` so the signature can be named without
     * pulling in the whole class, which the platform-facing plumbing needs.
     *
     * Receives the selected paths, or an empty list when the user cancelled — and also when the
     * platform has no file dialogs at all. Those two are deliberately the same answer: a caller
     * has already registered a continuation by the time it could learn the dialog is unavailable,
     * so anything other than invoking it would leave that continuation pending forever.
     */
    using FileDialogResultCallback = std::function<void(const std::vector<std::string>& files)>;
}

#endif // CNA_DEVICES
