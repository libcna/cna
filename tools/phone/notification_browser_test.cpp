// SPDX-License-Identifier: MS-PL
#include "Microsoft/Phone/Notification/PushNotificationSender.hpp"
#include "Microsoft/Phone/Notification/HttpNotificationChannel.hpp"

#include <cstdio>

int main()
{
    using namespace Microsoft::Phone::Notification;
    const System::Uri unreachable("http://127.0.0.1:9/notify");
    RawPushNotificationMessage raw;
    raw.RawData = {'x'};
    ToastPushNotificationMessage toast;
    toast.Title = "notification transport regression";
    const bool rawAccepted = raw.SendAsync(unreachable);
    const bool toastAccepted = toast.SendAsync(unreachable);
    std::printf("NOTIFICATION_BROWSER raw=%d toast=%d\n", rawAccepted, toastAccepted);
    HttpNotificationChannel channel("browser-open-failure", "regression");
    unsigned errors = 0, uris = 0;
    bool correctError = true;
    channel.ChannelUriUpdated += [&](System::Object*, const auto&) { ++uris; };
    channel.ErrorOccurred += [&](System::Object*, const NotificationChannelErrorEventArgs& args) {
        ++errors;
        correctError &= args.getErrorTypeProperty() == ChannelErrorType::ChannelOpenFailed &&
            args.getErrorCodeProperty() == -2147467259 &&
            args.getErrorAdditionalDataProperty() != 0 && !args.getMessageProperty().empty();
    };
    channel.Open();
    const bool queued = errors == 0;
    channel.DispatchPendingNotificationsEXT();
    channel.DispatchPendingNotificationsEXT();
    const bool closed = !channel.getChannelUriProperty() &&
        !HttpNotificationChannel::Find("browser-open-failure");
    std::printf("NOTIFICATION_OPEN errors=%u uris=%u closed=%d queued=%d correct=%d\n",
                errors, uris, closed, queued, correctError);
    return rawAccepted || toastAccepted || errors != 1 || uris || !closed || !queued || !correctError ? 1 : 0;
}
