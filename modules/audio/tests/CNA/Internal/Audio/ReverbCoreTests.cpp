// SPDX-License-Identifier: MS-PL
#include "../../../../../../third_party/faudio-reverb/Reverb.hpp"
#include <gtest/gtest.h>
#include "../../../../src/Internal/ReverbProcessor.hpp"
#include <limits>
#include <array>
#include <cstdlib>
#include <new>
namespace
{
namespace FX = CNA::ThirdParty::FAudioReverb;
struct Core
{
    FX::DspReverb value{};
    Core()
    {
        const auto allocate = +[](std::size_t n) -> void *
        {
            auto *p = std::malloc(n);
            if (!p)
                throw std::bad_alloc();
            return p;
        };
        try
        {
            FX::DspReverb_Create(&value, 44100, 1, 1, allocate);
        }
        catch (...)
        {
            FX::DspReverb_Destroy(&value, std::free);
            throw;
        }
        FX::FAudioFXReverbParameters p{};
        p.WetDryMix = 100;
        p.ReflectionsDelay = 10;
        p.ReverbDelay = 11;
        p.PositionLeft = p.PositionRight = 6;
        p.PositionMatrixLeft = p.PositionMatrixRight = 27;
        p.EarlyDiffusion = p.LateDiffusion = 8;
        p.LowEQGain = p.HighEQGain = 8;
        p.LowEQCutoff = p.HighEQCutoff = 4;
        p.RoomFilterFreq = 5000;
        p.DecayTime = 1;
        p.Density = p.RoomSize = 100;
        FX::DspReverb_SetParameters(&value, &p);
    }
    ~Core() { FX::DspReverb_Destroy(&value, std::free); }
};
TEST(ReverbCore, ResetClearsDelayCombAndFilterHistory)
{
    Core core;
    std::array<float, 4096> input{}, output{};
    input[0] = .125f;
    FX::DspReverb_INTERNAL_Process_1_to_1(&core.value, input.data(), output.data(), 4096);
    // Delay441; early APF feedthrough -.6+(8/15)*.2 = -37/75.
    EXPECT_NEAR(output[441], -.125f * 37 / 75, 1e-7f);
    // Fill past the 300ms delay-ring capacity before reset; untouched prefix cells
    // would otherwise hide a missing clear until the first wrap.
    input.fill(.125f);
    for(int block=0;block<4;++block)
        FX::DspReverb_INTERNAL_Process_1_to_1(&core.value,input.data(),output.data(),4096);
    FX::Reset(core.value);
    input.fill(0);
    FX::DspReverb_INTERNAL_Process_1_to_1(&core.value, input.data(), output.data(), 4096);
    for (float v : output)
        ASSERT_EQ(v, 0); // source/initial-state oracle, not a CNA snapshot
    input[0] = .125f;
    FX::DspReverb_INTERNAL_Process_1_to_1(&core.value, input.data(), output.data(), 4096);
    EXPECT_NEAR(output[441], -.125f * 37 / 75, 1e-7f);
}
TEST(ReverbCore, Rt60UsesAmplitudeMinusSixtyDecibelsPerDecayTime)
{
    // FX_reverb.c:153-159: g=10^(-3*delaySamples/(rate*decaySeconds)).
    FX::DspDelay delay{};
    delay.delay = 44100;
    delay.sampleRate = 44100;
    EXPECT_NEAR(FX::DspComb_FeedbackFromRT60(&delay, 1000), .001f, 1e-8f);
    delay.delay = 441;
    EXPECT_NEAR(FX::DspComb_FeedbackFromRT60(&delay, 1000), .933254300796991f, 1e-6f);
}
TEST(ReverbCore,RejectsNonfiniteAndOutOfDomainPresets)
{
    using namespace CNA::Internal::Audio;
    const ReverbParameters valid{10,11,6,6,27,27,8,8,8,4,8,4,5,5000,0,0,0,0,1,100,100,0};
    // FAudioFX.h effect-domain limits; refusing nonfinite input is CNA's bounded DSP contract.
    for(float mix:{101.0f,std::numeric_limits<float>::quiet_NaN()})
    {
        auto p=valid; p[21]=mix;
        EXPECT_THROW((ReverbProcessor(44100,p)),std::runtime_error);
    }
    auto p=valid; p[18]=0;
    EXPECT_THROW((ReverbProcessor(44100,p)),std::runtime_error);
    p=valid; p[0]=301;
    EXPECT_THROW((ReverbProcessor(44100,p)),std::runtime_error);
    EXPECT_THROW((ReverbProcessor(19999,valid)),std::runtime_error);
    EXPECT_THROW((ReverbProcessor(192001,valid)),std::runtime_error);
}
} // namespace
