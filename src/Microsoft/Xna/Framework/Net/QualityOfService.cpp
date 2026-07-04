// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/QualityOfService.hpp"

namespace Microsoft::Xna::Framework::Net
{
    QualityOfService::QualityOfService()
        : averageRoundtripTime_(System::TimeSpan::Zero)
        , minimumRoundtripTime_(System::TimeSpan::Zero)
    {
    }

    QualityOfService QualityOfService::CreateInternal()
    {
        return QualityOfService();
    }

    System::TimeSpan QualityOfService::getAverageRoundtripTimeProperty() const { return averageRoundtripTime_; }
    int QualityOfService::getBytesPerSecondDownstreamProperty() const          { return bytesPerSecondDownstream_; }
    int QualityOfService::getBytesPerSecondUpstreamProperty() const            { return bytesPerSecondUpstream_; }
    bool QualityOfService::getIsAvailableProperty() const                      { return isAvailable_; }
    System::TimeSpan QualityOfService::getMinimumRoundtripTimeProperty() const  { return minimumRoundtripTime_; }
}
