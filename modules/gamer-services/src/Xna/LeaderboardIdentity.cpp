// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardIdentity.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    std::string LeaderboardIdentity::getKeyProperty() const { return key_; }
    void LeaderboardIdentity::setKeyProperty(const std::string& value) { key_ = value; }

    int LeaderboardIdentity::getGameModeProperty() const { return gameMode_; }
    void LeaderboardIdentity::setGameModeProperty(int value) { gameMode_ = value; }

    namespace {
        std::string leaderboardKeyToString(LeaderboardKey key)
        {
            switch (key)
            {
                case LeaderboardKey::BestScoreLifeTime: return "BestScoreLifeTime";
                case LeaderboardKey::BestScoreRecent:   return "BestScoreRecent";
                case LeaderboardKey::BestTimeLifeTime:  return "BestTimeLifeTime";
                case LeaderboardKey::BestTimeRecent:    return "BestTimeRecent";
                default:                                return "";
            }
        }
    }

    LeaderboardIdentity LeaderboardIdentity::Create(LeaderboardKey key)
    {
        LeaderboardIdentity id;
        id.setKeyProperty(leaderboardKeyToString(key));
        id.setGameModeProperty(0);
        return id;
    }

    LeaderboardIdentity LeaderboardIdentity::Create(LeaderboardKey key, int gameMode)
    {
        LeaderboardIdentity id;
        id.setKeyProperty(leaderboardKeyToString(key));
        id.setGameModeProperty(gameMode);
        return id;
    }
}
