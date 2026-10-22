// SPDX-License-Identifier: MS-PL
#include "Microsoft/Phone/Notification/PushNotificationSender.hpp"

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
    return rawAccepted || toastAccepted ? 1 : 0;
}
