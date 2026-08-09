// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Backends/Metal/MetalRetainedResource.hpp"

#include <gtest/gtest.h>

#include <vector>

using CNA::Internal::Backends::Metal::MetalRetainedResource;

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
