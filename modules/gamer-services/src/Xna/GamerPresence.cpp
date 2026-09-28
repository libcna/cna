// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GamerPresence.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    const std::array<std::string, 60> GamerPresence::presenceModeStrings_ = {{
        "",
        "Single Player",
        "Multiplayer",
        "Local Co-Op",
        "Local Versus",
        "Online Co-Op",
        "Online Versus",
        "Versus Computer",
        "Stage {0}",
        "Level {0}",
        "Co-Op: Stage {0}",
        "Co-Op: Level {0}",
        "Arcade Mode",
        "Campaign Mode",
        "Challenge Mode",
        "Exploration Mode",
        "Practice Mode",
        "Puzzle Mode",
        "Scenario Mode",
        "Story Mode",
        "Survival Mode",
        "Tutorial Mode",
        "Difficulty: Easy",
        "Difficulty: Medium",
        "Difficulty: Hard",
        "Difficulty: Extreme",
        "Score {0}",
        "Versus: Score {0}",
        "Winning",
        "Losing",
        "Score is Tied",
        "Outnumbered",
        "On a Roll",
        "In Combat",
        "Battling Boss",
        "Time Attack",
        "Trying For Record",
        "Free Play",
        "Wasting Time",
        "Stuck on a Hard Bit",
        "Nearly Finished",
        "Looking For Games",
        "Waiting For Players",
        "Waiting In Lobby",
        "Setting Up Match",
        "Playing With Friends",
        "At Menu",
        "Starting Game",
        "Paused",
        "Game Over",
        "Won the Game",
        "Configuring Settings",
        "Customizing Player",
        "Editing Level",
        "In Game Store",
        "Watching Cutscene",
        "Watching Credits",
        "Playing Minigame",
        "Found Secret",
        "Cornflower Blue"
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
        if(value==presenceMode_)return;
        presenceMode_=value;
        const auto index=static_cast<std::size_t>(value);
        presence_=index<presenceModeStrings_.size()?presenceModeStrings_[index]:std::string{};
        changed_=true;++revision_;
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
            changed_=true;++revision_;
        }
    }

    void GamerPresence::SetPresenceModeStringEXT(const std::string& mode)
    {
        presence_=mode;changed_=true;++revision_;
    }
}
