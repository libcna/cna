//
// Created by robertvokac on 6/1/25.
//

#pragma once

namespace CNA {
    enum class DesktopOS {
        Windows,
        Linux,
        MacOSX,
        Other
    };
    DesktopOS getCurrentDesktopOS();
}


