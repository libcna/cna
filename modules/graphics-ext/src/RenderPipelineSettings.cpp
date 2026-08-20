// SPDX-License-Identifier: MS-PL
#include <algorithm>
#include "CNA/Graphics/RenderPipelineSettings.hpp"

#ifdef CNA_CNAEXT
#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/FxaaPass.hpp"
#include "CNA/Graphics/SsaoPass.hpp"
#endif

#ifdef CNA_CNAEXT

#include <iomanip>
#include <sstream>
#include <string>

namespace CNA::Graphics {

    namespace {
        /// plans/plan_modern.md MOD-730. Only the values whose out-of-range case is *undefined* are
        /// clamped -- a gamma of zero is a division by zero, a negative exposure or intensity is a
        /// sign error rather than a look. Values that are merely extreme (a bloom threshold of 100,
        /// a sample count of 500) are stored as given, because MOD-22 established that the passes
        /// clamp what they apply: a settings bag that clamped to one pass's limits would silently
        /// change the number a caller reads back, and a quality preset would have to know every
        /// pass's range to avoid it.
        [[nodiscard]] float AtLeast(const float value, const float floorValue)
        {
            return value < floorValue ? floorValue : value;
        }
    }

    RenderPipelineSettings::RenderPipelineSettings() = default;

    bool            RenderPipelineSettings::isHDREnabled()       const { return hdrEnabled_; }
    void            RenderPipelineSettings::setHDREnabled(bool v)      { hdrEnabled_ = v; }

    float           RenderPipelineSettings::getExposure()         const { return exposure_; }
    void            RenderPipelineSettings::setExposure(float v)         { exposure_ = AtLeast(v, 0.0f); }

    float           RenderPipelineSettings::getGamma()            const { return gamma_; }
    void            RenderPipelineSettings::setGamma(float v)           { gamma_ = AtLeast(v, kMinimumGamma); }

    TonemappingMode RenderPipelineSettings::getTonemappingMode()  const { return tonemappingMode_; }
    void            RenderPipelineSettings::setTonemappingMode(TonemappingMode m) { tonemappingMode_ = m; }

    bool            RenderPipelineSettings::isBloomEnabled()      const { return bloomEnabled_; }
    void            RenderPipelineSettings::setBloomEnabled(bool v)     { bloomEnabled_ = v; }

    float           RenderPipelineSettings::getBloomIntensity()   const { return bloomIntensity_; }
    void            RenderPipelineSettings::setBloomIntensity(float v)   { bloomIntensity_ = AtLeast(v, 0.0f); }

    float           RenderPipelineSettings::getBloomThreshold()  const { return bloomThreshold_; }
    void            RenderPipelineSettings::setBloomThreshold(float v)   { bloomThreshold_ = AtLeast(v, 0.0f); }

    int             RenderPipelineSettings::getBloomIterations() const { return bloomIterations_; }
    void            RenderPipelineSettings::setBloomIterations(int v)   { bloomIterations_ = v; }

    bool            RenderPipelineSettings::isSSAOEnabled()       const { return ssaoEnabled_; }
    TransparencyMode RenderPipelineSettings::getTransparencyMode() const { return transparencyMode_; }
    void RenderPipelineSettings::setTransparencyMode(const TransparencyMode value)
    {
        transparencyMode_ = value;
    }
    void            RenderPipelineSettings::setSSAOEnabled(bool v)      { ssaoEnabled_ = v; }

    float           RenderPipelineSettings::getSSAORadius()       const { return ssaoRadius_; }
    void            RenderPipelineSettings::setSSAORadius(float v)       { ssaoRadius_ = AtLeast(v, 0.0f); }

    float           RenderPipelineSettings::getSSAOIntensity()    const { return ssaoIntensity_; }
    void            RenderPipelineSettings::setSSAOIntensity(float v)    { ssaoIntensity_ = AtLeast(v, 0.0f); }

    int             RenderPipelineSettings::getSSAOSampleCount()  const { return ssaoSampleCount_; }
    void            RenderPipelineSettings::setSSAOSampleCount(int v)   { ssaoSampleCount_ = v; }

