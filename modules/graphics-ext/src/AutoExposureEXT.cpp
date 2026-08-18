// SPDX-License-Identifier: MS-PL
#include "CNA/Graphics/AutoExposureEXT.hpp"

#ifdef CNA_CNAEXT

#include "CNA/Graphics/ComputeShader.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/StorageBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CNA::Graphics {

    using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
    using Microsoft::Xna::Framework::Graphics::Texture2D;

    namespace {

        /// 8 x 8 groups of 8 x 8 invocations: a 64 x 64 sample grid over the frame, and 64
        /// partial sums to bring back.
        constexpr int kGroups = 8;
        constexpr int kGroupSize = 8;
        constexpr int kPartials = kGroups * kGroups;

        const char* const kReduction = R"(#version 310 es
precision highp float;
layout(local_size_x = 8, local_size_y = 8) in;
uniform sampler2D uScene;
layout(std430, binding = 0) writeonly buffer Partials { float partials[]; };
shared float sharedSums[64];
void main() {
    // One sample per invocation, at the centre of its cell of a 64 x 64 grid over the frame.
    vec2 grid = vec2(gl_NumWorkGroups.xy * gl_WorkGroupSize.xy);
    vec2 uv = (vec2(gl_GlobalInvocationID.xy) + 0.5) / grid;
    vec3 colour = texture(uScene, uv).rgb;
    float luminance = dot(colour, vec3(0.2126, 0.7152, 0.0722));
    // The log-average is what a plain mean is not: robust to a handful of very bright pixels.
    // The epsilon keeps a black frame finite rather than negative infinity.
    float value = log(max(luminance, 1e-4));

    uint local = gl_LocalInvocationIndex;
    sharedSums[local] = value;
    memoryBarrierShared();
    barrier();
    for (uint stride = 32u; stride > 0u; stride >>= 1u) {
        if (local < stride) sharedSums[local] += sharedSums[local + stride];
        memoryBarrierShared();
        barrier();
    }
    if (local == 0u) {
        uint group = gl_WorkGroupID.y * gl_NumWorkGroups.x + gl_WorkGroupID.x;
        partials[group] = sharedSums[0];
    }
}
)";

    } // namespace

    AutoExposureEXT::AutoExposureEXT(GraphicsDevice& device)
        : reducer_(std::make_unique<ComputeShader>(device, kReduction)),
          partials_(std::make_unique<StorageBufferT<float>>(device, kPartials))
    {
    }

    AutoExposureEXT::~AutoExposureEXT() = default;

    float AutoExposureEXT::measureAverageLuminance(Texture2D& scene)
    {
        reducer_->bindTexture(0, "uScene", scene);
        reducer_->bindStorageBuffer(0, partials_->getBuffer());
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
