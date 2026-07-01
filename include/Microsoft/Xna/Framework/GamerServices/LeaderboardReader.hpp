// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardIdentity.hpp"
#include "System/AsyncCallback.hpp"
#include "System/Collections/ObjectModel/ReadOnlyCollection.hpp"
#include "System/IAsyncResult.hpp"
#include "System/IDisposable.hpp"
#include <any>
#include <vector>

namespace Microsoft::Xna::Framework::GamerServices
{
    class Gamer;

    /**
     * @brief Provides paged, read-only access to entries in a leaderboard.
     */
    class LeaderboardReader final : public System::IDisposable
    {
    public:
        /**
         * @brief Gets whether this reader has been disposed.
         *
         * @return true if disposed.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets whether there are more entries available below the current page.
         *
         * @return true if PageDown() would reveal additional entries.
         */
        [[nodiscard]] bool getCanPageDownProperty() const;

        /**
         * @brief Gets whether there are more entries available above the current page.
         *
         * @return true if PageUp() would reveal additional entries.
         */
        [[nodiscard]] bool getCanPageUpProperty() const;

        /**
         * @brief Gets the leaderboard entries in the current page.
         *
         * @return A read-only collection of the current page's entries.
         */
        [[nodiscard]] System::Collections::ObjectModel::ReadOnlyCollection<LeaderboardEntry> getEntriesProperty() const;

        /**
         * @brief Gets the identity of the leaderboard this reader is reading from.
         *
         * @return The leaderboard identity.
         */
        [[nodiscard]] const LeaderboardIdentity& getLeaderboardIdentityProperty() const;

        /**
         * @brief Gets the index of the first entry in the current page.
         *
         * @return The page start index.
         */
        [[nodiscard]] int getPageStartProperty() const;

        /**
         * @brief Gets the total number of entries in the leaderboard.
         *
         * @return The total leaderboard size.
         */
        [[nodiscard]] int getTotalLeaderboardSizeProperty() const;

        /**
         * @brief Releases the resources held by this reader.
         */
        void Dispose() override;

        /**
         * @brief Synchronously advances to the next page of entries.
         */
        void PageDown();

        /**
         * @brief Begins an asynchronous request to advance to the next page.
         *
         * @param callback   Invoked when the operation completes.
         * @param asyncState User-defined state passed through to the callback.
         * @return Never returns; always throws in this platform's implementation.
         */
        [[nodiscard]] System::IAsyncResult* BeginPageDown(System::AsyncCallback callback, std::any asyncState);

        /**
         * @brief Completes an asynchronous PageDown request.
         *
         * @param result The result returned by BeginPageDown.
         */
        void EndPageDown(System::IAsyncResult* result);

        /**
         * @brief Synchronously moves to the previous page of entries.
         */
        void PageUp();

        /**
         * @brief Begins an asynchronous request to move to the previous page.
         *
         * @param callback   Invoked when the operation completes.
         * @param asyncState User-defined state passed through to the callback.
         * @return Never returns; always throws in this platform's implementation.
         */
        [[nodiscard]] System::IAsyncResult* BeginPageUp(System::AsyncCallback callback, std::any asyncState);

        /**
         * @brief Completes an asynchronous PageUp request.
         *
         * @param result The result returned by BeginPageUp.
         */
        void EndPageUp(System::IAsyncResult* result);

        /**
         * @brief Synchronously reads a page of a leaderboard.
         *
         * @param leaderboardId The leaderboard to read.
         * @param pageStart     The index of the first entry to read.
         * @param pageSize      The number of entries per page.
         * @return Never returns; always throws in this platform's implementation.
         */
        [[nodiscard]] static LeaderboardReader Read(
            const LeaderboardIdentity& leaderboardId,
            int pageStart,
            int pageSize
        );

