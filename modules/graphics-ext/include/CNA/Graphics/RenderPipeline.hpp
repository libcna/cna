// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/PostProcessChain.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <memory>
#include <vector>

namespace Microsoft::Xna::Framework {
    struct Color;
}

namespace Microsoft::Xna::Framework::Graphics {
    class GraphicsDevice;
    class RenderTarget2D;
    class Texture2D;
}

namespace CNA::Graphics {

    class BlitPass;
    class BloomPass;
    class FxaaPass;
    class PostProcessPass;
    class SsaoPass;
    class TonemapPass;

    /**
     * @brief Frame-level renderer that ties HDR rendering and post-processing together.
     *
     * Usage is two calls around whatever a game already draws:
     *
     * ```
     * pipeline.begin(Color::CornflowerBlue);
     * // ... every SpriteBatch, Model and Effect draw the game already makes ...
     * pipeline.end();
     * ```
     *
     * Between them, drawing goes to an off-screen scene target in a float format where the
     * renderer supports one, so values above 1.0 survive to be tonemapped instead of being clamped
     * the moment they are written. `end()` runs the enabled passes and resolves to the back buffer.
     *
     * Nothing about it is mandatory. With default settings, and on a renderer with no float
     * targets or custom effects, the whole thing degrades to rendering directly to the back buffer
     * -- the same pixels the game would produce without a pipeline at all. That property is what
     * makes it safe to wrap an existing game in.
     */
    class RenderPipeline
    {
    public:
        /**
         * @brief Creates a pipeline for one device.
         *
         * Nothing is allocated here: the scene target's size is not known until @ref resize, and
         * its format depends on settings the caller has not made yet.
         *
         * @param device The device to render with.
         */
        explicit RenderPipeline(Microsoft::Xna::Framework::Graphics::GraphicsDevice& device);

        /** @brief Destroys the pipeline and every target and pass it owns. */
        ~RenderPipeline();

        RenderPipeline(const RenderPipeline&)            = delete;
        RenderPipeline& operator=(const RenderPipeline&) = delete;

        /**
         * @brief Returns the mutable pipeline configuration.
         *
         * Changes take effect on the next frame; nothing needs to be rebuilt. Deliberately the
         * only place these settings live -- exposing them from `GraphicsDevice` as well would give
         * an XNA type a member whose type exists only under a compile option.
         *
         * @return The settings this pipeline reads each frame.
         */
        [[nodiscard]] RenderPipelineSettings& getSettings();

        /** @brief Returns the pipeline configuration. */
        [[nodiscard]] const RenderPipelineSettings& getSettings() const;

        /**
         * @brief Sets the size of the frame the pipeline renders.
         *
         * Idempotent for a size it already holds. Call it when the back buffer changes size; the
         * pipeline reallocates its targets, and drops the old ones so a resized game does not keep
         * paying for every size it has ever been.
         *
         * @param width  Frame width in pixels; must be positive.
         * @param height Frame height in pixels; must be positive.
         * @throws std::invalid_argument If either dimension is not positive.
         */
        void resize(int width, int height);

        /**
         * @brief Begins a frame: binds the scene target and clears it.
         *
         * @param clearColor The colour to clear the scene target to.
         * @throws std::logic_error If a frame is already open, or the size has never been set.
         */
        void begin(const Microsoft::Xna::Framework::Color& clearColor);

        /**
         * @brief Ends the frame: runs the enabled passes and resolves to the back buffer.
         *
         * @throws std::logic_error If no frame is open.
         */
        void end();

        /**
         * @brief Appends a pass the caller owns, after the built-in ones.
         *
         * @param pass The pass; must outlive this pipeline. Null is ignored.
         */
        void addUserPass(PostProcessPass* pass);

        /** @brief Removes every user pass. The built-in passes are unaffected. */
        void clearUserPasses();

        /**
         * @brief Supplies the scene depth and view-space normals SSAO needs.
         *
         * The pipeline does not render these itself: producing them means drawing the game's own
         * geometry a second time with a different effect, which only the game can do. Where they
         * are absent, SSAO reports it once and renders an unoccluded frame rather than failing.
         *
         * @param depth   Linear scene depth, or null to clear.
         * @param normals View-space normals encoded as `n * 0.5 + 0.5`, or null to clear.
         */
        void setDepthNormalInputs(Microsoft::Xna::Framework::Graphics::Texture2D* depth,
                                  Microsoft::Xna::Framework::Graphics::Texture2D* normals);

        /**
         * @brief Returns the scene target the frame is being rendered into.
         *
         * Null outside `begin`/`end`, and null when the pipeline is rendering straight to the back
         * buffer because nothing is enabled.
         *
         * @return The HDR scene target, or null.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::RenderTarget2D* getSceneTarget() const;

        /**
         * @brief Returns the format the scene target was actually created with.
         *
         * Not necessarily the one implied by the settings: a renderer without float targets gets
         * `Color`, which the pipeline reports here rather than pretending otherwise.
         *
         * @return The scene target's surface format.
         */
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::SurfaceFormat getSceneTargetFormat() const;

        /**
         * @brief Returns whether the last frame rendered through an off-screen target.
         *
         * False means the pipeline short-circuited to the back buffer because nothing was enabled
         * -- the case that has to cost nothing.
         *
         * @return True when a scene target was used.
         */
        [[nodiscard]] bool isUsingSceneTarget() const;

        /**
         * @brief Returns how many passes ran during the last frame.
         *
         * @return The pass count of the most recent `end()`.
         */
        [[nodiscard]] int getLastFramePassCount() const;

        /**
         * @brief Returns an estimate of the GPU memory the pipeline's own targets occupy.
         *
         * @return Estimated bytes of colour storage.
         */
        [[nodiscard]] std::size_t getGpuMemoryEstimateBytes() const;

    private:
        /// Whether anything at all would happen between begin() and end() this frame.
        [[nodiscard]] bool wantsSceneTarget() const;
        /// The best scene-target format the settings ask for and the renderer can actually create.
        [[nodiscard]] Microsoft::Xna::Framework::Graphics::SurfaceFormat chooseSceneFormat() const;

        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device_;
        RenderPipelineSettings settings_;
        PostProcessChain chain_;

        std::unique_ptr<Microsoft::Xna::Framework::Graphics::RenderTarget2D> sceneTarget_;
        std::unique_ptr<BloomPass> bloomPass_;
        std::unique_ptr<TonemapPass> tonemapPass_;
        std::unique_ptr<FxaaPass> fxaaPass_;
        std::unique_ptr<SsaoPass> ssaoPass_;
        Microsoft::Xna::Framework::Graphics::Texture2D* sceneDepth_   = nullptr;
        Microsoft::Xna::Framework::Graphics::Texture2D* sceneNormals_ = nullptr;
        std::vector<PostProcessPass*> userPasses_;

        int  width_  = 0;
        int  height_ = 0;
        bool frameOpen_        = false;
        bool usingSceneTarget_ = false;
        int  lastFramePassCount_ = 0;
        Microsoft::Xna::Framework::Graphics::SurfaceFormat sceneFormat_ =
            Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color;
    };

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
