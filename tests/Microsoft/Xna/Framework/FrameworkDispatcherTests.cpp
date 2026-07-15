// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/FrameworkDispatcher.hpp"
#include "Microsoft/Xna/Framework/Audio/DynamicSoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "System/Environment.hpp"
#include "System/EventArgs.hpp"
#include "System/Object.hpp"

#include <chrono>
#include <cstdlib>
#include <future>
#include <thread>
#include <vector>

using Microsoft::Xna::Framework::FrameworkDispatcher;
using Microsoft::Xna::Framework::Audio::AudioChannels;
using Microsoft::Xna::Framework::Audio::DynamicSoundEffectInstance;

TEST(FrameworkDispatcherTest, UpdateDoesNotThrowWithNoStreams)
{
    FrameworkDispatcher::Streams.clear();
    FrameworkDispatcher::ActiveSongChanged = false;
    FrameworkDispatcher::MediaStateChanged = false;
    EXPECT_NO_THROW(FrameworkDispatcher::Update());
}

TEST(FrameworkDispatcherTest, InitialStateIsEmpty)
{
    EXPECT_FALSE(FrameworkDispatcher::ActiveSongChanged);
    EXPECT_FALSE(FrameworkDispatcher::MediaStateChanged);
}

// P11-DISPATCH-001: a realistic XNA usage pattern is disposing a DynamicSoundEffectInstance from
// inside its own BufferNeeded handler once no more data will be provided. Update() (called from
// this loop) synchronously raises BufferNeeded, and Dispose() -> Stop() -> StopInternal() locks
// FrameworkDispatcher::StreamsMutex to remove itself from Streams -- if Update() itself still
// held that same (non-recursive) mutex while calling instance->Update(), this would self-deadlock
// the calling thread the first time real game code did this. Verified with a bounded wait rather
// than a bare call, since a regression here would hang forever, not throw or assert.
TEST(FrameworkDispatcherTest, UpdateDoesNotDeadlockWhenBufferNeededDisposesTheInstance)
{
    System::Environment::SetEnvironmentVariable("SDL_AUDIODRIVER", "dummy");
    FrameworkDispatcher::Streams.clear();

    auto instance = std::make_unique<DynamicSoundEffectInstance>(44100, AudioChannels::Stereo);
    std::vector<unsigned char> pcm(4 * 256, 0); // 256 stereo S16 frames of silence
    instance->SubmitBuffer(pcm);
    try
    {
        instance->Play();
    }
    catch (...)
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }
    if (FrameworkDispatcher::Streams.empty())
    {
        GTEST_SKIP() << "no audio device (dummy driver unavailable)";
    }

    DynamicSoundEffectInstance* raw = instance.get();
    raw->BufferNeeded += [raw](System::Object*, const System::EventArgs&)
    {
        raw->Dispose();
    };

    std::promise<void> done;
    std::future<void> future = done.get_future();
    std::thread worker([&done]()
    {
        FrameworkDispatcher::Update();
        done.set_value();
    });
    worker.detach(); // never joined: if this hangs, only the leaked thread blocks, not this test

    const auto status = future.wait_for(std::chrono::seconds(2));
    EXPECT_EQ(status, std::future_status::ready)
        << "FrameworkDispatcher::Update() did not return -- likely deadlocked on StreamsMutex";

    if (status != std::future_status::ready)
    {
        // The instance may still be referenced by the stuck worker thread; leak it deliberately
        // rather than risk a use-after-free by destructing out from under that thread.
        instance.release();
    }
}
