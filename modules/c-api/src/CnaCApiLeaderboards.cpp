// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiOutput.hpp"

#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardIdentity.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardKey.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp"
#include "Microsoft/Xna/Framework/GamerServices/PropertyDictionary.hpp"

#include <any>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::BorrowAnyGamer;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::CreateOwnedPropertyDictionary;
using CNA::C::Detail::Fail;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::ValidateVersionedStructure;

using Microsoft::Xna::Framework::GamerServices::Gamer;
using Microsoft::Xna::Framework::GamerServices::LeaderboardEntry;
using Microsoft::Xna::Framework::GamerServices::LeaderboardIdentity;
using Microsoft::Xna::Framework::GamerServices::LeaderboardKey;
using Microsoft::Xna::Framework::GamerServices::LeaderboardReader;
using Microsoft::Xna::Framework::GamerServices::PropertyDictionary;

struct LeaderboardReaderResource final {
    std::shared_ptr<LeaderboardReader> value;
};

// Every entry this ABI publishes is one it owns. The canonical writer's entries are not reachable
// here at all -- see the coverage record for why -- so there is no borrowed kind to keep alive.
struct LeaderboardEntryResource final {
    std::shared_ptr<LeaderboardEntry> owned;
    std::shared_ptr<LeaderboardReader> readerOwner;

    [[nodiscard]] LeaderboardEntry* Value() const noexcept { return owned.get(); }
};

[[nodiscard]] CNA_Result BorrowReader(
    const CNA_Handle handle,
    std::shared_ptr<LeaderboardReaderResource>* const outReader)
{
    return LookupHandle(handle, ObjectKind::LeaderboardReader, outReader);
}

[[nodiscard]] CNA_Result BorrowEntry(
    const CNA_Handle handle,
    std::shared_ptr<LeaderboardEntryResource>* const outEntry)
{
    return LookupHandle(handle, ObjectKind::LeaderboardEntry, outEntry);
}

[[nodiscard]] CNA_Result ToNativeIdentity(
    const CNA_LeaderboardIdentity* const identity,
    LeaderboardIdentity* const outIdentity)
{
    if (const CNA_Result result =
            ValidateVersionedStructure(identity, "The LeaderboardIdentity structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    // The inline key is NUL-padded, so a key that fills the buffer with no terminator would have no
    // end; refusing it is what keeps the field a string rather than a fixed blob.
    const void* const terminator =
        std::memchr(identity->key, '\0', sizeof(identity->key));
    if (terminator == nullptr) {
        return InvalidArgument("The leaderboard key is not terminated within its inline capacity.");
    }
    outIdentity->setKeyProperty(std::string(identity->key));
    outIdentity->setGameModeProperty(static_cast<int>(identity->game_mode));
    return CNA_RESULT_SUCCESS;
}

// An `_init` route writes the whole structure including its header, so it must not demand one the
// caller has not filled in yet; a route that reads into a caller-initialized structure validates
// first and then writes through this.
[[nodiscard]] CNA_Result WriteCIdentity(
    const LeaderboardIdentity& identity,
    CNA_LeaderboardIdentity* const outIdentity)
{
    const std::string key = identity.getKeyProperty();
    if (key.size() >= sizeof(outIdentity->key)) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The leaderboard key does not fit the identity's inline capacity.");
    }
    CNA_LeaderboardIdentity value = {
        sizeof(CNA_LeaderboardIdentity),
        StructureVersion,
        static_cast<int32_t>(identity.getGameModeProperty()),
        {0}
    };
    if (!key.empty()) {
        std::memcpy(value.key, key.data(), key.size());
    }
    *outIdentity = value;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToCIdentity(
    const LeaderboardIdentity& identity,
    CNA_LeaderboardIdentity* const outIdentity)
{
    if (const CNA_Result result =
            ValidateVersionedStructure(outIdentity, "The LeaderboardIdentity output structure is invalid.");
        result != CNA_RESULT_SUCCESS) {
        return result;
    }
    return WriteCIdentity(identity, outIdentity);
}

[[nodiscard]] CNA_Result BorrowOptionalGamer(const CNA_Handle handle, Gamer** const outGamer)
{
    *outGamer = nullptr;
    if (handle == CNA_INVALID_HANDLE) {
        return CNA_RESULT_SUCCESS;
    }
    return BorrowAnyGamer(handle, outGamer);
}

[[nodiscard]] CNA_Result PublishReader(LeaderboardReader value, CNA_Handle* const outReader)
{
    const auto resource = std::make_shared<LeaderboardReaderResource>();
    resource->value = std::make_shared<LeaderboardReader>(std::move(value));
    return CreateHandle(ObjectKind::LeaderboardReader, resource, outReader);
}

// The C ABI exposes a completed read and owns the underlying queued Begin/End operation.
template<typename TBegin>
[[nodiscard]] CNA_Result ReadThroughOperation(TBegin&& begin, CNA_Handle* const outReader)
{
    const std::unique_ptr<System::IAsyncResult> action(begin());
    return PublishReader(LeaderboardReader::EndRead(action.get()), outReader);
}

[[nodiscard]] CNA_Result PublishOwnedEntry(LeaderboardEntry value, CNA_Handle* const outEntry, std::shared_ptr<LeaderboardReader> readerOwner = {})
{
    const auto resource = std::make_shared<LeaderboardEntryResource>();
    resource->owned = std::make_shared<LeaderboardEntry>(std::move(value));
    resource->readerOwner = std::move(readerOwner);
    return CreateHandle(ObjectKind::LeaderboardEntry, resource, outEntry);
}

} // namespace

CNA_Result cna_leaderboard_reader_read_from_pivot(
    const CNA_LeaderboardIdentity* const identity,
    const CNA_GamerHandle pivotGamerHandle,
    const int32_t pageSize,
    CNA_LeaderboardReaderHandle* const outReader)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReader == nullptr) {
            return InvalidArgument("The LeaderboardReader output handle is null.");
        }
        *outReader = CNA_INVALID_HANDLE;
        LeaderboardIdentity nativeIdentity;
        if (const CNA_Result result = ToNativeIdentity(identity, &nativeIdentity);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Gamer* pivot = nullptr;
        if (const CNA_Result result = BorrowOptionalGamer(pivotGamerHandle, &pivot);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReadThroughOperation(
            [&]() {
                return LeaderboardReader::BeginRead(
                    nativeIdentity,
                    pivot,
                    static_cast<int>(pageSize),
                    System::AsyncCallback{},
                    std::any{});
            },
            outReader);
    });
}

