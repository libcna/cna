// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/RenderPipeline.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/FxaaPass.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/Skybox.hpp"
#include "CNA/Graphics/SsaoPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/TonemapPass.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"

#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Color;
    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Graphics::DepthFormat;
    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    RenderPipeline::RenderPipeline(GraphicsDevice& device)
        : device_(device), chain_(device), bloomPass_(std::make_unique<BloomPass>(device)),
          tonemapPass_(std::make_unique<TonemapPass>(device)),
          fxaaPass_(std::make_unique<FxaaPass>(device)),
          ssaoPass_(std::make_unique<SsaoPass>(device))
    {
    }

    RenderPipeline::~RenderPipeline() = default;

    RenderPipelineSettings& RenderPipeline::getSettings()             { return settings_; }
    const RenderPipelineSettings& RenderPipeline::getSettings() const { return settings_; }

    bool RenderPipeline::wantsSceneTarget() const
    {
        // The short circuit that makes an inert pipeline free: with nothing enabled there is
        // nothing for an off-screen target to enable, so the frame goes straight to the back
        // buffer and costs exactly what not using a pipeline costs.
        if (settings_.isHDREnabled())
            return true;
        if (settings_.getTonemappingMode() != TonemappingMode::None)
            return true;
        if (settings_.isBloomEnabled() || settings_.isSSAOEnabled() || settings_.isFXAAEnabled())
            return true;
        return !userPasses_.empty();
    }

    SurfaceFormat RenderPipeline::chooseSceneFormat() const
    {
        if (!settings_.isHDREnabled())
            return SurfaceFormat::Color;

        // HdrBlendable (RGBA16F) is the scene format worth having: enough range and precision for
        // scene-referred colour at half the bandwidth of RGBA32F, and blendable on far more
        // hardware. Where the renderer has neither, the pipeline runs in Color rather than
        // refusing -- the frame is then clamped, which is exactly what it would have been without
        // the pipeline, and getSceneTargetFormat() reports the truth.
        if (device_.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::HdrBlendable))
            return SurfaceFormat::HdrBlendable;
        if (device_.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4))
            return SurfaceFormat::Vector4;
        return SurfaceFormat::Color;
    }

    void RenderPipeline::resize(const int width, const int height)
    {
        if (width <= 0 || height <= 0)
            throw std::invalid_argument("CNA::Graphics::RenderPipeline::resize: size must be positive");
        if (width == width_ && height == height_)
            return;

        width_  = width;
        height_ = height;
        sceneTarget_.reset();
        chain_.resetTargets();
        bloomPass_->resetTargets();
        ssaoPass_->resetTargets();
    }

    void RenderPipeline::begin(const Color& clearColor)
    {
        if (frameOpen_)
            throw std::logic_error("CNA::Graphics::RenderPipeline::begin: a frame is already open");
        if (width_ <= 0 || height_ <= 0)
            throw std::logic_error(
                "CNA::Graphics::RenderPipeline::begin: resize() must be called before the first frame");

        frameOpen_        = true;
        usingSceneTarget_ = wantsSceneTarget();

        // MOD-858: before anything else, and before the scene target is bound. The shadow pass
        // binds a target of its own and restores the back buffer when it ends, so running it
        // after the scene target was bound would silently unbind it and send the whole frame to
        // the screen instead.
        shadowPassRan_ = false;
        if (settings_.isShadowsEnabled() && shadowMap_ != nullptr && drawCasters_)
        {
            shadowMap_->begin(shadowLight_, shadowBounds_);
            drawCasters_();
            shadowMap_->end();
            shadowPassRan_ = true;
        }

        if (!usingSceneTarget_)
        {
            device_.SetRenderTarget(nullptr);
            device_.Clear(clearColor);
            DrawSkybox();
            return;
        }

        const SurfaceFormat wanted = chooseSceneFormat();
        if (!sceneTarget_ || sceneFormat_ != wanted)
        {
            sceneFormat_ = wanted;
            // Depth is not optional: the scene drawn between begin() and end() is a real scene,
            // and a 3D one without depth renders in submission order.
            sceneTarget_ = std::make_unique<RenderTarget2D>(device_, width_, height_, false,
                                                            sceneFormat_,
                                                            DepthFormat::Depth24Stencil8);
        }

        device_.SetRenderTarget(sceneTarget_.get());
        device_.Clear(clearColor);
        DrawSkybox();
    }

    void RenderPipeline::DrawSkybox()
    {
        // MOD-1104: after the target is bound and cleared, before the game draws anything. See
        // setSkybox() for why that is the order rather than the usual draw-last-at-the-far-plane.
        skyboxDrawn_ = false;
        if (skybox_ == nullptr)
            return;
        skybox_->draw(skyboxView_, skyboxProjection_, width_, height_);
        skyboxDrawn_ = skybox_->isSupported() && skybox_->getEnvironment() != nullptr;
    }

    void RenderPipeline::setSkybox(Skybox* skybox)
    {
        skybox_ = skybox;
    }

    Skybox* RenderPipeline::getSkybox() const
    {
        return skybox_;
    }

    void RenderPipeline::setSkyboxCamera(const Matrix& view, const Matrix& projection)
    {
        skyboxView_       = view;
        skyboxProjection_ = projection;
    }

    bool RenderPipeline::didSkyboxDraw() const
    {
        return skyboxDrawn_;
    }

    void RenderPipeline::end()
    {
        if (!frameOpen_)
            throw std::logic_error("CNA::Graphics::RenderPipeline::end: no frame is open");
        frameOpen_ = false;

        if (!usingSceneTarget_)
        {
            lastFramePassCount_ = 0;
            return;
        }

        // The scene target stops being a target here and becomes a texture: every pass below samples
        // it. Unbinding first is not tidiness -- sampling a bound render target is undefined in GL --
        // and since MOD-203 it is also what leaves the *frame* bound at the end. `ScopedRenderTarget`
        // restores whatever a pass found bound, so a chain entered with the scene target still bound
        // would faithfully put it back after the last pass wrote the back buffer, and the next
        // Present would refuse: "Cannot present while render targets are bound".
        device_.SetRenderTarget(nullptr);

        // Fixed order, and the order is the point. Tonemapping maps scene-referred colour into
        // display range, so everything that reasons about scene values runs before it and
        // everything that reasons about displayed pixels runs after. User passes come last, where
        // they see the frame as it will be shown.
        chain_.clear();
        // SSAO first: it multiplies an occlusion term into the scene, and everything after it --
        // bloom's threshold above all -- should see the shaded result rather than the unshaded one.
        if (settings_.isSSAOEnabled())
            chain_.addPass(ssaoPass_.get());
        // Bloom reads scene-referred values -- its threshold separates genuinely bright pixels
        // from merely white ones -- so it runs before the tonemapper compresses that range away.
        if (settings_.isBloomEnabled())
            chain_.addPass(bloomPass_.get());
        if (settings_.getTonemappingMode() != TonemappingMode::None || settings_.isHDREnabled())
            chain_.addPass(tonemapPass_.get());
        // FXAA detects edges by luminance contrast, so it runs on displayed pixels: on
        // scene-referred values a highlight ten times brighter than white reads as an enormous
        // edge and gets blurred into its surroundings.
        if (settings_.isFXAAEnabled())
            chain_.addPass(fxaaPass_.get());
        for (PostProcessPass* pass : userPasses_)
            chain_.addPass(pass);

        PostProcessContext context;
        context.source      = sceneTarget_.get();
        context.destination = nullptr;   // the back buffer
        context.width       = width_;
        context.height      = height_;
        context.settings      = &settings_;
        context.sourceDepth   = sceneDepth_;
        context.sourceNormals = sceneNormals_;

        chain_.apply(context);
        lastFramePassCount_ = static_cast<int>(chain_.getPassCount());
    }

    void RenderPipeline::addUserPass(PostProcessPass* pass)
    {
        if (pass != nullptr)
            userPasses_.push_back(pass);
    }

    void RenderPipeline::setDepthNormalInputs(
        Microsoft::Xna::Framework::Graphics::Texture2D* depth,
        Microsoft::Xna::Framework::Graphics::Texture2D* normals)
    {
        sceneDepth_   = depth;
        sceneNormals_ = normals;
    }

    void RenderPipeline::setShadowScene(ShadowMap* shadowMap, const DirectionalLightEXT& light,
                                        const Microsoft::Xna::Framework::BoundingBox& sceneBounds,
                                        std::function<void()> drawCasters)
    {
        shadowMap_    = shadowMap;
        shadowLight_  = light;
        shadowBounds_ = sceneBounds;
        drawCasters_  = std::move(drawCasters);
    }

    bool RenderPipeline::didShadowPassRun() const
    {
        return shadowPassRan_;
    }

    ShadowMap* RenderPipeline::getShadowMap() const
    {
        return shadowMap_;
    }

    void RenderPipeline::clearUserPasses()
    {
        userPasses_.clear();
    }

    RenderTarget2D* RenderPipeline::getSceneTarget() const
    {
        return frameOpen_ && usingSceneTarget_ ? sceneTarget_.get() : nullptr;
    }

    SurfaceFormat RenderPipeline::getSceneTargetFormat() const
    {
        return sceneFormat_;
    }

    bool RenderPipeline::isUsingSceneTarget() const
    {
        return usingSceneTarget_;
    }

    int RenderPipeline::getLastFramePassCount() const
    {
        return lastFramePassCount_;
    }

    std::size_t RenderPipeline::getGpuMemoryEstimateBytes() const
    {
        std::size_t total = chain_.getTargetPool().getEstimatedBytes();
        if (sceneTarget_ != nullptr)
        {
            total += static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_)
                   * static_cast<std::size_t>(
                         Microsoft::Xna::Framework::Graphics::Texture::GetFormatSizeEXT(sceneFormat_));
        }
        return total;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
