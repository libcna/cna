// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

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
using CNA::C::Detail::CreateOwnedPropertyDictionary;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;

using Microsoft::Xna::Framework::GamerServices::Gamer;
using Microsoft::Xna::Framework::GamerServices::LeaderboardEntry;
using Microsoft::Xna::Framework::GamerServices::LeaderboardIdentity;
using Microsoft::Xna::Framework::GamerServices::LeaderboardKey;
using Microsoft::Xna::Framework::GamerServices::LeaderboardReader;
using Microsoft::Xna::Framework::GamerServices::PropertyDictionary;

constexpr uint32_t StructureVersion = UINT32_C(1);

struct LeaderboardReaderResource final {
    std::shared_ptr<LeaderboardReader> value;
};

// Every entry this ABI publishes is one it owns. The canonical writer's entries are not reachable
// here at all -- see the coverage record for why -- so there is no borrowed kind to keep alive.
struct LeaderboardEntryResource final {
    std::shared_ptr<LeaderboardEntry> owned;

    [[nodiscard]] LeaderboardEntry* Value() const noexcept { return owned.get(); }
};

[[nodiscard]] CNA_Result InvalidInput(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result BorrowReader(
    const CNA_Handle handle,
    std::shared_ptr<LeaderboardReaderResource>* const outReader)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::LeaderboardReader, outReader);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned LeaderboardReader handle is invalid for this call.");
}

[[nodiscard]] CNA_Result BorrowEntry(
    const CNA_Handle handle,
    std::shared_ptr<LeaderboardEntryResource>* const outEntry)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::LeaderboardEntry, outEntry);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned LeaderboardEntry handle is invalid for this call.");
}

[[nodiscard]] CNA_Result ToNativeIdentity(
    const CNA_LeaderboardIdentity* const identity,
    LeaderboardIdentity* const outIdentity)
{
    if (identity == nullptr || identity->struct_size < sizeof(CNA_LeaderboardIdentity) ||
        identity->struct_version != StructureVersion) {
        return InvalidInput("The LeaderboardIdentity structure is invalid.");
    }
    // The inline key is NUL-padded, so a key that fills the buffer with no terminator would have no
    // end; refusing it is what keeps the field a string rather than a fixed blob.
    const void* const terminator =
        std::memchr(identity->key, '\0', sizeof(identity->key));
    if (terminator == nullptr) {
        return InvalidInput("The leaderboard key is not terminated within its inline capacity.");
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
    if (outIdentity == nullptr || outIdentity->struct_size < sizeof(CNA_LeaderboardIdentity) ||
        outIdentity->struct_version != StructureVersion) {
        return InvalidInput("The LeaderboardIdentity output structure is invalid.");
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
    const CNA_Result result =
        GetRuntimeHandles().Create(ObjectKind::LeaderboardReader, resource, outReader);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned LeaderboardReader handle could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

// The canonical `Read` overloads call their own `BeginRead` and `EndRead` and then **never release
// the operation those create** -- unlike `Gamer::GetProfile`, which deletes its own. Doing the same
// two public calls here and releasing the operation performs exactly the work `Read` performs without
// leaking it, which is the deviation this ABI takes and records rather than passing on.
template<typename TBegin>
[[nodiscard]] CNA_Result ReadThroughOperation(TBegin&& begin, CNA_Handle* const outReader)
{
    const std::unique_ptr<System::IAsyncResult> action(begin());
    return PublishReader(LeaderboardReader::EndRead(action.get()), outReader);
}

[[nodiscard]] CNA_Result PublishOwnedEntry(LeaderboardEntry value, CNA_Handle* const outEntry)
{
    const auto resource = std::make_shared<LeaderboardEntryResource>();
    resource->owned = std::make_shared<LeaderboardEntry>(std::move(value));
    const CNA_Result result =
        GetRuntimeHandles().Create(ObjectKind::LeaderboardEntry, resource, outEntry);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned LeaderboardEntry handle could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}


} // namespace

CNA_Result cna_leaderboard_identity_init(
    const CNA_LeaderboardKey key,
    const int32_t gameMode,
    CNA_LeaderboardIdentity* const outIdentity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIdentity == nullptr) {
            return InvalidInput("The LeaderboardIdentity output is null.");
        }
        if (key > CNA_LEADERBOARD_KEY_MAXIMUM) {
            return InvalidInput("The leaderboard key is not a defined identity.");
        }
        // Both canonical creation routes are this one: the overload without a game mode is this one
        // with zero.
        return WriteCIdentity(
            LeaderboardIdentity::Create(static_cast<LeaderboardKey>(key), static_cast<int>(gameMode)),
            outIdentity);
    });
}

