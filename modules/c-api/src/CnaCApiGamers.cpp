// SPDX-License-Identifier: MS-PL

#include "CNA/C/gamer_services.h"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "CnaCApiGamerServicesDetail.hpp"
#include "CnaCApiNetDetail.hpp"
#include "CnaCApiRuntimeDetail.hpp"
#include "CnaCApiOutput.hpp"
#include "CnaCApiMarshal.hpp"
#include "CnaCApiDetail.hpp"

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
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "System/EventHandler.hpp"

#include <any>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using CNA::C::Detail::BorrowSignedIn;
using CNA::C::Detail::CallWithExceptionBarrier;
using CNA::C::Detail::CopyText;
using CNA::C::Detail::CreateBorrowedSignedInGamer;
using CNA::C::Detail::CreateHandle;
using CNA::C::Detail::ErrorCategoryForResult;
using CNA::C::Detail::Fail;
using CNA::C::Detail::GamerRegistration;
using CNA::C::Detail::GamerRegistrationBase;
using CNA::C::Detail::GetRuntimeHandles;
using CNA::C::Detail::InvalidArgument;
using CNA::C::Detail::InvalidState;
using CNA::C::Detail::LookupHandle;
using CNA::C::Detail::ObjectKind;
using CNA::C::Detail::ReadText;
using CNA::C::Detail::ReleaseHandle;
using CNA::C::Detail::StructureVersion;
using CNA::C::Detail::ToCBool;
using CNA::C::Detail::TryBorrowSignedInGamerQuietly;
using CNA::C::Detail::TryMapPlayerIndex;
using CNA::C::Detail::ValidateCanonicalBool;
using CNA::C::Detail::ValidateVersionedStructure;

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
    // A NetworkGamer is a Gamer, and for a remote one these routes are the only way to its
    // gamertag and display name.
    ObjectKind kind = ObjectKind::Unknown;
    if (GetRuntimeHandles().GetKind(handle, &kind) == CNA_RESULT_SUCCESS &&
        kind == ObjectKind::NetworkGamer) {
        Microsoft::Xna::Framework::Net::NetworkGamer* networkGamer = nullptr;
        if (const CNA_Result result = CNA::C::Detail::BorrowNetworkGamer(handle, &networkGamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        *outGamer = networkGamer;
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

[[nodiscard]] CNA_Result BorrowProfile(
    const CNA_Handle handle,
    std::shared_ptr<GamerProfileResource>* const outProfile)
{
    return LookupHandle(handle, ObjectKind::GamerProfile, outProfile);
}

[[nodiscard]] CNA_Result BorrowCollection(
    const CNA_Handle handle,
    std::shared_ptr<GamerCollectionResource>* const outCollection)
{
    return LookupHandle(handle, ObjectKind::GamerCollection, outCollection);
}

[[nodiscard]] CNA_Result BorrowEnumerator(
    const CNA_Handle handle,
    std::shared_ptr<GamerEnumeratorResource>* const outEnumerator)
{
    return LookupHandle(handle, ObjectKind::GamerEnumerator, outEnumerator);
}

[[nodiscard]] CNA_Result PublishGamer(
    std::shared_ptr<Gamer> value,
    FriendGamer* const friendGamer,
    CNA_Handle* const outGamer)
{
    const auto resource = std::make_shared<GamerResource>();
    resource->value = std::move(value);
    resource->friendGamer = friendGamer;
    return CreateHandle(ObjectKind::Gamer, resource, outGamer);
}

[[nodiscard]] CNA_Result PublishProfile(
    std::shared_ptr<GamerProfile> value,
    CNA_Handle* const outProfile)
{
    const auto resource = std::make_shared<GamerProfileResource>();
    resource->value = std::move(value);
    return CreateHandle(ObjectKind::GamerProfile, resource, outProfile);
}

[[nodiscard]] CNA_Result PublishRegistration(
    std::shared_ptr<GamerRegistrationBase> registration,
    CNA_Handle* const outRegistration)
{
    return CreateHandle(ObjectKind::GamerEventRegistration,
        std::move(registration),
        outRegistration);
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

CNA_Result CreateBorrowedGamer(
    Gamer* const value,
    std::shared_ptr<void> retentionOwner,
    CNA_Handle* const outGamer)
{
    if (value == nullptr || outGamer == nullptr) {
        return InvalidArgument("The borrowed Gamer factory arguments are invalid.");
    }
    *outGamer = CNA_INVALID_HANDLE;
    const auto resource = std::make_shared<GamerResource>();
    // The aliasing constructor keeps the owner alive without ever deleting the gamer itself.
    resource->value = std::shared_ptr<Gamer>(std::move(retentionOwner), value);
    resource->friendGamer = dynamic_cast<FriendGamer*>(value);
    return CreateHandle(ObjectKind::Gamer, resource, outGamer);
}

CNA_Result BorrowAnyGamer(const CNA_Handle handle, Gamer** const outGamer)
{
    if (outGamer == nullptr) {
        return InvalidArgument("The borrowed Gamer output is null.");
    }
    *outGamer = nullptr;
    return BorrowGamerBase(handle, outGamer);
}

} // namespace CNA::C::Detail

CNA_Result cna_gamer_presence_init(CNA_GamerPresence* const outPresence)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outPresence == nullptr) {
            return InvalidArgument("The GamerPresence output is null.");
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
        return ReleaseHandle(gamerHandle);
    });
}

