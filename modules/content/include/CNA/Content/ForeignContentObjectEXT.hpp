// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace CNA::Content
{
    /**
     * @brief CNAEXT carrier for an asset produced by a content type reader outside this library.
     *
     * XNA's content pipeline is extensible by design: a game declares a custom type, ships a
     * `ContentTypeReader` for it, and `ContentManager.Load<T>` finds it. Every part of that works
     * here except the type -- a caller reaching CNA through the C ABI, or through a language
     * binding above it, cannot name a C++ type for `T` and cannot author a class deriving from
     * `ContentTypeReader<T>`. This is the `T` such a caller uses instead.
     *
     * It holds one opaque pointer whose meaning belongs entirely to whoever produced it. CNA
     * never dereferences it, never copies what it points at, and **never frees it**: the reader
     * that returned it owns it, and its own lifetime rules apply. What CNA does do is cache it,
     * exactly as it caches any other asset, so two `Load` calls for the same asset name answer
     * the same pointer rather than reading the file twice -- which is what XNA's caching
     * guarantees for a reference type.
     *
     * Copyable by design: `std::any` requires it, and copying the handle is not copying the
     * object.
     */
    struct CNAEXT ForeignContentObjectEXT
    {
        /** @brief The opaque object the producing reader returned. */
        void* value = nullptr;
    };
}
