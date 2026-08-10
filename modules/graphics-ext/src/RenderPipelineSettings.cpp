// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/RenderPipelineSettings.hpp"

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

    bool            RenderPipelineSettings::isSSAOEnabled()       const { return ssaoEnabled_; }
    void            RenderPipelineSettings::setSSAOEnabled(bool v)      { ssaoEnabled_ = v; }

    RenderQuality   RenderPipelineSettings::getRenderQuality()    const { return renderQuality_; }
    void            RenderPipelineSettings::setRenderQuality(RenderQuality q) { renderQuality_ = q; }

    ShadowQuality   RenderPipelineSettings::getShadowQuality()    const { return shadowQuality_; }
    void            RenderPipelineSettings::setShadowQuality(ShadowQuality q) { shadowQuality_ = q; }

    bool            RenderPipelineSettings::isShadowsEnabled()    const { return shadowsEnabled_; }
    void            RenderPipelineSettings::setShadowsEnabled(bool v)   { shadowsEnabled_ = v; }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