    bool            RenderPipelineSettings::isSSREnabled()        const { return ssrEnabled_; }
    void            RenderPipelineSettings::setSSREnabled(bool v)       { ssrEnabled_ = v; }

    float           RenderPipelineSettings::getSSRMaxDistance()   const { return ssrMaxDistance_; }
    void            RenderPipelineSettings::setSSRMaxDistance(float v)  { ssrMaxDistance_ = AtLeast(v, 0.0f); }

    int             RenderPipelineSettings::getSSRStepCount()     const { return ssrStepCount_; }
    void            RenderPipelineSettings::setSSRStepCount(int v)      { ssrStepCount_ = v; }

    float           RenderPipelineSettings::getSSRThickness()     const { return ssrThickness_; }
    void            RenderPipelineSettings::setSSRThickness(float v)    { ssrThickness_ = AtLeast(v, 0.0f); }

    float           RenderPipelineSettings::getSSRDepthBias()     const { return ssrDepthBias_; }
    void            RenderPipelineSettings::setSSRDepthBias(float v)    { ssrDepthBias_ = AtLeast(v, 0.0f); }

    float           RenderPipelineSettings::getSSREdgeFade()      const { return ssrEdgeFade_; }
    void            RenderPipelineSettings::setSSREdgeFade(float v)     { ssrEdgeFade_ = std::clamp(v, 0.0f, 0.5f); }

    float           RenderPipelineSettings::getVolumetricFogDensity() const { return volumetricFogDensity_; }
    void            RenderPipelineSettings::setVolumetricFogDensity(float v) { volumetricFogDensity_ = AtLeast(v, 0.0f); }

    float           RenderPipelineSettings::getLightShaftThreshold() const { return lightShaftThreshold_; }
    void            RenderPipelineSettings::setLightShaftThreshold(float v) { lightShaftThreshold_ = AtLeast(v, 0.0f); }

    float           RenderPipelineSettings::getLightShaftIntensity() const { return lightShaftIntensity_; }
    void            RenderPipelineSettings::setLightShaftIntensity(float v) { lightShaftIntensity_ = AtLeast(v, 0.0f); }

    float           RenderPipelineSettings::getLightShaftDecay()     const { return lightShaftDecay_; }
    void            RenderPipelineSettings::setLightShaftDecay(float v)     { lightShaftDecay_ = std::clamp(v, 0.0f, 1.0f); }

    float           RenderPipelineSettings::getHeightFogDensity()    const { return heightFogDensity_; }
    void            RenderPipelineSettings::setHeightFogDensity(float v)    { heightFogDensity_ = AtLeast(v, 0.0f); }

    float           RenderPipelineSettings::getHeightFogFalloff()    const { return heightFogFalloff_; }
    void            RenderPipelineSettings::setHeightFogFalloff(float v)    { heightFogFalloff_ = AtLeast(v, 0.0f); }

    float           RenderPipelineSettings::getHeightFogBaseHeight() const { return heightFogBaseHeight_; }
    void            RenderPipelineSettings::setHeightFogBaseHeight(float v) { heightFogBaseHeight_ = v; }

    float           RenderPipelineSettings::getMotionBlurStrength()    const { return motionBlurStrength_; }
    void            RenderPipelineSettings::setMotionBlurStrength(float v)    { motionBlurStrength_ = std::clamp(v, 0.0f, 1.0f); }

    float           RenderPipelineSettings::getMotionBlurMaxDistance() const { return motionBlurMaxDistance_; }
    void            RenderPipelineSettings::setMotionBlurMaxDistance(float v) { motionBlurMaxDistance_ = std::clamp(v, 0.0f, 0.25f); }

    float           RenderPipelineSettings::getChromaticAberrationStrength() const { return chromaticAberration_; }
    void            RenderPipelineSettings::setChromaticAberrationStrength(float v) { chromaticAberration_ = std::clamp(v, 0.0f, 0.1f); }

