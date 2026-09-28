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
    /** @brief Reports whether system UI currently owns application focus.
     * @return True while a modal system screen is visible; false for a drawing-only overlay.
     */
    [[nodiscard]] virtual bool isModalVisible() const { return false; }
};
}
