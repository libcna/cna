# Notification error contract evidence

Verified 2026-10-22 against Microsoft's Windows Phone SDK 8.0 reference assembly,
not inferred from CNA or the museum's suggested spelling.

- Official download page: <https://www.microsoft.com/en-us/download/details.aspx?id=35471>
- Official ISO: <https://download.microsoft.com/download/9/3/8/938A5074-461F-4E3D-89F4-5CE2F42C1E36/wpsdkv80_enu1.iso>
- ISO9660 entry `packages/MobileTools/wpsdkcore/WPSDK_en.cab`: sector638032,
  sector size2048, length40163652. Only that byte range was downloaded; no SDK
  installer was run. The bootstrapper's individual cabinet redirect returned404.
- Cabinet SHA1 `e4e41cff011bf11c53a3ebf216253aa042706660` matches its official
  `WPexpress_full.exe` Burn manifest. The cabinet's signing trailer explains
  cabextract's extra-bytes warning.
- Assembly entry `REFASM_MICROSOFT_PHONE_DLL.CACC910A_708E_42B4_AAFA_C07F28123076`,
  SHA256 `a642cb8e71a11a4c6780a5729834d39d07e21c67ed512e085c5cb1a318707134`.
- Metadata read with Mono.Cecil0.11.1; SDK code was not executed. `monodis`
  successfully read fields/constants/method signatures but its full disassembly
  aborted resolving SDK dependencies; the complete-disassembly attempt is not
  evidence for implementation behavior.

`ChannelErrorType` is an Int32 enum: ChannelOpenFailed=0, PayloadFormatError=1,
MessageBadContent=2, NotificationRateTooHigh=3, PowerLevelChanged=4, Unknown=5.
`NotificationChannelErrorEventArgs` derives from System.EventArgs, exposes
read-only ErrorType (ChannelErrorType), ErrorCode (Int32), ErrorAdditionalData
(Int32), Message (String), and has no public constructor. The SDK XML explicitly
identifies ErrorCode as an HRESULT. These declarations also appear in Microsoft's
[enum reference](https://learn.microsoft.com/en-us/previous-versions/windows/apps/ff707611(v=vs.105))
and [event-arguments reference](https://learn.microsoft.com/en-us/previous-versions/ff707848(v=vs.110)).

CNA's existing implementation is a local HTTP listener, not the retired Microsoft
push service. Its failed socket setup queues ChannelOpenFailed for
DispatchPendingNotificationsEXT; a failed Open publishes no URI or registry entry.
The original native error survives cleanup in ErrorAdditionalData. ErrorCode is
HRESULT_FROM_WIN32(native error) on Windows and E_FAIL on POSIX; CNA does not
mislabel an errno value as a successful HRESULT or invent a retired-service code.
Message identifies the failed operation and native error. Error handlers execute
outside the channel mutex on the dispatch caller's thread. Close does not discard
queued errors, matching the existing queued-notification policy. Successful Open
still raises ChannelUriUpdated immediately; that behavior was not changed.

This proves the API shape and local transport failure behavior, not Windows Phone
service availability, device dispatcher timing, or delivery to background apps.
