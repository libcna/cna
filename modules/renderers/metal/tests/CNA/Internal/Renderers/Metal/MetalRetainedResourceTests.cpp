// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Metal/MetalRetainedResource.hpp"

#include <gtest/gtest.h>

#include <unordered_map>
#include <vector>

using CNA::Internal::Renderers::Metal::EmplaceMetalOwnedResource;
using CNA::Internal::Renderers::Metal::MetalRetainedResource;

namespace
{
    struct FakeResource
    {
        int identifier;
    };

    std::vector<int> lifetimeEvents;

    FakeResource* RetainFakeResource(FakeResource* resource)
    {
        lifetimeEvents.push_back(resource->identifier);
        return resource;
    }

    void ReleaseFakeResource(FakeResource* resource)
    {
        lifetimeEvents.push_back(-resource->identifier);
    }
}

TEST(MetalRetainedResource, RetainsReplacementBeforeReleasingPreviousResource)
{
    lifetimeEvents.clear();
    FakeResource first{1};
    FakeResource second{2};
    {
        MetalRetainedResource<FakeResource*> resource(RetainFakeResource, ReleaseFakeResource);
        resource.Reset(&first);
        resource.Reset(&second);
        EXPECT_EQ(resource.Get(), &second);
        EXPECT_TRUE(resource.HasValue());
    }
    EXPECT_EQ(lifetimeEvents, (std::vector<int>{1, 2, -1, -2}));
}

TEST(MetalRetainedResource, ResetToSameResourceDoesNotDoubleRetainOrRelease)
{
    lifetimeEvents.clear();
    FakeResource value{3};
    {
        MetalRetainedResource<FakeResource*> resource(RetainFakeResource, ReleaseFakeResource);
        resource.Reset(&value);
        resource.Reset(&value);
    }
    EXPECT_EQ(lifetimeEvents, (std::vector<int>{3, -3}));
}

TEST(MetalRetainedResource, ExplicitResetReleasesExactlyOnceAndLeavesDestructorIdle)
{
    lifetimeEvents.clear();
    FakeResource value{4};
    {
        MetalRetainedResource<FakeResource*> resource(RetainFakeResource, ReleaseFakeResource);
        resource.Reset(&value);
        resource.Reset();
        EXPECT_EQ(resource.Get(), nullptr);
        EXPECT_FALSE(resource.HasValue());
    }
    EXPECT_EQ(lifetimeEvents, (std::vector<int>{4, -4}));
}

TEST(MetalRetainedResource, AdoptDoesNotRetainCreateRuleResource)
{
    lifetimeEvents.clear();
    FakeResource value{5};
    {
        MetalRetainedResource<FakeResource*> resource(RetainFakeResource, ReleaseFakeResource);
        resource.Adopt(&value);
        EXPECT_EQ(resource.Get(), &value);
    }
    EXPECT_EQ(lifetimeEvents, (std::vector<int>{-5}));
}

TEST(MetalRetainedResource, ReleaseOwnershipLeavesDestructorIdle)
{
    lifetimeEvents.clear();
    FakeResource value{6};
    FakeResource* released = nullptr;
    {
        MetalRetainedResource<FakeResource*> resource(RetainFakeResource, ReleaseFakeResource);
        resource.Adopt(&value);
        released = resource.ReleaseOwnership();
        EXPECT_FALSE(resource.HasValue());
    }
    EXPECT_EQ(released, &value);
    EXPECT_TRUE(lifetimeEvents.empty());
    ReleaseFakeResource(released);
    EXPECT_EQ(lifetimeEvents, (std::vector<int>{-6}));
}

TEST(MetalRetainedResource, AdoptReplacementReleasesPriorOwnedResourceOnce)
{
    lifetimeEvents.clear();
    FakeResource first{7};
    FakeResource second{8};
    {
        MetalRetainedResource<FakeResource*> resource(RetainFakeResource, ReleaseFakeResource);
        resource.Adopt(&first);
        resource.Adopt(&second);
    }
    EXPECT_EQ(lifetimeEvents, (std::vector<int>{-7, -8}));
}

TEST(MetalRetainedResource, AdoptingSamePointerConsumesIncomingOwnershipUnit)
{
    lifetimeEvents.clear();
    FakeResource value{9};
    {
        MetalRetainedResource<FakeResource*> resource(RetainFakeResource, ReleaseFakeResource);
        resource.Adopt(&value);
        resource.Adopt(&value);
    }
    EXPECT_EQ(lifetimeEvents, (std::vector<int>{-9, -9}));
}

TEST(MetalRetainedResource, CacheInsertionTransfersOwnershipOnlyForNewKey)
{
    lifetimeEvents.clear();
    FakeResource first{10};
    FakeResource duplicate{11};
    std::unordered_map<int, FakeResource*> cache;
    {
        MetalRetainedResource<FakeResource*> owner(RetainFakeResource, ReleaseFakeResource);
        owner.Adopt(&first);
        const auto inserted = EmplaceMetalOwnedResource(cache, 1, owner);
        EXPECT_EQ(inserted->second, &first);
        EXPECT_FALSE(owner.HasValue());
    }
    {
        MetalRetainedResource<FakeResource*> owner(RetainFakeResource, ReleaseFakeResource);
        owner.Adopt(&duplicate);
        const auto existing = EmplaceMetalOwnedResource(cache, 1, owner);
        EXPECT_EQ(existing->second, &first);
        EXPECT_TRUE(owner.HasValue());
    }
    EXPECT_EQ(lifetimeEvents, (std::vector<int>{-11}));
    ReleaseFakeResource(cache.at(1));
    EXPECT_EQ(lifetimeEvents, (std::vector<int>{-11, -10}));
}
