// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp"
#include "CNA/Internal/GamerServices/LocalGamerServicesStore.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/Threading/EventWaitHandle.hpp"
#include <algorithm>
#include <optional>

namespace Microsoft::Xna::Framework::GamerServices
{
    namespace
    {
        // Task 4.4 (plans/plan_net.md Phase 4): no FNA reference exists for any of this - real
        // FNA.NetStub's own LeaderboardReader is identically all-NotSupportedException, so sort
        // order/pivot-centering/friends-restriction semantics below are CNA-original, documented
        // defaults, not FNA fidelity.
        //
        // Loads every locally-persisted entry for a leaderboard, sorted rating-descending (higher
        // rating ranks better - the common leaderboard convention), with correct 1-based
        // RankingEXT reflecting that full sorted order. A persisted gamertag with no currently
        // signed-in Gamer* match is skipped - LeaderboardEntry::getGamerProperty() needs a real,
        // live, non-owning Gamer*, and no such pointer exists for a gamertag that isn't currently
        // signed in on this machine (a documented limitation, not a fabricated stand-in gamer with
        // unclear ownership).
        std::vector<LeaderboardEntry> LoadFullLocalLeaderboardEXT(const LeaderboardIdentity& identity)
        {
            const std::string fileKey = CNA::Internal::GamerServices::MakeLeaderboardFileKeyEXT(
                identity.getKeyProperty(), identity.getGameModeProperty()
            );

            std::vector<CNA::Internal::GamerServices::PersistedLeaderboardEntry> records =
                CNA::Internal::GamerServices::LoadLeaderboardEntriesEXT(fileKey);
            std::sort(records.begin(), records.end(),
                      [](const auto& a, const auto& b) { return a.Rating > b.Rating; });

            SignedInGamerCollection* signedIn = Gamer::getSignedInGamersProperty();
            std::vector<LeaderboardEntry> entries;
            int rank = 0;
            for (const auto& record : records)
            {
                Gamer* matched = nullptr;
                for (int i = 0; i < signedIn->getCountProperty(); ++i)
                {
                    if ((*signedIn)[i]->getGamertagProperty() == record.Gamertag)
                    {
                        matched = (*signedIn)[i];
                        break;
                    }
                }
                if (matched == nullptr)
                {
                    continue;
                }
                ++rank;
                LeaderboardEntry entry = LeaderboardEntry::CreateInternal(matched, record.Rating, rank);
                CNA::Internal::GamerServices::LoadLeaderboardEntryColumnsEXT(
                    fileKey, record.Gamertag, entry.getColumnsProperty()
                );
                entries.push_back(std::move(entry));
            }
            return entries;
        }

