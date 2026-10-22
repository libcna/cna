// SPDX-License-Identifier: MS-PL
#include "CNA/Version.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#if __has_include("Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp")
#error The Math target leaked the graphics include root.
#endif
#if __has_include("System/Xml/XmlReader.hpp")
#error The Math target leaked the XML include root.
#endif
int main()
{
    using Microsoft::Xna::Framework::Vector3;
    return Vector3::Distance(Vector3::Zero, Vector3(3, 4, 0)) == 5 ? 0 : 1;
}
