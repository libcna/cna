// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Media
{
    /// Defines available media source types.
    enum class MediaSourceType
    {
        /// The local device storage.
        LocalDevice = 0,

        /// Windows Media Connect device.
        WindowsMediaConnect = 4
    };
}