    float           RenderPipelineSettings::getFilmGrainIntensity()  const { return filmGrainIntensity_; }
    void            RenderPipelineSettings::setFilmGrainIntensity(float v)  { filmGrainIntensity_ = std::clamp(v, 0.0f, 1.0f); }

    float           RenderPipelineSettings::getLensFlareThreshold()  const { return lensFlareThreshold_; }
    void            RenderPipelineSettings::setLensFlareThreshold(float v)  { lensFlareThreshold_ = AtLeast(v, 0.0f); }

    float           RenderPipelineSettings::getLensFlareIntensity()  const { return lensFlareIntensity_; }
    void            RenderPipelineSettings::setLensFlareIntensity(float v)  { lensFlareIntensity_ = AtLeast(v, 0.0f); }

    float           RenderPipelineSettings::getLensFlareDispersal()  const { return lensFlareDispersal_; }
    void            RenderPipelineSettings::setLensFlareDispersal(float v)  { lensFlareDispersal_ = std::clamp(v, 0.0f, 1.0f); }

    bool            RenderPipelineSettings::isColorGradeEnabled()   const { return colorGradeEnabled_; }
    void            RenderPipelineSettings::setColorGradeEnabled(bool v)   { colorGradeEnabled_ = v; }

    float           RenderPipelineSettings::getColorGradeStrength()  const { return colorGradeStrength_; }
    void            RenderPipelineSettings::setColorGradeStrength(float v) { colorGradeStrength_ = std::clamp(v, 0.0f, 1.0f); }

    bool            RenderPipelineSettings::isDOFEnabled()        const { return dofEnabled_; }
    void            RenderPipelineSettings::setDOFEnabled(bool v)       { dofEnabled_ = v; }

    float           RenderPipelineSettings::getDOFFocusDistance() const { return dofFocusDistance_; }
    void            RenderPipelineSettings::setDOFFocusDistance(float v) { dofFocusDistance_ = AtLeast(v, 0.0f); }

    float           RenderPipelineSettings::getDOFFocalLength()   const { return dofFocalLength_; }
    void            RenderPipelineSettings::setDOFFocalLength(float v)  { dofFocalLength_ = AtLeast(v, 0.0f); }

    float           RenderPipelineSettings::getDOFFNumber()       const { return dofFNumber_; }
    void            RenderPipelineSettings::setDOFFNumber(float v)      { dofFNumber_ = AtLeast(v, 0.0f); }

    float           RenderPipelineSettings::getDOFMaxRadius()     const { return dofMaxRadius_; }
    void            RenderPipelineSettings::setDOFMaxRadius(float v)    { dofMaxRadius_ = std::clamp(v, 0.0f, 0.25f); }

    float           RenderPipelineSettings::getSSRRoughnessBlur() const { return ssrRoughnessBlur_; }
    void            RenderPipelineSettings::setSSRRoughnessBlur(float v) { ssrRoughnessBlur_ = std::clamp(v, 0.0f, 0.25f); }

    float           RenderPipelineSettings::getSSRIntensity()     const { return ssrIntensity_; }
    void            RenderPipelineSettings::setSSRIntensity(float v)    { ssrIntensity_ = AtLeast(v, 0.0f); }

    bool            RenderPipelineSettings::isFXAAEnabled()       const { return fxaaEnabled_; }
    void            RenderPipelineSettings::setFXAAEnabled(bool v)      { fxaaEnabled_ = v; }

    RenderQuality   RenderPipelineSettings::getRenderQuality()    const { return renderQuality_; }
    void            RenderPipelineSettings::setRenderQuality(RenderQuality q) { renderQuality_ = q; }

    float RenderPipelineSettings::getFXAAEdgeThresholdEXT() const { return fxaaEdgeThreshold_; }
    void  RenderPipelineSettings::setFXAAEdgeThresholdEXT(float v)
    { fxaaEdgeThreshold_ = AtLeast(v, kMinimumFxaaEdgeThreshold); }


