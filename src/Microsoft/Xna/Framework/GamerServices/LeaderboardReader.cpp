// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardReader.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "System/NotSupportedException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
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

    bool LeaderboardReader::getIsDisposedProperty() const { return isDisposed_; }

    bool LeaderboardReader::getCanPageDownProperty() const
    {
        if (entryCache_.empty())
            return false;
        if (isFriendBoard_)
            return (pageStart_ + pageSize_) < static_cast<int>(entryCache_.size());
        return pageStart_ < static_cast<int>(entryCache_.size())
            || entryCache_.back().getRankingEXTProperty() < totalLeaderboardSize_;
    }

    bool LeaderboardReader::getCanPageUpProperty() const
    {
        if (entryCache_.empty())
            return false;
        if (isFriendBoard_)
            return (pageStart_ - pageSize_) >= 0;
        return pageStart_ > 0 || entryCache_.front().getRankingEXTProperty() > 1;
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

    System::IAsyncResult* LeaderboardReader::BeginPageDown(System::AsyncCallback /*callback*/, std::any /*asyncState*/)
    {
        throw System::NotSupportedException();
    }

    void LeaderboardReader::EndPageDown(System::IAsyncResult* /*result*/)
    {
        throw System::NotSupportedException();
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

    System::IAsyncResult* LeaderboardReader::BeginPageUp(System::AsyncCallback /*callback*/, std::any /*asyncState*/)
    {
        throw System::NotSupportedException();
    }

    void LeaderboardReader::EndPageUp(System::IAsyncResult* /*result*/)
    {
        throw System::NotSupportedException();
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
        const LeaderboardIdentity& /*leaderboardId*/,
        int /*pageStart*/,
        int /*pageSize*/,
        System::AsyncCallback /*callback*/,
        std::any /*asyncState*/
    ) {
        throw System::NotSupportedException();
    }

    System::IAsyncResult* LeaderboardReader::BeginRead(
        const LeaderboardIdentity& /*leaderboardId*/,
        Gamer* /*pivotGamer*/,
        int /*pageSize*/,
        System::AsyncCallback /*callback*/,
        std::any /*asyncState*/
    ) {
        throw System::NotSupportedException();
    }

    System::IAsyncResult* LeaderboardReader::BeginRead(
        const LeaderboardIdentity& /*leaderboardId*/,
        const std::vector<Gamer*>& /*gamers*/,
        Gamer* /*pivotGamer*/,
        int /*pageSize*/,
        System::AsyncCallback /*callback*/,
        std::any /*asyncState*/
    ) {
        throw System::NotSupportedException();
    }

    LeaderboardReader LeaderboardReader::EndRead(System::IAsyncResult* /*result*/)
    {
        throw System::NotSupportedException();
    }
}
