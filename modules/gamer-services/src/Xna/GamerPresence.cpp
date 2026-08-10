// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GamerPresence.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    const std::array<std::string, 60> GamerPresence::presenceModeStrings_ = {{
        "Arcade Mode",
        "At Menu",
        "Battling Boss",
        "Campaign Mode",
        "Challenge Mode",
        "Configuring Settings",
        "Co-Op: Level {0}",
        "Co-Op: Stage {0}",
        "Cornflower Blue",
        "Customizing Player",
        "Difficulty: Easy",
        "Difficulty: Extreme",
        "Difficulty: Hard",
        "Difficulty: Medium",
        "Editing Level",
        "Exploration Mode",
        "Found Secret",
        "Free Play",
        "Game Over",
        "In Combat",
        "In Game Store",
        "Level {0}",
        "Local Co-Op",
        "Local Versus",
        "Looking For Games",
        "Losing",
        "Multiplayer",
        "Nearly Finished",
        "",
        "On a Roll",
        "Online Co-Op",
        "Online Versus",
        "Outnumbered",
        "Paused",
        "Playing Minigame",
        "Playing With Friends",
        "Practice Mode",
        "Puzzle Mode",
        "Scenario Mode",
        "Score {0}",
        "Score is Tied",
        "Setting Up Match",
        "Single Player",
        "Stage {0}",
        "Starting Game",
        "Story Mode",
        "Stuck on a Hard Bit",
        "Survival Mode",
        "Time Attack",
        "Trying For Record",
        "Tutorial Mode",
        "Versus Computer",
        "Versus: Score {0}",
        "Waiting For Players",
        "Waiting In Lobby",
        "Wasting Time",
        "Watching Credits",
        "Watching Cutscene",
        "Winning",
        "Won the Game"
    }};

    GamerPresence::GamerPresence()
        : presenceMode_(GamerPresenceMode::None)
        , presenceValue_(0)
    {
    }

    GamerPresence GamerPresence::CreateInternal()
    {
        return GamerPresence();
    }

    GamerPresenceMode GamerPresence::getPresenceModeProperty() const
    {
        return presenceMode_;
    }

    void GamerPresence::setPresenceModeProperty(GamerPresenceMode value)
    {
        auto idx = static_cast<std::size_t>(value);
        if (idx < presenceModeStrings_.size())
        {
            const std::string& s = presenceModeStrings_[idx];
            if (s != presence_)
            {
                presence_ = s;
                SetPresenceModeStringEXT(presence_);
            }
        }
        presenceMode_ = value;
    }

    int GamerPresence::getPresenceValueProperty() const
    {
        return presenceValue_;
    }

    void GamerPresence::setPresenceValueProperty(int value)
    {
        if (value != presenceValue_)
        {
            presenceValue_ = value;
            SetPresenceModeStringEXT(presence_);
        }
    }

    void GamerPresence::SetPresenceModeStringEXT(const std::string& /*mode*/)
    {
    }
}
