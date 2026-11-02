// SPDX-License-Identifier: MS-PL
#pragma once
struct Layout {
    int value = 11;
    /**
     * @brief Read a member using the consumer's compiled layout.
     * @return The stored value.
     */
    int read() const { return value; }
};
/**
 * @brief Access the runtime-owned object.
 * @return The object.
 */
const Layout& GetLayout();
