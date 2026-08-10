// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "System/Collections/ObjectModel/ReadOnlyCollection.hpp"
#include "System/IDisposable.hpp"
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
         * @brief Marks this collection as disposed.
         */
        void Dispose() override;

        /** @brief Creates an AvailableNetworkSessionCollection for CNA internal use. */
        CNAEXT static AvailableNetworkSessionCollection CreateInternal(std::vector<AvailableNetworkSession> sessions);

    private:
        explicit AvailableNetworkSessionCollection(std::vector<AvailableNetworkSession> sessions);

        bool isDisposed_{false};
    };
}
