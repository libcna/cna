// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <vector>

namespace CNA::Graphics {

    class ClusteredLightSetEXT;

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief Decides which of a scene's many lights actually get a shadow map.
     *
     * Clustered shading removed the limit on how many lights can *light* a scene. It removed
     * nothing at all about how many can *shadow* one: a shadow map is a render target and a
     * geometry pass -- six of them for a point light -- so a scene with two hundred lights and two
     * hundred shadow maps draws its geometry twelve hundred times before it shades a pixel. **The
     * honest statement is therefore that shadows stay a small budget while lighting does not**, and
     * this class is where that budget is spent rather than a place where it disappears.
     *
     * The rule is one sentence: **among the lights that asked for a shadow and are visible, the
     * budget goes to the ones contributing most light at the camera.** Contribution is measured
     * with the same windowed falloff the shading uses, so the ranking and the picture agree about
     * what "brightest" means; a light whose volume does not touch the view frustum scores nothing,
     * because a shadow nobody can see is the cheapest thing to drop.
     *
     * **Selection is sticky.** A light already holding a shadow keeps it unless a rival beats it by
     * @ref getHysteresis, because two lights whose scores cross every few frames would otherwise
     * trade the map back and forth and the shadow would blink -- which reads as a bug in the shadow
     * system rather than as a budget doing its job.
     */
    class ClusteredShadowPolicyEXT
    {
    public:
        /** @brief The default number of shadow maps the policy will hand out. */
        static constexpr int kDefaultBudget = 4;

        /** @brief The default margin a challenger must beat an incumbent by. */
        static constexpr float kDefaultHysteresis = 1.25f;

        /**
         * @brief Creates a policy with a budget.
         *
         * @param budget How many lights may hold a shadow map; must not be negative.
         * @throws std::invalid_argument When the budget is negative.
         */
        explicit ClusteredShadowPolicyEXT(int budget = kDefaultBudget);

        /** @brief Returns how many lights may hold a shadow map. */
        [[nodiscard]] int getBudget() const;
        /**
         * @brief Sets how many lights may hold a shadow map.
         *
         * @param value The budget; negatives are ignored. Zero is meaningful and means no shadows.
         */
        void setBudget(int value);

        /** @brief Returns the margin a challenger must beat an incumbent by to take its map. */
        [[nodiscard]] float getHysteresis() const;
        /**
         * @brief Sets the margin a challenger must beat an incumbent by.
         *
         * @param value A multiplier; 1 disables the stickiness. Values below 1 are ignored, since
         *              they would make the selection change for no reason at all.
         */
        void setHysteresis(float value);

        /**
         * @brief Chooses this frame's shadow casters.
         *
         * @param lights         The scene's lights.
         * @param view           The camera's view matrix.
         * @param projection     The camera's projection matrix.
         * @param cameraPosition The camera's world position.
         */
        void select(const ClusteredLightSetEXT& lights,
                    const Microsoft::Xna::Framework::Matrix& view,
                    const Microsoft::Xna::Framework::Matrix& projection,
                    const Microsoft::Xna::Framework::Vector3& cameraPosition);

        /** @brief Returns the chosen light indices, brightest first. */
        [[nodiscard]] const std::vector<int>& getSelected() const;

        /**
         * @brief Returns whether a light holds a shadow map after the last @ref select.
         *
         * @param lightIndex The light's index in the set.
         * @return True when it was chosen.
         */
        [[nodiscard]] bool isSelected(int lightIndex) const;

        /**
         * @brief Returns a light's score from the last @ref select.
         *
         * Offered so a game can see *why* a light lost, rather than only that it did.
         *
         * @param lightIndex The light's index in the set.
         * @return The score; zero for a light that did not ask, or is not visible.
         * @throws std::out_of_range When the index is outside the last selection.
         */
        [[nodiscard]] float getScore(int lightIndex) const;

        /** @brief Returns how many lights asked for a shadow in the last @ref select. */
        [[nodiscard]] int getRequestCount() const;

        /**
         * @brief Returns how many asking lights went without, which is the number worth logging.
         *
         * @return The count of lights that requested a shadow and did not receive one.
         */
        [[nodiscard]] int getRefusedCount() const;

        /** @brief Forgets the previous selection, so the next one starts with no incumbents. */
        void reset();

    private:
        int   budget_;
        float hysteresis_ = kDefaultHysteresis;

        std::vector<int>   selected_;
        std::vector<float> scores_;
        int requestCount_ = 0;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
