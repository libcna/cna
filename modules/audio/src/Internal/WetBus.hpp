// SPDX-License-Identifier: MS-PL
#pragma once
#include <array>
#include <cstdint>
namespace CNA::Internal::Audio
{
// All registry access is serialized by the selected mixer's callback barrier.
using ReverbParameters = std::array<float, 22>;
std::uint64_t CreateReverbBus(const ReverbParameters &parameters);
void UpdateReverbBus(std::uint64_t id, const ReverbParameters &parameters);
void ReleaseReverbBus(std::uint64_t id) noexcept;
void ClearReverbBuses() noexcept; // device stopped / mixer callback barrier already held
void AccumulateReverb(std::uint64_t id, float gain, const float *pcm, int channels,
                      int samples) noexcept;
void MixReverbReturns(float *pcm, int channels, int samples, float masterGain) noexcept;
} // namespace CNA::Internal::Audio
