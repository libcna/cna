// SPDX-License-Identifier: MS-PL
// Minimal-link probe for CNA::Math (plans/MODULARIZATION_PLAN.md §4): a consumer using only the
// XNA math surface must link nothing beyond the math module and sharp-runtime. The paired
// ModuleLinkClosure_Math ctest inspects this executable's generated link line.
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <cstdio>

int main()
{
    using namespace Microsoft::Xna::Framework;

    const Vector3 cross = Vector3::Cross(Vector3::UnitX, Vector3::UnitY);
    const Matrix rotation = Matrix::CreateRotationX(1.0f);
    const Rectangle a(0, 0, 4, 4);
    const Rectangle b(2, 2, 4, 4);
    const Color blue = Color::CornflowerBlue;

    std::printf("math probe: cross.Z=%f m22=%f intersects=%d packed=%u\n",
                static_cast<double>(cross.Z),
                static_cast<double>(rotation.M22),
                a.Intersects(b) ? 1 : 0,
                blue.getPackedValueProperty());
    return 0;
}
