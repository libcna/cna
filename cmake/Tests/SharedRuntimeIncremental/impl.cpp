// SPDX-License-Identifier: MS-PL
#include "layout.hpp"
#include "virtual.hpp"
int Implementation() { return 1; }
int Opaque() { Virtual object; return GetLayout().read() + ReadVirtual(object); }
