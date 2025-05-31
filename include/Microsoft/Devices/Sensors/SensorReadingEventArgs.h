//
// Created by robertvokac on 5/25/25.
//

#ifndef SENSORREADINGEVENTARGS_H
#define SENSORREADINGEVENTARGS_H


namespace Microsoft::Devices::Sensors {
    template<typename T>
    class SensorReadingEventArgs {

    private: T SensorReading_;
    public: T SensorReadingProperty() const;
    public: void SensorReadingProperty(T property);
    public: SensorReadingEventArgs();
    };

}


#endif //SENSORREADINGEVENTARGS_H
