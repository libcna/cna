//
// Created by robertvokac on 6/1/25.
//

#include "CNA/DesktopOS.hpp"

#include "CNA/CNAException.hpp"
#include "CNA/Platform.hpp"

namespace CNA {
    DesktopOS getCurrentDesktopOS() {
        if (getCurrentPlatform() != Platform::Desktop) {
            throw System::CNAException("Not a desktop platform.");
        }
        return DesktopOS::Linux;//TODO: Implement this.
    }

} // CNA