    void RenderPipelineSettings::applyRenderQualityPresetEXT()
    {
        bloomIterations_  = BloomPass::iterationsForQuality(renderQuality_);
        ssaoSampleCount_  = SsaoPass::sampleCountForQuality(renderQuality_);
        fxaaEdgeThreshold_ = FxaaPass::edgeThresholdForQuality(renderQuality_);
    }

    ShadowQuality   RenderPipelineSettings::getShadowQuality()    const { return shadowQuality_; }
    void            RenderPipelineSettings::setShadowQuality(ShadowQuality q) { shadowQuality_ = q; }

    bool            RenderPipelineSettings::isShadowsEnabled()    const { return shadowsEnabled_; }
    void            RenderPipelineSettings::setShadowsEnabled(bool v)   { shadowsEnabled_ = v; }


    // ---- Serialization (MOD-731) --------------------------------------------------------------

    namespace {

        [[nodiscard]] const char* NameOfTonemappingMode(const TonemappingMode mode)
        {
            switch (mode)
            {
            case TonemappingMode::Reinhard:   return "Reinhard";
            case TonemappingMode::Filmic:     return "Filmic";
            case TonemappingMode::Aces:       return "Aces";
            case TonemappingMode::Uncharted2: return "Uncharted2";
            case TonemappingMode::None:
            default:                          return "None";
            }
        }

        [[nodiscard]] bool ParseTonemappingMode(const std::string& text, TonemappingMode& out)
        {
            if (text == "None")       { out = TonemappingMode::None;       return true; }
            if (text == "Reinhard")   { out = TonemappingMode::Reinhard;   return true; }
            if (text == "Filmic")     { out = TonemappingMode::Filmic;     return true; }
            if (text == "Aces")       { out = TonemappingMode::Aces;       return true; }
            if (text == "Uncharted2") { out = TonemappingMode::Uncharted2; return true; }
            return false;
        }

        [[nodiscard]] const char* NameOfRenderQuality(const RenderQuality quality)
        {
            switch (quality)
            {
            case RenderQuality::Low:   return "Low";
            case RenderQuality::High:  return "High";
            case RenderQuality::Ultra: return "Ultra";
            case RenderQuality::Medium:
            default:                   return "Medium";
            }
        }

        [[nodiscard]] bool ParseRenderQuality(const std::string& text, RenderQuality& out)
        {
            if (text == "Low")    { out = RenderQuality::Low;    return true; }
            if (text == "Medium") { out = RenderQuality::Medium; return true; }
            if (text == "High")   { out = RenderQuality::High;   return true; }
            if (text == "Ultra")  { out = RenderQuality::Ultra;  return true; }
            return false;
        }

        [[nodiscard]] bool ParseFloat(const std::string& text, float& out)
        {
            try
            {
                std::size_t consumed = 0;
                const float value = std::stof(text, &consumed);
                if (consumed != text.size()) return false;
                out = value;
                return true;
            }
            catch (...) { return false; }
        }

        [[nodiscard]] bool ParseInt(const std::string& text, int& out)
        {
            try
            {
                std::size_t consumed = 0;
                const int value = std::stoi(text, &consumed);
                if (consumed != text.size()) return false;
                out = value;
                return true;
            }
            catch (...) { return false; }
        }

        [[nodiscard]] bool ParseBool(const std::string& text, bool& out)
        {
            if (text == "1" || text == "true")  { out = true;  return true; }
            if (text == "0" || text == "false") { out = false; return true; }
            return false;
        }

        [[nodiscard]] std::string FloatToString(const float value)
        {
            // Six significant digits: enough to round-trip a float that came from a slider, and
            // short enough that a settings line stays readable. Not std::to_string, which pads a
            // gamma of 2.2 to "2.200000".
            std::ostringstream out;
            out << std::setprecision(6) << value;
            return out.str();
        }

    } // namespace

