// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    SignedInGamerCollection* Gamer::signedInGamers_ = nullptr;

    Gamer::Gamer(const std::string& gamertag, const std::string& displayName)
        : displayName_(displayName)
        , gamertag_(gamertag)
    {
    }

    const std::string& Gamer::getDisplayNameProperty() const  { return displayName_; }
    void Gamer::setDisplayNameProperty(const std::string& v)  { displayName_ = v; }
    const std::string& Gamer::getGamertagProperty() const     { return gamertag_; }
    bool Gamer::getIsDisposedProperty() const                 { return isDisposed_; }
    std::any& Gamer::getTagProperty()                         { return tag_; }
    void Gamer::setTagProperty(const std::any& v)             { tag_ = v; }

    SignedInGamerCollection* Gamer::getSignedInGamersProperty()
    {
        if (!signedInGamers_)
            signedInGamers_ = new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({}));
        return signedInGamers_;
    }
}
