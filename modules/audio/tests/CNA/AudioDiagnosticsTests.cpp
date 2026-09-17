// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>

#if CNA_DIAGNOSTICS_LEVEL >= 1 && defined(SOUND_ENABLED)

#include "CNA/Diagnostics/Diagnostics.hpp"
#include "CNA/Internal/Audio/MixerEngine.hpp"

#include <algorithm>
#include <string_view>

namespace
{
    using CNA::Diagnostics::GetProvider;
    using CNA::Diagnostics::MetricSample;
    using CNA::Diagnostics::Mode;
    using CNA::Diagnostics::SetRuntimeMode;
    using CNA::Diagnostics::Snapshot;
    using CNA::Internal::Audio::CreateMixerTrack;
    using CNA::Internal::Audio::DestroyMixerTrack;
    using CNA::Internal::Audio::MixerTrack;

    [[nodiscard]] std::int64_t MetricValue(const Snapshot& snapshot, std::string_view name)
    {
        const auto found = std::find_if(snapshot.metrics.begin(), snapshot.metrics.end(),
            [name](const MetricSample& metric) { return metric.name == name; });
        return found == snapshot.metrics.end() ? 0 : found->value;
    }

    TEST(AudioDiagnosticsTest, VoiceGaugeAndCreationCounterTrackExactLifecycle)
    {
        ASSERT_TRUE(SetRuntimeMode(Mode::Stats));
        const Snapshot baseline = GetProvider().CaptureSnapshot();
        const std::int64_t baselineVoices = MetricValue(baseline, "Audio/AllocatedVoices");
        const std::int64_t baselineCreations = MetricValue(baseline, "Audio/VoiceCreations");

        MixerTrack* first = CreateMixerTrack();
        ASSERT_NE(first, nullptr);
        MixerTrack* second = CreateMixerTrack();
        if (second == nullptr)
        {
            DestroyMixerTrack(first);
            FAIL() << "the second mixer track could not be created";
        }

        const Snapshot created = GetProvider().CaptureSnapshot();
        EXPECT_EQ(MetricValue(created, "Audio/AllocatedVoices"), baselineVoices + 2);
        EXPECT_EQ(MetricValue(created, "Audio/VoiceCreations"), baselineCreations + 2);

        DestroyMixerTrack(first);
        const Snapshot oneRemaining = GetProvider().CaptureSnapshot();
        EXPECT_EQ(MetricValue(oneRemaining, "Audio/AllocatedVoices"), baselineVoices + 1);
        EXPECT_EQ(MetricValue(oneRemaining, "Audio/VoiceCreations"), baselineCreations + 2);

        DestroyMixerTrack(second);
        const Snapshot after = GetProvider().CaptureSnapshot();
        EXPECT_EQ(MetricValue(after, "Audio/AllocatedVoices"), baselineVoices);
        EXPECT_EQ(MetricValue(after, "Audio/VoiceCreations"), baselineCreations + 2);
    }
}

#endif
