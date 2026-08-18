// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_CNAEXT

#include "CNA/Graphics/DirectionalLightEXT.hpp"
#include "CNA/Graphics/PostProcessChain.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"

#include "System/EventArgs.hpp"
#include "System/EventHandler.hpp"
#include "Microsoft/Xna/Framework/BoundingBox.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <functional>
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

/** @addtogroup cnaext_engine
 *  @{
 */

    class BlitPass;
    class BloomPass;
    class FxaaPass;
    class PostProcessPass;
    class ShadowMap;
    class Skybox;
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
         * @brief Supplies the shadow pass `begin` should run before the frame.
         *
         * plan_modern.md MOD-858/MOD-859. The pipeline cannot draw the scene by itself, for the
         * same reason it cannot produce the depth and normals SSAO wants: only the game knows what
         * its casters are. Registering the draw here is a convenience over calling
         * `ShadowMap::begin`/`end` directly, not a replacement for it -- `ShadowMap` stays usable
         * on its own, which is the decision recorded in `MOD-860`.
         *
         * The contract this puts on the app is worth stating plainly, because it is not free: the
         * scene must be drawable twice per frame, once from the light and once from the camera.
         * @p drawCasters is called with the map already bound and cleared, and must draw only
         * geometry, never change render target or clear.
         *
         * @param shadowMap   The map to fill, or null to stop running a shadow pass.
         * @param light       The light to generate from.
         * @param sceneBounds The bounds to fit the light's volume to.
         * @param drawCasters The app's own caster draw calls. An empty function disables the pass
         *                    as surely as a null map does, and @ref didShadowPassRun reports it.
         */
        void setShadowScene(ShadowMap* shadowMap, const DirectionalLightEXT& light,
                            const Microsoft::Xna::Framework::BoundingBox& sceneBounds,
                            std::function<void()> drawCasters);

        /**
         * @brief Supplies a skybox for `begin` to draw behind the scene.
         *
         * plan_modern.md MOD-1104. Drawn immediately after the scene target is bound and cleared,
         * which is *before* the game's own geometry rather than after it. The ordering is the one
         * decision here, and it follows from the mechanism: the sky is a fullscreen `SpriteBatch`
         * draw and carries no depth configuration, so it cannot be made to lose a depth test
         * against geometry already present. Drawing it first gives the same picture; what it gives
         * up is skipping sky pixels the scene will cover.
         *
         * Null detaches it. A skybox on a renderer that cannot compile its shader draws nothing and
         * says so once, so this is safe to set unconditionally.
         *
         * @param skybox The skybox, or null. Must outlive this pipeline.
         */
        void setSkybox(Skybox* skybox);

        /** @brief Returns the skybox drawn behind the scene, or null. */
        [[nodiscard]] Skybox* getSkybox() const;

        /**
         * @brief Sets the camera the skybox is drawn from.
         *
         * The pipeline has no camera of its own -- a game's view and projection live wherever the
         * game keeps them -- so the sky needs to be told, once per frame, before `begin`.
         *
         * @param view       The camera's view matrix; its translation is ignored by the sky.
         * @param projection The camera's projection matrix.
         */
        void setSkyboxCamera(const Microsoft::Xna::Framework::Matrix& view,
                             const Microsoft::Xna::Framework::Matrix& projection);

        /** @brief Returns whether the last `begin` actually drew the sky. */
        [[nodiscard]] bool didSkyboxDraw() const;

        /**
         * @brief Returns whether the last `begin` actually ran a shadow pass.
         *
         * False when shadows are switched off in the settings, when no map has been supplied, or
         * when a map was supplied without a caster callback -- three different reasons the app can
         * tell apart from what it set, and one answer it can assert on.
         *
         * @return True if the most recent frame filled the shadow map.
         */
        [[nodiscard]] bool didShadowPassRun() const;

        /**
         * @brief Returns the shadow map the pipeline fills, or null.
         *
         * The map, not a copy of its texture: an app needs it to configure the effects that will
         * receive the shadow, which the pipeline deliberately does not do on its behalf.
         *
         * @return The registered shadow map, or null.
         */
        [[nodiscard]] ShadowMap* getShadowMap() const;

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

        /**
         * @brief What the last frame actually did.
         *
         * plan_modern.md `MOD-717`. The four numbers a reader needs to answer "why is this frame
         * slower than I expected", gathered where the pipeline already knows them. A POD rather
         * than a set of accessors, so a caller can snapshot a frame and compare it with another.
         *
         * Deliberately **not** a general profiler: there are no timings here. Timing a pass
         * meaningfully needs a GPU query the renderer contracts do not have, and a CPU-side stopwatch
         * around a call that only queues work would report a number that looks precise and means
         * nothing. `docs/cnaext-perf.md` has the measurements, taken with a readback that forces
         * the work to happen.
         */
        struct FrameStatistics
        {
            /** @brief How many post-process passes ran. */
            int passesRun = 0;
            /** @brief How many times a render target was bound, including the back buffer. */
            int targetSwitches = 0;
            /** @brief Whether the frame went through an off-screen scene target. */
            bool usedSceneTarget = false;
            /** @brief Whether the sky was drawn inside `begin()`. */
            bool drewSkybox = false;
            /** @brief Bytes the pipeline's own targets are estimated to occupy. */
            std::size_t gpuMemoryEstimateBytes = 0;
        };

        /**
         * @brief Returns the statistics for the most recently completed frame.
         *
         * @return The statistics; all zero before the first `end()`.
         */
        [[nodiscard]] FrameStatistics getStatistics() const;

        /**
         * @brief Drops every target the pipeline owns, so the next frame allocates fresh ones.
         *
         * plan_modern.md `MOD-715`. Called automatically when the device raises `DeviceReset`, and
         * exposed because a game that recreates its device by some other route needs the same
         * effect. After a context loss every GPU object the pipeline held names storage the driver
         * has already destroyed, and rendering into one is undefined rather than merely wrong.
         *
         * Cheap to call when nothing was allocated, and safe at any time **except** inside a frame,
         * which it refuses — releasing the scene target while it is bound is exactly the situation
         * this exists to avoid.
         *
         * @throws std::logic_error If a frame is open.
         */
        void releaseDeviceResourcesEXT();

    private:
        /// Draws the skybox, if one is set. Called by begin(), never by the caller.
        void drawSkybox();
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
        Skybox* skybox_ = nullptr;
        Microsoft::Xna::Framework::Matrix skyboxView_{};
        Microsoft::Xna::Framework::Matrix skyboxProjection_{};
        bool skyboxDrawn_ = false;
        ShadowMap* shadowMap_ = nullptr;
        DirectionalLightEXT shadowLight_{};
        Microsoft::Xna::Framework::BoundingBox shadowBounds_{
            Microsoft::Xna::Framework::Vector3(-1.0f, -1.0f, -1.0f),
            Microsoft::Xna::Framework::Vector3(1.0f, 1.0f, 1.0f)};
        std::function<void()> drawCasters_;
        bool shadowPassRan_ = false;
        Microsoft::Xna::Framework::Graphics::Texture2D* sceneDepth_   = nullptr;
        Microsoft::Xna::Framework::Graphics::Texture2D* sceneNormals_ = nullptr;
        std::vector<PostProcessPass*> userPasses_;

        int  width_  = 0;
        int  height_ = 0;
        bool frameOpen_        = false;
        bool usingSceneTarget_ = false;
        int  lastFramePassCount_ = 0;
        int  lastFrameTargetSwitches_ = 0;
        /// Token for the DeviceReset subscription, so the destructor can unsubscribe: the handler
        /// captures `this`, and a device outliving a pipeline would otherwise call into freed memory.
        System::EventHandler<System::EventArgs>::Token deviceResetToken_ = 0;
        Microsoft::Xna::Framework::Graphics::SurfaceFormat sceneFormat_ =
            Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color;
    };

/** @} */ // end of cnaext_engine

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
