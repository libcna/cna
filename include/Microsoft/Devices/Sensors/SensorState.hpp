//
// Created by robertvokac on 6/1/25.
//

#pragma once

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