    std::string RenderPipelineSettings::toStringEXT() const
    {
        std::ostringstream out;
        out << "hdr=" << (hdrEnabled_ ? 1 : 0) << ';'
            << "exposure=" << FloatToString(exposure_) << ';'
            << "gamma=" << FloatToString(gamma_) << ';'
            << "tonemap=" << NameOfTonemappingMode(tonemappingMode_) << ';'
            << "bloom=" << (bloomEnabled_ ? 1 : 0) << ';'
            << "bloomIntensity=" << FloatToString(bloomIntensity_) << ';'
            << "bloomThreshold=" << FloatToString(bloomThreshold_) << ';'
            << "bloomIterations=" << bloomIterations_ << ';'
            << "ssao=" << (ssaoEnabled_ ? 1 : 0) << ';'
            << "ssaoRadius=" << FloatToString(ssaoRadius_) << ';'
            << "ssaoIntensity=" << FloatToString(ssaoIntensity_) << ';'
            << "ssaoSampleCount=" << ssaoSampleCount_ << ';'
            << "fxaa=" << (fxaaEnabled_ ? 1 : 0) << ';'
            << "fxaaEdgeThreshold=" << FloatToString(fxaaEdgeThreshold_) << ';'
            << "quality=" << NameOfRenderQuality(renderQuality_) << ';';
        return out.str();
    }

    int RenderPipelineSettings::applyFromStringEXT(const std::string& text)
    {
        int applied = 0;
        std::size_t position = 0;

        while (position < text.size())
        {
            const std::size_t end = text.find(';', position);
            const std::string field =
                text.substr(position, end == std::string::npos ? std::string::npos : end - position);
            position = (end == std::string::npos) ? text.size() : end + 1;

            const std::size_t equals = field.find('=');
            if (equals == std::string::npos) continue;
            const std::string key   = field.substr(0, equals);
            const std::string value = field.substr(equals + 1);

            // Every branch goes through the public setter, so the clamping above applies to a
            // loaded value exactly as it does to a set one -- a settings file with a stale gamma
            // of 0 in it must not be the one path that gets past the floor.
            bool  flag  = false;
            float scalar = 0.0f;
            int   integer = 0;

            if      (key == "hdr"    && ParseBool(value, flag))  { setHDREnabled(flag); ++applied; }
            else if (key == "bloom"  && ParseBool(value, flag))  { setBloomEnabled(flag); ++applied; }
            else if (key == "ssao"   && ParseBool(value, flag))  { setSSAOEnabled(flag); ++applied; }
            else if (key == "fxaa"   && ParseBool(value, flag))  { setFXAAEnabled(flag); ++applied; }
            else if (key == "exposure"        && ParseFloat(value, scalar)) { setExposure(scalar); ++applied; }
            else if (key == "gamma"           && ParseFloat(value, scalar)) { setGamma(scalar); ++applied; }
            else if (key == "bloomIntensity"  && ParseFloat(value, scalar)) { setBloomIntensity(scalar); ++applied; }
            else if (key == "bloomThreshold"  && ParseFloat(value, scalar)) { setBloomThreshold(scalar); ++applied; }
            else if (key == "ssaoRadius"      && ParseFloat(value, scalar)) { setSSAORadius(scalar); ++applied; }
            else if (key == "ssaoIntensity"   && ParseFloat(value, scalar)) { setSSAOIntensity(scalar); ++applied; }
            else if (key == "fxaaEdgeThreshold" && ParseFloat(value, scalar)) { setFXAAEdgeThresholdEXT(scalar); ++applied; }
            else if (key == "bloomIterations" && ParseInt(value, integer))   { setBloomIterations(integer); ++applied; }
            else if (key == "ssaoSampleCount" && ParseInt(value, integer))   { setSSAOSampleCount(integer); ++applied; }
            else if (key == "tonemap")
            {
                TonemappingMode mode = TonemappingMode::None;
                if (ParseTonemappingMode(value, mode)) { setTonemappingMode(mode); ++applied; }
            }
            else if (key == "quality")
            {
                RenderQuality quality = RenderQuality::Medium;
                if (ParseRenderQuality(value, quality)) { setRenderQuality(quality); ++applied; }
            }
        }

        return applied;
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
