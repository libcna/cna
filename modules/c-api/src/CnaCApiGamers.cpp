// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"

#include "Microsoft/Xna/Framework/Audio/Microphone.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPresence.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerProfile.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInEventArgs.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedOutEventArgs.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "System/EventHandler.hpp"

#include <any>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CreateBorrowedSignedInGamer;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::TryBorrowSignedInGamerQuietly;

using Microsoft::Xna::Framework::PlayerIndex;
using Microsoft::Xna::Framework::Audio::Microphone;
using Microsoft::Xna::Framework::GamerServices::FriendCollection;
using Microsoft::Xna::Framework::GamerServices::FriendGamer;
using Microsoft::Xna::Framework::GamerServices::Gamer;
using Microsoft::Xna::Framework::GamerServices::GamerPresence;
using Microsoft::Xna::Framework::GamerServices::GamerPrivileges;
using Microsoft::Xna::Framework::GamerServices::GamerProfile;
using Microsoft::Xna::Framework::GamerServices::SignedInEventArgs;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;
using Microsoft::Xna::Framework::GamerServices::SignedInGamerCollection;
using Microsoft::Xna::Framework::GamerServices::SignedOutEventArgs;

constexpr uint32_t StructureVersion = UINT32_C(1);

// A gamer that is not the local signed-in gamer. The friend pointer is non-null exactly when the
// canonical object is a friend, which is what lets the friend-only routes refuse an ordinary gamer
// instead of reinterpreting one.
struct GamerResource final {
    std::shared_ptr<Gamer> value;
    FriendGamer* friendGamer = nullptr;
};

struct GamerProfileResource final {
    std::shared_ptr<GamerProfile> value;
};

// The canonical collection stores pointers it does not own, so the C collection keeps every gamer
// resource it holds alive and keeps their handles beside them: a borrowed handle handed back from an
// index or a cursor has to name the same object the collection stores.
struct GamerCollectionResource final {
    std::shared_ptr<FriendCollection> friends;
    std::vector<std::shared_ptr<GamerResource>> retained;
    std::vector<CNA_Handle> handles;
};

struct GamerEnumeratorResource final {
    std::shared_ptr<GamerCollectionResource> collection;
    int position = -1;
};

class GamerRegistrationBase {
public:
    GamerRegistrationBase() = default;
    GamerRegistrationBase(const GamerRegistrationBase&) = delete;
    GamerRegistrationBase& operator=(const GamerRegistrationBase&) = delete;
    virtual ~GamerRegistrationBase() = default;
};

template<typename TEventArgs>
class GamerRegistration final : public GamerRegistrationBase {
public:
    using Source = System::EventHandler<TEventArgs>;
    using Token = typename Source::Token;

    GamerRegistration(Source* const source, const Token token)
        : source_(source)
        , token_(token)
    {
    }

    ~GamerRegistration() override
    {
        source_->Remove(token_);
    }

private:
    Source* source_;
    Token token_;
};

[[nodiscard]] CNA_Result InvalidInput(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_ARGUMENT, CNA_ERROR_CATEGORY_ARGUMENT, message);
}

[[nodiscard]] CNA_Result InvalidState(const char* const message)
{
    return Fail(CNA_RESULT_INVALID_STATE, CNA_ERROR_CATEGORY_STATE, message);
}

[[nodiscard]] CNA_Result GetGamerResource(
    const CNA_Handle handle,
    std::shared_ptr<GamerResource>* const outGamer)
{
    return GetRuntimeHandles().Get(handle, ObjectKind::Gamer, outGamer);
}

// Every `cna_gamer_*` route reaches the canonical base, whichever handle kind named it.
[[nodiscard]] CNA_Result BorrowGamerBase(const CNA_Handle handle, Gamer** const outGamer)
{
    std::shared_ptr<GamerResource> gamer;
    if (GetGamerResource(handle, &gamer) == CNA_RESULT_SUCCESS) {
        *outGamer = gamer->value.get();
        return CNA_RESULT_SUCCESS;
    }
    SignedInGamer* signedInGamer = nullptr;
    if (const CNA_Result result = TryBorrowSignedInGamerQuietly(handle, &signedInGamer);
        result == CNA_RESULT_SUCCESS) {
        *outGamer = signedInGamer;
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        CNA_RESULT_INVALID_HANDLE,
        CNA_ERROR_CATEGORY_HANDLE,
        "The handle does not name a gamer this call can use.");
}

[[nodiscard]] CNA_Result BorrowFriend(
    const CNA_Handle handle,
    std::shared_ptr<GamerResource>* const outGamer,
    FriendGamer** const outFriend)
{
    if (const CNA_Result result = GetGamerResource(handle, outGamer);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned Gamer handle is invalid for this call.");
    }
    if ((*outGamer)->friendGamer == nullptr) {
        return InvalidState("The gamer handle does not name a friend.");
    }
    *outFriend = (*outGamer)->friendGamer;
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowSignedIn(const CNA_Handle handle, SignedInGamer** const outGamer)
{
    if (const CNA_Result result = TryBorrowSignedInGamerQuietly(handle, outGamer);
        result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned SignedInGamer handle is invalid for this call.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result BorrowProfile(
    const CNA_Handle handle,
    std::shared_ptr<GamerProfileResource>* const outProfile)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::GamerProfile, outProfile);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned GamerProfile handle is invalid for this call.");
}

[[nodiscard]] CNA_Result BorrowCollection(
    const CNA_Handle handle,
    std::shared_ptr<GamerCollectionResource>* const outCollection)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::GamerCollection, outCollection);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned gamer collection handle is invalid for this call.");
}

