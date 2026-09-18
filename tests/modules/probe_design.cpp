// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/Design/Vector3Converter.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

#include <any>
#include <string>

int main() {
    const Microsoft::Xna::Framework::Design::Vector3Converter converter;
    const auto value = std::any_cast<Microsoft::Xna::Framework::Vector3>(
        converter.ConvertFromInvariantString("1, 2, 3"));
    return value.Z == 3.0F ? 0 : 1;
}