CNA_Result cna_gamer_set_display_name(
    const CNA_GamerHandle gamerHandle,
    const CNA_StringView displayName)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeName;
        if (const CNA_Result result =
                ReadText(displayName, "The display name is invalid.", &nativeName);
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

CNA_Result cna_gamer_get_tag(const CNA_GamerHandle gamerHandle, uint64_t* const outTag)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outTag == nullptr) {
            return InvalidArgument("The gamer tag output is null.");
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
            return InvalidArgument("The Gamer output handle is null.");
        }
        *outGamer = CNA_INVALID_HANDLE;
        std::string nativeGamertag;
        if (const CNA_Result result =
                ReadText(gamertag, "The gamertag is invalid.", &nativeGamertag);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        // A real service lookup, or the canonical refusal without a service (offline profiles have no
        // directory); either way the answer is the canonical one, not an early return invented here.
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
            return InvalidArgument("The partner-token size output is null.");
        }
        std::string nativeAudienceUri;
        if (const CNA_Result result =
                ReadText(audienceUri, "The audience URI is invalid.", &nativeAudienceUri);
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
                ReadText(audienceUri, "The audience URI is invalid.", &nativeAudienceUri);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return CopyText(
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

CNA_Result cna_gamer_get_signed_in_gamer_at(
    const int32_t index,
    CNA_SignedInGamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGamer == nullptr) {
            return InvalidArgument("The signed-in gamer output handle is null.");
        }
        *outGamer = CNA_INVALID_HANDLE;
        SignedInGamerCollection* const collection = Gamer::getSignedInGamersProperty();
        if (collection == nullptr || index < 0 || index >= collection->getCountProperty()) {
            return InvalidArgument("The signed-in gamer position is outside the collection.");
        }
        SignedInGamer* const gamer = (*collection)[static_cast<int>(index)];
        if (gamer == nullptr) {
            // The collection reported the position as valid, so a null element is an internal
            // inconsistency rather than an ordinary "nobody is signed in at that slot".
            return Fail(
                CNA_RESULT_INTERNAL,
                CNA_ERROR_CATEGORY_INTERNAL,
                "The signed-in gamer collection reported an empty element inside its own count.");
        }
        return CreateBorrowedSignedInGamer(gamer, outGamer);
    });
}

