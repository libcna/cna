//
// Created by robertvokac on 6/1/25.
//

#include "CNA/Platform.hpp"

#if defined(__APPLE__)
#  include <TargetConditionals.h>
#endif

namespace CNA {

    Platform getCurrentPlatform() {
#if defined(__EMSCRIPTEN__)
        return Platform::Web;
#elif defined(__ANDROID__)
        return Platform::Android;
#elif defined(__APPLE__)
#  if TARGET_OS_IPHONE
        return Platform::iOS;
#  else
        return Platform::Desktop;
#  endif
#else
        return Platform::Desktop;
#endif
    }

} // CNA