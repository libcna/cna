// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Phone::Notification {

    /** @brief Identifies an asynchronous notification channel error. */
    enum class ChannelErrorType {
        /** @brief The notification channel could not be opened. */
        ChannelOpenFailed = 0,
        /** @brief A notification payload has an invalid format. */
        PayloadFormatError = 1,
        /** @brief A notification contains unsupported content. */
        MessageBadContent = 2,
        /** @brief Notifications arrive faster than the service permits. */
        NotificationRateTooHigh = 3,
        /** @brief The device's notification power policy changed. */
        PowerLevelChanged = 4,
        /** @brief The channel encountered an unclassified error. */
        Unknown = 5
    };

} // namespace Microsoft::Phone::Notification
