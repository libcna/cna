// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines how render-target contents are handled after presentation or target changes.
    enum class RenderTargetUsage
    {
        DiscardContents,
        PreserveContents,
        PlatformContents
    };
}
