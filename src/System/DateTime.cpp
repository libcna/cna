//
// Created by robertvokac on 6/7/25.
//

#include "System/DateTime.hpp"

namespace System {

    DateTime::DateTime()
        : ticks_(0) {
    }

    DateTime::DateTime(longcs ticks)
        : ticks_(ticks) {
    }

    longcs DateTime::getTicksProperty() const {
        return ticks_;
    }

    DateTime DateTime::Add(const TimeSpan& value) const {
        return DateTime(ticks_ + value.getTicksProperty());
    }

    DateTime DateTime::Subtract(const TimeSpan& value) const {
        return DateTime(ticks_ - value.getTicksProperty());
    }

    TimeSpan DateTime::Subtract(const DateTime& value) const {
        return TimeSpan(ticks_ - value.ticks_);
    }

    std::string DateTime::ToString() const {
        return "DateTime(" + std::to_string(ticks_) + ")";
    }

    bool DateTime::operator==(const DateTime& other) const {
        return ticks_ == other.ticks_;
    }

    bool DateTime::operator!=(const DateTime& other) const {
        return ticks_ != other.ticks_;
    }

    bool DateTime::operator<(const DateTime& other) const {
        return ticks_ < other.ticks_;
    }

    bool DateTime::operator<=(const DateTime& other) const {
        return ticks_ <= other.ticks_;
    }

    bool DateTime::operator>(const DateTime& other) const {
        return ticks_ > other.ticks_;
    }

    bool DateTime::operator>=(const DateTime& other) const {
        return ticks_ >= other.ticks_;
    }
    GetTypeNameCPP(DateTime, "System::DateTime")

} // namespace System