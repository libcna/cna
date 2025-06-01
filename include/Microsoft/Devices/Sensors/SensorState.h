//
// Created by robertvokac on 6/1/25.
//

#ifndef SENSORSTATE_H
#define SENSORSTATE_H

namespace Microsoft::Devices::Sensors {
    enum SensorState {
        NotSupported,
        Ready,
        Initializing,
        NoData,
        NoPermissions,
        Disabled,
    };
}
#endif //SENSORSTATE_H