CNA_Result cna_leaderboard_reader_read_from_gamers(
    const CNA_LeaderboardIdentity* const identity,
    const CNA_GamerHandle* const gamers,
    const uint64_t gamerCount,
    const CNA_GamerHandle pivotGamerHandle,
    const int32_t pageSize,
    CNA_LeaderboardReaderHandle* const outReader)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReader == nullptr) {
            return InvalidArgument("The LeaderboardReader output handle is null.");
        }
        *outReader = CNA_INVALID_HANDLE;
        if (gamers == nullptr && gamerCount != UINT64_C(0)) {
            return InvalidArgument("The gamer array is null.");
        }
        LeaderboardIdentity nativeIdentity;
        if (const CNA_Result result = ToNativeIdentity(identity, &nativeIdentity);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::vector<Gamer*> nativeGamers;
        nativeGamers.reserve(static_cast<std::size_t>(gamerCount));
        for (uint64_t index = UINT64_C(0); index < gamerCount; ++index) {
            Gamer* gamer = nullptr;
            if (const CNA_Result result = BorrowAnyGamer(gamers[index], &gamer);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            nativeGamers.push_back(gamer);
        }
        Gamer* pivot = nullptr;
        if (const CNA_Result result = BorrowOptionalGamer(pivotGamerHandle, &pivot);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReadThroughOperation(
            [&]() {
                return LeaderboardReader::BeginRead(
                    nativeIdentity,
                    nativeGamers,
                    pivot,
                    static_cast<int>(pageSize),
                    System::AsyncCallback{},
                    std::any{});
            },
            outReader);
    });
}

