// SPDX-License-Identifier: MS-PL
#pragma once

#include "System/EventArgs.hpp"

namespace Microsoft::Phone::Shell {

    /**
     * @brief Arguments for the event raised when the application moves to the background.
     *
     * This is the application's opportunity to persist anything it wants back: on a phone the
     * process may be terminated after this without any further notice, which is what makes this
     * event, rather than Closing, the one a game saves from.
     */
    class DeactivatedEventArgs : public System::EventArgs {
    public:
        /** @brief Constructs the arguments. */
        DeactivatedEventArgs() = default;
    };

} // namespace Microsoft::Phone::Shell
