// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <future>
#include <memory>
#include <thread>
#include <utility>
#include "CNA/Internal/ContentOwnership.hpp"

namespace
{
    using CNA::Internal::ContentOwnershipAccess;
    using CNA::Internal::ContentOwnershipLink;

    class ValueResource final : public System::IDisposable, public CNA::Internal::ContentOwnedResource
    {
    public:
        mutable std::shared_ptr<int> native = std::make_shared<int>(42);
        mutable bool disposed = false;
        std::function<void()> onDispose;
        ValueResource() = default;
        ValueResource(const ValueResource& source) : native(source.native), ownership(this, source.ownership)
        { disposed = ownership.IsDisposed(); }
        ValueResource(ValueResource&& source) noexcept : native(std::move(source.native)),
            disposed(source.disposed), ownership(this, std::move(source.ownership)) { }
        ValueResource& operator=(const ValueResource& source)
        {
            if (this != &source)
            {
                native = source.native;
                ownership = source.ownership;
                disposed = ownership.IsDisposed();
            }
            return *this;
        }
        ValueResource& operator=(ValueResource&& source) noexcept
        {
            if (this != &source)
            {
                native = std::move(source.native);
                disposed = source.disposed;
                ownership = std::move(source.ownership);
            }
            return *this;
        }
        void Dispose() override
        {
            if (ownership.DisposeRepresentative()) return;
            if (disposed) return;
            native.reset();
            ownership.MarkDisposed();
            disposed = true;
            const auto callback = onDispose;
            if (callback) callback();
        }
    private:
        friend struct CNA::Internal::ContentOwnershipAccess;
        mutable ContentOwnershipLink ownership{this};
        std::shared_ptr<System::IDisposable> GetContentDisposalRecord() const override
        {
            if (auto record = ownership.Record()) return record;
            auto representative = std::make_shared<ValueResource>(*this);
            return ownership.Capture(representative, representative->ownership);
        }
    public:
        void Detach() { ownership.Detach(); }
        std::shared_ptr<System::IDisposable> Transfer() { return ownership.ReleaseExclusiveOwnership(); }
    };
}

TEST(ContentOwnershipLinkTest, RetainsResourceAndConcreteDisposerAfterReturnedWrapperDies)
{
    std::weak_ptr<int> native;
    std::shared_ptr<System::IDisposable> record;
    {
        ValueResource resource;
        native = resource.native;
        record = ContentOwnershipAccess::Record(resource);
        const auto* representative = dynamic_cast<ValueResource*>(record.get());
        ASSERT_NE(representative, nullptr);
        EXPECT_EQ(*representative->native, 42);
        EXPECT_EQ(record, ContentOwnershipAccess::Record(resource));
    }
    EXPECT_FALSE(native.expired());
    record->Dispose();
    EXPECT_TRUE(native.expired());
}

TEST(ContentOwnershipLinkTest, CopiesAndMovesShareOwningDisposalButNotExplicitDisposal)
{
    ValueResource original;
    const auto native = std::weak_ptr(original.native);
    auto record = ContentOwnershipAccess::Record(original);
    ValueResource copy(original);
    ValueResource moved(std::move(copy));
    ValueResource assigned;
    assigned = moved;
    ValueResource moveAssigned;
    moveAssigned = std::move(assigned);
    original.Dispose();
    EXPECT_TRUE(original.disposed);
    EXPECT_FALSE(moved.disposed);
    EXPECT_FALSE(moveAssigned.disposed);
    EXPECT_FALSE(native.expired());
    record->Dispose();
    EXPECT_TRUE(moved.disposed);
    EXPECT_TRUE(moveAssigned.disposed);
    EXPECT_TRUE(native.expired());
    ValueResource later(moved);
    EXPECT_TRUE(later.disposed);
}

TEST(ContentOwnershipLinkTest, DroppingRecordDoesNotDisposeRemainingWrappersOrLeakTheirResource)
{
    ValueResource resource;
    const auto native = std::weak_ptr(resource.native);
    auto record = ContentOwnershipAccess::Record(resource);
    record.reset();
    EXPECT_FALSE(resource.disposed);
    EXPECT_FALSE(native.expired());
    resource.Dispose();
    EXPECT_TRUE(native.expired());
}

TEST(ContentOwnershipLinkTest, LastExplicitDisposalReleasesResourceDespiteRetainedRecord)
{
    ValueResource resource;
    const auto native = std::weak_ptr(resource.native);
    auto record = ContentOwnershipAccess::Record(resource);
    resource.Dispose();
    EXPECT_TRUE(native.expired());
    EXPECT_NO_THROW(record->Dispose());
}

TEST(ContentOwnershipLinkTest, DetachedWrapperSurvivesOwningDisposal)
{
    ValueResource original;
    const auto native = std::weak_ptr(original.native);
    auto record = ContentOwnershipAccess::Record(original);
    ValueResource independent(original);
    independent.Detach();
    record->Dispose();
    EXPECT_TRUE(original.disposed);
    EXPECT_FALSE(independent.disposed);
    EXPECT_FALSE(native.expired());
    independent.Dispose();
    EXPECT_TRUE(native.expired());
}

