// SPDX-License-Identifier: MS-PL
#pragma once
#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace CNA::Internal::GamerServices::Avatars {
/** @brief Half-turn between the frozen catalog's +Z front and XNA's -Z front.
 * @return Exact, self-inverse rotation about Y (no reflection or change of handedness). */
inline Microsoft::Xna::Framework::Matrix avatarSpaceBasis()
{
    return Microsoft::Xna::Framework::Matrix::CreateScale(-1.0f, 1.0f, -1.0f);
}

/** @brief Changes a local bone transform between catalog and XNA coordinates.
 * @param transform Transform in either space. @return The same transform in the other space. */
inline Microsoft::Xna::Framework::Matrix changeAvatarSpace(const Microsoft::Xna::Framework::Matrix& transform)
{
    const auto basis = avatarSpaceBasis();
    return basis * transform * basis;
}
}
