// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/CompassReading.hpp"

#include <functional>
#include <sstream>

namespace Microsoft::Devices::Sensors
{
    CompassReading::CompassReading()
        : HeadingAccuracy_(0.0),
          MagneticHeading_(0.0),
          MagnetometerReading_(Vector3()),
          Timestamp_(System::DateTimeOffset()),
          TrueHeading_(0.0)
    {
    }

    CompassReading::CompassReading(
        double headingAccuracy,
        double magneticHeading,
        const Vector3& magnetometerReading,
        const System::DateTimeOffset& timestamp,
        double trueHeading)
        : HeadingAccuracy_(headingAccuracy),
          MagneticHeading_(magneticHeading),
          MagnetometerReading_(magnetometerReading),
          Timestamp_(timestamp),
          TrueHeading_(trueHeading)
    {
    }

    double CompassReading::getHeadingAccuracyProperty() const
    {
        return HeadingAccuracy_;
    }

    void CompassReading::setHeadingAccuracyProperty(double value)
    {
        HeadingAccuracy_ = value;
    }

    double CompassReading::getMagneticHeadingProperty() const
    {
        return MagneticHeading_;
    }

    void CompassReading::setMagneticHeadingProperty(double value)
    {
        MagneticHeading_ = value;
    }

    const Vector3& CompassReading::getMagnetometerReadingProperty() const
    {
        return MagnetometerReading_;
    }

    void CompassReading::setMagnetometerReadingProperty(const Vector3& value)
    {
        MagnetometerReading_ = value;
    }

    const System::DateTimeOffset& CompassReading::getTimestampProperty() const
    {
        return Timestamp_;
    }

    void CompassReading::setTimestampProperty(const System::DateTimeOffset& value)
    {
        Timestamp_ = value;
    }

    double CompassReading::getTrueHeadingProperty() const
    {
        return TrueHeading_;
    }

    void CompassReading::setTrueHeadingProperty(double value)
    {
        TrueHeading_ = value;
    }

    bool CompassReading::operator==(const CompassReading& other) const
    {
        return HeadingAccuracy_ == other.HeadingAccuracy_
            && MagneticHeading_ == other.MagneticHeading_
            && MagnetometerReading_ == other.MagnetometerReading_
            && Timestamp_ == other.Timestamp_
            && TrueHeading_ == other.TrueHeading_;
    }

    bool CompassReading::operator!=(const CompassReading& other) const
    {
        return !(*this == other);
    }

    std::string CompassReading::ToString() const
    {
        std::ostringstream s;
        s << "MagneticHeading:" << MagneticHeading_
          << " TrueHeading:" << TrueHeading_
          << " HeadingAccuracy:" << HeadingAccuracy_
          << " MagnetometerReading:" << MagnetometerReading_.ToString();
        return s.str();
    }

    std::size_t CompassReading::GetHashCode() const
    {
        std::size_t h = std::hash<double>{}(HeadingAccuracy_);
        h ^= std::hash<double>{}(MagneticHeading_) << 1u;
        h ^= static_cast<std::size_t>(MagnetometerReading_.GetHashCode()) << 1u;
        h ^= std::hash<std::int64_t>{}(Timestamp_.getUtcTicksProperty()) << 1u;
        h ^= std::hash<double>{}(TrueHeading_) << 1u;
        return h;
    }

    std::string CompassReading::GetTypeName() const
    {
        return "Microsoft.Devices.Sensors.CompassReading";
    }
} // namespace Microsoft::Devices::Sensors
