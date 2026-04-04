//
// Created by robertvokac on 5/25/25.
//

#pragma once
#include <functional>

namespace System {
    template<typename T>

    class EventHandler {
        using EventHandlerI = std::function<void(const T &)>;

    private:
        std::vector<EventHandlerI> currentValueChangedHandlers;

    public:
        EventHandler& operator+=(EventHandlerI handler) {
            currentValueChangedHandlers.push_back(handler);
            return *this;
        }

    public:
        void RaiseCurrentValueChanged(const T& value) {
            for (auto& handler : currentValueChangedHandlers) {
                handler(value);
            }
        }
    };


} // System
