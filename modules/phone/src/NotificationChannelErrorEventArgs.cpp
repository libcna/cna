// SPDX-License-Identifier: MS-PL
#include "Microsoft/Phone/Notification/NotificationChannelErrorEventArgs.hpp"

#include <utility>

namespace Microsoft::Phone::Notification {

using SharpRuntime::intcs;
using SharpRuntime::String;

NotificationChannelErrorEventArgs::NotificationChannelErrorEventArgs(
    ChannelErrorType type, intcs code, intcs additionalData, String message)
    : type_(type), code_(code), additionalData_(additionalData), message_(std::move(message))
{
}

ChannelErrorType NotificationChannelErrorEventArgs::getErrorTypeProperty() const { return type_; }
intcs NotificationChannelErrorEventArgs::getErrorCodeProperty() const { return code_; }
intcs NotificationChannelErrorEventArgs::getErrorAdditionalDataProperty() const { return additionalData_; }
const String& NotificationChannelErrorEventArgs::getMessageProperty() const { return message_; }

} // namespace Microsoft::Phone::Notification