[[nodiscard]] CNA_Result BorrowEnumerator(
    const CNA_Handle handle,
    std::shared_ptr<GamerEnumeratorResource>* const outEnumerator)
{
    const CNA_Result result =
        GetRuntimeHandles().Get(handle, ObjectKind::GamerEnumerator, outEnumerator);
    if (result == CNA_RESULT_SUCCESS) {
        return CNA_RESULT_SUCCESS;
    }
    return Fail(
        result,
        ErrorCategoryForResult(result),
        "The owned gamer enumerator handle is invalid for this call.");
}

[[nodiscard]] CNA_Result CopyGamerText(
    const std::string& value,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    if (outBytes == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
        return InvalidInput("The gamer text output is invalid.");
    }
    *outBytes = value.size();
    if (capacity < value.size()) {
        return Fail(
            CNA_RESULT_BUFFER_TOO_SMALL,
            CNA_ERROR_CATEGORY_RANGE,
            "The destination capacity is smaller than the gamer text.");
    }
    if (!value.empty()) {
        std::memcpy(destination, value.data(), value.size());
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result ToNativeText(
    const CNA_StringView view,
    const char* const message,
    std::string* const outText)
{
    if (view.data == nullptr && view.byte_length != UINT64_C(0)) {
        return InvalidInput(message);
    }
    outText->assign(
        view.data == nullptr ? "" : view.data,
        static_cast<std::size_t>(view.byte_length));
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] bool TryMapPlayerIndex(
    const CNA_PlayerIndex index,
    PlayerIndex* const outIndex) noexcept
{
    if (index > CNA_PLAYER_INDEX_FOUR) {
        return false;
    }
    *outIndex = static_cast<PlayerIndex>(index);
    return true;
}

[[nodiscard]] CNA_Result PublishGamer(
    std::shared_ptr<Gamer> value,
    FriendGamer* const friendGamer,
    CNA_Handle* const outGamer)
{
    const auto resource = std::make_shared<GamerResource>();
    resource->value = std::move(value);
    resource->friendGamer = friendGamer;
    const CNA_Result result = GetRuntimeHandles().Create(ObjectKind::Gamer, resource, outGamer);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned Gamer handle could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result PublishProfile(
    std::shared_ptr<GamerProfile> value,
    CNA_Handle* const outProfile)
{
    const auto resource = std::make_shared<GamerProfileResource>();
    resource->value = std::move(value);
    const CNA_Result result =
        GetRuntimeHandles().Create(ObjectKind::GamerProfile, resource, outProfile);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The owned GamerProfile handle could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

[[nodiscard]] CNA_Result PublishRegistration(
    std::shared_ptr<GamerRegistrationBase> registration,
    CNA_Handle* const outRegistration)
{
    const CNA_Result result = GetRuntimeHandles().Create(
        ObjectKind::GamerEventRegistration,
        std::move(registration),
        outRegistration);
    if (result != CNA_RESULT_SUCCESS) {
        return Fail(
            result,
            ErrorCategoryForResult(result),
            "The gamer-services registration could not be created.");
    }
    return CNA_RESULT_SUCCESS;
}

// The canonical event carries a gamer the runtime owns, so the callback gets a handle that lives
// exactly as long as the call it is passed to.
template<typename TEventArgs>
void RaiseGamerEvent(
    const CNA_SignedInGamerEventCallback callback,
    void* const context,
    const TEventArgs& args)
{
    CNA_Handle gamerHandle = CNA_INVALID_HANDLE;
    if (CreateBorrowedSignedInGamer(args.getGamerProperty(), &gamerHandle) !=
        CNA_RESULT_SUCCESS) {
        return;
    }
    const CNA_SignedInGamerEventInfo info = {
        sizeof(CNA_SignedInGamerEventInfo),
        StructureVersion,
        UINT32_C(0),
        gamerHandle
    };
    callback(context, &info);
    static_cast<void>(CNA::C::Detail::ReleaseBorrowedSignedInGamer(gamerHandle));
}

} // namespace

namespace CNA::C::Detail {

CNA_Result BorrowAnyGamer(const CNA_Handle handle, Gamer** const outGamer)
{
    if (outGamer == nullptr) {
        return InvalidInput("The borrowed Gamer output is null.");
    }
    *outGamer = nullptr;
    return BorrowGamerBase(handle, outGamer);
}

} // namespace CNA::C::Detail

CNA_Result cna_gamer_presence_init(CNA_GamerPresence* const outPresence)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPresence == nullptr) {
            return InvalidInput("The GamerPresence output is null.");
        }
        const GamerPresence presence = GamerPresence::CreateInternal();
        const CNA_GamerPresence value = {
            sizeof(CNA_GamerPresence),
            StructureVersion,
            static_cast<CNA_GamerPresenceMode>(presence.getPresenceModeProperty()),
            presence.getPresenceValueProperty()
        };
        *outPresence = value;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_destroy(const CNA_GamerHandle gamerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GamerResource> gamer;
        if (const CNA_Result result = GetGamerResource(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned Gamer handle is invalid for this call.");
        }
        const CNA_Result result = GetRuntimeHandles().Release(gamerHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned Gamer handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_get_display_name_size(
    const CNA_GamerHandle gamerHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The display-name size output is null.");
        }
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowGamerBase(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = gamer->getDisplayNameProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_copy_display_name(
    const CNA_GamerHandle gamerHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowGamerBase(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyGamerText(gamer->getDisplayNameProperty(), destination, capacity, outBytes);
    });
}

CNA_Result cna_gamer_set_display_name(
    const CNA_GamerHandle gamerHandle,
    const CNA_StringView displayName)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeName;
        if (const CNA_Result result =
                ToNativeText(displayName, "The display name is invalid.", &nativeName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowGamerBase(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        gamer->setDisplayNameProperty(nativeName);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_get_gamertag_size(const CNA_GamerHandle gamerHandle, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The gamertag size output is null.");
        }
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowGamerBase(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = gamer->getGamertagProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_copy_gamertag(
    const CNA_GamerHandle gamerHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowGamerBase(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyGamerText(gamer->getGamertagProperty(), destination, capacity, outBytes);
    });
}

CNA_Result cna_gamer_get_text_size(const CNA_GamerHandle gamerHandle, uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The gamer text size output is null.");
        }
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowGamerBase(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = gamer->ToString().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_copy_text(
    const CNA_GamerHandle gamerHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowGamerBase(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyGamerText(gamer->ToString(), destination, capacity, outBytes);
    });
}

CNA_Result cna_gamer_get_is_disposed(
    const CNA_GamerHandle gamerHandle,
    CNA_Bool* const outIsDisposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsDisposed == nullptr) {
            return InvalidInput("The gamer disposal output is null.");
        }
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowGamerBase(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsDisposed = gamer->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_get_tag(const CNA_GamerHandle gamerHandle, uint64_t* const outTag)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTag == nullptr) {
            return InvalidInput("The gamer tag output is null.");
        }
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowGamerBase(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::any& tag = gamer->getTagProperty();
        // A tag this ABI never wrote holds whatever the canonical object was constructed with, so a
        // value C did not put there reads back as zero rather than as a reinterpreted box.
        const uint64_t* const value = std::any_cast<uint64_t>(&tag);
        *outTag = value == nullptr ? UINT64_C(0) : *value;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_set_tag(const CNA_GamerHandle gamerHandle, const uint64_t tag)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowGamerBase(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        gamer->setTagProperty(std::any(tag));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_get_profile(
    const CNA_GamerHandle gamerHandle,
    CNA_GamerProfileHandle* const outProfile)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outProfile == nullptr) {
            return InvalidInput("The GamerProfile output handle is null.");
        }
        *outProfile = CNA_INVALID_HANDLE;
        Gamer* gamer = nullptr;
        if (const CNA_Result result = BorrowGamerBase(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical accessor hands back a profile the caller owns and must delete.
        return PublishProfile(std::shared_ptr<GamerProfile>(gamer->GetProfile()), outProfile);
    });
}

CNA_Result cna_gamer_begin_get_profile(
    const CNA_GamerHandle gamerHandle,
    const CNA_GamerAsyncCallback callback,
    void* const context,
    CNA_GamerProfileHandle* const outProfile)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = cna_gamer_get_profile(gamerHandle, outProfile);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (callback != nullptr) {
            callback(context);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_get_from_gamertag(
    const CNA_StringView gamertag,
    CNA_GamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGamer == nullptr) {
            return InvalidInput("The Gamer output handle is null.");
        }
        *outGamer = CNA_INVALID_HANDLE;
        std::string nativeGamertag;
        if (const CNA_Result result =
                ToNativeText(gamertag, "The gamertag is invalid.", &nativeGamertag);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical lookup refuses outright on every platform this ABI builds on, so the refusal
        // is the canonical one rather than an early return invented here.
        return PublishGamer(
            std::shared_ptr<Gamer>(Gamer::GetFromGamertag(nativeGamertag)),
            nullptr,
            outGamer);
    });
}

CNA_Result cna_gamer_begin_get_from_gamertag(
    const CNA_StringView gamertag,
    const CNA_GamerAsyncCallback callback,
    void* const context,
    CNA_GamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result = cna_gamer_get_from_gamertag(gamertag, outGamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (callback != nullptr) {
            callback(context);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_get_partner_token_size(
    const CNA_StringView audienceUri,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The partner-token size output is null.");
        }
        std::string nativeAudienceUri;
        if (const CNA_Result result =
                ToNativeText(audienceUri, "The audience URI is invalid.", &nativeAudienceUri);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = Gamer::GetPartnerToken(nativeAudienceUri).size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_copy_partner_token(
    const CNA_StringView audienceUri,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeAudienceUri;
        if (const CNA_Result result =
                ToNativeText(audienceUri, "The audience URI is invalid.", &nativeAudienceUri);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyGamerText(
            Gamer::GetPartnerToken(nativeAudienceUri),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_gamer_begin_get_partner_token(
    const CNA_StringView audienceUri,
    const CNA_GamerAsyncCallback callback,
    void* const context,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result =
                cna_gamer_copy_partner_token(audienceUri, destination, capacity, outBytes);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (callback != nullptr) {
            callback(context);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_get_signed_in_gamer_at_player_index(
    const CNA_PlayerIndex playerIndex,
    CNA_Bool* const outHasGamer,
    CNA_SignedInGamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasGamer == nullptr || outGamer == nullptr) {
            return InvalidInput("The signed-in gamer output is invalid.");
        }
        *outHasGamer = CNA_FALSE;
        PlayerIndex nativeIndex = PlayerIndex::One;
        if (!TryMapPlayerIndex(playerIndex, &nativeIndex)) {
            return InvalidInput("The player index is not a defined identity.");
        }
        SignedInGamerCollection* const collection = Gamer::getSignedInGamersProperty();
        SignedInGamer* const gamer = collection == nullptr ? nullptr : (*collection)[nativeIndex];
        if (gamer == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        if (const CNA_Result result = CreateBorrowedSignedInGamer(gamer, outGamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outHasGamer = CNA_TRUE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_get_is_guest(
    const CNA_SignedInGamerHandle gamerHandle,
    CNA_Bool* const outIsGuest)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsGuest == nullptr) {
            return InvalidInput("The guest output is null.");
        }
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsGuest = gamer->getIsGuestProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_get_is_signed_in_to_live(
    const CNA_SignedInGamerHandle gamerHandle,
    CNA_Bool* const outIsSignedInToLive)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsSignedInToLive == nullptr) {
            return InvalidInput("The online sign-in output is null.");
        }
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsSignedInToLive = gamer->getIsSignedInToLiveProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_get_party_size(
    const CNA_SignedInGamerHandle gamerHandle,
    int32_t* const outPartySize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPartySize == nullptr) {
            return InvalidInput("The party-size output is null.");
        }
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outPartySize = static_cast<int32_t>(gamer->getPartySizeProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_set_party_size(
    const CNA_SignedInGamerHandle gamerHandle,
    const int32_t partySize)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        gamer->setPartySizeProperty(static_cast<int>(partySize));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_get_player_index(
    const CNA_SignedInGamerHandle gamerHandle,
    CNA_PlayerIndex* const outPlayerIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPlayerIndex == nullptr) {
            return InvalidInput("The player-index output is null.");
        }
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outPlayerIndex = static_cast<CNA_PlayerIndex>(gamer->getPlayerIndexProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_get_presence(
    const CNA_SignedInGamerHandle gamerHandle,
    CNA_GamerPresence* const outPresence)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPresence == nullptr || outPresence->struct_size < sizeof(CNA_GamerPresence) ||
            outPresence->struct_version != StructureVersion) {
            return InvalidInput("The GamerPresence output structure is invalid.");
        }
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const GamerPresence& presence = gamer->getPresenceProperty();
        const CNA_GamerPresence value = {
            sizeof(CNA_GamerPresence),
            StructureVersion,
            static_cast<CNA_GamerPresenceMode>(presence.getPresenceModeProperty()),
            presence.getPresenceValueProperty()
        };
        *outPresence = value;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_set_presence(
    const CNA_SignedInGamerHandle gamerHandle,
    const CNA_GamerPresence* const presence)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (presence == nullptr || presence->struct_size < sizeof(CNA_GamerPresence) ||
            presence->struct_version != StructureVersion) {
            return InvalidInput("The GamerPresence structure is invalid.");
        }
        if (presence->presence_mode > CNA_GAMER_PRESENCE_MODE_MAXIMUM) {
            return InvalidInput("The gamer presence mode is not a defined identity.");
        }
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        GamerPresence& target = gamer->getPresenceProperty();
        target.setPresenceModeProperty(
            static_cast<Microsoft::Xna::Framework::GamerServices::GamerPresenceMode>(
                presence->presence_mode));
        target.setPresenceValueProperty(presence->presence_value);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_set_presence_mode_string_ext(
    const CNA_SignedInGamerHandle gamerHandle,
    const CNA_StringView mode)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeMode;
        if (const CNA_Result result = ToNativeText(mode, "The presence mode is invalid.", &nativeMode);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        gamer->getPresenceProperty().SetPresenceModeStringEXT(nativeMode);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_get_privileges(
    const CNA_SignedInGamerHandle gamerHandle,
    CNA_GamerPrivileges* const outPrivileges)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPrivileges == nullptr ||
            outPrivileges->struct_size < sizeof(CNA_GamerPrivileges) ||
            outPrivileges->struct_version != StructureVersion) {
            return InvalidInput("The GamerPrivileges output structure is invalid.");
        }
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const GamerPrivileges& privileges = gamer->getPrivilegesProperty();
        const CNA_GamerPrivileges value = {
            sizeof(CNA_GamerPrivileges),
            StructureVersion,
            static_cast<CNA_GamerPrivilegeSetting>(privileges.getAllowCommunicationProperty()),
            static_cast<CNA_GamerPrivilegeSetting>(privileges.getAllowProfileViewingProperty()),
            static_cast<CNA_GamerPrivilegeSetting>(privileges.getAllowUserCreatedContentProperty()),
            privileges.getAllowOnlineSessionsProperty() ? CNA_TRUE : CNA_FALSE,
            privileges.getAllowPremiumContentProperty() ? CNA_TRUE : CNA_FALSE,
            privileges.getAllowPurchaseContentProperty() ? CNA_TRUE : CNA_FALSE,
            privileges.getAllowTradeContentProperty() ? CNA_TRUE : CNA_FALSE,
            {0U, 0U, 0U, 0U}
        };
        *outPrivileges = value;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_is_friend(
    const CNA_SignedInGamerHandle gamerHandle,
    const CNA_GamerHandle otherHandle,
    CNA_Bool* const outIsFriend)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsFriend == nullptr) {
            return InvalidInput("The friendship output is null.");
        }
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        Gamer* other = nullptr;
        if (const CNA_Result result = BorrowGamerBase(otherHandle, &other);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsFriend = gamer->IsFriend(other) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_is_headset(
    const CNA_SignedInGamerHandle gamerHandle,
    const uint64_t microphoneIndex,
    CNA_Bool* const outIsHeadset)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsHeadset == nullptr) {
            return InvalidInput("The headset output is null.");
        }
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<Microphone*>& microphones = Microphone::getAllProperty();
        if (microphoneIndex >= static_cast<uint64_t>(microphones.size()) ||
            microphones[static_cast<std::size_t>(microphoneIndex)] == nullptr) {
            return InvalidInput("The microphone index is outside the capture-device list.");
        }
        *outIsHeadset =
            gamer->IsHeadset(*microphones[static_cast<std::size_t>(microphoneIndex)])
                ? CNA_TRUE
                : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_get_friends(
    const CNA_SignedInGamerHandle gamerHandle,
    CNA_GamerCollectionHandle* const outFriends)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFriends == nullptr) {
            return InvalidInput("The friend-collection output handle is null.");
        }
        *outFriends = CNA_INVALID_HANDLE;
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<GamerCollectionResource>();
        resource->friends = std::make_shared<FriendCollection>(gamer->GetFriends());
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::GamerCollection, resource, outFriends);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned gamer collection handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_award_achievement(
    const CNA_SignedInGamerHandle gamerHandle,
    const CNA_StringView achievementKey)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeKey;
        if (const CNA_Result result =
                ToNativeText(achievementKey, "The achievement key is invalid.", &nativeKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        gamer->AwardAchievement(nativeKey);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_begin_award_achievement(
    const CNA_SignedInGamerHandle gamerHandle,
    const CNA_StringView achievementKey,
    const CNA_GamerAsyncCallback callback,
    void* const context)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result =
                cna_signed_in_gamer_award_achievement(gamerHandle, achievementKey);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (callback != nullptr) {
            callback(context);
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_subscribe_signed_in_ext(
    const CNA_SignedInGamerEventCallback callback,
    void* const context,
    CNA_Handle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The gamer registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The gamer event callback is null.");
        }
        auto* const source = &SignedInGamer::SignedIn;
        const auto token = source->Add(
            [callback, context](System::Object*, const SignedInEventArgs& args) {
                RaiseGamerEvent(callback, context, args);
            });
        return PublishRegistration(
            std::make_shared<GamerRegistration<SignedInEventArgs>>(source, token),
            outRegistration);
    });
}

CNA_Result cna_signed_in_gamer_subscribe_signed_out_ext(
    const CNA_SignedInGamerEventCallback callback,
    void* const context,
    CNA_Handle* const outRegistration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outRegistration == nullptr) {
            return InvalidInput("The gamer registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidInput("The gamer event callback is null.");
        }
        auto* const source = &SignedInGamer::SignedOut;
        const auto token = source->Add(
            [callback, context](System::Object*, const SignedOutEventArgs& args) {
                RaiseGamerEvent(callback, context, args);
            });
        return PublishRegistration(
            std::make_shared<GamerRegistration<SignedOutEventArgs>>(source, token),
            outRegistration);
    });
}

CNA_Result cna_gamer_unsubscribe_ext(const CNA_Handle registration)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GamerRegistrationBase> value;
        if (const CNA_Result result = GetRuntimeHandles().Get(
                registration,
                ObjectKind::GamerEventRegistration,
                &value);
            result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The gamer registration handle is invalid for this call.");
        }
        const CNA_Result result = GetRuntimeHandles().Release(registration);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The gamer registration handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_profile_get_info(
    const CNA_GamerProfileHandle profileHandle,
    CNA_GamerProfileInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_GamerProfileInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidInput("The GamerProfile info output structure is invalid.");
        }
        std::shared_ptr<GamerProfileResource> profile;
        if (const CNA_Result result = BorrowProfile(profileHandle, &profile);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_GamerProfileInfo info = {
            sizeof(CNA_GamerProfileInfo),
            StructureVersion,
            static_cast<int32_t>(profile->value->getGamerScoreProperty()),
            static_cast<CNA_GamerZone>(profile->value->getGamerZoneProperty()),
            static_cast<int32_t>(profile->value->getTitlesPlayedProperty()),
            static_cast<int32_t>(profile->value->getTotalAchievementsProperty()),
            profile->value->getReputationProperty(),
            profile->value->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE,
            {0U, 0U, 0U}
        };
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_profile_get_motto_size(
    const CNA_GamerProfileHandle profileHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The motto size output is null.");
        }
        std::shared_ptr<GamerProfileResource> profile;
        if (const CNA_Result result = BorrowProfile(profileHandle, &profile);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = profile->value->getMottoProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_profile_copy_motto(
    const CNA_GamerProfileHandle profileHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GamerProfileResource> profile;
        if (const CNA_Result result = BorrowProfile(profileHandle, &profile);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyGamerText(profile->value->getMottoProperty(), destination, capacity, outBytes);
    });
}

CNA_Result cna_gamer_profile_get_region_name_size(
    const CNA_GamerProfileHandle profileHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The region-name size output is null.");
        }
        std::shared_ptr<GamerProfileResource> profile;
        if (const CNA_Result result = BorrowProfile(profileHandle, &profile);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = profile->value->getRegionProperty().getNameProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_profile_copy_region_name(
    const CNA_GamerProfileHandle profileHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GamerProfileResource> profile;
        if (const CNA_Result result = BorrowProfile(profileHandle, &profile);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyGamerText(
            profile->value->getRegionProperty().getNameProperty(),
            destination,
            capacity,
            outBytes);
    });
}

CNA_Result cna_gamer_profile_get_picture_size(
    const CNA_GamerProfileHandle profileHandle,
    CNA_Bool* const outHasPicture,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasPicture == nullptr || outBytes == nullptr) {
            return InvalidInput("The gamer-picture output is invalid.");
        }
        *outHasPicture = CNA_FALSE;
        *outBytes = UINT64_C(0);
        std::shared_ptr<GamerProfileResource> profile;
        if (const CNA_Result result = BorrowProfile(profileHandle, &profile);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        System::IO::Stream* const picture = profile->value->GetGamerPicture();
        if (picture == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        *outHasPicture = CNA_TRUE;
        *outBytes = static_cast<uint64_t>(picture->getLengthProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_profile_destroy(const CNA_GamerProfileHandle profileHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GamerProfileResource> profile;
        if (const CNA_Result result = BorrowProfile(profileHandle, &profile);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        profile->value->Dispose();
        const CNA_Result result = GetRuntimeHandles().Release(profileHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned GamerProfile handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_friend_gamer_create_ext(
    const CNA_StringView gamertag,
    const CNA_StringView displayName,
    const CNA_Bool isOnline,
    const CNA_Bool isPlaying,
    const CNA_Bool isAway,
    const CNA_Bool isBusy,
    const CNA_Bool friendRequestSentTo,
    const CNA_Bool friendRequestReceivedFrom,
    CNA_GamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGamer == nullptr) {
            return InvalidInput("The Gamer output handle is null.");
        }
        *outGamer = CNA_INVALID_HANDLE;
        std::string nativeGamertag;
        std::string nativeDisplayName;
        if (const CNA_Result result =
                ToNativeText(gamertag, "The gamertag is invalid.", &nativeGamertag);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ToNativeText(displayName, "The display name is invalid.", &nativeDisplayName);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto value = std::make_shared<FriendGamer>(FriendGamer::CreateInternal(
            nativeGamertag,
            nativeDisplayName,
            isOnline != CNA_FALSE,
            isPlaying != CNA_FALSE,
            isAway != CNA_FALSE,
            isBusy != CNA_FALSE,
            friendRequestSentTo != CNA_FALSE,
            friendRequestReceivedFrom != CNA_FALSE));
        FriendGamer* const raw = value.get();
        return PublishGamer(std::move(value), raw, outGamer);
    });
}

CNA_Result cna_friend_collection_create_ext(
    const CNA_GamerHandle* const friends,
    const uint64_t count,
    CNA_GamerCollectionHandle* const outCollection)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCollection == nullptr) {
            return InvalidInput("The friend-collection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        if (friends == nullptr && count != UINT64_C(0)) {
            return InvalidInput("The friend array is null.");
        }
        const auto resource = std::make_shared<GamerCollectionResource>();
        std::vector<FriendGamer*> nativeFriends;
        nativeFriends.reserve(static_cast<std::size_t>(count));
        for (uint64_t index = UINT64_C(0); index < count; ++index) {
            std::shared_ptr<GamerResource> gamer;
            FriendGamer* friendGamer = nullptr;
            if (const CNA_Result result = BorrowFriend(friends[index], &gamer, &friendGamer);
                result != CNA_RESULT_SUCCESS) {
                return result;
            }
            nativeFriends.push_back(friendGamer);
            resource->retained.push_back(gamer);
            resource->handles.push_back(friends[index]);
        }
        resource->friends =
            std::make_shared<FriendCollection>(FriendCollection::CreateInternal(nativeFriends));
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::GamerCollection, resource, outCollection);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned gamer collection handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_friend_gamer_get_info(
    const CNA_GamerHandle gamerHandle,
    CNA_FriendGamerInfo* const outInfo)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outInfo == nullptr || outInfo->struct_size < sizeof(CNA_FriendGamerInfo) ||
            outInfo->struct_version != StructureVersion) {
            return InvalidInput("The FriendGamer info output structure is invalid.");
        }
        std::shared_ptr<GamerResource> gamer;
        FriendGamer* friendGamer = nullptr;
        if (const CNA_Result result = BorrowFriend(gamerHandle, &gamer, &friendGamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const CNA_FriendGamerInfo info = {
            sizeof(CNA_FriendGamerInfo),
            StructureVersion,
            friendGamer->getFriendRequestReceivedFromProperty() ? CNA_TRUE : CNA_FALSE,
            friendGamer->getFriendRequestSentToProperty() ? CNA_TRUE : CNA_FALSE,
            friendGamer->getHasVoiceProperty() ? CNA_TRUE : CNA_FALSE,
            friendGamer->getInviteAcceptedProperty() ? CNA_TRUE : CNA_FALSE,
            friendGamer->getInviteReceivedFromProperty() ? CNA_TRUE : CNA_FALSE,
            friendGamer->getInviteRejectedProperty() ? CNA_TRUE : CNA_FALSE,
            friendGamer->getInviteSentToProperty() ? CNA_TRUE : CNA_FALSE,
            friendGamer->getIsAwayProperty() ? CNA_TRUE : CNA_FALSE,
            friendGamer->getIsBusyProperty() ? CNA_TRUE : CNA_FALSE,
            friendGamer->getIsJoinableProperty() ? CNA_TRUE : CNA_FALSE,
            friendGamer->getIsOnlineProperty() ? CNA_TRUE : CNA_FALSE,
            friendGamer->getIsPlayingProperty() ? CNA_TRUE : CNA_FALSE,
            {0U, 0U, 0U, 0U}
        };
        *outInfo = info;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_friend_gamer_get_presence_size(
    const CNA_GamerHandle gamerHandle,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outBytes == nullptr) {
            return InvalidInput("The friend presence size output is null.");
        }
        std::shared_ptr<GamerResource> gamer;
        FriendGamer* friendGamer = nullptr;
        if (const CNA_Result result = BorrowFriend(gamerHandle, &gamer, &friendGamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outBytes = friendGamer->getPresenceProperty().size();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_friend_gamer_copy_presence(
    const CNA_GamerHandle gamerHandle,
    char* const destination,
    const uint64_t capacity,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GamerResource> gamer;
        FriendGamer* friendGamer = nullptr;
        if (const CNA_Result result = BorrowFriend(gamerHandle, &gamer, &friendGamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyGamerText(friendGamer->getPresenceProperty(), destination, capacity, outBytes);
    });
}

CNA_Result cna_gamer_collection_get_count(
    const CNA_GamerCollectionHandle collectionHandle,
    int32_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr) {
            return InvalidInput("The collection count output is null.");
        }
        std::shared_ptr<GamerCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outCount = static_cast<int32_t>(collection->friends->getCountProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_collection_get_at(
    const CNA_GamerCollectionHandle collectionHandle,
    const int32_t index,
    CNA_GamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGamer == nullptr) {
            return InvalidInput("The collection element output handle is null.");
        }
        *outGamer = CNA_INVALID_HANDLE;
        std::shared_ptr<GamerCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical indexer validates the index and throws; letting it do so keeps the range
        // rule in one place rather than duplicating it here.
        FriendGamer* const gamer = (*collection->friends)[static_cast<int>(index)];
        for (std::size_t slot = 0U; slot < collection->retained.size(); ++slot) {
            if (collection->retained[slot]->friendGamer == gamer) {
                *outGamer = collection->handles[slot];
                return CNA_RESULT_SUCCESS;
            }
        }
        return InvalidState("The collection holds a gamer this ABI never published.");
    });
}

CNA_Result cna_gamer_collection_index_of(
    const CNA_GamerCollectionHandle collectionHandle,
    const CNA_GamerHandle gamerHandle,
    int32_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIndex == nullptr) {
            return InvalidInput("The collection index output is null.");
        }
        std::shared_ptr<GamerCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<GamerResource> gamer;
        FriendGamer* friendGamer = nullptr;
        if (const CNA_Result result = BorrowFriend(gamerHandle, &gamer, &friendGamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIndex = static_cast<int32_t>(collection->friends->IndexOf(friendGamer));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_collection_contains(
    const CNA_GamerCollectionHandle collectionHandle,
    const CNA_GamerHandle gamerHandle,
    CNA_Bool* const outContains)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContains == nullptr) {
            return InvalidInput("The collection containment output is null.");
        }
        std::shared_ptr<GamerCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<GamerResource> gamer;
        FriendGamer* friendGamer = nullptr;
        if (const CNA_Result result = BorrowFriend(gamerHandle, &gamer, &friendGamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outContains = collection->friends->Contains(friendGamer) ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_collection_copy_to(
    const CNA_GamerCollectionHandle collectionHandle,
    CNA_GamerHandle* const destination,
    const uint64_t capacity,
    const int32_t index,
    uint64_t* const outCount)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outCount == nullptr || (destination == nullptr && capacity != UINT64_C(0))) {
            return InvalidInput("The collection copy output is invalid.");
        }
        if (index < 0) {
            return InvalidInput("The destination index is negative.");
        }
        std::shared_ptr<GamerCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const uint64_t count = static_cast<uint64_t>(collection->handles.size());
        *outCount = count;
        if (capacity < static_cast<uint64_t>(index) + count) {
            return Fail(
                CNA_RESULT_BUFFER_TOO_SMALL,
                CNA_ERROR_CATEGORY_RANGE,
                "The destination capacity is smaller than the collection.");
        }
        for (uint64_t slot = UINT64_C(0); slot < count; ++slot) {
            destination[static_cast<uint64_t>(index) + slot] =
                collection->handles[static_cast<std::size_t>(slot)];
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_collection_add(
    const CNA_GamerCollectionHandle collectionHandle,
    const CNA_GamerHandle gamerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GamerCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<GamerResource> gamer;
        FriendGamer* friendGamer = nullptr;
        if (const CNA_Result result = BorrowFriend(gamerHandle, &gamer, &friendGamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->friends->Add(friendGamer);
        collection->retained.push_back(gamer);
        collection->handles.push_back(gamerHandle);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_collection_remove(
    const CNA_GamerCollectionHandle collectionHandle,
    const CNA_GamerHandle gamerHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GamerCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::shared_ptr<GamerResource> gamer;
        FriendGamer* friendGamer = nullptr;
        if (const CNA_Result result = BorrowFriend(gamerHandle, &gamer, &friendGamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->friends->Remove(friendGamer);
        for (std::size_t slot = collection->retained.size(); slot > 0U; --slot) {
            if (collection->retained[slot - 1U]->friendGamer == friendGamer) {
                collection->retained.erase(collection->retained.begin() +
                                           static_cast<std::ptrdiff_t>(slot - 1U));
                collection->handles.erase(collection->handles.begin() +
                                          static_cast<std::ptrdiff_t>(slot - 1U));
            }
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_collection_clear(const CNA_GamerCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GamerCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->friends->Clear();
        collection->retained.clear();
        collection->handles.clear();
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_collection_create_enumerator(
    const CNA_GamerCollectionHandle collectionHandle,
    CNA_GamerEnumeratorHandle* const outEnumerator)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outEnumerator == nullptr) {
            return InvalidInput("The enumerator output handle is null.");
        }
        *outEnumerator = CNA_INVALID_HANDLE;
        std::shared_ptr<GamerCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<GamerEnumeratorResource>();
        resource->collection = collection;
        resource->position = -1;
        const CNA_Result result =
            GetRuntimeHandles().Create(ObjectKind::GamerEnumerator, resource, outEnumerator);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned gamer enumerator handle could not be created.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_enumerator_move_next(
    const CNA_GamerEnumeratorHandle enumeratorHandle,
    CNA_Bool* const outHasCurrent)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasCurrent == nullptr) {
            return InvalidInput("The enumerator advance output is null.");
        }
        std::shared_ptr<GamerEnumeratorResource> enumerator;
        if (const CNA_Result result = BorrowEnumerator(enumeratorHandle, &enumerator);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // The canonical cursor is a value over the collection's own storage, so it is rebuilt at the
        // recorded position rather than kept across calls: a C caller may add or remove between two
        // advances, and a stored cursor would be pointing into a vector that has since moved.
        auto cursor = enumerator->collection->friends->GetEnumerator();
        for (int step = -1; step < enumerator->position; ++step) {
            static_cast<void>(cursor.MoveNext());
        }
        const bool hasCurrent = cursor.MoveNext();
        ++enumerator->position;
        *outHasCurrent = hasCurrent ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_enumerator_get_current(
    const CNA_GamerEnumeratorHandle enumeratorHandle,
    CNA_GamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGamer == nullptr) {
            return InvalidInput("The enumerator element output handle is null.");
        }
        *outGamer = CNA_INVALID_HANDLE;
        std::shared_ptr<GamerEnumeratorResource> enumerator;
        if (const CNA_Result result = BorrowEnumerator(enumeratorHandle, &enumerator);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (enumerator->position < 0 ||
            enumerator->position >= enumerator->collection->friends->getCountProperty()) {
            return InvalidState("The enumerator does not name a gamer.");
        }
        // A fresh cursor sits before the first element, so reaching position N takes N+1 advances.
        auto cursor = enumerator->collection->friends->GetEnumerator();
        for (int step = -1; step < enumerator->position; ++step) {
            static_cast<void>(cursor.MoveNext());
        }
        FriendGamer* const gamer = cursor.getCurrent();
        for (std::size_t slot = 0U; slot < enumerator->collection->retained.size(); ++slot) {
            if (enumerator->collection->retained[slot]->friendGamer == gamer) {
                *outGamer = enumerator->collection->handles[slot];
                return CNA_RESULT_SUCCESS;
            }
        }
        return InvalidState("The collection holds a gamer this ABI never published.");
    });
}

CNA_Result cna_gamer_enumerator_reset(const CNA_GamerEnumeratorHandle enumeratorHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GamerEnumeratorResource> enumerator;
        if (const CNA_Result result = BorrowEnumerator(enumeratorHandle, &enumerator);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto cursor = enumerator->collection->friends->GetEnumerator();
        cursor.Reset();
        enumerator->position = -1;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_enumerator_destroy(const CNA_GamerEnumeratorHandle enumeratorHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GamerEnumeratorResource> enumerator;
        if (const CNA_Result result = BorrowEnumerator(enumeratorHandle, &enumerator);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        auto cursor = enumerator->collection->friends->GetEnumerator();
        cursor.Dispose();
        const CNA_Result result = GetRuntimeHandles().Release(enumeratorHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned gamer enumerator handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_friend_collection_get_is_disposed(
    const CNA_GamerCollectionHandle collectionHandle,
    CNA_Bool* const outIsDisposed)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIsDisposed == nullptr) {
            return InvalidInput("The collection disposal output is null.");
        }
        std::shared_ptr<GamerCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outIsDisposed = collection->friends->getIsDisposedProperty() ? CNA_TRUE : CNA_FALSE;
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_collection_destroy(const CNA_GamerCollectionHandle collectionHandle)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::shared_ptr<GamerCollectionResource> collection;
        if (const CNA_Result result = BorrowCollection(collectionHandle, &collection);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        collection->friends->Dispose();
        const CNA_Result result = GetRuntimeHandles().Release(collectionHandle);
        if (result != CNA_RESULT_SUCCESS) {
            return Fail(
                result,
                ErrorCategoryForResult(result),
                "The owned gamer collection handle could not be released.");
        }
        return CNA_RESULT_SUCCESS;
    });
}
