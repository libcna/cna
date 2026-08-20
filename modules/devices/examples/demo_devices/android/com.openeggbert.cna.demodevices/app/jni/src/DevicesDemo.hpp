#pragma once

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Devices/Sensors/Accelerometer.hpp"
#include "Microsoft/Devices/Sensors/AccelerometerReading.hpp"
#include "Microsoft/Devices/Sensors/Compass.hpp"
#include "Microsoft/Devices/Sensors/CompassReading.hpp"
#include "Microsoft/Devices/Sensors/Gyroscope.hpp"
#include "Microsoft/Devices/Sensors/GyroscopeReading.hpp"
#include "Microsoft/Devices/Sensors/Motion.hpp"
#include "Microsoft/Devices/Sensors/MotionReading.hpp"
#include "Microsoft/Devices/Sensors/SensorState.hpp"
#include "Microsoft/Devices/VibrateController.hpp"
#include "CNA/CNAHelper.hpp"

// Demo/manual-verification screen for Microsoft::Devices (plans/plan_devices_phase4.md
// Task P4-14). Mirrors examples/demo_input's Game-subclass, rectangle-only visual
// style (no SpriteFont/Content dependency) — see docs/devices-hardware-checklist.md
// for what a human should look for when running this on real hardware.
class DevicesDemo : public Microsoft::Xna::Framework::Game
{
public:
    DevicesDemo();
    ~DevicesDemo() override;

    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    GetTypeNameHPP()

private:
    using Color       = Microsoft::Xna::Framework::Color;
    using Rect        = Microsoft::Xna::Framework::Rectangle;
    using Keys        = Microsoft::Xna::Framework::Input::Keys;
    using KbState     = Microsoft::Xna::Framework::Input::KeyboardState;
    using SensorState = Microsoft::Devices::Sensors::SensorState;

    using Accelerometer       = Microsoft::Devices::Sensors::Accelerometer;
    using AccelerometerReading = Microsoft::Devices::Sensors::AccelerometerReading;
    using Compass             = Microsoft::Devices::Sensors::Compass;
    using CompassReading      = Microsoft::Devices::Sensors::CompassReading;
    using Gyroscope           = Microsoft::Devices::Sensors::Gyroscope;
    using GyroscopeReading    = Microsoft::Devices::Sensors::GyroscopeReading;
    using Motion              = Microsoft::Devices::Sensors::Motion;
    using MotionReading       = Microsoft::Devices::Sensors::MotionReading;
    using VibrateController   = Microsoft::Devices::VibrateController;

    // --- Drawing primitives ---
    void DrawRect(int x, int y, int w, int h, Color color);
    void DrawSupportedIndicator(int x, int y, bool supported);
    void DrawStateIndicator(int x, int y, SensorState state);
    void DrawEventFlash(int x, int y, int framesSinceEvent);
    void DrawSignedBar(int x, int y, int w, int h, float value, float maxAbs);
    void DrawUnsignedBar(int x, int y, int w, int h, float value, float maxValue);

    // --- Per-sensor sections ---
    void DrawAccelerometerSection(int ox, int oy);
    void DrawCompassSection(int ox, int oy);
    void DrawGyroscopeSection(int ox, int oy);
    void DrawMotionSection(int ox, int oy);
    void DrawVibrationSection(int ox, int oy);

    void HandleVibrationInput(const KbState& kb);
    void HandleSensorToggleInput(const KbState& kb);
    void UpdateWindowTitle();

    Microsoft::Xna::Framework::Graphics::SpriteBatch* spriteBatch_ = nullptr;
    Microsoft::Xna::Framework::Graphics::Texture2D pixel_;

    Accelerometer accelerometer_;
    Compass compass_;
    Gyroscope gyroscope_;
    Motion motion_;
    VibrateController* vibrateController_ = nullptr;

    AccelerometerReading latestAccelReading_;
    CompassReading latestCompassReading_;
    GyroscopeReading latestGyroReading_;
    MotionReading latestMotionReading_;

    int accelEventCount_ = 0;
    int compassEventCount_ = 0;
    int gyroEventCount_ = 0;
    int motionEventCount_ = 0;

    // Frames elapsed since the last CurrentValueChanged for each sensor;
    // drives DrawEventFlash()'s brief "something just happened" highlight.
    // Starts large so no sensor shows a stale flash before its first event.
    int accelFramesSinceEvent_ = 1000;
    int compassFramesSinceEvent_ = 1000;
    int gyroFramesSinceEvent_ = 1000;
    int motionFramesSinceEvent_ = 1000;

    // Same flash pattern, but for the vibration key bindings below (visual
    // confirmation a keypress was actually registered and acted on).
    int vibrateStartFlash_ = 1000;
    int vibrateStopFlash_ = 1000;

    KbState previousKeyboardState_;
    int frameCounter_ = 0;

    // Task P9-6: tracks whether the demo's own last Start()/Stop() call for
    // each real sensor succeeded, so the 'A'/'G' toggle keys below know
    // which direction to go next. Independent of getStateProperty() (which
    // can also read NotSupported/NoPermissions/etc.) — this is purely
    // "what did the demo itself last ask for."
    bool accelStarted_ = false;
    bool gyroStarted_ = false;
};
