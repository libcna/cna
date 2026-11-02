// SPDX-License-Identifier: MS-PL
#include "virtual.hpp"
Virtual::~Virtual() = default;
int Virtual::value() const { return 11; }
int ReadVirtual(const Virtual& object) { return object.value(); }
