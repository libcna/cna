//
// Created by robertvokac on 5/26/25.
//

#ifndef SENSORFAILEDEXCEPTION_H
#define SENSORFAILEDEXCEPTION_H
#include "System/Exception.h"


namespace Microsoft::Devices::Sensors {

    class SensorFailedException : public System::Exception {
    public:
        SensorFailedException();

        explicit SensorFailedException(const char * str);
    };


}


#endif //SENSORFAILEDEXCEPTION_H
