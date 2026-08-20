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
         * Task 4.4 (plans/plan_net.md Phase 4): completes synchronously (a local reslice of the
         * already-cached entries, no disk I/O) - unlike a real networked page fetch, there is no
         * real deferred work to wait on.
         *
         * @param callback   Invoked when the operation completes.
         * @param asyncState User-defined state passed through to the callback.
         * @return An IAsyncResult already marked complete; pass to EndPageDown.
         * @throws System::InvalidOperationException if getCanPageDownProperty() is false.
         */
        [[nodiscard]] System::IAsyncResult* BeginPageDown(System::AsyncCallback callback, std::any asyncState);

        /**
         * @brief Completes an asynchronous PageDown request, advancing this reader to the next page.
         *
         * @param result The result returned by BeginPageDown.
         * @throws System::ArgumentException if result was not returned by BeginPageDown.
         */
        void EndPageDown(System::IAsyncResult* result);

        /**
         * @brief Synchronously moves to the previous page of entries.
         */
        void PageUp();

        /**
         * @brief Begins an asynchronous request to move to the previous page.
         *
         * Completes synchronously - see BeginPageDown's own doc comment for why.
         *
         * @param callback   Invoked when the operation completes.
         * @param asyncState User-defined state passed through to the callback.
         * @return An IAsyncResult already marked complete; pass to EndPageUp.
         * @throws System::InvalidOperationException if getCanPageUpProperty() is false.
         */
        [[nodiscard]] System::IAsyncResult* BeginPageUp(System::AsyncCallback callback, std::any asyncState);

        /**
         * @brief Completes an asynchronous PageUp request, moving this reader to the previous page.
         *
         * @param result The result returned by BeginPageUp.
         * @throws System::ArgumentException if result was not returned by BeginPageUp.
         */
        void EndPageUp(System::IAsyncResult* result);

        /**
         * @brief Synchronously reads a page of a leaderboard.
         *
         * Task 4.4 (plans/plan_net.md Phase 4): real, local-store-backed implementation. Entries are
         * sorted by Rating descending (CNA-original default - no FNA reference behavior exists to
         * match, since FNA's own LeaderboardReader is identically all-NotSupportedException); a
         * persisted gamertag with no currently signed-in Gamer* match is skipped (documented
         * limitation - LeaderboardEntry needs a real, live Gamer* to attach to).
         *
         * @param leaderboardId The leaderboard to read.
         * @param pageStart     The index of the first entry to read.
         * @param pageSize      The number of entries per page.
         * @return The requested page of the leaderboard.
         */
        [[nodiscard]] static LeaderboardReader Read(
            const LeaderboardIdentity& leaderboardId,
            int pageStart,
            int pageSize
        );

        /**
         * @brief Synchronously reads a page of a leaderboard centered on a gamer.
         *
         * CNA-original default: centers the page on pivotGamer's rank
         * (`max(0, rank - pageSize / 2)`); if pivotGamer has no entry on this leaderboard, starts
         * at the top instead (a conservative fallback - no reference behavior exists to match).
         *
         * @param leaderboardId The leaderboard to read.
         * @param pivotGamer    The gamer around which the page is centered.
         * @param pageSize      The number of entries per page.
         * @return The requested page of the leaderboard.
         */
        [[nodiscard]] static LeaderboardReader Read(
            const LeaderboardIdentity& leaderboardId,
            Gamer* pivotGamer,
            int pageSize
        );

        /**
         * @brief Synchronously reads a page of a leaderboard restricted to a set of gamers.
         *
         * CNA-original default: restricts the full leaderboard to only the given gamers (real
         * XNA's "friends leaderboard" pattern), then centers on pivotGamer as the pivotGamer
         * overload above does. Paging uses the same bounded-array math as every other reader -
         * getCanPageDownProperty()/getCanPageUpProperty() only ever look at this reader's own
         * cached entries, so a restricted board pages correctly with no special-casing.
         *
         * @param leaderboardId The leaderboard to read.
         * @param gamers        The gamers to restrict the leaderboard to.
         * @param pivotGamer    The gamer around which the page is centered.
         * @param pageSize      The number of entries per page.
         * @return The requested page of the restricted leaderboard.
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
         * Completes synchronously (a local disk read - see the Read() overload's own doc comment
         * for the real sort/matching behavior).
         *
         * @param leaderboardId The leaderboard to read.
         * @param pageStart     The index of the first entry to read.
         * @param pageSize      The number of entries per page.
         * @param callback      Invoked when the operation completes.
         * @param asyncState    User-defined state passed through to the callback.
         * @return An IAsyncResult already marked complete; pass to EndRead.
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
         * Completes synchronously - see the pivotGamer Read() overload's own doc comment for the
         * real centering behavior.
         *
         * @param leaderboardId The leaderboard to read.
         * @param pivotGamer    The gamer around which the page is centered.
         * @param pageSize      The number of entries per page.
         * @param callback      Invoked when the operation completes.
         * @param asyncState    User-defined state passed through to the callback.
         * @return An IAsyncResult already marked complete; pass to EndRead.
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
         * Completes synchronously - see the gamers-restricted Read() overload's own doc comment
         * for the real restriction/centering behavior.
         *
         * @param leaderboardId The leaderboard to read.
         * @param gamers        The gamers to restrict the leaderboard to.
         * @param pivotGamer    The gamer around which the page is centered.
         * @param pageSize      The number of entries per page.
         * @param callback      Invoked when the operation completes.
         * @param asyncState    User-defined state passed through to the callback.
         * @return An IAsyncResult already marked complete; pass to EndRead.
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
         * @return The LeaderboardReader constructed by the matching BeginRead call.
         * @throws System::ArgumentException if result was not returned by BeginRead.
         */
        [[nodiscard]] static LeaderboardReader EndRead(System::IAsyncResult* result);

        /**
         * @brief Creates a LeaderboardReader for CNA internal use.
         *
         * Task 10.6: `entries` is sliced into the reader's own page (`entries_`) using a loop
         * bounded by `i < size` (not `i < start + size`) — this matches FNA's own real internal
         * constructor exactly (`for (int i = PageStart; i < pageSize && i < entryCache.Count;
         * ...)`), a genuine, non-obvious upstream quirk, not a CNA bug. This means `entries` must
         * already be pre-sliced/ordered by the caller consistently with that exact bound before
         * calling this: passing a full, un-sliced leaderboard here (rather than the specific page
         * `[start, start + size)`) will silently produce a wrong (too-short-or-shifted) page,
         * since this constructor trusts its input completely and never re-derives the intended
         * slice itself. Task 4.4 (plans/plan_net.md Phase 4): every real `BeginRead` overload now calls
         * this with the *full* local leaderboard as `entries` (not pre-sliced to one page),
         * relying on the slicing loop above to derive the actual page - the real (non-stub)
         * implementation this precondition originally anticipated, now in place.
         *
         * @param identity The leaderboard identity being read.
         * @param start    The zero-based starting index of the page within the leaderboard.
         * @param size     The requested page size.
         * @param entries  The pre-sliced entries for exactly this `[start, start + size)` page.
         * @param friends  Whether this reader represents a friends-only leaderboard.
         * @return A new LeaderboardReader wrapping the given page.
         */
        CNAEXT static LeaderboardReader CreateInternal(
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

        /**
         * @brief Rebuilds entries_ from entryCache_ using the real `[pageStart_, pageStart_ +
         * pageSize_)` window - shared by BeginRead (Task 4.4's real implementation) and
         * EndPageDown/EndPageUp. Deliberately not the constructor's own `i < pageSize` FNA-
         * matching quirk (see EndPageDown's own doc comment in LeaderboardReader.cpp for why that
         * bound is unsuitable once pageStart_ can be nonzero from real usage, not just the
         * Task 10.6 test scenario it was originally validated for).
         */
        void ResliceEntriesEXT();

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
