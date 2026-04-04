//
// Created by robertvokac on 6/1/25.
//

#include "CNA/Platform.hpp"

namespace CNA {

    Platform getCurrentPlatform() {
#if defined(__EMSCRIPTEN__)
        return Platform::Web;
#elif defined(__ANDROID__)
        return Platform::Android;
#elif defined(__APPLE__)
        return Platform::iOS;
#else
        return Platform::Desktop;
#endif
    }

} // CNA