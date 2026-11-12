// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstddef>
#include <mutex>

#include "CNA/Internal/Audio/MixerEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"

// Test-only accessor for DynamicSoundEffectInstance's queue state (friend declaration in
// DynamicSoundEffectInstance.hpp), shared by DynamicSoundEffectInstanceTests.cpp and the
// manual-device harness (DynamicSoundEffectOfflineCases.hpp).
namespace Microsoft::Xna::Framework::Audio
{
    struct DynamicSoundEffectInstanceTestAccess
    {
        static auto QueuedBuffers(const DynamicSoundEffectInstance& instance)
        {
            std::lock_guard<std::mutex> lock(instance.queueMutex_);
            return instance.queuedBuffers_;
        }

        static bool IsFloat(const DynamicSoundEffectInstance& instance)
        {
            return instance.isFloat_;
        }

        // Observe native consumption without retiring CNA's chunks or posting notifications.
        static int StreamQueuedBytes(const DynamicSoundEffectInstance& instance)
        {
            std::lock_guard<std::mutex> lock(instance.queueMutex_);
            return CNA::Internal::Audio::GetMixerStreamQueuedBytes(
                static_cast<CNA::Internal::Audio::MixerStream*>(instance.audioStream_));
        }

        static std::size_t PendingBuffers(const DynamicSoundEffectInstance& instance)
        {
            std::lock_guard<std::mutex> lock(instance.queueMutex_);
            return instance.queuedBuffers_.size() + instance.submittedChunkSizes_.size();
        }
    };
}
