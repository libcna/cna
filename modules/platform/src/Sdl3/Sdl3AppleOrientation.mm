// SPDX-License-Identifier: MS-PL
#import <UIKit/UIKit.h>

#include <SDL3/SDL.h>

#include "Sdl3AppleOrientation.hpp"

namespace CNA::Platform::Sdl3 {

    void RequestAppleOrientationUpdate(SDL_Window* window)
    {
        if (window == nullptr)
        {
            return;
        }

        @autoreleasepool
        {
            const SDL_PropertiesID properties = SDL_GetWindowProperties(window);
            void* nativeWindow = SDL_GetPointerProperty(
                properties, SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
            UIWindow* uiWindow = (__bridge UIWindow*) nativeWindow;
            UIViewController* controller = uiWindow.rootViewController;
            if (controller == nil)
            {
                return;
            }

            // The UIKit view controller re-reads the orientation set each time UIKit asks for
            // supportedInterfaceOrientations. The application sets its preference after the
            // initial window exists, so UIKit's cached answer has to be invalidated explicitly.
            // CNA's iOS floor is 16.3 (floating-point std::to_chars availability), so the modern
            // instance API is always present. Keeping the pre-iOS-16 class-method fallback would
            // only compile a dead, deprecated branch.
            [controller setNeedsUpdateOfSupportedInterfaceOrientations];
        }
    }

} // namespace CNA::Platform::Sdl3
