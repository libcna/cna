// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <functional>
#include <vector>

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Orders transparent draws back to front, without owning any of them.
     *
     * plan_modern.md `MOD-2101`. Until this existed the layer said so plainly in
     * `MaterialBinding.hpp`: *"Draw order still belongs to the application: CNA does not sort."*
     * That is a real gap — an alpha-blended surface only composites correctly if what is behind it
     * was drawn first, and getting that wrong produces a frame that looks almost right, which is
     * this layer's characteristic failure.
     *
     * **It is not a scene graph, and that is deliberate** (`OQ-6`). The pipeline takes a callback
     * and the application owns its scene; this list decides *when* draws happen, never *what* they
     * are. It holds a bounding box and a callable per entry, copies no geometry, and forgets
     * everything on @ref clear.
     *
     * ```cpp
     * transparent.clear();                                  // once per frame
     * for (const auto& pane : windows)
     *     transparent.submit(pane.worldBounds, [&pane] { pane.draw(); });
     * transparent.drawSorted(view);                         // back to front
     * ```
     *
     * **Sorting is an approximation and this one has a stated failure mode.** Two surfaces that
     * interpenetrate have no correct order at all, and no per-object sort can produce one; that is
     * what `WeightedBlendedTransparency` (`MOD-2106`) is for. What this does get right, and a centre-based
     * sort does not, is a long object crossing another — see @ref sortKey.
     */
    class TransparentDrawList
    {
    public:
        /** @brief Creates an empty list. */
        TransparentDrawList();

        /** @brief Destroys the list; the callables it holds are released with it. */
        ~TransparentDrawList();

        TransparentDrawList(const TransparentDrawList&)            = delete;
        TransparentDrawList& operator=(const TransparentDrawList&) = delete;

        /**
         * @brief Forgets every submission.
         *
         * Call it once per frame before submitting. It is separate from @ref drawSorted on
         * purpose: a game that draws the same transparent set into several views — a reflection, a
         * shadow, a cube face — submits once and draws several times.
         */
        void clear();

        /**
         * @brief Registers one transparent draw.
         *
         * @param bounds The draw's extent in **world** space; the sort reads it and nothing else.
         * @param draw   What to run when its turn comes. Must not be empty.
         * @throws std::invalid_argument If @p draw is empty.
         */
        void submit(const Microsoft::Xna::Framework::BoundingBox& bounds,
                    std::function<void()> draw);

        /** @brief Returns how many draws are registered. */
        [[nodiscard]] int getCount() const;

        /**
         * @brief Runs every registered draw, furthest first.
         *
         * The list is left intact; @ref clear is what ends a frame.
         *
         * @param view The camera the order is computed against.
         */
        void drawSorted(const Microsoft::Xna::Framework::Matrix& view);

        /**
         * @brief The order @ref drawSorted would use, without running anything.
         *
         * @param view The camera to order against.
         * @return Submission indices, furthest first.
         */
        [[nodiscard]] std::vector<int> getSortedOrderEXT(
            const Microsoft::Xna::Framework::Matrix& view) const;

        /**
         * @brief The distance a draw is sorted by: from the eye to the **nearest point** of its
         *        bounds.
         *
         * plan_modern.md `MOD-2103`. Not the centre, and the difference is the classic sorted-
         * transparency artefact: a long object crossing a short one has a distant centre and a near
         * end, so a centre sort puts it behind something it visibly passes in front of. The nearest
         * point is what the camera actually meets first.
         *
         * @param bounds         The draw's world-space bounds.
         * @param cameraPosition Where the eye is, in world space.
         * @return The distance; 0 when the eye is inside the bounds.
         */
        [[nodiscard]] static float sortKey(const Microsoft::Xna::Framework::BoundingBox& bounds,
                                           const Microsoft::Xna::Framework::Vector3& cameraPosition);

        /**
         * @brief Recovers the eye position from a view matrix.
         *
         * @param view The view matrix.
         * @return The camera's world-space position.
         */
        [[nodiscard]] static Microsoft::Xna::Framework::Vector3 cameraPositionOf(
            const Microsoft::Xna::Framework::Matrix& view);

    private:
        struct Entry
        {
            Microsoft::Xna::Framework::BoundingBox Bounds;
            std::function<void()> Draw;
        };

        std::vector<Entry> entries_;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
