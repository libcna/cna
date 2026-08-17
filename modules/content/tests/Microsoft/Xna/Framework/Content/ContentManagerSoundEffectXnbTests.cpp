// SPDX-License-Identifier: MS-PL
//
// plan_xnb.md XNB-33A: end-to-end M3 milestone test -- content.Load<SoundEffect>("fixture")
// against a real, externally-produced .xnb fixture, going through ContentManager (not a
// standalone parser call).

#include <filesystem>
#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/SoundEffectContentTypeReader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Content/ContentTypeReaderManager.hpp"

using Microsoft::Xna::Framework::Audio::SoundEffect;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Content::ContentTypeReaderManager;

namespace
{
    class ContentManagerSoundEffectXnbTest : public ::testing::Test
    {
    protected:
        void SetUp() override
        {
            ContentTypeReaderManager::ClearTypeCreators();
            CNA::Internal::Xnb::RegisterSoundEffectXnbReader();
        }

        void TearDown() override { ContentTypeReaderManager::ClearTypeCreators(); }
    };
}

// plan_platform.md PLAT-SDL2-8: needs the decoder/mixer engine, which is the SDL3_mixer
// implementation and is absent from the archive for every other CNA_AUDIO_PLATFORM value.
// Without it a SoundEffect reports a zero duration and VideoPlayer opens no audio stream,
// so this case is unobservable there rather than merely untested.
#ifdef SOUND_ENABLED
TEST_F(ContentManagerSoundEffectXnbTest, LoadRealMonoGameFixtureEndToEnd)
{
    // Real, externally-produced fixture (MonoGame's own Tests/Assets/Audio/tone_mono_44khz_16bit.xnb),
    // vendored at tests/assets/xnb/monogame/windows/uncompressed/audio/.
    ContentManager cm(nullptr, "tests/assets/xnb/monogame/windows/uncompressed/audio");

    SoundEffect effect = cm.Load<SoundEffect>("tone_mono_44khz_16bit");

    EXPECT_EQ(effect.getNameProperty(), "tone_mono_44khz_16bit");
    EXPECT_GT(effect.getDurationProperty().getTicksProperty(), 0);
}
#endif  // SOUND_ENABLED