CNA_Result cna_leaderboard_reader_read(
    const CNA_LeaderboardIdentity* const identity,
    const int32_t pageStart,
    const int32_t pageSize,
    CNA_LeaderboardReaderHandle* const outReader)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReader == nullptr) {
            return InvalidInput("The LeaderboardReader output handle is null.");
        }
        *outReader = CNA_INVALID_HANDLE;
        LeaderboardIdentity nativeIdentity;
        if (const CNA_Result result = ToNativeIdentity(identity, &nativeIdentity);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReadThroughOperation(
            [&]() {
                return LeaderboardReader::BeginRead(
                    nativeIdentity,
                    static_cast<int>(pageStart),
                    static_cast<int>(pageSize),
                    System::AsyncCallback{},
                    std::any{});
            },
            outReader);
    });
}

CNA_Result cna_leaderboard_reader_read_from_pivot(
    const CNA_LeaderboardIdentity* const identity,
    const CNA_GamerHandle pivotGamerHandle,
    const int32_t pageSize,
    CNA_LeaderboardReaderHandle* const outReader)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outReader == nullptr) {
            return InvalidInput("The LeaderboardReader output handle is null.");
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
            return InvalidInput("The LeaderboardReader output handle is null.");
        }
        *outReader = CNA_INVALID_HANDLE;
        if (gamers == nullptr && gamerCount != UINT64_C(0)) {
            return InvalidInput("The gamer array is null.");
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

CNA_Result cna_leaderboard_reader_destroy(const CNA_LeaderboardReaderHandle readerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<LeaderboardReaderResource> reader;
        if (const CNA_Result result = BorrowReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        reader->value->Dispose();
        const CNA_Result result = GetRuntimeHandles().Release(readerHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned LeaderboardReader handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_leaderboard_reader_get_info(
    const CNA_LeaderboardReaderHandle readerHandle,
    CNA_LeaderboardReaderInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_LeaderboardReaderInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidInput("The LeaderboardReader info output structure is invalid.");
        }
        std::shared_ptr<LeaderboardReaderResource> reader;
        if (const CNA_Result result = BorrowReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_LeaderboardReaderInfo info = {
            sizeof(CNA_LeaderboardReaderInfo),
            StructureVersion,
            static_cast<int32_t>(reader->value->getPageStartProperty()),
            static_cast<int32_t>(reader->value->getTotalLeaderboardSizeProperty()),
            static_cast<int32_t>(reader->value->getEntriesProperty().getCountProperty()),
            reader->value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE,
            reader->value->getCanPageDownProperty() ? CNA_TRUE : CNA_FALSE,
            reader->value->getCanPageUpProperty() ? CNA_TRUE : CNA_FALSE,
            0U
        };
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_leaderboard_reader_get_identity(
    const CNA_LeaderboardReaderHandle readerHandle,
    CNA_LeaderboardIdentity* const outIdentity)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<LeaderboardReaderResource> reader;
        if (const CNA_Result result = BorrowReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ToCIdentity(reader->value->getLeaderboardIdentityProperty(), outIdentity);
    });
}

CNA_Result cna_leaderboard_reader_get_entry_at(
    const CNA_LeaderboardReaderHandle readerHandle,
    const int32_t index,
    CNA_LeaderboardEntryHandle* const outEntry)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEntry == nullptr) {
            return InvalidInput("The LeaderboardEntry output handle is null.");
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
            return InvalidInput("The entry index is outside the reader's page.");
        }
        return PublishOwnedEntry(entries[static_cast<int>(index)], outEntry);
    });
}

