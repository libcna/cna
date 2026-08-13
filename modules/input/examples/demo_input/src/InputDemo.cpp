#include "InputDemo.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "Microsoft/Xna/Framework/Input/GamePadButtons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDPad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadTriggers.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocation.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "System/TimeSpan.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;
using namespace Microsoft::Xna::Framework::Input::Touch;

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------

static const Color BG         {15,  15,  20,  255};
static const Color KEY_OFF    {50,  50,  50,  255};
static const Color KEY_ON     {70,  220, 70,  255};
static const Color MOUSE_OFF  {30,  30,  80,  255};
static const Color MOUSE_ON   {80,  140, 255, 255};
static const Color TOUCH_CLR  {255, 200, 0,   255};
static const Color GP_OFF     {60,  30,  10,  255};
static const Color GP_ON      {255, 130, 0,   255};
static const Color DPAD_OFF   {50,  50,  50,  255};
static const Color DPAD_ON    {210, 210, 210, 255};
static const Color TRIG_BG    {40,  40,  40,  255};
static const Color TRIG_FILL  {200, 80,  0,   255};
static const Color RUMBLE_FILL{255, 60,  180, 255};
static const Color STICK_BG   {30,  30,  30,  255};
static const Color STICK_DOT  {200, 200, 200, 255};
static const Color CONN_YES   {0,   200, 80,  255};
static const Color CONN_NO    {160, 30,  30,  255};
static const Color SECT_BG    {25,  25,  35,  255};
static const Color CURSOR_CLR {255, 80,  80,  255};

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

InputDemo::InputDemo()
    : pixel_()
{
    System::TimeSpan target = System::TimeSpan::FromMilliseconds(1000.0 / 60.0);
    Game::setTargetElapsedTimeProperty(target);
    Game::getWindowProperty().setTitleProperty("CNA Input & Touch Demo");

    TouchPanel::setEnabledGesturesProperty(
        GestureType::Tap | GestureType::FreeDrag | GestureType::Flick);
}

InputDemo::~InputDemo()
{
    // Detach the text-input callbacks before destruction so no event can call into a
    // destroyed InputDemo, and stop text input mode.
    TextInputEXT::TextInput = nullptr;
    TextInputEXT::TextEditing = nullptr;
    TextInputEXT::StopTextInput();
    delete spriteBatch_;
}

// ---------------------------------------------------------------------------
// LoadContent
// ---------------------------------------------------------------------------

