// SPDX-License-Identifier: MS-PL
#include "virtual.hpp"
class Derived : public Virtual {
public:
    int value() const override { return 13; }
};
int main() { Derived object; return ReadVirtual(object) == 13 ? 0 : 1; }
