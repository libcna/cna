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
#include "SoundBankTestAccess.hpp"
#include "System/InvalidOperationException.hpp"
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

namespace
{
using namespace Microsoft::Xna::Framework::Audio;

struct CountClockBanks
{
    using Clock=std::chrono::steady_clock;
    Clock::time_point now{std::chrono::milliseconds(1000)};
    std::filesystem::path root;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<WaveBank> wave;
    std::unique_ptr<SoundBank> sound;
    explicit CountClockBanks(const char* profile="unlimited")
        : root(std::filesystem::path(CNA_GAP05_FIXTURE_ROOT).parent_path()/"instance-count"/profile)
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

TEST(XactInstanceCountClock, SdkInstanceLimitFadeRetainsPlayingPausedAndCountUntilDeadline)
{
    // Microsoft3.6 native controls establish state8/72/count2 during eviction,
    // state32/count1 after; exact authored deadlines use the FAudio >= predicate.
    for(const char* profile:{"cue_oldest","cat_oldest"})
    for(bool paused:{false,true})
    {
        SCOPED_TRACE(profile);
        SCOPED_TRACE(paused);
        const unsigned fade=std::string(profile)=="cue_oldest"?50:70;
        CountClockBanks banks(profile);auto first=banks.Prepare();first->Play();banks.Tick(100);
        if(paused)first->Pause();
        auto incoming=banks.Prepare();incoming->Play();banks.Tick(120);
        EXPECT_TRUE(first->getIsPlayingProperty());EXPECT_FALSE(first->getIsStoppingProperty());
        EXPECT_EQ(first->getIsPausedProperty(),paused);
        EXPECT_FLOAT_EQ(first->GetVariable("NumCueInstances"),2);
        EXPECT_FLOAT_EQ(incoming->GetVariable("NumCueInstances"),2);
        banks.Tick(100+fade);
        EXPECT_TRUE(first->getIsStoppedProperty());EXPECT_FALSE(first->getIsPausedProperty());
        EXPECT_FLOAT_EQ(first->GetVariable("NumCueInstances"),1);
        EXPECT_FLOAT_EQ(incoming->GetVariable("NumCueInstances"),1);
    }
}


TEST(XactInstanceCountClock, AuthoredTailDoesNotBlockCueOrCategoryFailAdmission)
{
    // Native Microsoft3.6 x86 cue/x64 category controls include state16/count0
    // with a successful new Prepare/Play; other coarse snapshots already reach32.
    for(const char* profile:{"cue_fail","cat_fail_tail"})
    {
        SCOPED_TRACE(profile);CountClockBanks banks(profile);auto live=banks.Prepare();live->Play();
        live->Stop(AudioStopOptions::AsAuthored);banks.Tick(20);
        ASSERT_TRUE(live->getIsStoppingProperty());
        EXPECT_FLOAT_EQ(live->GetVariable("NumCueInstances"),0);
        auto incoming=banks.Prepare();incoming->Play();
        EXPECT_TRUE(incoming->getIsPlayingProperty());
        EXPECT_FLOAT_EQ(incoming->GetVariable("NumCueInstances"),1);
        EXPECT_FLOAT_EQ(live->GetVariable("NumCueInstances"),1);
        banks.Tick(50);EXPECT_TRUE(live->getIsStoppedProperty());
        EXPECT_FLOAT_EQ(incoming->GetVariable("NumCueInstances"),1);
    }
}

TEST(XactInstanceCountClock, AuthoredTailBankOwnershipOutlastsItsAdmissionCount)
{
    for(const char* profile:{"cue_fail","cat_fail_tail"})
    {
        SCOPED_TRACE(profile);CountClockBanks banks(profile);auto live=banks.Prepare();live->Play();
        ASSERT_TRUE(banks.sound->getIsInUseProperty());ASSERT_TRUE(banks.wave->getIsInUseProperty());
        live->Stop(AudioStopOptions::AsAuthored);banks.Tick(20);
        ASSERT_TRUE(live->getIsStoppingProperty());EXPECT_FLOAT_EQ(live->GetVariable("NumCueInstances"),0);
        EXPECT_TRUE(banks.sound->getIsInUseProperty());EXPECT_TRUE(banks.wave->getIsInUseProperty());
        banks.now=CountClockBanks::Clock::time_point(std::chrono::milliseconds(1049));
        EXPECT_TRUE(banks.sound->getIsInUseProperty());EXPECT_TRUE(banks.wave->getIsInUseProperty());
        banks.now=CountClockBanks::Clock::time_point(std::chrono::milliseconds(1050));
        EXPECT_FALSE(banks.sound->getIsInUseProperty());EXPECT_FALSE(banks.wave->getIsInUseProperty());
        EXPECT_TRUE(live->getIsStoppedProperty());
    }
}

TEST(XactInstanceCountClock, PreparedOwnershipAndStopDoNotRequireAPlayingVoice)
{
    CountClockBanks banks("cue_oldest");auto cue=banks.Prepare();
    EXPECT_TRUE(banks.sound->getIsInUseProperty());EXPECT_TRUE(banks.wave->getIsInUseProperty());
    EXPECT_FLOAT_EQ(cue->GetVariable("NumCueInstances"),0);
    EXPECT_EQ(AudioEngineTestAccess::ActiveCueCount(*banks.engine),0u);
    banks.Tick(200);cue->Stop(AudioStopOptions::AsAuthored);
    EXPECT_TRUE(cue->getIsStoppedProperty());EXPECT_FALSE(cue->getIsStoppingProperty());
    EXPECT_FALSE(banks.sound->getIsInUseProperty());EXPECT_FALSE(banks.wave->getIsInUseProperty());
}

TEST(XactInstanceCountClock, SelectedPreparedHandleRetainsItsBanksAndCanPlayLater)
{
    for(const char* profile:{"cue_oldest","cat_oldest"})
    {
        SCOPED_TRACE(profile);CountClockBanks banks(profile);auto p=banks.Prepare();
        auto l=banks.Prepare();l->Play();banks.Tick(100);auto i=banks.Prepare();i->Play();banks.Tick(300);
        EXPECT_TRUE(p->getIsPreparedProperty());EXPECT_TRUE(l->getIsPlayingProperty());
        EXPECT_TRUE(i->getIsPlayingProperty());EXPECT_FLOAT_EQ(p->GetVariable("NumCueInstances"),2);
        l.reset();i.reset();banks.Tick(500);
        EXPECT_FLOAT_EQ(p->GetVariable("NumCueInstances"),0);
        EXPECT_TRUE(banks.sound->getIsInUseProperty());EXPECT_TRUE(banks.wave->getIsInUseProperty());
        p->Play();EXPECT_TRUE(p->getIsPlayingProperty());EXPECT_FLOAT_EQ(p->GetVariable("NumCueInstances"),1);
    }
}

TEST(XactInstanceCountClock, MissingWavePreparationFailureLeavesNoCueRegistration)
{
    CountClockBanks banks;banks.wave.reset();
    EXPECT_THROW((void)banks.Prepare(),System::InvalidOperationException);
    EXPECT_EQ(SoundBankTestAccess::RetainedCueCount(*banks.sound),0u);
    EXPECT_EQ(AudioEngineTestAccess::ActiveCueCount(*banks.engine),0u);
    banks.engine->Update();
    banks.wave=std::make_unique<WaveBank>(banks.engine.get(),(banks.root/"clock.xwb").string());
    auto cue=banks.Prepare();EXPECT_TRUE(cue->getIsPreparedProperty());
    EXPECT_TRUE(banks.sound->getIsInUseProperty());EXPECT_TRUE(banks.wave->getIsInUseProperty());
}

}
