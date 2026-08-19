// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_CONTENT_DETAIL_HPP
#define CNA_C_API_CONTENT_DETAIL_HPP

#include "CnaCApiDetail.hpp"

#include <memory>

namespace Microsoft::Xna::Framework::Content {
class ContentManager;
}

namespace CNA::C::Detail {

// The canonical ContentReader takes a raw ContentManager pointer that may be null. The reader
// adapter borrows the manager through this record so it never learns the content adapter's own
// resource layout, and the retained owner keeps the manager alive for the reader's whole lifetime.
struct BorrowedContentManager final {
    Microsoft::Xna::Framework::Content::ContentManager* value = nullptr;
    std::shared_ptr<void> owner;
    /// The game the manager belongs to, so a resource loaded through it can be created as that
    /// game's child from a translation unit that does not own the manager's own resource type.
    CNA_Handle parentGame = CNA_INVALID_HANDLE;
};

[[nodiscard]] CNA_Result BorrowContentManager(
    CNA_Handle handle,
    BorrowedContentManager* outContentManager);

} // namespace CNA::C::Detail

#endif
