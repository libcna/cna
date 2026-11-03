// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Graphics/Texture.hpp"
#include "System/InvalidOperationException.hpp"
#include <typeinfo>

extern "C" int cna_test_texture_format_capability_exception(void)
{
    using namespace Microsoft::Xna::Framework::Graphics;
    try {
        Texture::ValidateFormat(SurfaceFormat::Color);
    } catch (...) {
        return 0;
    }
    try {
        Texture::ValidateFormat(SurfaceFormat::Bgr565);
    } catch (const System::InvalidOperationException& error) {
        return typeid(error) == typeid(System::InvalidOperationException);
    } catch (...) {
        return 0;
    }
    return 0;
}
