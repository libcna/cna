// SPDX-License-Identifier: MS-PL
#pragma once

#include <atomic>

namespace CNA::Internal::FrameworkNotifications
{
    /**
     * @brief The storage subsystem's per-frame native-notification poll, or null.
     *
     * XNA's StorageDevice type initializer registers its DeviceChanged callback with the
     * framework (`StorageDevice..cctor` IL_0000-000c), and `FrameworkDispatcher.Update`
     * (IL_00ec-00f1) delivers a storage-device notification through it. CNA's storage module,
     * which depends only on core, installs its volume poll here when it is linked, and
     * FrameworkDispatcher::Update() runs it -- so the dispatcher never links storage and a game
     * that never touches storage pays nothing for it. Constant-initialized; any thread.
     */
    inline std::atomic<void (*)()> StorageDevices{nullptr};
}
