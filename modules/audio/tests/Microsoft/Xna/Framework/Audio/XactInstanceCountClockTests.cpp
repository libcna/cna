// SPDX-License-Identifier: MS-PL
// SDK count-cache checks also run with NULL audio. They keep future markers pending;
// NULL voices do not supply real EOF, and no sample-output claim is made here.
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Audio/AudioEngine.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundBank.hpp"
#include "Microsoft/Xna/Framework/Audio/WaveBank.hpp"
#include "Microsoft/Xna/Framework/Audio/Cue.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "AudioEngineTestAccess.hpp"
#include <chrono>
#include <filesystem>
#include <memory>

namespace
{
using namespace Microsoft::Xna::Framework::Audio;

struct CountClockBanks
{
    using Clock=std::chrono::steady_clock;
    Clock::time_point now{std::chrono::milliseconds(1000)};
    std::filesystem::path root=std::filesystem::path(CNA_GAP05_FIXTURE_ROOT).parent_path()/
        "instance-count/unlimited";
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<WaveBank> wave;
    std::unique_ptr<SoundBank> sound;
    CountClockBanks()
    {
        engine=std::make_unique<AudioEngine>((root/"clock.xgs").string());
        AudioEngineTestAccess::SetEventClock(*engine,[this]{return now;});
        wave=std::make_unique<WaveBank>(engine.get(),(root/"clock.xwb").string());
        sound=std::make_unique<SoundBank>(engine.get(),(root/"clock.xsb").string());
    }
    std::unique_ptr<Cue> Prepare(const char* name="ClockCue")
    { return std::unique_ptr<Cue>(sound->GetCue(name)); }
    void Tick(unsigned ms)
    { now=Clock::time_point(std::chrono::milliseconds(1000+ms));engine->Update(); }
};

TEST(XactInstanceCountClock, SdkCachePublishesOnlyDefinitionChangesAcrossPreparedPausedAndStoppedHandles)
{
    // Microsoft3.6 x86/x64 scope probes: fresh0 despite bank1, then broadcast
    // to every retained same-definition handle on1->2->1->0->1.
    CountClockBanks banks;auto first=banks.Prepare();
    EXPECT_FLOAT_EQ(first->GetVariable("NumCueInstances"),0);first->Play();
    EXPECT_FLOAT_EQ(first->GetVariable("NumCueInstances"),1);
    auto later=banks.Prepare();banks.Tick(150);
    EXPECT_FLOAT_EQ(later->GetVariable("NumCueInstances"),0);
    auto disposable=banks.Prepare();disposable->Dispose();
    EXPECT_FLOAT_EQ(later->GetVariable("NumCueInstances"),0);
    auto foreign=banks.Prepare("OtherCue");foreign->Play();
    auto secondBank=std::make_unique<SoundBank>(banks.engine.get(),(banks.root/"clock.xsb").string());
    std::unique_ptr<Cue> second(secondBank->GetCue("ClockCue"));second->Play();
    EXPECT_FLOAT_EQ(foreign->GetVariable("NumCueInstances"),1);
    EXPECT_FLOAT_EQ(second->GetVariable("NumCueInstances"),1);
    EXPECT_FLOAT_EQ(later->GetVariable("NumCueInstances"),0);
    first->Pause();auto incoming=banks.Prepare();incoming->Play();
    EXPECT_TRUE(first->getIsPausedProperty());
    EXPECT_FLOAT_EQ(first->GetVariable("NumCueInstances"),2);
    EXPECT_FLOAT_EQ(later->GetVariable("NumCueInstances"),2);
    EXPECT_FLOAT_EQ(foreign->GetVariable("NumCueInstances"),1);
    EXPECT_FLOAT_EQ(second->GetVariable("NumCueInstances"),1);
    incoming->Stop(AudioStopOptions::Immediate);
    EXPECT_FLOAT_EQ(incoming->GetVariable("NumCueInstances"),1);
    first->Resume();first->Stop(AudioStopOptions::Immediate);
    EXPECT_FLOAT_EQ(later->GetVariable("NumCueInstances"),0);
    auto replacement=banks.Prepare();replacement->Play();
    EXPECT_FLOAT_EQ(first->GetVariable("NumCueInstances"),1);
    EXPECT_FLOAT_EQ(later->GetVariable("NumCueInstances"),1);
}

TEST(XactInstanceCountClock, DirectCueDisposalAndBankCascadeKeepLifetimeValidation)
{
    CountClockBanks banks;auto prepared=banks.Prepare();auto live=banks.Prepare();
    live->Play();EXPECT_FLOAT_EQ(prepared->GetVariable("NumCueInstances"),1);
    live->Dispose();EXPECT_FLOAT_EQ(prepared->GetVariable("NumCueInstances"),0);
    auto other=banks.Prepare();other->Play();
    EXPECT_FLOAT_EQ(prepared->GetVariable("NumCueInstances"),1);
    banks.sound->Dispose();
    EXPECT_TRUE(prepared->getIsDisposedProperty());EXPECT_TRUE(other->getIsDisposedProperty());
    EXPECT_THROW((void)prepared->GetVariable("NumCueInstances"),System::ObjectDisposedException);
    EXPECT_THROW((void)prepared->GetVariable(""),System::ArgumentNullException);
}
}
