// SPDX-License-Identifier: MS-PL
#pragma once
namespace CNA::Internal::Runtime {
/** @brief Optional system overlay rendered after application Draw and before presentation. */
class IGameOverlay {
public:
    /** @brief Releases the overlay. */
    virtual ~IGameOverlay() = default;
    /** @brief Draws currently visible system UI. */
    virtual void draw() = 0;
};
}
