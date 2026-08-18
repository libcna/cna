// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include <cstddef>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics { class ModelMeshPart; }

namespace CNA::Graphics {

    /** @brief How a `LodGroupEXT` decides which level a distance selects. */
    enum class LodSelectionMode
    {
        /**
         * @brief By camera distance alone: the first level whose maximum distance covers it.
         *
         * Simple and predictable, and wrong in one specific way -- it ignores how large the object
         * actually appears, so a boulder and a pebble at the same distance get the same level.
         */
        Distance,
        /**
         * @brief By projected screen size, which is what "detail you can see" actually depends on.
         *
         * The level's maximum distance is reinterpreted as a *screen-space error threshold*: see
         * @ref LodGroupEXT::setScreenSpaceParameters for the formula and what its inputs mean.
         */
        ScreenSpaceError,
    };

    /**
     * @brief One mesh at several levels of detail, and the rule for choosing between them.
     *
     * plan_modern.md `MOD-1406`–`MOD-1408`. A group holds levels in increasing distance order and
     * answers one question: at this distance, which one do I draw? It owns no GPU resources and
     * issues no draws -- the parts belong to whatever loaded them, and drawing stays the caller's.
     *
     * Nothing here is XNA: XNA had no LOD concept at all, so this is an addition rather than a
     * reinterpretation.
     */
    class LodGroupEXT
    {
    public:
        /** @brief One level: the part to draw, and the distance past which it stops being used. */
        struct Level
        {
            /** @brief The mesh part this level draws; may be null to mean "draw nothing". */
            Microsoft::Xna::Framework::Graphics::ModelMeshPart* Part = nullptr;
            /**
             * @brief The distance (or screen-space error) at which this level gives way.
             *
             * The last level's value is what makes an object disappear entirely rather than stay
             * at its coarsest form: a group whose last level ends at 200 selects nothing past 200.
             */
            float MaxDistance = 0.0f;
        };

        /** @brief Constructs an empty group; @ref select returns nothing until a level is added. */
        LodGroupEXT();

        /**
         * @brief Adds one level, keeping the list ordered finest first.
         *
         * Sorted on insert rather than on selection, because selection runs per object per frame
         * and insertion runs once. Adding a level with a distance that already exists keeps both,
         * in insertion order; the first one wins, which is the same rule a stable sort gives.
         *
         * "Finest first" is ascending distance in @ref LodSelectionMode::Distance and *descending*
         * threshold in @ref LodSelectionMode::ScreenSpaceError, because a pixel-size threshold
         * shrinks as detail falls where a distance grows. Changing the mode re-sorts.
         *
         * @param maxDistance The distance past which this level gives way; must be positive.
         * @param part        The part to draw, or null for a level that draws nothing.
         * @throws std::invalid_argument If @p maxDistance is not positive.
         */
        void addLevel(float maxDistance,
                      Microsoft::Xna::Framework::Graphics::ModelMeshPart* part);

        /** @brief Removes every level. */
        void clear();

        /** @brief Returns the levels, ordered finest first (see @ref addLevel). */
        [[nodiscard]] const std::vector<Level>& getLevels() const;

        /**
         * @brief Returns the index of the level a distance selects, or -1 for none.
         *
         * Binary search over the sorted distances, so a group with many levels costs
         * `O(log n)` per object per frame rather than `O(n)`.
         *
         * With hysteresis enabled this is **stateful**: it remembers what it returned last and
         * only moves on once the distance has passed the boundary by the configured margin. That
         * is what stops an object sitting exactly on a boundary from switching every frame, which
         * reads as flicker rather than as detail.
         *
         * @param distance Camera distance (or screen-space error, in that mode); negative is
         *                 treated as 0.
         * @return The selected level index, or -1 when nothing covers the distance.
         */
        [[nodiscard]] int selectIndex(float distance);

        /**
         * @brief Returns the part a distance selects, or null.
         *
         * @param distance Camera distance, as @ref selectIndex.
         * @return The selected part, or null when no level covers the distance or the selected
         *         level deliberately draws nothing.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::ModelMeshPart* select(float distance);

        /**
         * @brief Sets how far past a boundary the distance must go before the level changes.
         *
         * plan_modern.md `MOD-1407`. Zero -- the default -- switches exactly at the boundary,
         * which flickers when an object hovers there. A margin of 2 world units means the group
         * moves to the coarser level at `boundary + 2` and back to the finer one at `boundary - 2`.
         *
         * @param margin The margin in the same units as the distances; negative is treated as 0.
         */
        void setHysteresis(float margin);

        /** @brief Returns the hysteresis margin. */
        [[nodiscard]] float getHysteresis() const;

        /** @brief Forgets the remembered level, so the next @ref selectIndex starts fresh. */
        void resetHysteresis();

        /** @brief Returns the selection mode. */
        [[nodiscard]] LodSelectionMode getSelectionMode() const;

        /**
         * @brief Sets whether levels are chosen by distance or by projected screen size.
         *
         * @param mode The mode; switching resets the hysteresis state, because a remembered level
         *             chosen under one rule says nothing about the other.
         */
        void setSelectionMode(LodSelectionMode mode);

        /**
         * @brief Sets the inputs the screen-space-error mode needs.
         *
         * plan_modern.md `MOD-1408`. The error a level is compared against is
         *
         * ```
         * error = (radius * viewportHeight) / (distance * 2 * tan(fovY / 2))
         * ```
         *
         * -- the object's projected radius in pixels. It is a *size*, so it falls as distance
         * rises, which is the opposite direction to the distance mode's own comparison: a level's
         * `MaxDistance` is read as a minimum pixel size, and the first level whose threshold the
         * projected size still meets is selected. Both readings keep the same list order, finest
         * first.
         *
         * @param radius         The object's bounding radius in world units; must be positive.
         * @param verticalFov    The camera's vertical field of view in radians; must be positive
         *                       and below pi.
         * @param viewportHeight The viewport height in pixels; must be positive.
         * @throws std::invalid_argument If any argument is outside those ranges.
         */
        void setScreenSpaceParameters(float radius, float verticalFov, float viewportHeight);

        /**
         * @brief Returns the projected radius in pixels a distance produces.
         *
         * Exposed because a game usually wants to log or tune it, and because a formula that can
         * only be observed through its effect on a selection is a formula nobody can check.
         *
         * @param distance The camera distance; values at or below zero return a very large size.
         * @return The projected radius, in pixels.
         */
        [[nodiscard]] float projectedRadiusPixels(float distance) const;

    private:
        std::vector<Level> levels_;
        float hysteresis_ = 0.0f;
        int   lastIndex_ = -1;
        LodSelectionMode mode_ = LodSelectionMode::Distance;
        float radius_ = 1.0f;
        float verticalFov_ = 0.7853981634f;   // pi/4, XNA's usual default
        float viewportHeight_ = 720.0f;

        void sortLevels();
        [[nodiscard]] int selectByDistance(float distance) const;
        [[nodiscard]] int selectByScreenSpaceError(float distance) const;
        [[nodiscard]] int applyHysteresis(int candidate, float distance) const;
    };

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
