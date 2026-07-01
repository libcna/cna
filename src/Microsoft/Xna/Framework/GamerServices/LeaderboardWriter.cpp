// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardWriter.hpp"
#include "System/NotSupportedException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    LeaderboardEntry* LeaderboardWriter::GetLeaderboard(const LeaderboardIdentity& /*leaderboardId*/)
    {
        throw System::NotSupportedException();
    }
}
