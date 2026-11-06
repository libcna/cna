// SPDX-License-Identifier: MS-PL
// Included inside the isolated offline harness; all callbacks are real selected-adapter callbacks.
namespace Gap04
{
ReverbParameters BusParameters()
{
    return {10, 11, 6, 6, 27, 27, 8, 8, 8, 4, 8, 4, 5, 5000, 0, 0, 0, 0, 1, 100, 100, 0};
}
struct BusHandles
{
    std::vector<std::uint64_t> ids;
    ~BusHandles()
    {
        for (auto id : ids)
            ReleaseReverbBus(id);
    }
};
TEST(XactReverbBus, ThirtyTwoSlotsRefuseOverflowAndRetireWithoutLeaking)
{
    EnsureMixer();
    BusHandles handles;
    for (int i = 0; i < 32; ++i)
        handles.ids.push_back(CreateReverbBus(BusParameters()));
    // Owner-approved bounded registry: no eviction or stealing a live engine's slot.
    EXPECT_THROW(CreateReverbBus(BusParameters()), std::runtime_error);
    const auto old = handles.ids[0];
    ReleaseReverbBus(old);
    handles.ids[0] = 0;
    handles.ids[0] = CreateReverbBus(BusParameters());
    EXPECT_NE(handles.ids[0], old);
    for (int i = 0; i < 100; ++i)
    {
        const auto previous = handles.ids[0];
        ReleaseReverbBus(previous);
        handles.ids[0] = 0;
        handles.ids[0] = CreateReverbBus(BusParameters());
        EXPECT_NE(handles.ids[0], previous);
    }
}
TEST(XactReverbBus, ReleasedHandleCannotSendIntoReusedSlotAndStereoFoldCancels)
{
    EnsureMixer();
    BusHandles buses;
    auto old = CreateReverbBus(BusParameters());
    ReleaseReverbBus(old);
    buses.ids.push_back(CreateReverbBus(BusParameters()));
    std::array<float, 384> source{};
    source[0] = .125f;
    source[1] = -.125f;
    auto audio =
        LoadMixerRawAudio(std::as_bytes(std::span(source)), {44100, 2, MixerSampleFormat::Float32});
    ASSERT_TRUE(audio);
    struct Track
    {
        MixerTrack *value = CreateMixerTrack();
        ~Track() { DestroyMixerTrack(value); }
    } track;
    ASSERT_NE(track.value, nullptr);
    ASSERT_TRUE(SetMixerTrackAudio(track.value, audio.get()));
    SetMixerTrackStereoUnity(track.value);
    SetMixerTrackWetSend(track.value, old, 1);
    ASSERT_TRUE(PlayMixerTrack(track.value));
    auto out = manualDevice->Render(1024);
    ASSERT_EQ(out[0], .125f);
    ASSERT_EQ(out[1], -.125f);
    for (std::size_t i = 2; i < out.size(); ++i)
        ASSERT_EQ(out[i], 0);
    // The valid route also cancels: m=(L+R)/2=0, so the return is exactly zero.
    SetMixerTrackWetSend(track.value, buses.ids[0], 1);
    ASSERT_TRUE(PlayMixerTrack(track.value));
    out = manualDevice->Render(1024);
    ASSERT_EQ(out[0], .125f);
    ASSERT_EQ(out[1], -.125f);
    for (std::size_t i = 2; i < out.size(); ++i)
        ASSERT_EQ(out[i], 0);
}
TEST(XactReverbBus, RemovalWaitsForAnEnteredRealCallback)
{
    EnsureMixer();
    BusHandles buses;
    buses.ids.push_back(CreateReverbBus(BusParameters()));
    std::promise<void> entered, permit, started, finished;
    auto enteredFuture = entered.get_future();
    auto permitFuture = permit.get_future().share();
    auto startedFuture = started.get_future();
    auto finishedFuture = finished.get_future();
    struct Latch
    {
        std::atomic<bool> once = false;
        std::promise<void> *entered;
        std::shared_future<void> permit;
    } latch{{false}, &entered, permitFuture};
    const auto callback = +[](void *p, int, float *, int)
    {
        auto &l = *static_cast<Latch *>(p);
        if (!l.once.exchange(true))
            l.entered->set_value();
        l.permit.wait(); // test-only latch, never installed by the runtime
    };
    ASSERT_TRUE(SetMixerPostMixCallback(callback, &latch));
    std::jthread renderer([&] { manualDevice->Render(64); });
    const auto entry = enteredFuture.wait_for(std::chrono::seconds(2));
    EXPECT_EQ(entry, std::future_status::ready);
    if (entry != std::future_status::ready)
    {
        permit.set_value();
        renderer.join();
        (void)SetMixerPostMixCallback(nullptr, nullptr);
        return;
    }
    const auto id = buses.ids[0];
    buses.ids[0] = 0;
    std::jthread control(
        [&]
        {
            started.set_value();
            ReleaseReverbBus(id);
            finished.set_value();
        });
    startedFuture.wait();
    // This timeout concerns a held callback barrier, not audio scheduling/sample expectations.
    EXPECT_EQ(finishedFuture.wait_for(std::chrono::milliseconds(30)), std::future_status::timeout);
    permit.set_value();
    renderer.join();
    control.join();
    EXPECT_EQ(finishedFuture.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
    ASSERT_TRUE(SetMixerPostMixCallback(nullptr, nullptr));
    ExpectSilence(manualDevice->Render(4096));
}
} // namespace Gap04

#if defined(CNA_AUDIO_PLATFORM_SDL3) && !defined(CNA_GAP01C_CNA_MIXER)
namespace Gap04 {
TEST(XactReverbBus,NativeMixerGenerationRetiresHistoryAndRecreatesEngineRoutes)
{
    // A fresh exec isolates the harness's one-device invariant from other tests.
    ::testing::FLAGS_gtest_death_test_style="threadsafe";
    ASSERT_EXIT({
        Banks b; auto old=b.Play();
        int observerCalls=0;
        const auto observer=+[](void* p,int,float*,int){++*static_cast<int*>(p);};
        ASSERT_TRUE(SetMixerPostMixCallback(observer,&observerCalls));
        manualDevice->Render(192); const int observedBeforeShutdown=observerCalls;
        const auto generation=GetMixerEngineGeneration();
        DestroyMixer(); EXPECT_GT(GetMixerEngineGeneration(),generation);
        EnsureMixer(); EXPECT_EQ(factoryCalls,2);
        ExpectSilence(manualDevice->Render(4096)); // no history from the destroyed native mixer
        EXPECT_EQ(observerCalls,observedBeforeShutdown); // old-generation observer cannot resurrect
        b.engine->Update(); auto fresh=b.Play();
        Impulse(manualDevice->Render(192),P); Tail(manualDevice->Render(4096));
        b.engine->Dispose(); ExpectSilence(manualDevice->Render(4096));
        std::_Exit(::testing::Test::HasFailure()?1:0);
    },::testing::ExitedWithCode(0),"");
}
}
#endif
