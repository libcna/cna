// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include <gtest/gtest.h>

#include "CNA/Internal/Net/ENetLibrary.hpp"

using CNA::Internal::Net::ENetLibrary;

// Task 5.18: EnsureInitialized()'s double-init idempotency (a function-local static InitGuard,
// so enet_initialize() only ever actually runs once per process) was previously only exercised
// incidentally - every other Net test transitively calls this via ENetHostHandle::CreateHost/
// CreateClient, but nothing directly asserted that calling it repeatedly is itself safe.

TEST(ENetLibraryTest, EnsureInitializedDoesNotThrow) {
    EXPECT_NO_THROW(ENetLibrary::EnsureInitialized());
}

TEST(ENetLibraryTest, EnsureInitializedIsIdempotentAcrossManyCalls) {
    for (int i = 0; i < 10; ++i) {
        EXPECT_NO_THROW(ENetLibrary::EnsureInitialized());
    }
}