void InputDemo::LoadContent()
{
    platformCapabilities_ = CNA::Platform::GetCurrentPlatform().GetCapabilities();
    spriteBatch_ = new Graphics::SpriteBatch(getGraphicsDeviceProperty());

    // Create a 1×1 white pixel used for all rectangle drawing.
    pixel_ = Graphics::Texture2D(getGraphicsDeviceProperty(), 1, 1);
    Color white{255, 255, 255, 255};
    pixel_.SetData(&white, 1);

    // Text input: collect committed characters and IME composition draft. The window
    // handle is published by GraphicsDevice during initialization, so StartTextInput
    // here targets the real window.
    if (!platformCapabilities_.textInput)
    {
        // Keep the remaining inspector useful on a terminal/headless host without trying to
        // start an unavailable IME/text service.
        return;
    }

    TextInputEXT::TextInput = [this](charcs c)
    {
        lastTextChar_ = static_cast<int>(c);
        switch (c)
        {
        case 8:  // Backspace control char (synthesized from the Back key)
            if (!textBuffer_.empty()) textBuffer_.pop_back();
            pendingHighSurrogate_ = 0;
            break;
        case 13: // Enter control char clears the line
            textBuffer_.clear();
            pendingHighSurrogate_ = 0;
            break;
        default:
            AppendTextCodeUnit(c);
            break;
        }
    };
    TextInputEXT::TextEditing = [this](const std::string& text, int, int)
    {
        editBuffer_ = text;
    };

    TextInputEXT::StartTextInput();
    textInputActive_ = true;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

void InputDemo::AppendTextCodeUnit(const charcs c)
{
    // Combine UTF-16 surrogate pairs into a single code point, then append as UTF-8.
    std::uint32_t cp;
    if (c >= 0xD800 && c <= 0xDBFF)
    {
        pendingHighSurrogate_ = c; // high surrogate — wait for its low surrogate
        return;
    }
    if (c >= 0xDC00 && c <= 0xDFFF)
    {
        if (pendingHighSurrogate_ == 0) return; // unpaired low surrogate — drop defensively
        cp = 0x10000u
           + ((static_cast<std::uint32_t>(pendingHighSurrogate_) - 0xD800u) << 10)
           + (static_cast<std::uint32_t>(c) - 0xDC00u);
        pendingHighSurrogate_ = 0;
    }
    else
    {
        pendingHighSurrogate_ = 0;
        cp = c;
    }

    if (textBuffer_.size() > 64) return; // keep the demo line bounded

    if (cp < 0x80)
    {
        textBuffer_ += static_cast<char>(cp);
    }
    else if (cp < 0x800)
    {
        textBuffer_ += static_cast<char>(0xC0 | (cp >> 6));
        textBuffer_ += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        textBuffer_ += static_cast<char>(0xE0 | (cp >> 12));
        textBuffer_ += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        textBuffer_ += static_cast<char>(0x80 | (cp & 0x3F));
    }
    else
    {
        textBuffer_ += static_cast<char>(0xF0 | (cp >> 18));
        textBuffer_ += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        textBuffer_ += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        textBuffer_ += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

void InputDemo::Update(GameTime& gameTime)
{
    (void)gameTime;
    ++frame_;

    const KbState kb = Keyboard::GetState();
    if (kb.IsKeyDown(Keys::Escape))
    {
        Exit();
    }

    // F1 toggles text input on/off, exercising StartTextInput / StopTextInput end-to-end.
    const bool toggle = platformCapabilities_.textInput && kb.IsKeyDown(Keys::F1);
    if (toggle && !prevToggleKey_)
    {
        textInputActive_ = !textInputActive_;
        if (textInputActive_) TextInputEXT::StartTextInput();
        else                  TextInputEXT::StopTextInput();
    }
    prevToggleKey_ = toggle;

    // INP-0219: F2 toggles relative mouse mode (pointer lock); F3 warps the cursor. Both exercise the
    // Mouse EXT/warp APIs end-to-end. In relative mode SetPosition is a documented no-op.
    const bool relKey = platformCapabilities_.relativeMouse && kb.IsKeyDown(Keys::F2);
    if (relKey && !prevRelKey_)
    {
        relativeMouse_ = !relativeMouse_;
        Mouse::setIsRelativeMouseModeEXTProperty(relativeMouse_);
    }
    prevRelKey_ = relKey;

    const bool warpKey = platformCapabilities_.pixelAccurateMouse && kb.IsKeyDown(Keys::F3);
    if (warpKey && !prevWarpKey_)
    {
        Mouse::SetPosition(507, 300); // warp toward the window centre (exercises SetPosition/warp)
        warpFlash_ = 30;
    }
    prevWarpKey_ = warpKey;
    if (warpFlash_ > 0) --warpFlash_;

    TouchPanel::Update();

    // INP-0220: drain the recognized-gesture queue (ReadGesture) and remember the latest, so the demo
    // surfaces recognized gestures (Tap/FreeDrag/Flick are enabled in LoadContent), not just raw points.
    while (TouchPanel::getIsGestureAvailableProperty())
    {
        const GestureSample g = TouchPanel::ReadGesture();
        lastGesture_  = static_cast<int>(g.getGestureTypeProperty());
        gestureFlash_ = 45;
    }
    if (gestureFlash_ > 0) --gestureFlash_;

    // Drive rumble motors from trigger pressure for every player slot (One..Four), exercising
    // GamePad::SetVibration end-to-end. Harmless no-op (returns false) for disconnected slots.
    if (platformCapabilities_.gamepad && platformCapabilities_.gamepadRumble)
    {
        for (int p = 0; p < 4; ++p)
        {
            const auto playerIndex = static_cast<PlayerIndex>(p);
            const auto& trig = GamePad::GetState(playerIndex).getTriggersProperty();
            GamePad::SetVibration(playerIndex, trig.getLeftProperty(), trig.getRightProperty());
        }
    }

    // INP-0221: cycle Player One's light bar and read its motion sensors. All are capability-gated:
    // no-ops / false when the controller is absent or lacks the feature (harmless on other pads).
    if (platformCapabilities_.gamepad)
    {
        const auto p1  = PlayerIndex::One;
        const auto hue = static_cast<std::uint8_t>((frame_ * 2) & 0xFF);
        lightBar_ = Color(hue, static_cast<std::uint8_t>(255 - hue), std::uint8_t{128}, std::uint8_t{255});
        GamePad::SetLightBarEXT(p1, lightBar_);
        if (platformCapabilities_.gamepadSensors)
        {
            Vector3 g, a;
            haveGyro_  = GamePad::GetGyroEXT(p1, g);          if (haveGyro_)  gyro_  = g;
            haveAccel_ = GamePad::GetAccelerometerEXT(p1, a); if (haveAccel_) accel_ = a;
        }
    }
}

// ---------------------------------------------------------------------------
// Draw
// ---------------------------------------------------------------------------

void InputDemo::Draw(const GameTime& gameTime)
{
    (void)gameTime;

    getGraphicsDeviceProperty().Clear(BG);

    const KbState kb      = Keyboard::GetState();
    const MsState ms      = Mouse::GetState();
    const GpState gpOne   = GamePad::GetState(PlayerIndex::One);
    const GpState gpTwo   = GamePad::GetState(PlayerIndex::Two);
    const GpState gpThree = GamePad::GetState(PlayerIndex::Three);
    const GpState gpFour  = GamePad::GetState(PlayerIndex::Four);
    const TC      tc      = TouchPanel::GetState();

    spriteBatch_->Begin();

    // Section backgrounds
    DrawRect(10,  10,  440, 380, SECT_BG);  // keyboard section
    DrawRect(10,  400, 440, 190, SECT_BG);  // mouse section
    DrawRect(460, 10,  330, 580, SECT_BG);  // gamepad section (Player One, detailed)
    DrawRect(800, 10,  204, 580, SECT_BG);  // gamepad section (Players Two-Four, compact)
    DrawRect(10,  600, 1004, 158, SECT_BG); // text input section

    DrawKeyboard(20, 20, kb);
    DrawMouse(20, 410, ms);
    DrawGamePad(470, 20, gpOne);
    DrawGamePadMini(810, 20,  gpTwo);
    DrawGamePadMini(810, 210, gpThree);
    DrawGamePadMini(810, 400, gpFour);
    DrawTouchPoints(tc);
    DrawTextPanel(10, 600, 1004, 158);

    spriteBatch_->End();
}

// ---------------------------------------------------------------------------
// Primitives
// ---------------------------------------------------------------------------

void InputDemo::DrawRect(int x, int y, int w, int h, Color color)
{
    Rect dest{x, y, w, h};
    Rect src{0, 0, 1, 1};
    spriteBatch_->Draw(pixel_, dest, src, color);
}

void InputDemo::DrawKey(int x, int y, int w, int h, bool pressed)
{
    DrawRect(x, y, w, h, pressed ? KEY_ON : KEY_OFF);
}

void InputDemo::DrawButton(int x, int y, int w, int h, bool pressed, Color onColor)
{
    DrawRect(x, y, w, h, pressed ? onColor : GP_OFF);
}

void InputDemo::DrawBar(int x, int y, int bw, int bh, float value,
                        Color bgColor, Color fillColor)
{
    DrawRect(x, y, bw, bh, bgColor);
    int fill = static_cast<int>(value * static_cast<float>(bw));
    if (fill > 0)
    {
        DrawRect(x, y, fill, bh, fillColor);
    }
}

void InputDemo::DrawStick(int cx, int cy, int radius, float vx, float vy)
{
    DrawRect(cx - radius, cy - radius, radius * 2, radius * 2, STICK_BG);
    // Dot position: map [-1,1] to pixel offset
    const int dx = static_cast<int>(vx * static_cast<float>(radius - 4));
    const int dy = static_cast<int>(-vy * static_cast<float>(radius - 4));
    DrawRect(cx + dx - 4, cy + dy - 4, 8, 8, STICK_DOT);
}

// ---------------------------------------------------------------------------
// Keyboard section (ox, oy = top-left of section content)
// ---------------------------------------------------------------------------

void InputDemo::DrawKeyboard(int ox, int oy, const KbState& kb)
{
    // Each key cell: 32px wide, 28px tall, 3px gap -> step = 35, 31
    constexpr int KW  = 32;
    constexpr int KH  = 28;
    constexpr int SX  = 35; // step x
    constexpr int SY  = 31; // step y

    auto key = [&](int col, int row, int colSpan, Keys k)
    {
        DrawKey(ox + col * SX, oy + row * SY, KW + (colSpan - 1) * SX, KH,
                kb.IsKeyDown(k));
    };

    // Row 0 — special
    key(0, 0, 1, Keys::Escape);
    key(3, 0, 1, Keys::Tab);
    key(5, 0, 1, Keys::Back);
    key(7, 0, 1, Keys::Enter);
    key(9, 0, 1, Keys::LeftShift);
    key(11, 0, 1, Keys::LeftControl);

    // Row 1 — Q..P
    const Keys row1[] = {Keys::Q, Keys::W, Keys::E, Keys::R, Keys::T,
                         Keys::Y, Keys::U, Keys::I, Keys::O, Keys::P};
    for (int c = 0; c < 10; ++c) key(c, 1, 1, row1[c]);

    // Row 2 — A..L
    const Keys row2[] = {Keys::A, Keys::S, Keys::D, Keys::F, Keys::G,
                         Keys::H, Keys::J, Keys::K, Keys::L};
    for (int c = 0; c < 9; ++c) key(c, 2, 1, row2[c]);

    // Row 3 — Z..M
    const Keys row3[] = {Keys::Z, Keys::X, Keys::C, Keys::V,
                         Keys::B, Keys::N, Keys::M};
    for (int c = 0; c < 7; ++c) key(c, 3, 1, row3[c]);

    // Row 4 — Space bar
    key(0, 4, 5, Keys::Space);

    // Row 5 — arrow cluster
    key(1, 5, 1, Keys::Up);
    key(0, 6, 1, Keys::Left);
    key(1, 6, 1, Keys::Down);
    key(2, 6, 1, Keys::Right);
}

// ---------------------------------------------------------------------------
// Mouse section
// ---------------------------------------------------------------------------

void InputDemo::DrawMouse(int ox, int oy, const MsState& ms)
{
    // Buttons: L M R
    const bool ldown = ms.getLeftButtonProperty()   == BS::Pressed;
    const bool mdown = ms.getMiddleButtonProperty() == BS::Pressed;
    const bool rdown = ms.getRightButtonProperty()  == BS::Pressed;

    DrawRect(ox,       oy,      80, 50, ldown ? MOUSE_ON : MOUSE_OFF);
    DrawRect(ox + 90,  oy,      50, 50, mdown ? MOUSE_ON : MOUSE_OFF);
    DrawRect(ox + 150, oy,      80, 50, rdown ? MOUSE_ON : MOUSE_OFF);

    // Scroll bar — wheel value mapped into a relative indicator
    const float scrollNorm = 0.5f + static_cast<float>(ms.getScrollWheelValueProperty() % 200) / 200.0f;
    DrawBar(ox, oy + 60, 280, 20, std::fmod(scrollNorm, 1.0f), TRIG_BG, MOUSE_ON);

    // XButton1 and XButton2
    const bool x1 = ms.getXButton1Property() == BS::Pressed;
    const bool x2 = ms.getXButton2Property() == BS::Pressed;
    DrawRect(ox + 300, oy,      40, 50, x1 ? MOUSE_ON : MOUSE_OFF);
    DrawRect(ox + 350, oy,      40, 50, x2 ? MOUSE_ON : MOUSE_OFF);

    // Cursor indicator — clamp to section area (show relative within 440x580 window area)
    const int mx = std::max(0, std::min(ms.getXProperty(), 440));
    const int my = std::max(0, std::min(ms.getYProperty(), 580));
    DrawRect(mx - 5, my - 5, 10, 10, CURSOR_CLR);

    // --- INP-0219: relative-mode (F2, green when active) + warp flash (F3, yellow pulse).
    DrawRect(ox,      oy + 95, 30, 30, relativeMouse_ ? MOUSE_ON : MOUSE_OFF);
    DrawRect(ox + 40, oy + 95, 30, 30, warpFlash_ > 0 ? Color{255, 220, 0, 255} : MOUSE_OFF);

    // --- INP-0220: last recognized gesture (ReadGesture) — one lit cell per set GestureType bit,
    // flashing briefly after a gesture is read.
    for (int b = 0; b < 8; ++b)
    {
        const bool on = gestureFlash_ > 0 && (lastGesture_ & (1 << b)) != 0;
        DrawRect(ox + 90 + b * 16, oy + 95, 14, 30, on ? TOUCH_CLR : MOUSE_OFF);
    }
}

// ---------------------------------------------------------------------------
// GamePad section
// ---------------------------------------------------------------------------

void InputDemo::DrawGamePad(int ox, int oy, const GpState& gp)
{
    const bool connected = gp.getIsConnectedProperty();
    DrawRect(ox, oy, 310, 25, connected ? CONN_YES : CONN_NO);

    if (!connected) return;

    const auto& btns  = gp.getButtonsProperty();
    const auto& dpad  = gp.getDPadProperty();
    const auto& stick = gp.getThumbSticksProperty();
    const auto& trig  = gp.getTriggersProperty();

    auto btn = [&](int x, int y, bool pressed, Color c)
    {
        DrawRect(ox + x, oy + y, 30, 30, pressed ? c : GP_OFF);
    };
    auto dpadBtn = [&](int x, int y, bool pressed)
    {
        DrawRect(ox + x, oy + y, 28, 28, pressed ? DPAD_ON : DPAD_OFF);
    };

    // --- DPad (top-left of gamepad area)
    const int dpx = 10, dpy = 40;
    dpadBtn(dpx + 30, dpy,       dpad.getUpProperty()    == BS::Pressed);
    dpadBtn(dpx,      dpy + 30,  dpad.getLeftProperty()  == BS::Pressed);
    dpadBtn(dpx + 30, dpy + 30,  dpad.getDownProperty()  == BS::Pressed);
    dpadBtn(dpx + 60, dpy + 30,  dpad.getRightProperty() == BS::Pressed);

    // --- ABXY (diamond, right side of DPad)
    const int abx = 160, aby = 40;
    btn(abx + 30, aby,       btns.getYProperty() == BS::Pressed, Color{220, 220, 0,   255});
    btn(abx,      aby + 30,  btns.getXProperty() == BS::Pressed, Color{80,  80,  220, 255});
    btn(abx + 60, aby + 30,  btns.getBProperty() == BS::Pressed, Color{220, 60,  60,  255});
    btn(abx + 30, aby + 60,  btns.getAProperty() == BS::Pressed, Color{60,  200, 60,  255});

    // --- Start / Back
    btn(100, 55, btns.getStartProperty() == BS::Pressed,  Color{180, 180, 180, 255});
    btn(135, 55, btns.getBackProperty()  == BS::Pressed,  Color{140, 140, 140, 255});

    // --- Shoulders
    const bool lb = btns.getLeftShoulderProperty()  == BS::Pressed;
    const bool rb = btns.getRightShoulderProperty() == BS::Pressed;
    DrawRect(ox + 10,  oy + 130, 60, 22, lb ? GP_ON : GP_OFF);
    DrawRect(ox + 240, oy + 130, 60, 22, rb ? GP_ON : GP_OFF);

    // --- Triggers as bars
    DrawBar(ox + 10,  oy + 158, 130, 20, trig.getLeftProperty(),  TRIG_BG, TRIG_FILL);
    DrawBar(ox + 170, oy + 158, 130, 20, trig.getRightProperty(), TRIG_BG, TRIG_FILL);

    // --- Thumbstick clicks
    const bool ls = btns.getLeftStickProperty()  == BS::Pressed;
    const bool rs = btns.getRightStickProperty() == BS::Pressed;
    DrawRect(ox + 10,  oy + 185, 20, 20, ls ? STICK_DOT : GP_OFF);
    DrawRect(ox + 280, oy + 185, 20, 20, rs ? STICK_DOT : GP_OFF);

    // --- Left thumbstick visualizer
    DrawStick(ox + 70,  oy + 270, 55,
              stick.getLeftProperty().X,
              stick.getLeftProperty().Y);

    // --- Right thumbstick visualizer
    DrawStick(ox + 235, oy + 270, 55,
              stick.getRightProperty().X,
              stick.getRightProperty().Y);

    // --- INP-0221: light-bar swatch (the color SetLightBarEXT is driving) + gyro/accel magnitude bars.
    DrawRect(ox + 10, oy + 350, 60, 24, lightBar_);
    auto sensorBar = [&](int y, const Vector3& v, bool have)
    {
        const float mag = have
            ? std::min(1.0f, std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z) / 10.0f)
            : 0.0f;
        DrawBar(ox + 90, oy + y, 200, 18, mag, TRIG_BG, have ? MOUSE_ON : GP_OFF);
    };
    sensorBar(350, gyro_,  haveGyro_);
    sensorBar(374, accel_, haveAccel_);
}

// ---------------------------------------------------------------------------
// GamePad section (compact — Players Two/Three/Four)
// ---------------------------------------------------------------------------

void InputDemo::DrawGamePadMini(int ox, int oy, const GpState& gp)
{
    const bool connected = gp.getIsConnectedProperty();
    DrawRect(ox, oy, 184, 14, connected ? CONN_YES : CONN_NO);

    if (!connected) return;

    const auto& btns  = gp.getButtonsProperty();
    const auto& dpad  = gp.getDPadProperty();
    const auto& stick = gp.getThumbSticksProperty();
    const auto& trig  = gp.getTriggersProperty();

    auto btn = [&](int x, int y, bool pressed, Color c)
    {
        DrawRect(ox + x, oy + y, 14, 14, pressed ? c : GP_OFF);
    };
    auto dpadBtn = [&](int x, int y, bool pressed)
    {
        DrawRect(ox + x, oy + y, 12, 12, pressed ? DPAD_ON : DPAD_OFF);
    };

    // --- DPad (left)
    const int dpx = 4, dpy = 20;
    dpadBtn(dpx + 14, dpy,       dpad.getUpProperty()    == BS::Pressed);
    dpadBtn(dpx,      dpy + 14,  dpad.getLeftProperty()  == BS::Pressed);
    dpadBtn(dpx + 14, dpy + 14,  dpad.getDownProperty()  == BS::Pressed);
    dpadBtn(dpx + 28, dpy + 14,  dpad.getRightProperty() == BS::Pressed);

    // --- ABXY (diamond, right of DPad)
    const int abx = 70, aby = 20;
    btn(abx + 14, aby,      btns.getYProperty() == BS::Pressed, Color{220, 220, 0,   255});
    btn(abx,      aby + 14, btns.getXProperty() == BS::Pressed, Color{80,  80,  220, 255});
    btn(abx + 28, aby + 14, btns.getBProperty() == BS::Pressed, Color{220, 60,  60,  255});
    btn(abx + 14, aby + 28, btns.getAProperty() == BS::Pressed, Color{60,  200, 60,  255});

    // --- Shoulders
    const bool lb = btns.getLeftShoulderProperty()  == BS::Pressed;
    const bool rb = btns.getRightShoulderProperty() == BS::Pressed;
    DrawRect(ox,      oy + 58, 88, 12, lb ? GP_ON : GP_OFF);
    DrawRect(ox + 96, oy + 58, 88, 12, rb ? GP_ON : GP_OFF);

    // --- Triggers as bars
    DrawBar(ox,      oy + 74, 88, 10, trig.getLeftProperty(),  TRIG_BG, TRIG_FILL);
    DrawBar(ox + 96, oy + 74, 88, 10, trig.getRightProperty(), TRIG_BG, TRIG_FILL);

    // --- Rumble motor level currently sent to this pad (Update() mirrors it 1:1 from the
    // trigger bars above via GamePad::SetVibration); shown separately so the feedback loop
    // driving real hardware rumble is visible, not just the trigger input.
    DrawBar(ox,      oy + 88, 88, 8, trig.getLeftProperty(),  TRIG_BG, RUMBLE_FILL);
    DrawBar(ox + 96, oy + 88, 88, 8, trig.getRightProperty(), TRIG_BG, RUMBLE_FILL);

    // --- Thumbstick visualizers
    DrawStick(ox + 40,  oy + 132, 28, stick.getLeftProperty().X,  stick.getLeftProperty().Y);
    DrawStick(ox + 144, oy + 132, 28, stick.getRightProperty().X, stick.getRightProperty().Y);
}

// ---------------------------------------------------------------------------
// Touch points overlay
// ---------------------------------------------------------------------------

void InputDemo::DrawTouchPoints(const TC& touches)
{
    for (const auto& loc : touches)
    {
        const auto state = loc.getStateProperty();
        if (state == TouchLocationState::Released) continue;

        const auto& pos = loc.getPositionProperty();
        const int tx = static_cast<int>(pos.X);
        const int ty = static_cast<int>(pos.Y);

        // Outer ring
        DrawRect(tx - 18, ty - 18, 36, 36, Color{255, 200, 0, 80});
        // Inner dot
        DrawRect(tx - 8,  ty - 8,  16, 16, TOUCH_CLR);
    }
}

// ---------------------------------------------------------------------------
// Text input section (TextInputEXT)
// ---------------------------------------------------------------------------

void InputDemo::DrawTextPanel(int ox, int oy, int w, int h)
{
    (void)h;
    const Color HEADER_ON  {0,   200, 80,  255};
    const Color HEADER_OFF {160, 30,  30,  255};
    const Color CELL_PRINT {70,  220, 70,  255};
    const Color CELL_CTRL  {220, 140, 0,   255};
    const Color EDIT_CELL  {220, 220, 60,  255};
    const Color CARET      {255, 255, 255, 255};

    // Active indicator — reflects SDL's real text-input state (falls back to the toggle
    // flag when no window is available).
    const bool active = platformCapabilities_.textInput &&
                        (TextInputEXT::IsTextInputActive() || textInputActive_);
    DrawRect(ox + 12, oy + 12, 26, 26, active ? HEADER_ON : HEADER_OFF);

    // Most recent TextInput byte as 8 bit-LEDs (MSB..LSB) so the actual value is verifiable
    // (e.g. 'A' = 0100_0001, Backspace = 0000_1000, paste = 0001_0110).
    const int bx = ox + w - 8 * 22 - 14;
    for (int b = 0; b < 8; ++b)
    {
        const bool on = lastTextChar_ >= 0 && ((lastTextChar_ >> (7 - b)) & 1);
        DrawRect(bx + b * 22, oy + 12, 18, 18, on ? CELL_PRINT : KEY_OFF);
    }

    // Committed text buffer: one cell per character (green = printable, orange = control/UTF-8).
    constexpr int CW = 12, CH = 26, GAP = 2;
    const int cellsX   = ox + 12;
    const int cellsY   = oy + 56;
    const int maxCells = (w - 24) / (CW + GAP);
    const int total    = static_cast<int>(textBuffer_.size());
    const int shown    = std::min(total, maxCells);
    const int start    = total - shown;
    for (int i = 0; i < shown; ++i)
    {
        const unsigned char ch = static_cast<unsigned char>(textBuffer_[start + i]);
        const bool printable = ch >= 32 && ch < 127;
        DrawRect(cellsX + i * (CW + GAP), cellsY, CW, CH, printable ? CELL_PRINT : CELL_CTRL);
    }
    // Blinking caret while text input is active.
    if (active && (frame_ / 30) % 2 == 0)
    {
        DrawRect(cellsX + shown * (CW + GAP), cellsY, 3, CH, CARET);
    }

    // IME composition draft (TextEditing) rendered below as yellow cells.
    const int editY     = oy + 100;
    const int editShown = std::min(static_cast<int>(editBuffer_.size()), maxCells);
    for (int i = 0; i < editShown; ++i)
    {
        DrawRect(cellsX + i * (CW + GAP), editY, CW, CH, EDIT_CELL);
    }
}

GetTypeNameCPP(InputDemo, "InputDemo")
