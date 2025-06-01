//
// Created by robertvokac on 6/1/25.
//

#ifndef PLATFORM_H
#define PLATFORM_H

namespace CNA {

    enum Platform {
        Desktop,
        Android,
        iOS,
        Web
    };
    Platform getCurrentPlatform();

} // CNA

#endif //PLATFORM_H
