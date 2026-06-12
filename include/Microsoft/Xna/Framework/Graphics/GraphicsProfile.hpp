// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines the requested graphics feature level.
    enum class GraphicsProfile
    {
        /// Limited feature set intended for broad hardware compatibility.
        Reach,

        /// Expanded feature set intended for more capable graphics hardware.
        HiDef
    };
}
