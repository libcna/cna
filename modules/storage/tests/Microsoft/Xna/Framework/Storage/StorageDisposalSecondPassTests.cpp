// SPDX-License-Identifier: MS-PL
#include "StorageSecondPassFixture.hpp"
#include <stdexcept>
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
namespace Tq042
{
TEST_F(StorageSecondPassTest, ReentrantDisposeRaisesOnceAndProtectsPropertiesInsideTheEvent)
{
    // XNA Dispose(bool) IL_0000-000a sets the guard before Invoke at IL_002d;
    // get_DisplayName/get_StorageDevice IL_0000-0001 both VerifyNotDisposed.
    int calls = 0;
    const auto token = container->Disposing.Add([&](System::Object* sender, const System::EventArgs& args)
    {
        ++calls;
        EXPECT_EQ(sender, container.get());
        EXPECT_EQ(&args, &System::EventArgs::Empty);
        EXPECT_TRUE(container->getIsDisposedProperty());
        Exact<System::ObjectDisposedException>([&] { (void)container->getDisplayNameProperty(); });
        Exact<System::ObjectDisposedException>([&] { (void)container->getStorageDeviceProperty(); });
        // Bound the call even under a mutant that moves the guard after the event.
        if (calls == 1) container->Dispose();
    });
    container->Dispose();
    container->Dispose();
    container->Disposing.Remove(token);
    EXPECT_EQ(calls, 1);
    SeedUnchanged();
}

TEST_F(StorageSecondPassTest, ThrowingDisposalHandlerLeavesTheContainerDisposedAndStreamUsable)
{
    // XNA Dispose(bool) IL_000a stores true; IL_002d invokes without a catch.
    // The private DisposeOverride IL_0000 is ret: opened streams are not closed.
    struct HandlerError : std::runtime_error { HandlerError() : std::runtime_error("handler") {} };
    auto stream = container->OpenFile("seed.bin", System::IO::FileMode::Open, System::IO::FileAccess::Read);
    int calls = 0;
    const auto token = container->Disposing.Add([&](System::Object*, const System::EventArgs&)
    {
        ++calls;
        throw HandlerError();
    });
    EXPECT_THROW(container->Dispose(), HandlerError);
    // Remove before destruction even under a mutant that loses the disposed guard.
    container->Disposing.Remove(token);
    EXPECT_TRUE(container->getIsDisposedProperty());
    container->Dispose();
    EXPECT_EQ(calls, 1);
    for (const int value : {'s', 'e', 'e', 'd'}) EXPECT_EQ(stream->ReadByte(), value);
    EXPECT_EQ(stream->ReadByte(), -1);
    Exact<System::ObjectDisposedException>([&] { (void)container->getDisplayNameProperty(); });
    stream->Close();
    SeedUnchanged();
}
}
#endif
