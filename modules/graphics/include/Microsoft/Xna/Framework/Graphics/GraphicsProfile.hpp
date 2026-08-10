// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines a set of graphic capabilities. */
    enum class GraphicsProfile
    {
        /** @brief Use a limited set of graphic features to support the widest variety of devices. */
        Reach,

        /** @brief Use the largest available set of graphic features targeting more capable hardware. */
        HiDef
    };
}
