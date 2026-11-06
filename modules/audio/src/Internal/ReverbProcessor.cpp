// SPDX-License-Identifier: MS-PL
#include "Internal/ReverbProcessor.hpp"
#include "../../../../third_party/faudio-reverb/Reverb.hpp"
#include <cstdlib>
#include <limits>
#include <stdexcept>
namespace CNA::Internal::Audio
{
namespace
{
namespace FX = CNA::ThirdParty::FAudioReverb;
// XGS->FX map: FACT_internal.c:1154-1175. Domain limits: FAudioFX.h.
FX::FAudioFXReverbParameters Map(const ReverbParameters &p)
{
    constexpr ReverbParameters lo{0, 0, 0,  0,    0,    0,    0,    0,   0, 0, 0,
                                  0, 0, 20, -100, -100, -100, -100, .1f, 0, 0, 0};
    constexpr ReverbParameters hi{300,
                                  85,
                                  30,
                                  30,
                                  30,
                                  30,
                                  15,
                                  15,
                                  12,
                                  9,
                                  8,
                                  14,
                                  5,
                                  20000,
                                  0,
                                  0,
                                  20,
                                  20,
                                  std::numeric_limits<float>::max(),
                                  100,
                                  100,
                                  100};
    for (std::size_t i = 0; i < p.size(); ++i)
        if (!std::isfinite(p[i]) || p[i] < lo[i] || p[i] > hi[i])
            throw std::runtime_error("XACT: invalid reverb parameter");
    FX::FAudioFXReverbParameters r{};
    r.ReflectionsDelay = static_cast<uint32_t>(p[0]);
    r.ReverbDelay = static_cast<uint8_t>(p[1]);
    r.PositionLeft = static_cast<uint8_t>(p[2]);
    r.PositionRight = static_cast<uint8_t>(p[3]);
    r.PositionMatrixLeft = static_cast<uint8_t>(p[4]);
    r.PositionMatrixRight = static_cast<uint8_t>(p[5]);
    r.EarlyDiffusion = static_cast<uint8_t>(p[6]);
    r.LateDiffusion = static_cast<uint8_t>(p[7]);
    r.LowEQGain = static_cast<uint8_t>(p[8]);
    r.LowEQCutoff = static_cast<uint8_t>(p[9]);
    r.HighEQGain = static_cast<uint8_t>(p[10]);
    r.HighEQCutoff = static_cast<uint8_t>(p[11]);
    r.RearDelay = static_cast<uint8_t>(p[12]);
    r.RoomFilterFreq = p[13];
    r.RoomFilterMain = p[14];
    r.RoomFilterHF = p[15];
    r.ReflectionsGain = p[16];
    r.ReverbGain = p[17];
    r.DecayTime = p[18];
    r.Density = p[19];
    r.RoomSize = p[20];
    r.WetDryMix = p[21];
    return r;
}
void *Allocate(std::size_t n)
{
    auto *p = std::malloc(n);
    if (!p)
        throw std::bad_alloc();
    return p;
}
} // namespace
struct ReverbProcessor::Impl
{
    FX::DspReverb core{};
    ReverbParameters parameters;
    Impl(int rate, const ReverbParameters &p) : parameters(p)
    {
        auto mapped = Map(p);
        if (rate < 20000 || rate > 192000)
            throw std::runtime_error("XACT: unsupported reverb rate");
        try
        {
            FX::DspReverb_Create(&core, rate, 1, 1, Allocate);
            FX::DspReverb_SetParameters(&core, &mapped);
        }
        catch (...)
        {
            FX::DspReverb_Destroy(&core, std::free);
            throw;
        }
    }
    ~Impl() { FX::DspReverb_Destroy(&core, std::free); }
};
ReverbProcessor::ReverbProcessor(int rate, const ReverbParameters &p)
    : impl_(std::make_unique<Impl>(rate, p))
{
}
ReverbProcessor::~ReverbProcessor() = default;
void ReverbProcessor::Update(const ReverbParameters &p)
{
    if (impl_->parameters == p)
        return;
    auto mapped = Map(p);
    FX::DspReverb_SetParameters(&impl_->core, &mapped);
    impl_->parameters = p;
}
void ReverbProcessor::Process(float *input, float *output, std::size_t frames) noexcept
{
    FX::DspReverb_INTERNAL_Process_1_to_1(&impl_->core, input, output, frames);
}
} // namespace CNA::Internal::Audio
