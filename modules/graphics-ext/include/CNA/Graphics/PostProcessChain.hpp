// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderTargetPool.hpp"

#include <memory>
#include <vector>

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
}

namespace CNA::Graphics {

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

    private:
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        RenderTargetPool pool_;
        std::vector<PostProcessPass*> passes_;
        std::vector<std::unique_ptr<PostProcessPass>> ownedPasses_;
        std::unique_ptr<PostProcessPass> copyPass_;
    };

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
