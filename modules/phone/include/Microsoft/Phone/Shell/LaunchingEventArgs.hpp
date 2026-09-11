// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/EventArgs.hpp"

namespace Microsoft::Phone::Shell {

    /**
     * @brief Arguments for the event raised when the application starts fresh.
     *
     * Fresh means no preserved state: the application was started by the user rather than
     * resumed, and anything it saved before must be read back deliberately. A run raises either
     * this or Activated, never both.
     */
    class LaunchingEventArgs : public System::EventArgs {
    public:
        /** @brief Constructs the arguments. */
        LaunchingEventArgs() = default;
    };

} // namespace Microsoft::Phone::Shell
