// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/AutoExposureEXT.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/ShaderCodeEXT.hpp"
#include "CNA/Graphics/ShaderPackageEXT.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "shaders/auto_exposure/AutoExposureShaderPackage.generated.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        /// 8 x 8 groups of 8 x 8 invocations: a 64 x 64 sample grid over the frame, and 64
        /// partial sums to bring back.
        constexpr int kGroups = 8;
        constexpr int kGroupSize = 8;
        constexpr int kPartials = kGroups * kGroups;

        template <std::size_t N>
        [[nodiscard]] std::vector<std::uint8_t> ToBytes(const std::uint32_t (&words)[N])
        {
            const auto* begin = reinterpret_cast<const std::uint8_t*>(words);
            return std::vector<std::uint8_t>(begin, begin + sizeof(words));
        }

        [[nodiscard]] ShaderPackageEXT CreateReductionPackage()
        {
            using namespace detail::AutoExposureGenerated;
            return ShaderPackageEXT(
                {
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslEs,
                                  CNA::ShaderStageEXT::Compute, "main",
                                  "auto_exposure/reduction.es.comp.glsl",
                                  std::string(kReductionEsComputeSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                  CNA::ShaderStageEXT::Compute, "main",
                                  "auto_exposure/reduction.desktop.comp.glsl",
                                  std::string(kReductionDesktopComputeSource)),
                    ShaderCodeEXT(CNA::ShaderLanguageEXT::SpirV,
                                  CNA::ShaderStageEXT::Compute, "main",
                                  "auto_exposure/reduction.vulkan.comp.spv",
                                  ToBytes(kReductionVulkanComputeSpirV)),
                },
                {CNA::ShaderStageEXT::Compute},
                {
                    ShaderBindingRequirementEXT(
                        "uScene", 0, ShaderBindingTypeEXT::SampledTexture2D,
                        CNA::ShaderStageEXT::Compute),
                    ShaderBindingRequirementEXT(
                        "Partials", 1, ShaderBindingTypeEXT::StorageBuffer,
                        CNA::ShaderStageEXT::Compute),
                });
        }

    } // namespace

    AutoExposureEXT::AutoExposureEXT(GraphicsDevice& device)
        : reducer_(std::make_unique<ComputeShader>(device, CreateReductionPackage())),
          partials_(std::make_unique<StorageBufferT<float>>(device, kPartials))
    {
    }

    AutoExposureEXT::~AutoExposureEXT() = default;

    float AutoExposureEXT::measureAverageLuminance(Texture2D& scene)
    {
        reducer_->bindTexture(0, "uScene", scene);
        reducer_->bindStorageBuffer(1, partials_->getBuffer());
        reducer_->dispatch(kGroups, kGroups);

        const std::vector<float> partials = partials_->getData();
        float sum = 0.0f;
        for (const float partial : partials) sum += partial;
        // The last add happens here rather than in a second dispatch: 64 floats is less work to
        // bring back than a kernel launch is to make.
        const float samples = static_cast<float>(kPartials)
                            * static_cast<float>(kGroupSize * kGroupSize);
        return std::exp(sum / samples);
    }

    float AutoExposureEXT::update(Texture2D& scene, const float deltaSeconds)
    {
        const float average = measureAverageLuminance(scene);
        const float target = std::clamp(keyValue_ / std::max(average, 1e-4f), minimumExposure_,
                                        maximumExposure_);

        if (deltaSeconds <= 0.0f)
        {
            exposure_ = target;
            return exposure_;
        }

        // Exponential approach, with the eye's own asymmetry: adapting to a brighter scene is
        // fast, adapting to a darker one is slow. A camera that snapped to each frame would
        // strobe. Note which comparison that is -- a BRIGHTER scene needs a LOWER exposure, so the
        // fast direction is the target falling, not rising. Getting this backwards is invisible in
        // a still frame and obvious the moment anything moves.
        const float speed = target < exposure_ ? brighteningSpeed_ : darkeningSpeed_;
        const float blend = 1.0f - std::exp(-speed * deltaSeconds);
        exposure_ = std::clamp(exposure_ + (target - exposure_) * blend, minimumExposure_,
                               maximumExposure_);
        return exposure_;
    }

    void AutoExposureEXT::applyTo(RenderPipelineSettings& settings) const
    {
        settings.setExposure(exposure_);
    }

    float AutoExposureEXT::getExposure() const { return exposure_; }

    void AutoExposureEXT::setExposure(const float value)
    {
        if (!(value > 0.0f))
            throw std::invalid_argument(
                "CNA::Graphics::AutoExposureEXT::setExposure: the exposure must be positive");
        exposure_ = std::clamp(value, minimumExposure_, maximumExposure_);
    }

    void AutoExposureEXT::setKeyValue(const float value)
    {
        if (!(value > 0.0f))
            throw std::invalid_argument(
                "CNA::Graphics::AutoExposureEXT::setKeyValue: the key value must be positive");
        keyValue_ = value;
    }

    float AutoExposureEXT::getKeyValue() const { return keyValue_; }

    void AutoExposureEXT::setAdaptationSpeeds(const float brighteningPerSecond,
                                              const float darkeningPerSecond)
    {
        if (!(brighteningPerSecond > 0.0f) || !(darkeningPerSecond > 0.0f))
            throw std::invalid_argument(
                "CNA::Graphics::AutoExposureEXT::setAdaptationSpeeds: both speeds must be positive");
        brighteningSpeed_ = brighteningPerSecond;
        darkeningSpeed_ = darkeningPerSecond;
    }

    float AutoExposureEXT::getBrighteningSpeed() const { return brighteningSpeed_; }

    float AutoExposureEXT::getDarkeningSpeed() const { return darkeningSpeed_; }

    void AutoExposureEXT::setExposureRange(const float minimum, const float maximum)
    {
        if (!(minimum > 0.0f) || maximum < minimum)
            throw std::invalid_argument(
                "CNA::Graphics::AutoExposureEXT::setExposureRange: need 0 < minimum <= maximum");
        minimumExposure_ = minimum;
        maximumExposure_ = maximum;
        exposure_ = std::clamp(exposure_, minimumExposure_, maximumExposure_);
    }

} // namespace CNA::Graphics

#endif // CNA_CNAEXT