        int FindGamerIndex(const std::vector<LeaderboardEntry>& entries, Gamer* gamer)
        {
            if (gamer == nullptr)
            {
                return -1;
            }
            for (std::size_t i = 0; i < entries.size(); ++i)
            {
                if (entries[i].getGamerProperty() == gamer)
                {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        // pivotIndex not found (-1) falls back to starting at the top of the page, a conservative
        // default (no FNA reference exists to contradict it).
        int CenterPageOnPivot(int pivotIndex, int pageSize)
        {
            if (pivotIndex < 0)
            {
                return 0;
            }
            return std::max(0, pivotIndex - pageSize / 2);
        }

        // Shared IAsyncResult for every Begin* below - completes synchronously (a local disk read
        // is inherently instant, matching this codebase's established "fake async" convention for
        // synchronous local work) and, per audit_net.md's High finding precedent, actually invokes
        // its callback exactly once instead of only storing it.
        class LeaderboardAction final : public System::IAsyncResult
        {
        public:
            LeaderboardAction(std::any state, System::AsyncCallback callback)
                : Callback(std::move(callback))
                , asyncState_(std::move(state))
                , asyncWaitHandle_(true, System::Threading::EventResetMode::ManualReset)
            {
            }

            [[nodiscard]] bool getIsCompletedProperty() const override { return true; }
            [[nodiscard]] bool getCompletedSynchronouslyProperty() const override { return true; }
            [[nodiscard]] const std::any& getAsyncStateProperty() const override { return asyncState_; }
            [[nodiscard]] System::Threading::WaitHandle& getAsyncWaitHandleProperty() const override
            {
                return asyncWaitHandle_;
            }

            const System::AsyncCallback Callback;
            std::optional<LeaderboardReader> Reader;

        private:
            std::any asyncState_;
            mutable System::Threading::EventWaitHandle asyncWaitHandle_;
        };

        System::IAsyncResult* CompleteReadEXT(LeaderboardReader reader, System::AsyncCallback callback, std::any asyncState)
        {
            auto* action = new LeaderboardAction(std::move(asyncState), std::move(callback));
            action->Reader.emplace(std::move(reader));
            if (action->Callback)
            {
                action->Callback(*action);
            }
            return action;
        }

        System::IAsyncResult* CompletePageEXT(System::AsyncCallback callback, std::any asyncState)
        {
            auto* action = new LeaderboardAction(std::move(asyncState), std::move(callback));
            if (action->Callback)
            {
                action->Callback(*action);
            }
            return action;
        }
    }

    LeaderboardReader::LeaderboardReader(
        const LeaderboardIdentity& identity,
        int start,
        int size,
        std::vector<LeaderboardEntry> entries,
        bool friends
    )
        : leaderboardIdentity_(identity)
        , pageStart_(start)
        , pageSize_(size)
        // entries.size() is read here (declaration order runs this before entryCache_'s own
        // initializer below, regardless of this list's textual order) because `entries` is
        // moved-from once entryCache_ is constructed - entryCache_ always holds the complete
        // locally-known board (full or gamer-restricted per `friends`), so its size *is* this
        // reader's true total, unlike a real networked XNA board where only a page is cached
        // client-side and the remote total can exceed it.
        , totalLeaderboardSize_(static_cast<int>(entries.size()))
        , isFriendBoard_(friends)
        , entryCache_(std::move(entries))
    {
        // Loop bound matches FNA exactly (`i < pageSize`, not `i < pageStart + pageSize`) —
        // not a typo introduced here, this is the reference behavior.
        for (int i = pageStart_; i < pageSize_ && i < static_cast<int>(entryCache_.size()); ++i)
        {
            entries_.push_back(entryCache_[static_cast<std::size_t>(i)]);
        }
    }

    LeaderboardReader LeaderboardReader::CreateInternal(
        const LeaderboardIdentity& identity,
        int start,
        int size,
        std::vector<LeaderboardEntry> entries,
        bool friends
    ) {
        return LeaderboardReader(identity, start, size, std::move(entries), friends);
    }

    void LeaderboardReader::ResliceEntriesEXT()
    {
        entries_.clear();
        for (int i = pageStart_; i < pageStart_ + pageSize_ && i < static_cast<int>(entryCache_.size()); ++i)
        {
            entries_.push_back(entryCache_[static_cast<std::size_t>(i)]);
        }
    }

    bool LeaderboardReader::getIsDisposedProperty() const { return isDisposed_; }

    bool LeaderboardReader::getCanPageDownProperty() const
    {
        // Both board kinds use the same bounded-array check: entryCache_ always holds this
        // reader's *complete* board (full local leaderboard, or the gamer-restricted subset for a
        // friends board) rather than a partial client-side window of a larger remote total, so
        // there is no separate "more exists beyond what's cached" case to special-case here -
        // unlike the isFriendBoard_ split this used to have (see Task 4.4's fix-up, plans/plan_net.md
        // Phase 4: the old non-friend branch's `pageStart_ < entryCache_.size()` was true for
        // almost the entire board, letting PageDown() walk one page past the real end).
        return (pageStart_ + pageSize_) < static_cast<int>(entryCache_.size());
    }

    bool LeaderboardReader::getCanPageUpProperty() const
    {
        return pageStart_ > 0;
    }

    System::Collections::ObjectModel::ReadOnlyCollection<LeaderboardEntry> LeaderboardReader::getEntriesProperty() const
    {
        return System::Collections::ObjectModel::ReadOnlyCollection<LeaderboardEntry>(entries_);
    }

    const LeaderboardIdentity& LeaderboardReader::getLeaderboardIdentityProperty() const { return leaderboardIdentity_; }
    int LeaderboardReader::getPageStartProperty() const                                   { return pageStart_; }
    int LeaderboardReader::getTotalLeaderboardSizeProperty() const                        { return totalLeaderboardSize_; }

    void LeaderboardReader::Dispose()
    {
        isDisposed_ = true;
    }

    void LeaderboardReader::PageDown()
    {
        System::IAsyncResult* result = BeginPageDown(System::AsyncCallback{}, std::any{});
        while (!result->getIsCompletedProperty())
        {
            GamerServicesDispatcher::UpdateAsync();
        }
        EndPageDown(result);
    }

    System::IAsyncResult* LeaderboardReader::BeginPageDown(System::AsyncCallback callback, std::any asyncState)
    {
        if (!getCanPageDownProperty())
        {
            throw System::InvalidOperationException("Cannot page down: no further leaderboard entries are available.");
        }
        return CompletePageEXT(std::move(callback), std::move(asyncState));
    }

    // Task 4.4: PageDown/PageUp reslice the already-cached entryCache_ (no new disk read) via
    // ResliceEntriesEXT()'s real `[pageStart, pageStart + pageSize)` window - deliberately NOT
    // CreateInternal's own `i < pageSize` FNA-matching quirk, which is only ever correct for the
    // *initial* page (pageStart_ == 0 makes it equivalent to the real window); reused verbatim for
    // subsequent pages, it makes `entries_` permanently empty from the second page on, since
    // `i < pageSize_` can never hold once pageStart_ has advanced past pageSize_. No FNA reference
    // exists for PageDown/PageUp's own behavior at all (both are NotSupportedException stubs
    // there too), so there is no fidelity reason to propagate a quirk that would make real paging
    // silently stop working.
    void LeaderboardReader::EndPageDown(System::IAsyncResult* result)
    {
        if (dynamic_cast<LeaderboardAction*>(result) == nullptr)
        {
            throw System::ArgumentException("result was not returned by a call to BeginPageDown.", "result");
        }
        pageStart_ += pageSize_;
        ResliceEntriesEXT();
    }

    void LeaderboardReader::PageUp()
    {
        System::IAsyncResult* result = BeginPageUp(System::AsyncCallback{}, std::any{});
        while (!result->getIsCompletedProperty())
        {
            GamerServicesDispatcher::UpdateAsync();
        }
        EndPageUp(result);
    }

    System::IAsyncResult* LeaderboardReader::BeginPageUp(System::AsyncCallback callback, std::any asyncState)
    {
        if (!getCanPageUpProperty())
        {
            throw System::InvalidOperationException("Cannot page up: no earlier leaderboard entries are available.");
        }
        return CompletePageEXT(std::move(callback), std::move(asyncState));
    }

    void LeaderboardReader::EndPageUp(System::IAsyncResult* result)
    {
        if (dynamic_cast<LeaderboardAction*>(result) == nullptr)
        {
            throw System::ArgumentException("result was not returned by a call to BeginPageUp.", "result");
        }
        pageStart_ = std::max(0, pageStart_ - pageSize_);
        ResliceEntriesEXT();
    }

    LeaderboardReader LeaderboardReader::Read(const LeaderboardIdentity& leaderboardId, int pageStart, int pageSize)
    {
        System::IAsyncResult* result = BeginRead(leaderboardId, pageStart, pageSize, System::AsyncCallback{}, std::any{});
        while (!result->getIsCompletedProperty())
        {
            GamerServicesDispatcher::UpdateAsync();
        }
        return EndRead(result);
    }

    LeaderboardReader LeaderboardReader::Read(const LeaderboardIdentity& leaderboardId, Gamer* pivotGamer, int pageSize)
    {
        System::IAsyncResult* result = BeginRead(leaderboardId, pivotGamer, pageSize, System::AsyncCallback{}, std::any{});
        while (!result->getIsCompletedProperty())
        {
            GamerServicesDispatcher::UpdateAsync();
        }
        return EndRead(result);
    }

    LeaderboardReader LeaderboardReader::Read(
        const LeaderboardIdentity& leaderboardId,
        const std::vector<Gamer*>& gamers,
        Gamer* pivotGamer,
        int pageSize
    ) {
        System::IAsyncResult* result = BeginRead(leaderboardId, gamers, pivotGamer, pageSize, System::AsyncCallback{}, std::any{});
        while (!result->getIsCompletedProperty())
        {
            GamerServicesDispatcher::UpdateAsync();
        }
        return EndRead(result);
    }

    System::IAsyncResult* LeaderboardReader::BeginRead(
        const LeaderboardIdentity& leaderboardId,
        int pageStart,
        int pageSize,
        System::AsyncCallback callback,
        std::any asyncState
    ) {
        std::vector<LeaderboardEntry> entries = LoadFullLocalLeaderboardEXT(leaderboardId);
        LeaderboardReader reader = LeaderboardReader::CreateInternal(
            leaderboardId, pageStart, pageSize, std::move(entries), false
        );
        // CreateInternal's own ctor uses a quirky slice bound only ever validated for the
        // pageStart == 0 case (see ResliceEntriesEXT()'s own doc comment) - a real caller-supplied
        // pageStart deep into the leaderboard needs the actually-correct window.
        reader.ResliceEntriesEXT();
        return CompleteReadEXT(std::move(reader), std::move(callback), std::move(asyncState));
    }

    System::IAsyncResult* LeaderboardReader::BeginRead(
        const LeaderboardIdentity& leaderboardId,
        Gamer* pivotGamer,
        int pageSize,
        System::AsyncCallback callback,
        std::any asyncState
    ) {
        std::vector<LeaderboardEntry> entries = LoadFullLocalLeaderboardEXT(leaderboardId);
        const int pageStart = CenterPageOnPivot(FindGamerIndex(entries, pivotGamer), pageSize);
        LeaderboardReader reader = LeaderboardReader::CreateInternal(
            leaderboardId, pageStart, pageSize, std::move(entries), false
        );
        reader.ResliceEntriesEXT();
        return CompleteReadEXT(std::move(reader), std::move(callback), std::move(asyncState));
    }

    System::IAsyncResult* LeaderboardReader::BeginRead(
        const LeaderboardIdentity& leaderboardId,
        const std::vector<Gamer*>& gamers,
        Gamer* pivotGamer,
        int pageSize,
        System::AsyncCallback callback,
        std::any asyncState
    ) {
        std::vector<LeaderboardEntry> allEntries = LoadFullLocalLeaderboardEXT(leaderboardId);
        std::vector<LeaderboardEntry> restricted;
        for (const LeaderboardEntry& entry : allEntries)
        {
            if (std::find(gamers.begin(), gamers.end(), entry.getGamerProperty()) != gamers.end())
            {
                restricted.push_back(entry);
            }
        }
        const int pageStart = CenterPageOnPivot(FindGamerIndex(restricted, pivotGamer), pageSize);
        // friends=true: this reader represents a gamer-restricted subset (real XNA's "friends
        // leaderboard" pattern - a caller passes their own FriendCollection + pivot self).
        // isFriendBoard_ records that identity but no longer branches any paging math on it - see
        // getCanPageDownProperty()'s own comment for why both board kinds share one bounded-array
        // check now.
        LeaderboardReader reader = LeaderboardReader::CreateInternal(
            leaderboardId, pageStart, pageSize, std::move(restricted), true
        );
        reader.ResliceEntriesEXT();
        return CompleteReadEXT(std::move(reader), std::move(callback), std::move(asyncState));
    }

    LeaderboardReader LeaderboardReader::EndRead(System::IAsyncResult* result)
    {
        auto* action = dynamic_cast<LeaderboardAction*>(result);
        if (action == nullptr || !action->Reader.has_value())
        {
            throw System::ArgumentException("result was not returned by a call to BeginRead.", "result");
        }
        return std::move(*action->Reader);
    }
}
