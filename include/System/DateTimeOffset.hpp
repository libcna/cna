//
// Created by robertvokac on 6/7/25.
//

#pragma once

#include <string>

#include "System/Object.hpp"
#include "System/DateTime.hpp"
#include "System/TimeSpan.hpp"
#include "CNA/CnaHelper.hpp"

namespace System {

    /**
     * @brief Represents a point in time relative to UTC.
     *
     * This is a partial C++ counterpart of the .NET System::DateTimeOffset structure.
     *
     * @note Status: Partial.
     * @note This implementation stores a DateTime value and its UTC offset.
     */
    class DateTimeOffset : public Object {
    private:
        DateTime dateTime_;
        TimeSpan offset_;

    public:
        /**
         * @brief Initializes a new instance with zero DateTime and zero offset.
         */
        DateTimeOffset();

        /**
         * @brief Initializes a new instance with the specified DateTime and UTC offset.
         *
         * @param dateTime Date and time component.
         * @param offset UTC offset.
         */
        DateTimeOffset(const DateTime& dateTime, const TimeSpan& offset);

        /**
         * @brief Gets the DateTime component.
         *
         * @return DateTime value.
         */
        [[nodiscard]] const DateTime& getDateTimeProperty() const;

        /**
         * @brief Gets the UTC offset component.
         *
         * @return Offset from UTC.
         */
        [[nodiscard]] const TimeSpan& getOffsetProperty() const;

        /**
         * @brief Gets the UTC tick value represented by this instance.
         *
         * @return UTC tick count.
         */
        [[nodiscard]] CNA::longcs getUtcTicksProperty() const;

        /**
         * @brief Returns a string representation of the current instance.
         *
         * @return String representation.
         */
        [[nodiscard]] std::string ToString() const override;

        [[nodiscard]] bool operator==(const DateTimeOffset& other) const;
        [[nodiscard]] bool operator!=(const DateTimeOffset& other) const;
        GetTypeNameHPP()
    };

} // namespace System