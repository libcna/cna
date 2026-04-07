//
// Created by robertvokac on 6/7/25.
//

#include "System/DateTimeOffset.hpp"

namespace System {

    DateTimeOffset::DateTimeOffset()
        : dateTime_(DateTime()),
          offset_(TimeSpan::Zero) {
    }

    DateTimeOffset::DateTimeOffset(const DateTime& dateTime, const TimeSpan& offset)
        : dateTime_(dateTime),
          offset_(offset) {
    }

    const DateTime& DateTimeOffset::getDateTimeProperty() const {
        return dateTime_;
    }

    const TimeSpan& DateTimeOffset::getOffsetProperty() const {
        return offset_;
    }

    CNA::longcs DateTimeOffset::getUtcTicksProperty() const {
        return dateTime_.getTicksProperty() - offset_.getTicksProperty();
    }

    std::string DateTimeOffset::ToString() const {
        return "DateTimeOffset(" + dateTime_.ToString() + ", Offset=" + offset_.ToString() + ")";
    }

    bool DateTimeOffset::operator==(const DateTimeOffset& other) const {
        return getUtcTicksProperty() == other.getUtcTicksProperty();
    }

    bool DateTimeOffset::operator!=(const DateTimeOffset& other) const {
        return !(*this == other);
    }
    GetTypeNameCPP(DateTimeOffset, "System::DateTimeOffset")

} // namespace System