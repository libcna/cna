// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/GpuTimer.hpp"
#include "CNA/Graphics/RenderTargetPool.hpp"

#include <memory>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
}

namespace CNA::Graphics {

/** @addtogroup cnaext_engine
 *  @{
 */

    class PostProcessPass;

    /**
     * @brief Runs an ordered list of passes, ping-ponging between two intermediate targets.
     *
     * The bookkeeping this removes is the part that is easy to get subtly wrong: N passes need
     * N-1 intermediate images, the last pass must write the caller's real destination rather than
     * an intermediate, and no pass may read the target it is writing. The chain owns two pooled
     * targets and alternates them, so adding a pass costs no extra allocation.
     *
     * Passes are borrowed, not owned, unless added through @ref addOwnedPass -- a game that keeps
     * its own `BloomPass` to change settings on it should not have to give up ownership to use it
     * here.
     */
    class PostProcessChain
    {
    public:
        /**
         * @brief Creates an empty chain for one device.
         *
         * @param device The device its intermediate targets are created on.
         */
        explicit PostProcessChain(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the chain, its owned passes and its intermediate targets. */
        ~PostProcessChain();

        PostProcessChain(const PostProcessChain&)            = delete;
        PostProcessChain& operator=(const PostProcessChain&) = delete;

        /**
         * @brief Appends a pass the caller continues to own.
         *
         * @param pass The pass; must outlive this chain. Null is ignored.
         */
        void addPass(PostProcessPass* pass);

        /**
         * @brief Appends a pass and takes ownership of it.
         *
         * @param pass The pass. Null is ignored.
         */
        void addOwnedPass(std::unique_ptr<PostProcessPass> pass);

        /** @brief Removes every pass, owned or borrowed. Intermediate targets are kept. */
        void clear();

        /**
         * @brief Returns how many passes the chain will run.
         *
         * @return The pass count, owned and borrowed together.
         */
        [[nodiscard]] std::size_t getPassCount() const;

        /**
         * @brief Runs every pass in order, from @p context's source to its destination.
         *
         * With no passes this is a copy, so a chain that a game has emptied still produces the
         * image the caller expects rather than a blank target.
         *
         * @param context The source, destination, size and settings for this frame.
         * @throws std::invalid_argument If the context has no source or a non-positive size.
         */
        void apply(const PostProcessContext& context);

        /** @brief Releases the intermediate targets. Call on resize. */
        void resetTargets();

        /**
         * @brief Returns the intermediate-target pool, for diagnostics and memory accounting.
         *
         * @return The pool this chain ping-pongs through.
         */
        [[nodiscard]] const RenderTargetPool& getTargetPool() const;

        /**
         * @brief The chain's intermediate-target pool, for a caller that must release it.
         *
         * plan_modern.md `MOD-715`: after a device reset every pooled target names storage the
         * driver has destroyed, and `RenderPipeline` has to drop them. The const overload above
         * stays for the ordinary read-only uses.
         *
         * @return The pool.
         */
        [[nodiscard]] RenderTargetPool& getTargetPool();

        /**
         * @brief One pass's most recent GPU time.
         *
         * plan_modern.md `MOD-2164`.
         */
        struct PassTiming
        {
            /** @brief The pass's own name, from `PostProcessPass::getName()`. */
            std::string Name;

            /** @brief Its most recent measured GPU time in milliseconds, or 0 before one lands. */
            double Milliseconds = 0.0;

            /** @brief How many results this pass has reported since timing was switched on. */
            int SampleCount = 0;
        };

        /**
         * @brief Whether each pass is measured with a GPU timer query.
         *
         * @return True when timing is on and the renderer supplies timer queries.
         */
        [[nodiscard]] bool isGpuTimingEnabled() const;

        /**
         * @brief Measures each pass with its own GPU timer query.
         *
         * plan_modern.md `MOD-2164`. **Off by default**, and not only for cost: a timer query per
         * pass is a query object per pass and a driver-side range around every draw in the chain,
         * which is a change to the thing being measured.
         *
         * Results are read **a frame late and never waited for** — @ref apply polls each timer
         * without blocking, so a pass whose result has not landed keeps the last one it reported.
         * That is what keeps the measurement from becoming the stall it exists to look for.
         *
         * Turning it on where the renderer has no timer query is accepted and does nothing;
         * @ref isGpuTimingEnabled then returns false and @ref getPassTimings stays empty, so a
         * caller can tell "no timer here" from "this pass took no time".
         *
         * @param value True to measure.
         */
        void setGpuTimingEnabled(bool value);

        /**
         * @brief The most recent GPU time for each pass, in chain order.
         *
         * @return One entry per pass, or an empty span when timing is off or unavailable.
         */
        [[nodiscard]] const std::vector<PassTiming>& getPassTimings() const;

    private:
        void updateTimings();

        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        RenderTargetPool pool_;
        std::vector<PostProcessPass*> passes_;
        std::vector<std::unique_ptr<PostProcessPass>> ownedPasses_;
        std::unique_ptr<PostProcessPass> copyPass_;

        std::vector<std::unique_ptr<GpuTimer>> timers_;
        std::vector<PassTiming> timings_;
        bool gpuTimingRequested_ = false;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
