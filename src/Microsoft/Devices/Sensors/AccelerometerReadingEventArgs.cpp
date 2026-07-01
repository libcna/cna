// SPDX-License-Identifier: MS-PL

#include "Microsoft/Devices/Sensors/AccelerometerReadingEventArgs.hpp"

#include <functional>
#include <sstream>

namespace Microsoft::Devices::Sensors
{
    AccelerometerReadingEventArgs::AccelerometerReadingEventArgs()
        : X_(0.0),
          Y_(0.0),
          Z_(0.0),
          Timestamp_(System::DateTimeOffset())
    {
    }

    AccelerometerReadingEventArgs::AccelerometerReadingEventArgs(
        double x, double y, double z, const System::DateTimeOffset& timestamp)
        : X_(x),
          Y_(y),
          Z_(z),
          Timestamp_(timestamp)
    {
    }

    double AccelerometerReadingEventArgs::getXProperty() const
    {
        return X_;
    }

    void AccelerometerReadingEventArgs::setXProperty(double value)
    {
        X_ = value;
    }

    double AccelerometerReadingEventArgs::getYProperty() const
    {
        return Y_;
    }

    void AccelerometerReadingEventArgs::setYProperty(double value)
    {
        Y_ = value;
    }

    double AccelerometerReadingEventArgs::getZProperty() const
    {
        return Z_;
    }

    void AccelerometerReadingEventArgs::setZProperty(double value)
    {
        Z_ = value;
    }

    const System::DateTimeOffset& AccelerometerReadingEventArgs::getTimestampProperty() const
    {
        return Timestamp_;
    }

    void AccelerometerReadingEventArgs::setTimestampProperty(const System::DateTimeOffset& value)
    {
        Timestamp_ = value;
    }

    bool AccelerometerReadingEventArgs::operator==(const AccelerometerReadingEventArgs& other) const
    {
        return X_ == other.X_ && Y_ == other.Y_ && Z_ == other.Z_ && Timestamp_ == other.Timestamp_;
    }

    bool AccelerometerReadingEventArgs::operator!=(const AccelerometerReadingEventArgs& other) const
    {
        return !(*this == other);
    }

    std::string AccelerometerReadingEventArgs::ToString() const
    {
        std::ostringstream s;
        s << "{X:" << X_ << " Y:" << Y_ << " Z:" << Z_ << "}";
        return s.str();
    }

    std::size_t AccelerometerReadingEventArgs::GetHashCode() const
    {
        std::size_t h = std::hash<double>{}(X_);
        h ^= std::hash<double>{}(Y_) << 1u;
        h ^= std::hash<double>{}(Z_) << 1u;
        h ^= std::hash<std::int64_t>{}(Timestamp_.getUtcTicksProperty()) << 1u;
        return h;
    }

    std::string AccelerometerReadingEventArgs::GetTypeName() const
    {
        return "Microsoft.Devices.Sensors.AccelerometerReadingEventArgs";
    }
} // namespace Microsoft::Devices::Sensors
