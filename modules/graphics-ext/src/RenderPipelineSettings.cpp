// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/RenderPipelineSettings.hpp"

#ifdef CNA_CNAEXT
#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/SsaoPass.hpp"
#endif

#ifdef CNA_CNAEXT

namespace CNA::Graphics {
    RenderPipelineSettings::RenderPipelineSettings() = default;

    bool            RenderPipelineSettings::isHDREnabled()       const { return hdrEnabled_; }
    void            RenderPipelineSettings::setHDREnabled(bool v)      { hdrEnabled_ = v; }

    float           RenderPipelineSettings::getExposure()         const { return exposure_; }
    void            RenderPipelineSettings::setExposure(float v)        { exposure_ = v; }

    float           RenderPipelineSettings::getGamma()            const { return gamma_; }
    void            RenderPipelineSettings::setGamma(float v)           { gamma_ = v; }

    TonemappingMode RenderPipelineSettings::getTonemappingMode()  const { return tonemappingMode_; }
    void            RenderPipelineSettings::setTonemappingMode(TonemappingMode m) { tonemappingMode_ = m; }

    bool            RenderPipelineSettings::isBloomEnabled()      const { return bloomEnabled_; }
    void            RenderPipelineSettings::setBloomEnabled(bool v)     { bloomEnabled_ = v; }

    float           RenderPipelineSettings::getBloomIntensity()   const { return bloomIntensity_; }
    void            RenderPipelineSettings::setBloomIntensity(float v)  { bloomIntensity_ = v; }

    float           RenderPipelineSettings::getBloomThreshold()  const { return bloomThreshold_; }
    void            RenderPipelineSettings::setBloomThreshold(float v)  { bloomThreshold_ = v; }

    int             RenderPipelineSettings::getBloomIterations() const { return bloomIterations_; }
    void            RenderPipelineSettings::setBloomIterations(int v)   { bloomIterations_ = v; }

    bool            RenderPipelineSettings::isSSAOEnabled()       const { return ssaoEnabled_; }
    void            RenderPipelineSettings::setSSAOEnabled(bool v)      { ssaoEnabled_ = v; }

    float           RenderPipelineSettings::getSSAORadius()       const { return ssaoRadius_; }
    void            RenderPipelineSettings::setSSAORadius(float v)      { ssaoRadius_ = v; }

    float           RenderPipelineSettings::getSSAOIntensity()    const { return ssaoIntensity_; }
    void            RenderPipelineSettings::setSSAOIntensity(float v)   { ssaoIntensity_ = v; }

    int             RenderPipelineSettings::getSSAOSampleCount()  const { return ssaoSampleCount_; }
    void            RenderPipelineSettings::setSSAOSampleCount(int v)   { ssaoSampleCount_ = v; }

    bool            RenderPipelineSettings::isFXAAEnabled()       const { return fxaaEnabled_; }
    void            RenderPipelineSettings::setFXAAEnabled(bool v)      { fxaaEnabled_ = v; }

    RenderQuality   RenderPipelineSettings::getRenderQuality()    const { return renderQuality_; }
    void            RenderPipelineSettings::setRenderQuality(RenderQuality q) { renderQuality_ = q; }

    void RenderPipelineSettings::applyRenderQualityPresetEXT()
    {
        bloomIterations_  = BloomPass::iterationsForQuality(renderQuality_);
        ssaoSampleCount_  = SsaoPass::sampleCountForQuality(renderQuality_);
    }

    ShadowQuality   RenderPipelineSettings::getShadowQuality()    const { return shadowQuality_; }
    void            RenderPipelineSettings::setShadowQuality(ShadowQuality q) { shadowQuality_ = q; }

    bool            RenderPipelineSettings::isShadowsEnabled()    const { return shadowsEnabled_; }
    void            RenderPipelineSettings::setShadowsEnabled(bool v)   { shadowsEnabled_ = v; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
