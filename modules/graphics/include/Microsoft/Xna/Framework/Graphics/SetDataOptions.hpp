// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines how vertex or index buffer data will be flushed during a SetData operation. */
    enum class SetDataOptions
    {
        /** @brief SetData may overwrite portions of existing data. */
        None        = 0,
        /** @brief Discard the entire buffer; a new memory region is returned and rendering from the old region does not stall. */
        Discard     = 1,
        /** @brief SetData will not overwrite existing data, allowing the driver to return immediately and continue rendering. */
        NoOverwrite = 2,
    };
}
