// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndedEventArgs.hpp"

namespace Microsoft::Xna::Framework::Net
{
    NetworkSessionEndedEventArgs::NetworkSessionEndedEventArgs(NetworkSessionEndReason endReason)
        : endReason_(endReason)
    {
    }

    NetworkSessionEndReason NetworkSessionEndedEventArgs::getEndReasonProperty() const { return endReason_; }
}
