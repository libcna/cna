// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines how vertex or index buffer data will be flushed during a SetData operation.
    enum class SetDataOptions
    {
        None        = 0,
        Discard     = 1,
        NoOverwrite = 2,
    };
}
