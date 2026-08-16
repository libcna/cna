// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_GAMER_SERVICES_DETAIL_HPP
#define CNA_C_API_GAMER_SERVICES_DETAIL_HPP

#include "CnaCApiDetail.hpp"

#include <memory>

namespace Microsoft::Xna::Framework::GamerServices {
class Gamer;
class PropertyDictionary;
class SignedInGamer;
}

namespace CNA::C::Detail {

// A network session cannot be created without at least one signed-in gamer, so the session adapter
// borrows one through this entry point rather than learning the gamer-services resource layout.
[[nodiscard]] CNA_Result BorrowSignedInGamer(
    CNA_Handle handle,
    Microsoft::Xna::Framework::GamerServices::SignedInGamer** outGamer);

// The accepted-invite event carries a signed-in gamer, so the session adapter needs a handle for
// one it does not own; the view lives only as long as the callback that receives it.
[[nodiscard]] CNA_Result CreateBorrowedSignedInGamer(
    Microsoft::Xna::Framework::GamerServices::SignedInGamer* value,
    CNA_Handle* outGamer);

[[nodiscard]] CNA_Result ReleaseBorrowedSignedInGamer(CNA_Handle handle);

// Every `cna_gamer_*` route accepts either handle kind, because the canonical surface belongs to the
// gamer base. This reports a kind mismatch **without recording a diagnostic**, so the caller can try
// the other kind next and only the final failure is what a caller reads back.
// A leaderboard entry's columns are a property dictionary the entry owns, so the leaderboard adapter
// needs the same resource layout and the same factory the property adapter publishes.
struct PropertyDictionaryResource final {
    std::shared_ptr<Microsoft::Xna::Framework::GamerServices::PropertyDictionary> value;
};

[[nodiscard]] CNA_Result CreateOwnedPropertyDictionary(
    std::shared_ptr<Microsoft::Xna::Framework::GamerServices::PropertyDictionary> value,
    CNA_Handle* outDictionary);

// The guide takes gamers it did not create, and its routes accept either handle kind for the same
// reason every `cna_gamer_*` route does: the canonical parameter is the base both derive from.
// A leaderboard entry names a gamer the runtime owns, so the handle that reaches C borrows it and
// keeps whatever owns it alive for exactly as long as the handle names it.
[[nodiscard]] CNA_Result CreateBorrowedGamer(
    Microsoft::Xna::Framework::GamerServices::Gamer* value,
    std::shared_ptr<void> retentionOwner,
    CNA_Handle* outGamer);

[[nodiscard]] CNA_Result BorrowAnyGamer(
    CNA_Handle handle,
    Microsoft::Xna::Framework::GamerServices::Gamer** outGamer);

[[nodiscard]] CNA_Result TryBorrowSignedInGamerQuietly(
    CNA_Handle handle,
    Microsoft::Xna::Framework::GamerServices::SignedInGamer** outGamer) noexcept;

} // namespace CNA::C::Detail

#endif