CNA_Result cna_gamer_signed_in_index_of(
    const CNA_SignedInGamerHandle gamerHandle,
    int32_t* const outIndex)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outIndex == nullptr) {
            return InvalidArgument("The signed-in gamer index output is null.");
        }
        *outIndex = INT32_C(-1);
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = CNA::C::Detail::BorrowSignedInGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        SignedInGamerCollection* const collection = Gamer::getSignedInGamersProperty();
        if (collection == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        // Not being in the collection is an answer, not a failure -- the canonical IndexOf reports
        // a negative position for it, and so does this.
        *outIndex = static_cast<int32_t>(collection->IndexOf(gamer));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_signed_in_contains(
    const CNA_SignedInGamerHandle gamerHandle,
    CNA_Bool* const outContains)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outContains == nullptr) {
            return InvalidArgument("The signed-in gamer containment output is null.");
        }
        *outContains = CNA_FALSE;
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = CNA::C::Detail::BorrowSignedInGamer(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        SignedInGamerCollection* const collection = Gamer::getSignedInGamersProperty();
        *outContains = ToCBool(collection != nullptr && collection->Contains(gamer));
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
            return InvalidArgument("The signed-in gamer output is invalid.");
        }
        *outHasGamer = CNA_FALSE;
        PlayerIndex nativeIndex = PlayerIndex::One;
        if (!TryMapPlayerIndex(playerIndex, &nativeIndex)) {
            return InvalidArgument("The player index is not a defined identity.");
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

CNA_Result cna_signed_in_gamer_get_presence(
    const CNA_SignedInGamerHandle gamerHandle,
    CNA_GamerPresence* const outPresence)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (const CNA_Result result =
                ValidateVersionedStructure(outPresence, "The GamerPresence output structure is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
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
        if (const CNA_Result result =
                ValidateVersionedStructure(presence, "The GamerPresence structure is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (presence->presence_mode > CNA_GAMER_PRESENCE_MODE_MAXIMUM) {
            return InvalidArgument("The gamer presence mode is not a defined identity.");
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
        if (const CNA_Result result = ReadText(mode, "The presence mode is invalid.", &nativeMode);
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
        if (const CNA_Result result =
                ValidateVersionedStructure(outPrivileges, "The GamerPrivileges output structure is invalid.");
            result != CNA_RESULT_SUCCESS) {
            return result;
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
            ToCBool(privileges.getAllowOnlineSessionsProperty()),
            ToCBool(privileges.getAllowPremiumContentProperty()),
            ToCBool(privileges.getAllowPurchaseContentProperty()),
            ToCBool(privileges.getAllowTradeContentProperty()),
            {0U, 0U, 0U, 0U}
        };
        *outPrivileges = value;
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
            return InvalidArgument("The headset output is null.");
        }
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const std::vector<Microphone*>& microphones = Microphone::getAllProperty();
        if (microphoneIndex >= static_cast<uint64_t>(microphones.size()) ||
            microphones[static_cast<std::size_t>(microphoneIndex)] == nullptr) {
            return InvalidArgument("The microphone index is outside the capture-device list.");
        }
        *outIsHeadset =
            ToCBool(gamer->IsHeadset(*microphones[static_cast<std::size_t>(microphoneIndex)]));
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_signed_in_gamer_get_friends(
    const CNA_SignedInGamerHandle gamerHandle,
    CNA_GamerCollectionHandle* const outFriends)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outFriends == nullptr) {
            return InvalidArgument("The friend-collection output handle is null.");
        }
        *outFriends = CNA_INVALID_HANDLE;
        SignedInGamer* gamer = nullptr;
        if (const CNA_Result result = BorrowSignedIn(gamerHandle, &gamer);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        const auto resource = std::make_shared<GamerCollectionResource>();
        resource->friends = std::make_shared<FriendCollection>(gamer->GetFriends());
        return CreateHandle(ObjectKind::GamerCollection, resource, outFriends);
    });
}

CNA_Result cna_signed_in_gamer_award_achievement(
    const CNA_SignedInGamerHandle gamerHandle,
    const CNA_StringView achievementKey)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        std::string nativeKey;
        if (const CNA_Result result =
                ReadText(achievementKey, "The achievement key is invalid.", &nativeKey);
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
            return InvalidArgument("The gamer registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidArgument("The gamer event callback is null.");
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
            return InvalidArgument("The gamer registration output is null.");
        }
        *outRegistration = CNA_INVALID_HANDLE;
        if (callback == nullptr) {
            return InvalidArgument("The gamer event callback is null.");
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
        if (const CNA_Result result = LookupHandle(registration, ObjectKind::GamerEventRegistration, &value);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        return ReleaseHandle(registration);
    });
}

CNA_Result cna_gamer_profile_get_picture_size(
    const CNA_GamerProfileHandle profileHandle,
    CNA_Bool* const outHasPicture,
    uint64_t* const outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasPicture == nullptr || outBytes == nullptr) {
            return InvalidArgument("The gamer-picture output is invalid.");
        }
        *outHasPicture = CNA_FALSE;
        *outBytes = UINT64_C(0);
        std::shared_ptr<GamerProfileResource> profile;
        if (const CNA_Result result = BorrowProfile(profileHandle, &profile);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::unique_ptr<System::IO::Stream> picture(profile->value->GetGamerPicture());
        if (picture == nullptr) {
            return CNA_RESULT_SUCCESS;
        }
        *outHasPicture = CNA_TRUE;
        *outBytes = static_cast<uint64_t>(picture->getLengthProperty());
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_profile_copy_picture(const CNA_GamerProfileHandle handle,uint8_t* destination,uint64_t capacity,uint64_t* outBytes)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if(!outBytes||(!destination&&capacity))return InvalidArgument("The picture output is invalid.");
        std::shared_ptr<GamerProfileResource> profile;
        if(const auto result=BorrowProfile(handle,&profile);result!=CNA_RESULT_SUCCESS)return result;
        std::unique_ptr<System::IO::Stream> picture(profile->value->GetGamerPicture());
        return CNA::C::Detail::CopyPictureStream(picture.get(),destination,capacity,outBytes);
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
            return InvalidArgument("The Gamer output handle is null.");
        }
        *outGamer = CNA_INVALID_HANDLE;
        if (const CNA_Result result = ValidateCanonicalBool(isOnline, "is_online");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateCanonicalBool(isPlaying, "is_playing");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateCanonicalBool(isAway, "is_away");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateCanonicalBool(isBusy, "is_busy");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateCanonicalBool(friendRequestSentTo, "friend_request_sent_to");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result = ValidateCanonicalBool(friendRequestReceivedFrom, "friend_request_received_from");
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        std::string nativeGamertag;
        std::string nativeDisplayName;
        if (const CNA_Result result =
                ReadText(gamertag, "The gamertag is invalid.", &nativeGamertag);
            result != CNA_RESULT_SUCCESS) {
            return result;
        }
        if (const CNA_Result result =
                ReadText(displayName, "The display name is invalid.", &nativeDisplayName);
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
            return InvalidArgument("The friend-collection output handle is null.");
        }
        *outCollection = CNA_INVALID_HANDLE;
        if (friends == nullptr && count != UINT64_C(0)) {
            return InvalidArgument("The friend array is null.");
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
        return CreateHandle(ObjectKind::GamerCollection, resource, outCollection);
    });
}

CNA_Result cna_gamer_collection_get_at(
    const CNA_GamerCollectionHandle collectionHandle,
    const int32_t index,
    CNA_GamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGamer == nullptr) {
            return InvalidArgument("The collection element output handle is null.");
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
            return InvalidArgument("The collection index output is null.");
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
            return InvalidArgument("The collection containment output is null.");
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
        *outContains = ToCBool(collection->friends->Contains(friendGamer));
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
            return InvalidArgument("The collection copy output is invalid.");
        }
        if (index < 0) {
            return InvalidArgument("The destination index is negative.");
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
            return InvalidArgument("The enumerator output handle is null.");
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
        return CreateHandle(ObjectKind::GamerEnumerator, resource, outEnumerator);
    });
}

CNA_Result cna_gamer_enumerator_move_next(
    const CNA_GamerEnumeratorHandle enumeratorHandle,
    CNA_Bool* const outHasCurrent)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outHasCurrent == nullptr) {
            return InvalidArgument("The enumerator advance output is null.");
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
        *outHasCurrent = ToCBool(hasCurrent);
        return CNA_RESULT_SUCCESS;
    });
}

CNA_Result cna_gamer_enumerator_get_current(
    const CNA_GamerEnumeratorHandle enumeratorHandle,
    CNA_GamerHandle* const outGamer)
{
    return CallWithExceptionBarrier([&]() -> CNA_Result {
        if (outGamer == nullptr) {
            return InvalidArgument("The enumerator element output handle is null.");
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
        return ReleaseHandle(enumeratorHandle);
    });
}

// The mechanically regular routes of this family, generated from modules/c-api/spec/Gamers.toml.
#include "generated/Gamers.inc"
