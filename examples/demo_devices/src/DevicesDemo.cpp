#include "DevicesDemo.hpp"

#include <algorithm>
#include <sstream>

#include "Microsoft/Devices/Sensors/SensorReadingEventArgs.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/Exception.hpp"
#include "System/Object.hpp"
#include "System/TimeSpan.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;
using Microsoft::Devices::Sensors::SensorReadingEventArgs;

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------

static const Color BG          {15,  15,  20,  255};
static const Color SECT_BG     {25,  25,  35,  255};
static const Color SUPPORTED   {0,   200, 80,  255};
static const Color UNSUPPORTED {90,  30,  30,  255};
static const Color FLASH_ON    {255, 220, 60,  255};
static const Color FLASH_OFF   {50,  50,  40,  255};
static const Color BAR_BG      {40,  40,  40,  255};
static const Color BAR_POS     {80,  160, 255, 255};
static const Color BAR_NEG     {255, 120, 80,  255};
static const Color BAR_FILL    {80,  200, 120, 255};
static const Color KEY_OFF     {50,  50,  50,  255};
static const Color KEY_ON      {70,  220, 70,  255};

static const Color STATE_NOT_SUPPORTED {90,  90,  90,  255};
static const Color STATE_READY         {0,   200, 80,  255};
static const Color STATE_INITIALIZING  {220, 200, 0,   255};
static const Color STATE_NO_DATA       {160, 100, 0,   255};
static const Color STATE_NO_PERMS      {200, 40,  40,  255};
static const Color STATE_DISABLED      {120, 60,  180, 255};

// Frames a "just happened" flash indicator stays lit before fading back off.
static constexpr int FlashDurationFrames = 15;

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

DevicesDemo::DevicesDemo()
    : pixel_()
{
    System::TimeSpan target = System::TimeSpan::FromMilliseconds(1000.0 / 60.0);
    Game::setTargetElapsedTimeProperty(target);
    Game::getWindowProperty().setTitleProperty("CNA Devices Demo");

    vibrateController_ = VibrateController::getDefaultProperty();

    accelerometer_.CurrentValueChanged +=
        [this](System::Object*, const SensorReadingEventArgs<AccelerometerReading>& args)
        {
            latestAccelReading_ = args.getSensorReadingProperty();
            ++accelEventCount_;
            accelFramesSinceEvent_ = 0;
        };

    compass_.CurrentValueChanged +=
        [this](System::Object*, const SensorReadingEventArgs<CompassReading>& args)
        {
            latestCompassReading_ = args.getSensorReadingProperty();
            ++compassEventCount_;
            compassFramesSinceEvent_ = 0;
        };

    gyroscope_.CurrentValueChanged +=
        [this](System::Object*, const SensorReadingEventArgs<GyroscopeReading>& args)
        {
            latestGyroReading_ = args.getSensorReadingProperty();
            ++gyroEventCount_;
            gyroFramesSinceEvent_ = 0;
        };

    motion_.CurrentValueChanged +=
        [this](System::Object*, const SensorReadingEventArgs<MotionReading>& args)
        {
            latestMotionReading_ = args.getSensorReadingProperty();
            ++motionEventCount_;
            motionFramesSinceEvent_ = 0;
        };

    // Accelerometer/Gyroscope are real on platforms SDL3 supports; Compass/Motion
    // always throw SensorFailedException (no SDL3 magnetometer API on any
    // platform — see their own class doc comments) so their failure here is
    // expected, not an error worth surfacing.
    try { accelerometer_.Start(); accelStarted_ = true; } catch (const System::Exception&) {}
    try { gyroscope_.Start(); gyroStarted_ = true; } catch (const System::Exception&) {}
    try { compass_.Start(); } catch (const System::Exception&) {}
    try { motion_.Start(); } catch (const System::Exception&) {}
}

DevicesDemo::~DevicesDemo()
{
    delete spriteBatch_;
}

// ---------------------------------------------------------------------------
// LoadContent
// ---------------------------------------------------------------------------

