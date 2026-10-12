// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiDetail.hpp"

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
using CNA::C::Detail::BufferTooSmall;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CompleteAtOnce;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::CreateOwnedPropertyDictionary;
using CNA::C::Detail::CreateResourceHandle;
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
        return BufferTooSmall("The leaderboard key does not fit the identity's inline capacity.");
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
    if (const CNA_Result result = ValidateVersionedStructure(
            outIdentity, "The LeaderboardIdentity output structure is invalid.");
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
    return CreateResourceHandle<LeaderboardReaderResource>(
        ObjectKind::LeaderboardReader,
        std::make_shared<LeaderboardReader>(std::move(value)),
        outReader);
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

[[nodiscard]] CNA_Result LeaderboardReaderReadFromPivot(
    const CNA_LeaderboardIdentity* const identity,
    const CNA_GamerHandle pivotGamerHandle,
    const int32_t pageSize,
    CNA_LeaderboardReaderHandle* const outReader)
{
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
}

[[nodiscard]] CNA_Result LeaderboardReaderReadFromGamers(
    const CNA_LeaderboardIdentity* const identity,
    const CNA_GamerHandle* const gamers,
    const uint64_t gamerCount,
    const CNA_GamerHandle pivotGamerHandle,
    const int32_t pageSize,
    CNA_LeaderboardReaderHandle* const outReader)
{
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
}

[[nodiscard]] CNA_Result LeaderboardReaderGetEntryAt(
    const CNA_LeaderboardReaderHandle readerHandle,
    const int32_t index,
    CNA_LeaderboardEntryHandle* const outEntry)
{
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
}

[[nodiscard]] CNA_Result LeaderboardEntryGetGamer(
    const CNA_LeaderboardEntryHandle entryHandle,
    CNA_Bool* const outHasGamer,
    CNA_GamerHandle* const outGamer)
{
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
}

[[nodiscard]] CNA_Result LeaderboardEntryGetColumns(
    const CNA_LeaderboardEntryHandle entryHandle,
    CNA_PropertyDictionaryHandle* const outColumns)
{
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
}

[[nodiscard]] CNA_Result LeaderboardEntrySetRatingChangedHookExt(
    const CNA_LeaderboardEntryHandle entryHandle,
    const CNA_GamerAsyncCallback callback,
    void* const context)
{
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
}

} // namespace

// The routes generated from modules/c-api/spec/Leaderboards.toml.
#include "generated/Leaderboards.inc"
