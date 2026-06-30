// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/Achievement.hpp"
#include "System/NotImplementedException.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    Achievement::Achievement(
        const std::string& key,
        const std::string& name,
        const std::string& description,
        bool showBeforeEarned,
        bool earned,
        System::DateTime earnedDateTime
    )
        : description_(description)
        , displayBeforeEarned_(showBeforeEarned)
        , earnedDateTime_(earnedDateTime)
        , earnedOnline_(true)
        , gamerScore_(0)
        , howToEarn_()
        , isEarned_(earned)
        , key_(key)
        , name_(name)
    {
    }

    Achievement Achievement::CreateInternal(
        const std::string& key,
        const std::string& name,
        const std::string& description,
        bool showBeforeEarned,
        bool earned,
        System::DateTime earnedDateTime
    )
    {
        return Achievement(key, name, description, showBeforeEarned, earned, earnedDateTime);
    }

    const std::string& Achievement::getDescriptionProperty() const    { return description_; }
    bool Achievement::getDisplayBeforeEarnedProperty() const         { return displayBeforeEarned_; }
    System::DateTime Achievement::getEarnedDateTimeProperty() const  { return earnedDateTime_; }
    bool Achievement::getEarnedOnlineProperty() const                { return earnedOnline_; }
    int Achievement::getGamerScoreProperty() const                   { return gamerScore_; }
    const std::string& Achievement::getHowToEarnProperty() const     { return howToEarn_; }
    bool Achievement::getIsEarnedProperty() const                    { return isEarned_; }
    const std::string& Achievement::getKeyProperty() const           { return key_; }
    const std::string& Achievement::getNameProperty() const          { return name_; }

    System::IO::Stream* Achievement::GetPicture()
    {
        throw System::NotImplementedException();
    }
}