CNA_Result cna_leaderboard_reader_begin_read(
    const CNA_LeaderboardIdentity* const identity,
    const int32_t pageStart,
    const int32_t pageSize,
    const CNA_GamerAsyncCallback callback,
    void* const context,
    CNA_LeaderboardReaderHandle* const outReader)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result =
                cna_leaderboard_reader_read(identity, pageStart, pageSize, outReader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (callback != nullptr) {
            callback(context);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_leaderboard_reader_begin_read_from_pivot(
    const CNA_LeaderboardIdentity* const identity,
    const CNA_GamerHandle pivotGamerHandle,
    const int32_t pageSize,
    const CNA_GamerAsyncCallback callback,
    void* const context,
    CNA_LeaderboardReaderHandle* const outReader)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = cna_leaderboard_reader_read_from_pivot(
                identity,
                pivotGamerHandle,
                pageSize,
                outReader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (callback != nullptr) {
            callback(context);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_leaderboard_reader_begin_read_from_gamers(
    const CNA_LeaderboardIdentity* const identity,
    const CNA_GamerHandle* const gamers,
    const uint64_t gamerCount,
    const CNA_GamerHandle pivotGamerHandle,
    const int32_t pageSize,
    const CNA_GamerAsyncCallback callback,
    void* const context,
    CNA_LeaderboardReaderHandle* const outReader)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = cna_leaderboard_reader_read_from_gamers(
                identity,
                gamers,
                gamerCount,
                pivotGamerHandle,
                pageSize,
                outReader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (callback != nullptr) {
            callback(context);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_leaderboard_reader_get_entry_at(
    const CNA_LeaderboardReaderHandle readerHandle,
    const int32_t index,
    CNA_LeaderboardEntryHandle* const outEntry)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEntry == nullptr) {
            return InvalidArgument("The LeaderboardEntry output handle is null.");
        }
        *outEntry = CNA_INVALID_HANDLE;
        std::shared_ptr<LeaderboardReaderResource> reader;
        if (const CNA_Result result = BorrowReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical entry list is answered by value, so what a reader hands out is a snapshot
        // whatever this ABI does with it.
        const auto entries = reader->value->getEntriesProperty();
        if (index < 0 || index >= entries.getCountProperty()) {
            return InvalidArgument("The entry index is outside the reader's page.");
        }
        return PublishOwnedEntry(entries[static_cast<int>(index)], outEntry, reader->value);
    });
}

CNA_Result cna_leaderboard_reader_begin_page_down(
    const CNA_LeaderboardReaderHandle readerHandle,
    const CNA_GamerAsyncCallback callback,
    void* const context)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = cna_leaderboard_reader_page_down(readerHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (callback != nullptr) {
            callback(context);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_leaderboard_reader_begin_page_up(
    const CNA_LeaderboardReaderHandle readerHandle,
    const CNA_GamerAsyncCallback callback,
    void* const context)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = cna_leaderboard_reader_page_up(readerHandle);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (callback != nullptr) {
            callback(context);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_leaderboard_entry_get_gamer(
    const CNA_LeaderboardEntryHandle entryHandle,
    CNA_Bool* const outHasGamer,
    CNA_GamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasGamer == nullptr || outGamer == nullptr) {
            return InvalidArgument("The entry gamer output is invalid.");
        }
        *outHasGamer = CNA_FALSE;
        std::shared_ptr<LeaderboardEntryResource> entry;
        if (const CNA_Result result = BorrowEntry(entryHandle, &entry);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Gamer* const gamer = entry->Value()->getGamerProperty();
        if (gamer == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        if (const CNA_Result result =
                CNA::C::Detail::CreateBorrowedGamer(gamer, entry, outGamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasGamer = CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_leaderboard_entry_get_columns(
    const CNA_LeaderboardEntryHandle entryHandle,
    CNA_PropertyDictionaryHandle* const outColumns)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outColumns == nullptr) {
            return InvalidArgument("The property dictionary output handle is null.");
        }
        *outColumns = CNA_INVALID_HANDLE;
        std::shared_ptr<LeaderboardEntryResource> entry;
        if (const CNA_Result result = BorrowEntry(entryHandle, &entry);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The columns are the entry's own, so the dictionary handle aliases them and keeps the entry
        // resource alive for as long as it names them; writing through it changes the entry.
        PropertyDictionary* const columns = &entry->Value()->getColumnsProperty();
        return CreateOwnedPropertyDictionary(
            std::shared_ptr<PropertyDictionary>(
                std::shared_ptr<void>(entry),
                columns),
            outColumns);
    });
}

CNA_Result cna_leaderboard_entry_set_rating_changed_hook_ext(
    const CNA_LeaderboardEntryHandle entryHandle,
    const CNA_GamerAsyncCallback callback,
    void* const context)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<LeaderboardEntryResource> entry;
        if (const CNA_Result result = BorrowEntry(entryHandle, &entry);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (callback == nullptr) {
            entry->Value()->SetOnRatingChangedHookEXT(std::function<void()>{});
            return CNA_RESULT_SUCCESS;
        }
        entry->Value()->SetOnRatingChangedHookEXT([callback, context]() { callback(context); });
        return CNA_RESULT_SUCCESS;
    });
}

// The mechanically regular routes of this family, generated from modules/c-api/spec/Leaderboards.toml.
#include "generated/Leaderboards.inc"
