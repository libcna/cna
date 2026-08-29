// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_CONTENT_DETAIL_HPP
#define CNA_C_API_CONTENT_DETAIL_HPP

#include "CnaCApiDetail.hpp"

#include <memory>

namespace Microsoft::Xna::Framework::Content {
class ContentManager;
}

namespace CNA::Content {
class ObjectDictionaryEXT;
}

namespace System {
class Object;
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

// CBIND-118: the two seams the Model.Tag routes ask through. Both answer about types that stay
// private to the content-reader translation unit -- the dictionary's resource record, and the
// carrier a caller-made object arrives in -- so the model translation unit asks rather than
// includes.
//
// The dictionary shared_ptr may be an aliasing one that keeps the loaded Model alive, which is what
// lets a tag handle outlive the model handle without dangling.
[[nodiscard]] CNA_Result PublishObjectDictionary(
    std::shared_ptr<CNA::Content::ObjectDictionaryEXT> dictionary,
    CNA_Handle* outHandle);

[[nodiscard]] bool TryGetForeignReferenceObject(const System::Object* tag, void** outObject);

} // namespace CNA::C::Detail

#endif
