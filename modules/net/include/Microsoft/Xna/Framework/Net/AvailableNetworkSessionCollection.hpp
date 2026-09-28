// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "System/Collections/ObjectModel/ReadOnlyCollection.hpp"
#include "System/IDisposable.hpp"
#include <memory>
#include <vector>

namespace Microsoft::Xna::Framework::Net
{
    /**
     * @brief A disposable, read-only collection of AvailableNetworkSession entries.
     *
     * FNA's Dispose() clears the underlying shared List<T> that its ReadOnlyCollection<T>
     * base wraps by reference, so the collection also appears empty afterward. Sharp-runtime's
     * ReadOnlyCollection<T> instead copies its source into private storage with no mutator
     * exposed to derived classes, so Dispose() here only flips IsDisposed — the collection's
     * contents remain readable afterward. This mirrors the same simplification already accepted
     * for other disposable collections in this project.
     */
    class AvailableNetworkSessionCollection final
        : public System::Collections::ObjectModel::ReadOnlyCollection<AvailableNetworkSession>
        , public System::IDisposable
    {
    public:
        /**
         * @brief Gets whether this collection has been disposed.
         *
         * @return true if disposed.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Disposes the search results; its listings, and copies of them, can no longer be
         * joined (NetworkSession::Join/BeginJoin throw System::ObjectDisposedException).
         */
        void Dispose() override;

        /** @brief Creates an AvailableNetworkSessionCollection for CNA internal use. */
        CNAEXT static AvailableNetworkSessionCollection CreateInternal(std::vector<AvailableNetworkSession> sessions);

    private:
        AvailableNetworkSessionCollection(std::vector<AvailableNetworkSession> sessions, std::shared_ptr<bool> disposed);

        std::shared_ptr<bool> disposed_;
    };
}