TEST(ContentOwnershipLinkTest, DisposalCallbackCanDestroyAnotherWrapper)
{
    ValueResource original;
    const auto native = std::weak_ptr(original.native);
    auto record = ContentOwnershipAccess::Record(original);
    auto victim = std::make_unique<ValueResource>(original);
    ValueResource callbackOwner(original);
    callbackOwner.onDispose = [&] { victim.reset(); };
    record->Dispose();
    EXPECT_EQ(victim, nullptr);
    EXPECT_TRUE(original.disposed);
    EXPECT_TRUE(callbackOwner.disposed);
    EXPECT_TRUE(native.expired());
}

TEST(ContentOwnershipLinkTest, DisposalCallbackCanMoveAnotherWrapper)
{
    ValueResource original;
    auto record = ContentOwnershipAccess::Record(original);
    ValueResource victim(original);
    std::unique_ptr<ValueResource> moved;
    ValueResource callbackOwner(original);
    callbackOwner.onDispose = [&] { moved = std::make_unique<ValueResource>(std::move(victim)); };
    record->Dispose();
    ASSERT_NE(moved, nullptr);
    EXPECT_TRUE(moved->disposed);
    EXPECT_TRUE(original.disposed);
}

TEST(ContentOwnershipLinkTest, ConstWrapperCanBeInvalidatedByItsContentOwner)
{
    const ValueResource resource;
    const auto native = std::weak_ptr(resource.native);
    auto record = ContentOwnershipAccess::Record(resource);
    record->Dispose();
    EXPECT_TRUE(resource.disposed);
    EXPECT_TRUE(native.expired());
}

TEST(ContentOwnershipLinkTest, ConstPolymorphicReferencePreservesConcreteOwnership)
{
    const ValueResource resource;
    const System::IDisposable& erased = resource;
    auto record = ContentOwnershipAccess::Record(erased);
    ASSERT_NE(record, nullptr);
    ASSERT_NE(dynamic_cast<ValueResource*>(record.get()), nullptr);
    ValueResource copied(resource);
    const auto native = std::weak_ptr(resource.native);
    record->Dispose();
    EXPECT_TRUE(resource.disposed);
    EXPECT_TRUE(copied.disposed);
    EXPECT_TRUE(native.expired());
}

TEST(ContentOwnershipLinkTest, ExclusiveTransferDisarmsTheManagersRecord)
{
    ValueResource original;
    auto record = ContentOwnershipAccess::Record(original);
    const auto native = std::weak_ptr(original.native);
    auto retained = original.Transfer();
    ASSERT_NE(retained, nullptr);
    auto* representative = dynamic_cast<ValueResource*>(retained.get());
    ASSERT_NE(representative, nullptr);
    representative->native.reset();
    representative->disposed = true;
    record->Dispose();
    EXPECT_FALSE(original.disposed);
    EXPECT_FALSE(native.expired());
    original.Dispose();
    EXPECT_TRUE(native.expired());
}

TEST(ContentOwnershipLinkTest, TransferRefusesToAbandonAnotherWrappersOwnership)
{
    ValueResource original;
    auto record = ContentOwnershipAccess::Record(original);
    ValueResource another(original);
    EXPECT_EQ(original.Transfer(), nullptr);
    EXPECT_EQ(another.Transfer(), nullptr);
    record->Dispose();
    EXPECT_TRUE(original.disposed);
    EXPECT_TRUE(another.disposed);
}

TEST(ContentOwnershipLinkTest, ConcurrentDistinctWrapperCopiesMovesAndDestructionAreSafe)
{
    ValueResource original;
    const auto native = std::weak_ptr(original.native);
    auto record = ContentOwnershipAccess::Record(original);
    constexpr std::size_t workerCount = 4;
    std::barrier start(static_cast<std::ptrdiff_t>(workerCount));
    std::atomic<int> failures{0};
    std::array<std::thread, workerCount> workers;
    for (auto& worker : workers)
    {
        worker = std::thread([&]
        {
            start.arrive_and_wait();
            for (int i = 0; i < 1000; ++i)
            {
                ValueResource copied(original);
                ValueResource moved(std::move(copied));
                if (!moved.native || *moved.native != 42 || moved.disposed) ++failures;
            }
        });
    }
    for (auto& worker : workers) worker.join();
    EXPECT_EQ(failures.load(), 0);
    EXPECT_FALSE(original.disposed);
    EXPECT_FALSE(native.expired());
    record->Dispose();
    EXPECT_TRUE(original.disposed);
    EXPECT_TRUE(native.expired());
}

TEST(ContentOwnershipLinkTest, DisposalCallbackCanJoinAWorkerCopyingAnotherLiveWrapper)
{
    ValueResource original;
    auto record = ContentOwnershipAccess::Record(original);
    ValueResource callbackOwner(original);
    callbackOwner.onDispose = [&]
    {
        std::promise<void> completed;
        auto finished = completed.get_future();
        std::thread worker([&]
        {
            {
                ValueResource copied(original);
                EXPECT_FALSE(copied.disposed);
                EXPECT_EQ(*copied.native, 42);
            }
            completed.set_value();
        });
        // A lock held over Dispose would deadlock this join. Fail the process
        // instead of hanging the suite or detaching a thread with stack references.
        if (finished.wait_for(std::chrono::seconds(10)) != std::future_status::ready)
        {
            ADD_FAILURE() << "Disposal callback cannot join the resource-copy worker";
            std::abort();
        }
        worker.join();
    };
    EXPECT_NO_THROW(record->Dispose());
    EXPECT_TRUE(original.disposed);
    EXPECT_TRUE(callbackOwner.disposed);
}
