//
// Created by robertvokac on 6/1/25.
//

#ifndef DESKTOPOS_H
#define DESKTOPOS_H

namespace CNA {
    enum DesktopOS {
        Windows,
        Linux,
        MacOSX,
        Other
    };
    DesktopOS getCurrentDesktopOS();
}


#endif //DESKTOPOS_H
