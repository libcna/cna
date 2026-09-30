// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardWriter.hpp"
#include "System/ArgumentException.hpp"
#include "System/IO/MemoryStream.hpp"
#include "CNA/Internal/GamerServices/LocalGamerServicesStore.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    LeaderboardWriter::LeaderboardWriter(Gamer* owner)
        : owner_(owner)
    {
    }

    LeaderboardEntry* LeaderboardWriter::GetLeaderboard(const LeaderboardIdentity& leaderboardId)
    {
        if(!owner_->serviceUserId_.empty()) {
            if(owner_->getIsDisposedProperty())throw System::ObjectDisposedException("Gamer");
            if(!writeScope_||!writeScope_->active)throw System::InvalidOperationException("Leaderboards can only be written during network gameplay.");
        }
        const std::string fileKey = CNA::Internal::GamerServices::MakeLeaderboardFileKeyEXT(
            leaderboardId.getKeyProperty(), leaderboardId.getGameModeProperty()
        );

        auto existing = entriesByLeaderboardKeyEXT_.find(fileKey);
        if (existing != entriesByLeaderboardKeyEXT_.end())
        {
            return &existing->second;
        }

        if(!owner_->serviceUserId_.empty()) {
            if(owner_->getIsDisposedProperty())throw System::ObjectDisposedException("Gamer");
            auto entry=LeaderboardEntry::CreateInternal(owner_,0,0);
            auto [inserted,created]=entriesByLeaderboardKeyEXT_.emplace(fileKey,std::move(entry));(void)created;
            identities_[fileKey]=leaderboardId;BindServiceEntry(fileKey,inserted->second);
            return &inserted->second;
        }

        // First access for this leaderboard from this writer: seed the entry from whatever is
        // already locally persisted for the owning gamer (0 rating, no columns, if nothing was
        // ever written before).
        long long rating = 0;
        for (const auto& record : CNA::Internal::GamerServices::LoadLeaderboardEntriesEXT(fileKey))
        {
            if (record.Gamertag == owner_->getGamertagProperty())
            {
                rating = record.Rating;
                break;
            }
        }

        // RankingEXT is meaningless for a writer-only entry (real rank only makes sense within a
        // full, sorted leaderboard read) - 0, matching this class's own lack of any read/sort
        // context.
        LeaderboardEntry entry = LeaderboardEntry::CreateInternal(owner_, rating, 0);
        CNA::Internal::GamerServices::LoadLeaderboardEntryColumnsEXT(
            fileKey, owner_->getGamertagProperty(), entry.getColumnsProperty()
        );

        auto [inserted, didInsert] = entriesByLeaderboardKeyEXT_.emplace(fileKey, std::move(entry));
        (void) didInsert; // always true here - the find() above already ruled out an existing entry
        LeaderboardEntry* result = &inserted->second;

        // Explicit offline fixtures retain their legacy persistence hook; service writers
        // instead collect drafts for the session EndGame transaction.
        Gamer* owner = owner_;
        result->SetOnRatingChangedHookEXT([result, fileKey, owner]() {
            CNA::Internal::GamerServices::PersistedLeaderboardEntry record;
            record.Gamertag = owner->getGamertagProperty();
            record.Rating = result->getRatingProperty();
            CNA::Internal::GamerServices::SaveLeaderboardEntryEXT(fileKey, record, &result->getColumnsProperty());
        });

        return result;
    }
    void LeaderboardWriter::BindServiceEntry(const std::string& key,LeaderboardEntry& entry) {
        const std::weak_ptr<WriteScope> weak=writeScope_;
        auto guard=[weak,key] {const auto scope=weak.lock();if(!scope||!scope->active)throw System::InvalidOperationException("Leaderboards can only be written during network gameplay.");scope->dirty.insert(key);};
        entry.validateWrite_=guard;entry.columns_.writeGuard_=guard;entry.columns_.createsStreams_=true;
        entry.SetOnRatingChangedHookEXT({});
    }
    void LeaderboardWriter::BeginServiceGameplay() {
        if(owner_->serviceUserId_.empty())return;
        if(writeScope_)writeScope_->active=false;
        writeScope_=std::make_shared<WriteScope>();writeScope_->active=true;
        for(auto& [key,entry]:entriesByLeaderboardKeyEXT_) {
            entry.rating_=0;entry.columns_.dictionary_.clear();BindServiceEntry(key,entry);
        }
    }
    void LeaderboardWriter::EndServiceGameplay() {if(writeScope_)writeScope_->active=false;}
    std::vector<CNA::Internal::GamerServices::ServiceLeaderboardWrite> LeaderboardWriter::CollectServiceWrites() const {
        using CNA::Internal::GamerServices::ServiceLeaderboardWrite;
        using CNA::Internal::GamerServices::ServiceLeaderboardColumn;
        std::vector<ServiceLeaderboardWrite> result;
        if(!writeScope_||!writeScope_->active)return result;
        for(const auto& key:writeScope_->dirty) {
            const auto& entry=entriesByLeaderboardKeyEXT_.at(key);const auto& identity=identities_.at(key);
            ServiceLeaderboardWrite row;row.userId=owner_->serviceUserId_;row.key=identity.getKeyProperty();row.mode=identity.getGameModeProperty();row.rating=entry.getRatingProperty();
            for(const auto& [name,value]:entry.getColumnsProperty()) {
                ServiceLeaderboardColumn column;
                if(auto* v=std::any_cast<int>(&value)){column.type="int32";column.value=static_cast<long long>(*v);}
                else if(auto* v=std::any_cast<long long>(&value)){column.type="int64";column.value=*v;}
                else if(auto* v=std::any_cast<float>(&value)){column.type="single";column.value=static_cast<double>(*v);}
                else if(auto* v=std::any_cast<double>(&value)){column.type="double";column.value=*v;}
                else if(auto* v=std::any_cast<std::string>(&value)){column.type="string";column.value=*v;}
                else if(auto* v=std::any_cast<System::DateTime>(&value)){column.type="datetime";column.value=v->getTicksProperty();}
                else if(auto* v=std::any_cast<System::TimeSpan>(&value)){column.type="timespan";column.value=v->getTicksProperty();}
                else if(auto* v=std::any_cast<LeaderboardOutcome>(&value)){column.type="outcome";column.value=static_cast<long long>(*v);}
                else if(auto* v=std::any_cast<System::IO::Stream*>(&value)) {
                    // A Stream column: its whole contents, as the service carries them (hex, 256 bytes at most).
                    const auto* memory=dynamic_cast<const System::IO::MemoryStream*>(*v);
                    if(!memory)throw System::NotSupportedException("Only a leaderboard's own column streams can be written.");
                    const auto bytes=memory->ToArray();
                    if(bytes.size()>CNA::Internal::GamerServices::MaxLeaderboardStreamBytes)
                        throw System::ArgumentException("A leaderboard stream column holds at most 256 bytes.");
                    constexpr char digits[]="0123456789abcdef";std::string hex;
                    for(auto byte:bytes){hex+=digits[byte>>4];hex+=digits[byte&15];}
                    column.type="stream";column.value=std::move(hex);
                }
                else throw System::NotSupportedException("The service leaderboard column type has no supported transport representation.");
                row.columns.emplace(name,std::move(column));
            }
            result.push_back(std::move(row));
        }
        return result;
    }

}
