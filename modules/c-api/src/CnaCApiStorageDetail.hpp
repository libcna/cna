// SPDX-License-Identifier: MS-PL

#ifndef CNA_C_API_STORAGE_DETAIL_HPP
#define CNA_C_API_STORAGE_DETAIL_HPP

#include "CnaCApiDetail.hpp"

#include <memory>

namespace System::IO {
class Stream;
}

namespace CNA::C::Detail {

// A storage stream is the only C-reachable byte source, so an adapter that needs a native stream
// pointer -- the content reader is the first -- borrows it through this record instead of learning
// the storage resource layout. The retained owner keeps the stream alive and the borrow is counted,
// so closing a stream that still has a live borrower is refused rather than dangling.
struct BorrowedStorageStream final {
    System::IO::Stream* value = nullptr;
    std::shared_ptr<void> owner;
};

[[nodiscard]] CNA_Result AcquireStorageStream(CNA_Handle handle, BorrowedStorageStream* outStream);

void ReleaseStorageStream(const BorrowedStorageStream& stream) noexcept;

} // namespace CNA::C::Detail

#endif
