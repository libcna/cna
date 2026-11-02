// SPDX-License-Identifier: MS-PL
#pragma once
class Virtual {
public:
    /** @brief Destroy the object. */
    virtual ~Virtual();
    /**
     * @brief Read the polymorphic value.
     * @return The value.
     */
    virtual int value() const;
};
/**
 * @brief Dispatch through the runtime's compiled vtable layout.
 * @param object Object to query.
 * @return The object's value.
 */
int ReadVirtual(const Virtual& object);
