//
// Created by robertvokac on 5/25/25.
//

#ifndef EVENTHANDLER_H
#define EVENTHANDLER_H
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

#endif //EVENTHANDLER_H
