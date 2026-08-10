// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <string>

namespace Microsoft::Xna::Framework::Input::Touch
{
    /**
     * @brief Represents one touch point snapshot, including an optional previous location.
     */
    struct TouchLocation
    {
        /**
         * @brief Gets the id of this touch point.
         * @return The touch id.
         */
        [[nodiscard]] int getIdProperty() const;

        /**
         * @brief Gets the current state of this touch point.
         * @return The touch location state.
         */
        [[nodiscard]] TouchLocationState getStateProperty() const;

        /**
         * @brief Gets the current position of this touch point.
         * @return The position.
         */
        [[nodiscard]] const Microsoft::Xna::Framework::Vector2& getPositionProperty() const;

        /**
         * @brief CNAEXT/EXT: gets the touch pressure of this point, in the range 0..1.
         * @note XNA 4.0 dropped `TouchLocation.Pressure`; this restores the SDL finger pressure
         *       without changing the type's XNA shape. It is 0 when the device reports no pressure.
         * @return The pressure in 0..1.
         */
        CNAEXT [[nodiscard]] float getPressureEXT() const;

        /**
         * @brief Constructs an invalid touch location.
         * @note CNAEXT — FNA's `TouchLocation` has no explicit parameterless constructor
         *       (only the two below); this exists because C++ has no implicit
         *       struct-default equivalent to write behavior into.
         */
        CNAEXT TouchLocation();

        /**
         * @brief Constructs with id, state, and position. Previous state is Invalid.
         * @param id The touch id.
         * @param state The current touch state.
         * @param position The current touch position.
         */
        TouchLocation(int id, TouchLocationState state,
                      const Microsoft::Xna::Framework::Vector2& position);

        /**
         * @brief Constructs with full previous location information.
         * @param id The touch id.
         * @param state The current touch state.
         * @param position The current touch position.
         * @param previousState The previous touch state.
         * @param previousPosition The previous touch position.
         */
        TouchLocation(int id, TouchLocationState state,
                      const Microsoft::Xna::Framework::Vector2& position,
                      TouchLocationState previousState,
                      const Microsoft::Xna::Framework::Vector2& previousPosition);

        /**
         * @brief CNAEXT/EXT: constructs with id, state, position, and pressure. Previous state is Invalid.
         * @param id The touch id.
         * @param state The current touch state.
         * @param position The current touch position.
         * @param pressure The touch pressure in 0..1.
         */
        CNAEXT TouchLocation(int id, TouchLocationState state,
                            const Microsoft::Xna::Framework::Vector2& position,
                            float pressure);

        /**
         * @brief CNAEXT/EXT: constructs with full previous location information and pressure.
         * @param id The touch id.
         * @param state The current touch state.
         * @param position The current touch position.
         * @param previousState The previous touch state.
         * @param previousPosition The previous touch position.
         * @param pressure The touch pressure in 0..1.
         */
        CNAEXT TouchLocation(int id, TouchLocationState state,
                            const Microsoft::Xna::Framework::Vector2& position,
                            TouchLocationState previousState,
                            const Microsoft::Xna::Framework::Vector2& previousPosition,
                            float pressure);

        /**
         * @brief Tries to retrieve the previous location. Returns false if previous state is Invalid.
         * @param previousLocation Output parameter receiving the previous location.
         * @return True if the previous location is valid; false otherwise.
         */
        bool TryGetPreviousLocation(TouchLocation& previousLocation) const;

        /**
         * @brief Compares this instance with another for equality.
         * @param other The other TouchLocation to compare.
         * @return True if equal; false otherwise.
         */
        [[nodiscard]] bool Equals(const TouchLocation& other) const;

        /**
         * @brief Gets the hash code for this instance.
         * @return Hash code of the object.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Returns a string representation of this touch location.
         * @return The string representation.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Compares two TouchLocation instances for equality.
         * @param value1 The first instance to compare.
         * @param value2 The second instance to compare.
         * @return True if equal; false otherwise.
         */
        friend bool operator==(const TouchLocation& value1, const TouchLocation& value2);

        /**
         * @brief Compares two TouchLocation instances for inequality.
         * @param value1 The first instance to compare.
         * @param value2 The second instance to compare.
         * @return True if not equal; false otherwise.
         */
        friend bool operator!=(const TouchLocation& value1, const TouchLocation& value2);

    private:
        int id_;
        TouchLocationState state_;
        Microsoft::Xna::Framework::Vector2 position_;
        TouchLocationState prevState_;
        Microsoft::Xna::Framework::Vector2 prevPosition_;
        // CNAEXT/EXT: SDL finger pressure (0..1). Excluded from Equals/GetHashCode/ToString so those
        // stay FNA-frozen; defaults to 0 for the XNA constructors and the parameterless one.
        float pressure_ = 0.0f;
    };
}
