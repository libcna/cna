// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::GamerServices
{
    /**
     * @brief Describes the current loading state of an AvatarRenderer.
     */
    enum class AvatarRendererState
    {
        /** @brief The avatar's assets are loading. */
        Loading,
        /** @brief The avatar is ready to render. */
        Ready,
        /** @brief The avatar is unavailable. */
        Unavailable
    };
}
