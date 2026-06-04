#include "Microsoft/Xna/Framework/Input/MouseState.hpp"

#include <string>

namespace Microsoft::Xna::Framework::Input
{
    MouseState::MouseState()
        : x_(0), y_(0),
          leftButton_(ButtonState::Released),
          rightButton_(ButtonState::Released),
          middleButton_(ButtonState::Released),
          xButton1_(ButtonState::Released),
          xButton2_(ButtonState::Released),
          scrollWheelValue_(0)
    {
    }

    MouseState::MouseState(int x, int y, int scrollWheel,
                           ButtonState leftButton, ButtonState middleButton,
                           ButtonState rightButton,
                           ButtonState xButton1, ButtonState xButton2)
        : x_(x), y_(y),
          leftButton_(leftButton),
          rightButton_(rightButton),
          middleButton_(middleButton),
          xButton1_(xButton1),
          xButton2_(xButton2),
          scrollWheelValue_(scrollWheel)
    {
    }

    int         MouseState::getXProperty()               const { return x_; }
    int         MouseState::getYProperty()               const { return y_; }
    ButtonState MouseState::getLeftButtonProperty()      const { return leftButton_; }
    ButtonState MouseState::getRightButtonProperty()     const { return rightButton_; }
    ButtonState MouseState::getMiddleButtonProperty()    const { return middleButton_; }
    ButtonState MouseState::getXButton1Property()        const { return xButton1_; }
    ButtonState MouseState::getXButton2Property()        const { return xButton2_; }
    int         MouseState::getScrollWheelValueProperty() const { return scrollWheelValue_; }

    bool MouseState::Equals(const MouseState& other) const
    {
        return x_               == other.x_               &&
               y_               == other.y_               &&
               leftButton_      == other.leftButton_      &&
               rightButton_     == other.rightButton_     &&
               middleButton_    == other.middleButton_    &&
               xButton1_        == other.xButton1_        &&
               xButton2_        == other.xButton2_        &&
               scrollWheelValue_ == other.scrollWheelValue_;
    }

    int MouseState::GetHashCode() const
    {
        return x_ ^ (y_ * 31) ^ (scrollWheelValue_ * 17);
    }

    std::string MouseState::ToString() const
    {
        return "[MouseState X=" + std::to_string(x_) +
               " Y=" + std::to_string(y_) + "]";
    }

    bool operator==(const MouseState& left, const MouseState& right)
    {
        return left.Equals(right);
    }

    bool operator!=(const MouseState& left, const MouseState& right)
    {
        return !(left == right);
    }
}
