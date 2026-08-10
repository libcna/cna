// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/GamerServices/AvatarExpression.hpp"

namespace Microsoft::Xna::Framework::GamerServices
{
    AvatarMouth AvatarExpression::getMouthProperty() const { return mouth_; }
    void AvatarExpression::setMouthProperty(AvatarMouth value) { mouth_ = value; }

    AvatarEye AvatarExpression::getLeftEyeProperty() const { return leftEye_; }
    void AvatarExpression::setLeftEyeProperty(AvatarEye value) { leftEye_ = value; }

    AvatarEye AvatarExpression::getRightEyeProperty() const { return rightEye_; }
    void AvatarExpression::setRightEyeProperty(AvatarEye value) { rightEye_ = value; }

    AvatarEyebrow AvatarExpression::getLeftEyebrowProperty() const { return leftEyebrow_; }
    void AvatarExpression::setLeftEyebrowProperty(AvatarEyebrow value) { leftEyebrow_ = value; }

    AvatarEyebrow AvatarExpression::getRightEyebrowProperty() const { return rightEyebrow_; }
    void AvatarExpression::setRightEyebrowProperty(AvatarEyebrow value) { rightEyebrow_ = value; }
}
