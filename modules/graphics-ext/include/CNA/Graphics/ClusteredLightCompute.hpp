// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "Microsoft/Xna/Framework/BoundingSphere.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
}

namespace CNA::Graphics {

    class ClusteredLightAssignment;
    class ClusteredLightGrid;
    class ComputeShader;

/** @addtogroup cnaext_engine
 *  @{
 */

    /**
     * @brief The same light-to-cluster sort as `ClusteredLightAssignment`, run on the GPU.
     *
     * One invocation owns one cluster and walks every light in order. That is deliberately the
     * naive shape -- no atomics, no shared memory, no two-pass compaction -- and it is what makes
     * the result **bit-for-bit the same list the CPU builds**: a cluster's indices come out sorted
     * because the loop visits the lights in order, and nothing races because no two invocations
     * write to the same place.
     *
     * **It falls back rather than refuses.** Where the device has no compute, @ref assign runs the
     * CPU path instead and says so through @ref usedCompute, because a frame with no lights in it
     * is a worse answer than a frame whose lights were sorted more slowly.
     *
     * The per-cluster capacity is fixed (@ref getStride), since a GPU cannot grow an array. A
     * cluster holding more lights than that keeps the first @ref getStride of them and sets
     * @ref hasOverflowed, which is a number a game can act on rather than a silent truncation.
     */
    class ClusteredLightCompute
    {
    public:
        /** @brief The default number of lights one cluster may hold. */
        static constexpr int kDefaultStride = 64;

        /**
         * @brief Creates the compute path, or the fallback if the device has no compute.
         *
         * The constructor never throws for a missing capability: an object that cannot use the GPU
         * is still a usable object here, because it has a correct CPU path to fall back to.
         *
         * @param device The device to compile on.
         * @param stride How many lights one cluster may hold; must be positive.
         * @throws std::invalid_argument When @p stride is not positive.
         */
        explicit ClusteredLightCompute(
            Microsoft::Xna::Framework::Graphics::GraphicsDevice& device,
            int stride = kDefaultStride);

        /** @brief Destroys the compute program and its buffers. */
        ~ClusteredLightCompute();

        ClusteredLightCompute(const ClusteredLightCompute&)            = delete;
        ClusteredLightCompute& operator=(const ClusteredLightCompute&) = delete;

        /** @brief Returns whether the GPU path is available and compiled. */
        [[nodiscard]] bool isSupported() const;

        /** @brief Returns why the GPU path is unavailable, or an empty string when it is. */
        [[nodiscard]] const std::string& getUnsupportedReason() const;

        /** @brief Returns how many lights one cluster may hold. */
        [[nodiscard]] int getStride() const;

        /**
         * @brief Sorts lights into clusters, on the GPU where it can.
         *
         * @param grid   The grid to sort into; it must already have a projection.
         * @param view   The camera's view matrix.
         * @param lights The lights' bounding spheres, in world space.
         * @param result Receives the assignment, in the same layout the CPU path produces.
         * @throws std::invalid_argument When there are more lights than the assignment accepts.
         * @throws std::runtime_error    When the grid has no projection.
         */
        void assign(const ClusteredLightGrid& grid,
                    const Microsoft::Xna::Framework::Matrix& view,
                    const std::vector<Microsoft::Xna::Framework::BoundingSphere>& lights,
                    ClusteredLightAssignment& result);

        /** @brief Returns whether the last @ref assign ran on the GPU. */
        [[nodiscard]] bool usedCompute() const;

        /** @brief Returns whether the last @ref assign filled a cluster past @ref getStride. */
        [[nodiscard]] bool hasOverflowed() const;

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        std::unique_ptr<ComputeShader> program_;
        std::string unsupportedReason_;
        int  stride_;
        bool usedCompute_ = false;
        bool overflowed_  = false;
    };

/** @} */

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
