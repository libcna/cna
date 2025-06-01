//
// Created by robertvokac on 6/1/25.
//

#include "CNA/DesktopOS.h"

#include "CNA/Platform.h"

namespace CNA {
    DesktopOS getCurrentDesktopOS() {
        if (getCurrentPlatform() != Desktop) {
            throw "Not a desktop platform.";
        }
        return Linux;//TODO: Implement this.
    }

} // CNA