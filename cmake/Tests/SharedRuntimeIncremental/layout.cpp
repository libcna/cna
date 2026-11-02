// SPDX-License-Identifier: MS-PL
#include "layout.hpp"
const Layout& GetLayout() { static Layout object; return object; }
