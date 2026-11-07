// SPDX-License-Identifier: MS-PL
#include "Internal/WetBus.hpp"
#include "CNA/Internal/Audio/MixerEngine.hpp"
#include "Internal/ReverbProcessor.hpp"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <memory>
#include <stdexcept>

namespace CNA::Internal::Audio
{
namespace
{
struct Bus
{
    std::uint64_t id = 0;
    ReverbProcessor processor;
    std::array<float, 1024> input{}, output{};
    Bus(int rate, const ReverbParameters &p) : processor(rate, p) {}
};
// Trivial static storage has no destructor racing the process-wide device shutdown.
std::array<Bus *, 32> buses{};
std::uint64_t nextId = 1; // never recycled, including across mixer generations
Bus *Find(std::uint64_t id) noexcept
{
    if (id)
        for (auto *bus : buses)
            if (bus && bus->id == id)
                return bus;
    return nullptr;
}
} // namespace
std::uint64_t CreateReverbBus(const ReverbParameters &p)
{
#ifdef SOUND_ENABLED
    EnsureMixer();
    auto bus = std::make_unique<Bus>(GetMixerSampleRate(), p);
    MixerLock lock;
    for (auto &slot : buses)
        if (!slot)
        {
            if (nextId == 0)
                throw std::runtime_error("XACT: wet handle space exhausted");
            bus->id = nextId++;
            auto id = bus->id;
            slot = bus.release();
            return id;
        }
    throw std::runtime_error("XACT: 32 wet buses already reserved");
#else
    (void)p;
    throw std::runtime_error("XACT: reverb requires audio");
#endif
}
void UpdateReverbBus(std::uint64_t id, const ReverbParameters &p)
{
#ifdef SOUND_ENABLED
    MixerLock lock;
    auto *bus = Find(id);
    if (!bus)
        throw std::runtime_error("XACT: stale wet bus");
    bus->processor.Update(p);
#else
    (void)id;
    (void)p;
#endif
}
void ReleaseReverbBus(std::uint64_t id) noexcept
{
#ifdef SOUND_ENABLED
    if (!id)
        return;
    std::unique_ptr<Bus> retired;
    {
        MixerLock lock;
        for (auto &slot : buses)
            if (slot && slot->id == id)
            {
                retired.reset(slot);
                slot = nullptr;
                break;
            }
    }
#else
    (void)id;
#endif
}
void ClearReverbBuses() noexcept
{
    for (auto &slot : buses)
    {
        delete slot;
        slot = nullptr;
    }
}
void AccumulateReverb(std::uint64_t id, float gain, const float *pcm, int channels, int samples) noexcept
{
    auto *bus = Find(id);
    if (!bus || channels != 2)
        return;
    const int frames = samples / 2;
    assert(frames <= 1024);
    for (int f = 0; f < frames; ++f)
        bus->input[f] += (pcm[2 * f] + pcm[2 * f + 1]) * (.5f * gain);
}
void MixReverbReturns(float *pcm, int channels, int samples, float masterGain) noexcept
{
    if (channels <= 0)
        return;
    const int frames = samples / channels;
    assert(frames <= 1024);
    for (auto *bus : buses)
        if (bus)
        {
            bus->processor.Process(bus->input.data(), bus->output.data(), frames);
            for (int f = 0; f < frames; ++f)
            {
                const float wet = bus->output[f] * masterGain;
                pcm[f * channels] += wet;
                if (channels > 1)
                    pcm[f * channels + 1] += wet;
            }
            std::fill(bus->input.begin(), bus->input.end(), 0.0f);
        }
}
} // namespace CNA::Internal::Audio