        /**
         * @brief Synchronously reads a page of a leaderboard centered on a gamer.
         *
         * @param leaderboardId The leaderboard to read.
         * @param pivotGamer    The gamer around which the page is centered.
         * @param pageSize      The number of entries per page.
         * @return Never returns; always throws in this platform's implementation.
         */
        [[nodiscard]] static LeaderboardReader Read(
            const LeaderboardIdentity& leaderboardId,
            Gamer* pivotGamer,
            int pageSize
        );

        /**
         * @brief Synchronously reads a page of a leaderboard restricted to a set of gamers.
         *
         * @param leaderboardId The leaderboard to read.
         * @param gamers        The gamers to restrict the leaderboard to.
         * @param pivotGamer    The gamer around which the page is centered.
         * @param pageSize      The number of entries per page.
         * @return Never returns; always throws in this platform's implementation.
         */
        [[nodiscard]] static LeaderboardReader Read(
            const LeaderboardIdentity& leaderboardId,
            const std::vector<Gamer*>& gamers,
            Gamer* pivotGamer,
            int pageSize
        );

        /**
         * @brief Begins an asynchronous request to read a page of a leaderboard.
         *
         * @param leaderboardId The leaderboard to read.
         * @param pageStart     The index of the first entry to read.
         * @param pageSize      The number of entries per page.
         * @param callback      Invoked when the operation completes.
         * @param asyncState    User-defined state passed through to the callback.
         * @return Never returns; always throws in this platform's implementation.
         */
        [[nodiscard]] static System::IAsyncResult* BeginRead(
            const LeaderboardIdentity& leaderboardId,
            int pageStart,
            int pageSize,
            System::AsyncCallback callback,
            std::any asyncState
        );

        /**
         * @brief Begins an asynchronous request to read a page of a leaderboard centered on a gamer.
         *
         * @param leaderboardId The leaderboard to read.
         * @param pivotGamer    The gamer around which the page is centered.
         * @param pageSize      The number of entries per page.
         * @param callback      Invoked when the operation completes.
         * @param asyncState    User-defined state passed through to the callback.
         * @return Never returns; always throws in this platform's implementation.
         */
        [[nodiscard]] static System::IAsyncResult* BeginRead(
            const LeaderboardIdentity& leaderboardId,
            Gamer* pivotGamer,
            int pageSize,
            System::AsyncCallback callback,
            std::any asyncState
        );

        /**
         * @brief Begins an asynchronous request to read a page of a leaderboard restricted to a set of gamers.
         *
         * @param leaderboardId The leaderboard to read.
         * @param gamers        The gamers to restrict the leaderboard to.
         * @param pivotGamer    The gamer around which the page is centered.
         * @param pageSize      The number of entries per page.
         * @param callback      Invoked when the operation completes.
         * @param asyncState    User-defined state passed through to the callback.
         * @return Never returns; always throws in this platform's implementation.
         */
        [[nodiscard]] static System::IAsyncResult* BeginRead(
            const LeaderboardIdentity& leaderboardId,
            const std::vector<Gamer*>& gamers,
            Gamer* pivotGamer,
            int pageSize,
            System::AsyncCallback callback,
            std::any asyncState
        );

        /**
         * @brief Completes an asynchronous Read request.
         *
         * @param result The result returned by BeginRead.
         * @return Never returns; always throws in this platform's implementation.
         */
        [[nodiscard]] static LeaderboardReader EndRead(System::IAsyncResult* result);

        /** @brief Creates a LeaderboardReader for CNA internal use. */
        NOXNA static LeaderboardReader CreateInternal(
            const LeaderboardIdentity& identity,
            int start,
            int size,
            std::vector<LeaderboardEntry> entries,
            bool friends
        );

    private:
        LeaderboardReader(
            const LeaderboardIdentity& identity,
            int start,
            int size,
            std::vector<LeaderboardEntry> entries,
            bool friends
        );

        LeaderboardIdentity leaderboardIdentity_;
        int pageStart_;
        int pageSize_;
        int totalLeaderboardSize_{0};
        bool isFriendBoard_;
        std::vector<LeaderboardEntry> entries_;
        std::vector<LeaderboardEntry> entryCache_;
        bool isDisposed_{false};
    };
}
