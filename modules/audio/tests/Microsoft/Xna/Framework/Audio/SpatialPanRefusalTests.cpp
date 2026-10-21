// SPDX-License-Identifier: MS-PL
// ISSUE-073: public playback reproduction of the measured XNA refusal order.
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>
#include "Microsoft/Xna/Framework/Audio/AudioChannels.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioEmitter.hpp"
#include "Microsoft/Xna/Framework/Audio/AudioListener.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffect.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundEffectInstance.hpp"
#include "Microsoft/Xna/Framework/Audio/SoundState.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "SoundEffectInstanceTestAccess.hpp"

namespace
{
    using namespace Microsoft::Xna::Framework::Audio;

    class SpatialPanRefusalTest : public testing::TestWithParam<bool>
    {
    protected:
        void SetUp() override
        {
            // A real looping voice removes any dependency on how quickly a short clip ends.
            // A missing configured dummy device is a failure, never an implicit skip.
            const std::vector<unsigned char> pcm(44100 * 2, 0);
            effect = std::make_unique<SoundEffect>(pcm, 44100, AudioChannels::Mono);
        }

        void SetPan(SoundEffectInstance& instance, float value)
        {
            if (GetParam()) instance.setPanProperty(std::move(value));
            else instance.setPanProperty(static_cast<const float&>(value));
        }

        SoundEffectInstance Instance(bool spatial, bool play)
        {
            auto instance = effect->CreateInstance();
            SetPan(instance, -0.25f);
            instance.setIsLoopedProperty(true);
            if (spatial) instance.Apply3D(AudioListener{}, AudioEmitter{});
            if (play)
            {
                instance.Play();
                EXPECT_EQ(instance.getStateProperty(), SoundState::Playing);
            }
            return instance;
        }

        template<class Exception>
        void Refuse(SoundEffectInstance& instance, float value, bool expectedSpatial)
        {
            try
            {
                SetPan(instance, value);
                ADD_FAILURE() << "Pan assignment must be refused";
            }
            catch (const Exception& error)
            {
                EXPECT_EQ(typeid(error), typeid(Exception));
                if constexpr (std::is_same_v<Exception, System::ArgumentOutOfRangeException>)
                {
                    EXPECT_EQ(error.getParamNameProperty(), "value");
                }
                if constexpr (std::is_same_v<Exception, System::ObjectDisposedException>)
                {
                    EXPECT_EQ(error.getObjectNameProperty(), "SoundEffectInstance");
                }
            }
            catch (...)
            {
                ADD_FAILURE() << "Wrong exception: XNA checks disposal, mode, then range";
            }
            EXPECT_FLOAT_EQ(instance.getPanProperty(), -0.25f);
            EXPECT_EQ(SoundEffectInstanceTestAccess::Is3D(instance), expectedSpatial);
        }

        std::unique_ptr<SoundEffect> effect;
    };

    TEST_P(SpatialPanRefusalTest, LiveSpatialValidPanPreservesPriorPan)
    {
        auto instance = Instance(true, true);
        ASSERT_TRUE(SoundEffectInstanceTestAccess::Is3D(instance));
        Refuse<System::InvalidOperationException>(instance, 0.5f, true);
    }

    TEST_P(SpatialPanRefusalTest, LiveSpatialModePrecedesInvalidRange)
    {
        auto instance = Instance(true, true);
        Refuse<System::InvalidOperationException>(instance, 2.0f, true);
    }

    TEST_P(SpatialPanRefusalTest, LiveSpatialModePrecedesNaN)
    {
        auto instance = Instance(true, true);
        Refuse<System::InvalidOperationException>(instance, std::numeric_limits<float>::quiet_NaN(), true);
    }

    TEST_P(SpatialPanRefusalTest, NonSpatialInvalidRangePreservesPriorPan)
    {
        auto instance = Instance(false, true);
        Refuse<System::ArgumentOutOfRangeException>(instance, 2.0f, false);
    }

    TEST_P(SpatialPanRefusalTest, NonSpatialNaNPreservesPriorPan)
    {
        auto instance = Instance(false, true);
        Refuse<System::ArgumentOutOfRangeException>(instance, std::numeric_limits<float>::quiet_NaN(), false);
    }

    TEST_P(SpatialPanRefusalTest, PrepacketInvalidRangeClearsSpatialLatchButPreservesPan)
    {
        auto instance = Instance(true, false);
        ASSERT_TRUE(SoundEffectInstanceTestAccess::Is3D(instance));
        Refuse<System::ArgumentOutOfRangeException>(instance, 2.0f, false);
    }

    TEST_P(SpatialPanRefusalTest, PrepacketNaNClearsSpatialLatchButPreservesPan)
    {
        auto instance = Instance(true, false);
        ASSERT_TRUE(SoundEffectInstanceTestAccess::Is3D(instance));
        Refuse<System::ArgumentOutOfRangeException>(instance, std::numeric_limits<float>::quiet_NaN(), false);
    }

    TEST_P(SpatialPanRefusalTest, DisposedValidPanPreservesPriorState)
    {
        auto instance = Instance(true, true);
        instance.Dispose();
        ASSERT_TRUE(instance.getIsDisposedProperty());
        const bool spatial = SoundEffectInstanceTestAccess::Is3D(instance);
        Refuse<System::ObjectDisposedException>(instance, 0.5f, spatial);
    }

    TEST_P(SpatialPanRefusalTest, DisposalPrecedesInvalidRange)
    {
        auto instance = Instance(true, true);
        instance.Dispose();
        ASSERT_TRUE(instance.getIsDisposedProperty());
        const bool spatial = SoundEffectInstanceTestAccess::Is3D(instance);
        Refuse<System::ObjectDisposedException>(instance, 2.0f, spatial);
    }

    INSTANTIATE_TEST_SUITE_P(BothSetters, SpatialPanRefusalTest, testing::Bool());
}
