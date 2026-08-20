// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/RenderPipeline.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/FxaaPass.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/Skybox.hpp"
#include "CNA/Graphics/SsaoPass.hpp"
#include "CNA/Graphics/ColorGradePass.hpp"
#include "CNA/Graphics/ChromaticAberrationPass.hpp"
#include "CNA/Graphics/FilmGrainPass.hpp"
#include "CNA/Graphics/LensFlarePass.hpp"
#include "CNA/Graphics/HeightFogPass.hpp"
#include "CNA/Graphics/LightShaftPass.hpp"
#include "CNA/Graphics/VolumetricFogPass.hpp"
#include "CNA/Graphics/MotionBlurPass.hpp"
#include "CNA/Graphics/DepthOfFieldPass.hpp"
#include "CNA/Graphics/SsrPass.hpp"
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
          ssaoPass_(std::make_unique<SsaoPass>(device)),
          ssrPass_(std::make_unique<SsrPass>(device)),
          dofPass_(std::make_unique<DepthOfFieldPass>(device)),
          colorGradePass_(std::make_unique<ColorGradePass>(device)),
          chromaticAberrationPass_(std::make_unique<ChromaticAberrationPass>(device)),
          filmGrainPass_(std::make_unique<FilmGrainPass>(device)),
          lensFlarePass_(std::make_unique<LensFlarePass>(device)),
          motionBlurPass_(std::make_unique<MotionBlurPass>(device)),
          heightFogPass_(std::make_unique<HeightFogPass>(device)),
          lightShaftPass_(std::make_unique<LightShaftPass>(device)),
          volumetricFogPass_(std::make_unique<VolumetricFogPass>(device))
    {
        // plan_modern.md MOD-715. After a context loss every GPU object this pipeline holds names
        // storage the driver has already destroyed; rendering into one is undefined rather than
        // merely wrong. The device announces the reset, so the pipeline does not have to be told.
        deviceResetToken_ = device_.DeviceReset.Add(
            [this](System::Object*, const System::EventArgs&) {
                // A reset cannot arrive mid-frame in a single-threaded pipeline (the frame is
                // between begin() and end(), and neither pumps events), but if it somehow did,
                // dropping the bound target would be worse than keeping a stale one until end().
                if (frameOpen_) return;
                releaseDeviceResourcesEXT();
            });
    }

    RenderPipeline::~RenderPipeline()
    {
        // The handler captures `this`. A device outliving a pipeline -- which is the normal case,
        // since Game owns the device -- would otherwise call into freed memory on the next reset.
        device_.DeviceReset.Remove(deviceResetToken_);
    }

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
        if (settings_.isBloomEnabled() || settings_.isSSAOEnabled() || settings_.isFXAAEnabled() ||
            settings_.isSSREnabled() || settings_.isDOFEnabled() ||
            settings_.isColorGradeEnabled() || settings_.getChromaticAberrationStrength() > 0.0f ||
            settings_.getFilmGrainIntensity() > 0.0f || settings_.getLensFlareIntensity() > 0.0f ||
            settings_.getMotionBlurStrength() > 0.0f || settings_.getHeightFogDensity() > 0.0f ||
            settings_.getLightShaftIntensity() > 0.0f ||
            settings_.getVolumetricFogDensity() > 0.0f)
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
            drawSkybox();
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
        drawSkybox();
    }

    void RenderPipeline::drawSkybox()
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

    void RenderPipeline::setCamera(const Matrix& view, const Matrix& projection,
                                   const float nearPlane, const float farPlane)
    {
        if (nearPlane <= 0.0f || farPlane <= nearPlane)
            throw std::invalid_argument(
                "CNA::Graphics::RenderPipeline::setCamera: the near plane must be positive and the "
                "far plane beyond it -- the prepass normalises its depth by the far plane, so a "
                "zero or inverted range reconstructs positions of NaN rather than a wrong image");

        setSkyboxCamera(view, projection);
        cameraInverseProjection_ = Matrix::Invert(projection);
        cameraInverseView_       = Matrix::Invert(view);
        cameraViewProjection_    = view * projection;
        cameraNearPlane_         = nearPlane;
        cameraFarPlane_          = farPlane;
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
            lastFrameTargetSwitches_ = 0;
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
        // SSR next, and before the tonemapper for the same reason bloom is: a reflection carries
        // scene-referred colour, and mixing it in after the range has been compressed away makes a
        // reflected highlight indistinguishable from a reflected white wall. It comes after SSAO
        // because what a mirror shows should be the shaded scene, not the unshaded one.
        if (settings_.isSSREnabled())
            chain_.addPass(ssrPass_.get());
        // Volumetric fog before the analytic one: they are the same medium described two ways, and
        // a scene using both wants the lit volume first so the height fog fades what it produced
        // along with everything else.
        if (settings_.getVolumetricFogDensity() > 0.0f)
            chain_.addPass(volumetricFogPass_.get());
        // Shafts before fog: they are light travelling through the air, so the fog that dims
        // distance should dim them along with everything else rather than the other way round.
        if (settings_.getLightShaftIntensity() > 0.0f)
            chain_.addPass(lightShaftPass_.get());
        // Fog first among the scene-referred passes, and before motion blur: fog is part of the
        // scene the shutter collected, so a moving camera should smear the fogged image rather
        // than fog a smeared one.
        if (settings_.getHeightFogDensity() > 0.0f ||
            settings_.getLightShaftIntensity() > 0.0f ||
            settings_.getVolumetricFogDensity() > 0.0f)
            chain_.addPass(heightFogPass_.get());
        // Motion blur before the lens effects and before the tonemapper, for the reason every
        // scene-referred pass is: it is averaging light the shutter collected, and averaging
        // display-referred values instead would darken a moving highlight rather than smear it.
        if (settings_.getMotionBlurStrength() > 0.0f)
            chain_.addPass(motionBlurPass_.get());
        // Depth of field belongs to the lens, so it happens before anything the lens feeds: a
        // highlight that is out of focus should bloom as the spread circle it became, not as the
        // point it was. Putting it after bloom would bloom the point and then blur the glow, which
        // is the wrong order in the same way tonemapping before bloom would be.
        if (settings_.isDOFEnabled())
            chain_.addPass(dofPass_.get());
        // Flare before bloom, and both before the tonemapper: the threshold that decides what is
        // bright enough to throw a reflection has to separate a genuinely bright light from a merely
        // white wall, and after tonemapping those are the same number. Before bloom because a ghost
        // is a real image of the light and should bloom as one.
        if (settings_.getLensFlareIntensity() > 0.0f)
            chain_.addPass(lensFlarePass_.get());
        // Bloom reads scene-referred values -- its threshold separates genuinely bright pixels
        // from merely white ones -- so it runs before the tonemapper compresses that range away.
        if (settings_.isBloomEnabled())
            chain_.addPass(bloomPass_.get());
        if (settings_.getTonemappingMode() != TonemappingMode::None || settings_.isHDREnabled())
            chain_.addPass(tonemapPass_.get());
        // The grade is the last word on colour, and it runs on displayed pixels: a lookup table
        // indexed by scene-referred values would be asked about numbers past 1.0, where a table has
        // nothing to say. It comes before FXAA rather than after so the edge filter sees the
        // contrast the viewer will see -- a grade that lifts the blacks changes which edges there
        // are to find.
        if (settings_.isColorGradeEnabled())
            chain_.addPass(colorGradePass_.get());
        // The lens sits in front of the viewer, so these two describe displayed pixels: aberration
        // fringes what is shown, and grain lands on the finished image. Grain runs last of all --
        // after FXAA -- because an edge filter handed fresh noise would spend its budget smoothing
        // the grain instead of the edges.
        if (settings_.getChromaticAberrationStrength() > 0.0f)
            chain_.addPass(chromaticAberrationPass_.get());
        // FXAA detects edges by luminance contrast, so it runs on displayed pixels: on
        // scene-referred values a highlight ten times brighter than white reads as an enormous
        // edge and gets blurred into its surroundings.
        if (settings_.isFXAAEnabled())
            chain_.addPass(fxaaPass_.get());
        if (settings_.getFilmGrainIntensity() > 0.0f)
            chain_.addPass(filmGrainPass_.get());
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
        context.sourceVelocity = sceneVelocity_;
        context.projection        = skyboxProjection_;
        context.inverseProjection = cameraInverseProjection_;
        context.nearPlane         = cameraNearPlane_;
        context.farPlane          = cameraFarPlane_;
        context.inverseView             = cameraInverseView_;
        context.previousViewProjection  = previousViewProjection_;
        context.hasPreviousFrame        = hasPreviousFrame_;

        chain_.apply(context);
        lastFramePassCount_ = static_cast<int>(chain_.getPassCount());
        // One bind for the scene target in begin(), one to unbind it above, and one per pass --
        // each pass binds its own destination through FullscreenPass. Derived rather than counted
        // by a hook in the device, because a hook would make every renderer pay for a diagnostic.
        lastFrameTargetSwitches_ = 2 + lastFramePassCount_;

        // The history advances here and nowhere else. Doing it in setCamera would make a game that
        // sets the camera twice in a frame -- or once every other frame -- compare against a camera
        // that was never rendered from.
        previousViewProjection_ = cameraViewProjection_;
        hasPreviousFrame_       = cameraFarPlane_ > 0.0f;
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

    void RenderPipeline::setVelocityInputEXT(
        Microsoft::Xna::Framework::Graphics::Texture2D* velocity)
    {
        sceneVelocity_ = velocity;
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


    RenderPipeline::FrameStatistics RenderPipeline::getStatistics() const
    {
        FrameStatistics statistics;
        statistics.passesRun              = lastFramePassCount_;
        statistics.targetSwitches         = lastFrameTargetSwitches_;
        statistics.usedSceneTarget        = usingSceneTarget_;
        statistics.drewSkybox             = didSkyboxDraw();
        statistics.gpuMemoryEstimateBytes = getGpuMemoryEstimateBytes();
        return statistics;
    }

    void RenderPipeline::releaseDeviceResourcesEXT()
    {
        if (frameOpen_)
            throw std::logic_error("CNA::Graphics::RenderPipeline::releaseDeviceResourcesEXT: a "
                                   "frame is open -- releasing the scene target while it is bound "
                                   "is the situation this exists to avoid");

        sceneTarget_.reset();
        chain_.getTargetPool().reset();
        lastFramePassCount_      = 0;
        lastFrameTargetSwitches_ = 0;
        usingSceneTarget_        = false;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
