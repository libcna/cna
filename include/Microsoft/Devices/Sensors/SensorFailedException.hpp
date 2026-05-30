//
// Created by robertvokac on 5/26/25.
//

#pragma once
#include "System/Exception.hpp"


namespace Microsoft::Devices::Sensors
{
    class SensorFailedException : public System::Exception
    {
    public:
        SensorFailedException();

        explicit SensorFailedException(const char* str);
    };
}
