// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    GamerPrivileges::GamerPrivileges()
        : allowCommunication_(GamerPrivilegeSetting::Everyone)
        , allowOnlineSessions_(true)
        , allowPremiumContent_(true)
        , allowProfileViewing_(GamerPrivilegeSetting::Everyone)
        , allowPurchaseContent_(true)
        , allowTradeContent_(true)
        , allowUserCreatedContent_(GamerPrivilegeSetting::Everyone)
    {
    }

    GamerPrivileges GamerPrivileges::CreateInternal()
    {
        return GamerPrivileges();
    }

    GamerPrivilegeSetting GamerPrivileges::getAllowCommunicationProperty() const     { return allowCommunication_; }
    bool GamerPrivileges::getAllowOnlineSessionsProperty() const                     { return allowOnlineSessions_; }
    bool GamerPrivileges::getAllowPremiumContentProperty() const                     { return allowPremiumContent_; }
    GamerPrivilegeSetting GamerPrivileges::getAllowProfileViewingProperty() const    { return allowProfileViewing_; }
    bool GamerPrivileges::getAllowPurchaseContentProperty() const                    { return allowPurchaseContent_; }
    bool GamerPrivileges::getAllowTradeContentProperty() const                       { return allowTradeContent_; }
    GamerPrivilegeSetting GamerPrivileges::getAllowUserCreatedContentProperty() const{ return allowUserCreatedContent_; }
}