CNA_Result cna_leaderboard_reader_page_down(const CNA_LeaderboardReaderHandle readerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<LeaderboardReaderResource> reader;
        if (const CNA_Result result = BorrowReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        reader->value->PageDown();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_leaderboard_reader_page_up(const CNA_LeaderboardReaderHandle readerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<LeaderboardReaderResource> reader;
        if (const CNA_Result result = BorrowReader(readerHandle, &reader);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        reader->value->PageUp();
        return CNA_RESULT_SUCCESS;
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

CNA_Result cna_leaderboard_entry_create_ext(
    const CNA_GamerHandle gamerHandle,
    const int64_t rating,
    const int32_t ranking,
    CNA_LeaderboardEntryHandle* const outEntry)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEntry == nullptr) {
            return InvalidInput("The LeaderboardEntry output handle is null.");
        }
        *outEntry = CNA_INVALID_HANDLE;
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowOptionalGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return PublishOwnedEntry(
            LeaderboardEntry::CreateInternal(
                gamer,
                static_cast<long long>(rating),
                static_cast<int>(ranking)),
            outEntry);
    });
}

CNA_Result cna_leaderboard_entry_destroy(const CNA_LeaderboardEntryHandle entryHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<LeaderboardEntryResource> entry;
        if (const CNA_Result result = BorrowEntry(entryHandle, &entry);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_Result result = GetRuntimeHandles().Release(entryHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned LeaderboardEntry handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_leaderboard_entry_get_info(
    const CNA_LeaderboardEntryHandle entryHandle,
    CNA_LeaderboardEntryInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_LeaderboardEntryInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidInput("The LeaderboardEntry info output structure is invalid.");
        }
        std::shared_ptr<LeaderboardEntryResource> entry;
        if (const CNA_Result result = BorrowEntry(entryHandle, &entry);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_LeaderboardEntryInfo info = {
            sizeof(CNA_LeaderboardEntryInfo),
            StructureVersion,
            static_cast<int32_t>(entry->Value()->getRankingEXTProperty()),
            entry->Value()->getGamerProperty() != nullptr ? CNA_TRUE : CNA_FALSE,
            {0U, 0U, 0U},
            static_cast<int64_t>(entry->Value()->getRatingProperty())
        };
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_leaderboard_entry_set_rating(
    const CNA_LeaderboardEntryHandle entryHandle,
    const int64_t rating)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<LeaderboardEntryResource> entry;
        if (const CNA_Result result = BorrowEntry(entryHandle, &entry);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        entry->Value()->setRatingProperty(static_cast<long long>(rating));
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
            return InvalidInput("The entry gamer output is invalid.");
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
            return InvalidInput("The property dictionary output handle is null.");
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

CNA_Result cna_leaderboard_entry_equals(
    const CNA_LeaderboardEntryHandle entryHandle,
    const CNA_LeaderboardEntryHandle otherHandle,
    CNA_Bool* const outEquals)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEquals == nullptr) {
            return InvalidInput("The entry equality output is null.");
        }
        std::shared_ptr<LeaderboardEntryResource> entry;
        std::shared_ptr<LeaderboardEntryResource> other;
        if (const CNA_Result result = BorrowEntry(entryHandle, &entry);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = BorrowEntry(otherHandle, &other);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // Both canonical equality operators are this one answer; inequality is its negation.
        *outEquals = (*entry->Value() == *other->Value()) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}
