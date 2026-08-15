#pragma once

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GameTime.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureType.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "CNA/CNAHelper.hpp"
#include "CNA/Platform/PlatformCapabilities.hpp"

#include <string>

class InputDemo : public Microsoft::Xna::Framework::Game
{
public:
    InputDemo();
    ~InputDemo() override;

    void LoadContent() override;
    void Update(Microsoft::Xna::Framework::GameTime& gameTime) override;
    void Draw(const Microsoft::Xna::Framework::GameTime& gameTime) override;

    GetTypeNameHPP()

private:
    using Color    = Microsoft::Xna::Framework::Color;
    using Keys     = Microsoft::Xna::Framework::Input::Keys;
    using BS       = Microsoft::Xna::Framework::Input::ButtonState;
    using Rect     = Microsoft::Xna::Framework::Rectangle;
    using KbState  = Microsoft::Xna::Framework::Input::KeyboardState;
    using MsState  = Microsoft::Xna::Framework::Input::MouseState;
    using GpState  = Microsoft::Xna::Framework::Input::GamePadState;
    using TC       = Microsoft::Xna::Framework::Input::Touch::TouchCollection;

    void DrawRect(int x, int y, int w, int h, Color color);
    void DrawKey(int x, int y, int w, int h, bool pressed);
    void DrawButton(int x, int y, int w, int h, bool pressed, Color onColor);
    void DrawBar(int x, int y, int bw, int bh, float value,
                 Color bgColor, Color fillColor);
    void DrawStick(int cx, int cy, int radius, float vx, float vy);

    void DrawKeyboard(int ox, int oy, const KbState& kb);
    void DrawMouse(int ox, int oy, const MsState& ms);
    void DrawGamePad(int ox, int oy, const GpState& gp);
    void DrawGamePadMini(int ox, int oy, const GpState& gp);
    void DrawTouchPoints(const TC& touches);
    void DrawTextPanel(int ox, int oy, int w, int h);

    // Appends one UTF-16 code unit from TextInputEXT::TextInput to textBuffer_ as UTF-8,
    // buffering a high surrogate until its low surrogate arrives (astral code points).
    void AppendTextCodeUnit(Microsoft::Xna::Framework::Input::charcs c);

    Microsoft::Xna::Framework::Graphics::SpriteBatch* spriteBatch_ = nullptr;
    Microsoft::Xna::Framework::Graphics::Texture2D pixel_;

    // Text input (TextInputEXT) demo state.
    std::string textBuffer_;   // committed characters typed via TextInputEXT::TextInput
    std::string editBuffer_;   // in-progress IME composition via TextInputEXT::TextEditing
    int  lastTextChar_ = -1;   // value of the most recent TextInput code unit (for the bit display)
    Microsoft::Xna::Framework::Input::charcs pendingHighSurrogate_ = 0; // buffered high surrogate, 0 if none
    bool textInputActive_ = false;
    bool prevToggleKey_ = false;
    int  frame_ = 0;

    // INP-0219: relative mouse mode (F2) + cursor warp (F3) demo state.
    bool relativeMouse_ = false;
    bool prevRelKey_    = false;
    bool prevWarpKey_   = false;
    int  warpFlash_     = 0;   // countdown frames after a warp, for the on-screen flash

    // INP-0220: recognized-gesture readout (TouchPanel::ReadGesture).
    int  lastGesture_   = 0;   // GestureType flags of the last read gesture
    int  gestureFlash_  = 0;   // countdown frames after a gesture, for the on-screen flash

    // INP-0221: gamepad light bar (SetLightBarEXT) + motion sensors (GetGyroEXT/GetAccelerometerEXT).
    Microsoft::Xna::Framework::Vector3 gyro_{};
    Microsoft::Xna::Framework::Vector3 accel_{};
    bool haveGyro_  = false;
    bool haveAccel_ = false;
    Microsoft::Xna::Framework::Color lightBar_{0, 0, 0, 255}; // color currently driven to the LED

    // Read once during startup. The demo must not turn an unavailable host service into an
    // exception merely because it illustrates every input family.
    CNA::Platform::PlatformCapabilities platformCapabilities_{};
};
