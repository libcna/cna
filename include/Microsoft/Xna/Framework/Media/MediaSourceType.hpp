// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Media
{
    /** @brief Defines the type of a media source device. */
    enum class MediaSourceType
    {
        /** @brief The local device storage. */
        LocalDevice = 0,

        /** @brief A Windows Media Connect streaming device. */
        WindowsMediaConnect = 4
    };
}
