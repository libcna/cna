#import <UIKit/UIKit.h>

#include <SDL3/SDL.h>

#include "CNA/Internal/AppleOrientation.hpp"

namespace CNA::Internal
{
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

            // SDL's UIKit view controller reads SDL_HINT_ORIENTATIONS each time UIKit asks for
            // supportedInterfaceOrientations. The XNA property is set after Game has constructed
            // its initial window, so explicitly invalidate UIKit's cached answer here.
            // CNA's iOS floor is 16.3 (floating-point std::to_chars availability), so the modern
            // instance API is always present. Keeping the pre-iOS-16 class-method fallback would
            // only compile a dead, deprecated branch.
            [controller setNeedsUpdateOfSupportedInterfaceOrientations];
        }
    }
}