void DevicesDemo::LoadContent()
{
    spriteBatch_ = new Graphics::SpriteBatch(getGraphicsDeviceProperty());

    pixel_ = Graphics::Texture2D(getGraphicsDeviceProperty(), 1, 1);
    Color white{255, 255, 255, 255};
    pixel_.SetData(&white, 1);
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void DevicesDemo::Update(GameTime& gameTime)
{
    (void)gameTime;

    const KbState kb = Keyboard::GetState();
    if (kb.IsKeyDown(Keys::Escape))
    {
        Exit();
    }

    HandleVibrationInput(kb);
    HandleSensorToggleInput(kb);
    HandleTimeBetweenUpdatesInput(kb);
    previousKeyboardState_ = kb;

    ++accelFramesSinceEvent_;
    ++compassFramesSinceEvent_;
    ++gyroFramesSinceEvent_;
    ++motionFramesSinceEvent_;
    ++vibrateStartFlash_;
    ++vibrateStopFlash_;

    // Throttled so the title bar doesn't repaint every single frame.
    if (++frameCounter_ % 10 == 0)
    {
        UpdateWindowTitle();
    }
}

void DevicesDemo::HandleVibrationInput(const KbState& kb)
{
    auto justPressed = [&](Keys key)
    {
        return kb.IsKeyDown(key) && !previousKeyboardState_.IsKeyDown(key);
    };

    // D1: plain XNA-compliant Start(TimeSpan) at full intensity.
    if (justPressed(Keys::D1))
    {
        vibrateController_->Start(System::TimeSpan::FromSeconds(1));
        vibrateStartFlash_ = 0;
    }
    // D2/D3: NOXNA intensity extension, low vs. full strength.
    else if (justPressed(Keys::D2))
    {
        vibrateController_->Start(System::TimeSpan::FromSeconds(1), 0.3f);
        vibrateStartFlash_ = 0;
    }
    else if (justPressed(Keys::D3))
    {
        vibrateController_->Start(System::TimeSpan::FromSeconds(1), 1.0f);
        vibrateStartFlash_ = 0;
    }
    // D4/D5/D6: NOXNA dual-motor StartLeftRight — large-only, small-only, both.
    else if (justPressed(Keys::D4))
    {
        vibrateController_->StartLeftRight(1.0f, 0.0f, System::TimeSpan::FromSeconds(1));
        vibrateStartFlash_ = 0;
    }
    else if (justPressed(Keys::D5))
    {
        vibrateController_->StartLeftRight(0.0f, 1.0f, System::TimeSpan::FromSeconds(1));
        vibrateStartFlash_ = 0;
    }
    else if (justPressed(Keys::D6))
    {
        vibrateController_->StartLeftRight(1.0f, 1.0f, System::TimeSpan::FromSeconds(1));
        vibrateStartFlash_ = 0;
    }

    if (justPressed(Keys::Space))
    {
        vibrateController_->Stop();
        vibrateStopFlash_ = 0;
    }
}

// Task P9-6: 'A'/'G' toggle the Accelerometer/Gyroscope between Start() and
// Stop() — previously these two were only ever Start()'d once, in the
// constructor, with no interactive way to exercise Stop()/re-Start() on a
// running demo. Both wrapped in try/catch matching the constructor's own
// unsupported-platform handling; the on-screen State indicator (already
// drawn every frame) reflects the result (Ready green vs. Disabled purple)
// without needing its own separate visual element.
void DevicesDemo::HandleSensorToggleInput(const KbState& kb)
{
    auto justPressed = [&](Keys key)
    {
        return kb.IsKeyDown(key) && !previousKeyboardState_.IsKeyDown(key);
    };

    if (justPressed(Keys::A))
    {
        try
        {
            if (accelStarted_) { accelerometer_.Stop(); accelStarted_ = false; }
            else { accelerometer_.Start(); accelStarted_ = true; }
        }
        catch (const System::Exception&) {}
    }

    if (justPressed(Keys::G))
    {
        try
        {
            if (gyroStarted_) { gyroscope_.Stop(); gyroStarted_ = false; }
            else { gyroscope_.Start(); gyroStarted_ = true; }
        }
        catch (const System::Exception&) {}
    }
}

// Task DEMO-001: Numpad +/- double/halve timeBetweenUpdates_ (clamped to
// [1ms, 1000ms], a range comfortably spanning "as fast as this codebase
// will ever allow" to "clearly visibly throttled"), applied to all four
// sensors identically via their own SensorBase<T>::setTimeBetweenUpdatesProperty()
// -- a safe call whether or not each sensor is currently started
// (ACCEL-005/GYRO-004/ANDROID-BRIDGE-002's own contract: forwarded live to
// a running backend, or simply stored for the next Start() otherwise).
void DevicesDemo::HandleTimeBetweenUpdatesInput(const KbState& kb)
{
    auto justPressed = [&](Keys key)
    {
        return kb.IsKeyDown(key) && !previousKeyboardState_.IsKeyDown(key);
    };

    constexpr double MinMs = 1.0;
    constexpr double MaxMs = 1000.0;

    bool changed = false;
    double ms = timeBetweenUpdates_.getTotalMillisecondsProperty();

    if (justPressed(Keys::Add))
    {
        ms = std::min(ms * 2.0, MaxMs);
        changed = true;
    }
    else if (justPressed(Keys::Subtract))
    {
        ms = std::max(ms / 2.0, MinMs);
        changed = true;
    }

    if (changed)
    {
        timeBetweenUpdates_ = System::TimeSpan::FromMilliseconds(ms);
        accelerometer_.setTimeBetweenUpdatesProperty(timeBetweenUpdates_);
        gyroscope_.setTimeBetweenUpdatesProperty(timeBetweenUpdates_);
        compass_.setTimeBetweenUpdatesProperty(timeBetweenUpdates_);
        motion_.setTimeBetweenUpdatesProperty(timeBetweenUpdates_);
    }
}

void DevicesDemo::UpdateWindowTitle()
{
    const Vector3& accel        = latestAccelReading_.getAccelerationProperty();
    const Vector3& rot          = latestGyroReading_.getRotationRateProperty();
    const Vector3& magnetometer = latestCompassReading_.getMagnetometerReadingProperty();
    const Vector3& deviceAccel  = latestMotionReading_.getDeviceAccelerationProperty();
    const Vector3& deviceRot    = latestMotionReading_.getDeviceRotationRateProperty();
    const Vector3& gravity      = latestMotionReading_.getGravityProperty();
    const auto& attitude        = latestMotionReading_.getAttitudeProperty();

    // Task P9-6: added IsDataValid for Accelerometer/Gyroscope (previously
    // shown nowhere — only the underlying SensorState indicator square,
    // which doesn't distinguish "started but no reading has arrived yet"
    // from "the last reading is/isn't trustworthy"), the VibrateController
    // NOXNA device name diagnostic (getDeviceNameProperty(), previously
    // queried nowhere in this demo), and a Compass/Motion support note —
    // this demo deliberately has no SpriteFont/Content dependency (see this
    // class's own header comment), so the window title remains its one
    // text-output channel for all of this, not a crash or silent blank
    // reading. Task DEVICES-0137: the Compass/Motion note is now dynamic,
    // not a hardcoded "not supported by SDL backend" claim — both are real,
    // Detail::AndroidCompassBackend/AndroidMotionBackend-backed on Android
    // (this demo's constructor already calls compass_.Start()/motion_.Start(),
    // see the try/catch there), still an honest SDL3-has-no-magnetometer
    // stub everywhere else.
    //
    // Task DEMO-001 (2026-07-06): added Compass/Motion IsDataValid (only
    // Accelerometer/Gyroscope had this before — a real, previously-unnoticed
    // gap in "show every sensor's ... data validity"), every field of
    // CompassReading/MotionReading (previously only MagneticHeading/
    // TrueHeading were visible anywhere, via the on-screen bars — the raw
    // MagnetometerReading vector and HeadingAccuracy were not shown at all;
    // Motion's DeviceRotationRate and Attitude were not shown at all, and
    // DeviceAcceleration/Gravity only showed their X component via the
    // on-screen bars), and the current TimeBetweenUpdates value (so a
    // tester can see what HandleTimeBetweenUpdatesInput()'s Numpad +/-
    // controls actually changed it to). The on-screen bars remain a partial
    // "at a glance" view (unchanged by this task); this title bar is the
    // one channel that must carry literally everything, per this class's
    // own established rationale above and per Task DEMO-001's "enough data
    // to write a useful bug report from its output alone" requirement.
    std::ostringstream title;
    title.setf(std::ios::fixed);
    title.precision(3);
    title << "CNA Devices Demo | "
          << "[A/G toggle sensors, Numpad +/- rate, 1-6 vibrate, Space stop] | "
          << "TBU=" << timeBetweenUpdates_.getTotalMillisecondsProperty() << "ms"
          << " | Accel(" << accel.X << "," << accel.Y << "," << accel.Z << ") #" << accelEventCount_
          << " valid=" << (accelerometer_.getIsDataValidProperty() ? "Y" : "N")
          << " | Gyro(" << rot.X << "," << rot.Y << "," << rot.Z << ") #" << gyroEventCount_
          << " valid=" << (gyroscope_.getIsDataValidProperty() ? "Y" : "N")
          << " | Compass " << (Compass::getIsSupportedProperty() ? "supported" : "unsupported")
          << " valid=" << (compass_.getIsDataValidProperty() ? "Y" : "N")
          << " magHeading=" << latestCompassReading_.getMagneticHeadingProperty()
          << " trueHeading=" << latestCompassReading_.getTrueHeadingProperty()
          << " accuracy=" << latestCompassReading_.getHeadingAccuracyProperty()
          << " mag(" << magnetometer.X << "," << magnetometer.Y << "," << magnetometer.Z << ")"
          << " | Motion " << (Motion::getIsSupportedProperty() ? "supported" : "unsupported")
          << " valid=" << (motion_.getIsDataValidProperty() ? "Y" : "N")
          << " devAccel(" << deviceAccel.X << "," << deviceAccel.Y << "," << deviceAccel.Z << ")"
          << " gravity(" << gravity.X << "," << gravity.Y << "," << gravity.Z << ")"
          << " devRotRate(" << deviceRot.X << "," << deviceRot.Y << "," << deviceRot.Z << ")"
          << " attitude(pitch=" << attitude.getPitchProperty()
          << ",roll=" << attitude.getRollProperty()
          << ",yaw=" << attitude.getYawProperty() << ")"
          << " | Vibrate " << (vibrateController_->getIsSupportedProperty() ? "supported" : "unsupported")
          << " (" << vibrateController_->getDeviceNameProperty() << ")";

    Game::getWindowProperty().setTitleProperty(title.str());
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void DevicesDemo::Draw(const GameTime& gameTime)
{
    (void)gameTime;

    getGraphicsDeviceProperty().Clear(BG);

    spriteBatch_->Begin();

    DrawRect(10,  10,  380, 140, SECT_BG);  // accelerometer
    DrawRect(10,  160, 380, 140, SECT_BG);  // compass
    DrawRect(10,  310, 380, 140, SECT_BG);  // gyroscope
    DrawRect(10,  460, 380, 140, SECT_BG);  // motion
    DrawRect(400, 10,  260, 590, SECT_BG);  // vibration

    DrawAccelerometerSection(20, 20);
    DrawCompassSection(20, 170);
    DrawGyroscopeSection(20, 320);
    DrawMotionSection(20, 470);
    DrawVibrationSection(410, 20);

    spriteBatch_->End();
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

void DevicesDemo::DrawRect(int x, int y, int w, int h, Color color)
{
    Rect dest{x, y, w, h};
    Rect src{0, 0, 1, 1};
    spriteBatch_->Draw(pixel_, dest, src, color);
}

void DevicesDemo::DrawSupportedIndicator(int x, int y, bool supported)
{
    DrawRect(x, y, 24, 24, supported ? SUPPORTED : UNSUPPORTED);
}

void DevicesDemo::DrawStateIndicator(int x, int y, SensorState state)
{
    Color color = STATE_NOT_SUPPORTED;
    switch (state)
    {
    case SensorState::Ready:         color = STATE_READY;        break;
    case SensorState::Initializing:  color = STATE_INITIALIZING; break;
    case SensorState::NoData:        color = STATE_NO_DATA;      break;
    case SensorState::NoPermissions: color = STATE_NO_PERMS;     break;
    case SensorState::Disabled:      color = STATE_DISABLED;     break;
    case SensorState::NotSupported:  color = STATE_NOT_SUPPORTED; break;
    }
    DrawRect(x, y, 24, 24, color);
}

void DevicesDemo::DrawEventFlash(int x, int y, int framesSinceEvent)
{
    DrawRect(x, y, 24, 24, framesSinceEvent < FlashDurationFrames ? FLASH_ON : FLASH_OFF);
}

void DevicesDemo::DrawSignedBar(int x, int y, int w, int h, float value, float maxAbs)
{
    DrawRect(x, y, w, h, BAR_BG);

    const int center = x + w / 2;
    const float clamped = std::max(-maxAbs, std::min(value, maxAbs));
    const int half = static_cast<int>((clamped / maxAbs) * static_cast<float>(w / 2));

    if (half >= 0)
    {
        DrawRect(center, y, half, h, BAR_POS);
    }
    else
    {
        DrawRect(center + half, y, -half, h, BAR_NEG);
    }

    // Zero-line marker.
    DrawRect(center - 1, y, 2, h, Color{200, 200, 200, 255});
}

void DevicesDemo::DrawUnsignedBar(int x, int y, int w, int h, float value, float maxValue)
{
    DrawRect(x, y, w, h, BAR_BG);
    const float clamped = std::max(0.0f, std::min(value, maxValue));
    const int fill = static_cast<int>((clamped / maxValue) * static_cast<float>(w));
    if (fill > 0)
    {
        DrawRect(x, y, fill, h, BAR_FILL);
    }
}

// ---------------------------------------------------------------------------
// Per-sensor sections (ox, oy = top-left of section content)
// ---------------------------------------------------------------------------

void DevicesDemo::DrawAccelerometerSection(int ox, int oy)
{
    DrawSupportedIndicator(ox, oy, Accelerometer::getIsSupportedProperty());
    DrawStateIndicator(ox + 30, oy, accelerometer_.getStateProperty());
    DrawEventFlash(ox + 60, oy, accelFramesSinceEvent_);

    const Vector3& accel = latestAccelReading_.getAccelerationProperty();
    constexpr float MaxG = 2.0f;
    DrawSignedBar(ox, oy + 34, 360, 18, accel.X, MaxG);
    DrawSignedBar(ox, oy + 56, 360, 18, accel.Y, MaxG);
    DrawSignedBar(ox, oy + 78, 360, 18, accel.Z, MaxG);
}

void DevicesDemo::DrawCompassSection(int ox, int oy)
{
    DrawSupportedIndicator(ox, oy, Compass::getIsSupportedProperty());
    DrawStateIndicator(ox + 30, oy, compass_.getStateProperty());
    DrawEventFlash(ox + 60, oy, compassFramesSinceEvent_);

    // Task DEVICES-0137: real on Android (Detail::AndroidCompassBackend) —
    // draws whatever compass_ actually reports there; still an honest
    // NotSupported stub everywhere else (see Compass.hpp), so these bars
    // draw at 0 on non-Android platforms, faithfully, not as an oversight.
    DrawUnsignedBar(ox, oy + 34, 360, 18, latestCompassReading_.getMagneticHeadingProperty(), 360.0f);
    DrawUnsignedBar(ox, oy + 56, 360, 18, latestCompassReading_.getTrueHeadingProperty(), 360.0f);
}

void DevicesDemo::DrawGyroscopeSection(int ox, int oy)
{
    DrawSupportedIndicator(ox, oy, Gyroscope::getIsSupportedProperty());
    DrawStateIndicator(ox + 30, oy, gyroscope_.getStateProperty());
    DrawEventFlash(ox + 60, oy, gyroFramesSinceEvent_);

    const Vector3& rot = latestGyroReading_.getRotationRateProperty();
    constexpr float MaxRadPerSec = 5.0f;
    DrawSignedBar(ox, oy + 34, 360, 18, rot.X, MaxRadPerSec);
    DrawSignedBar(ox, oy + 56, 360, 18, rot.Y, MaxRadPerSec);
    DrawSignedBar(ox, oy + 78, 360, 18, rot.Z, MaxRadPerSec);
}

void DevicesDemo::DrawMotionSection(int ox, int oy)
{
    DrawSupportedIndicator(ox, oy, Motion::getIsSupportedProperty());
    DrawStateIndicator(ox + 30, oy, motion_.getStateProperty());
    DrawEventFlash(ox + 60, oy, motionFramesSinceEvent_);

    // Task DEVICES-0137: real on Android (Detail::AndroidMotionBackend, which
    // does not depend on a live Compass instance — see Motion.hpp's updated
    // comment) — draws whatever motion_ actually reports there; still an
    // honest NotSupported stub everywhere else, so DeviceAcceleration/Gravity
    // draw at 0 on non-Android platforms, faithfully, not as an oversight.
    const Vector3& deviceAccel = latestMotionReading_.getDeviceAccelerationProperty();
    const Vector3& gravity     = latestMotionReading_.getGravityProperty();
    constexpr float MaxG = 2.0f;
    DrawSignedBar(ox, oy + 34, 360, 18, deviceAccel.X, MaxG);
    DrawSignedBar(ox, oy + 56, 360, 18, gravity.X, MaxG);
}

void DevicesDemo::DrawVibrationSection(int ox, int oy)
{
    DrawSupportedIndicator(ox, oy, vibrateController_->getIsSupportedProperty());

    auto keyRow = [&](int row, bool pressed)
    {
        DrawRect(ox, oy + 34 + row * 34, 60, 28, pressed ? KEY_ON : KEY_OFF);
    };

    const KbState kb = Keyboard::GetState();
    keyRow(0, kb.IsKeyDown(Keys::D1));
    keyRow(1, kb.IsKeyDown(Keys::D2));
    keyRow(2, kb.IsKeyDown(Keys::D3));
    keyRow(3, kb.IsKeyDown(Keys::D4));
    keyRow(4, kb.IsKeyDown(Keys::D5));
    keyRow(5, kb.IsKeyDown(Keys::D6));
    keyRow(6, kb.IsKeyDown(Keys::Space));

    DrawEventFlash(ox + 70, oy + 34, vibrateStartFlash_);
    DrawEventFlash(ox + 70, oy + 34 + 6 * 34, vibrateStopFlash_);
}

GetTypeNameCPP(DevicesDemo, "DevicesDemo")
