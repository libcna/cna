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
            if (@available(iOS 16.0, *))
            {
                [controller setNeedsUpdateOfSupportedInterfaceOrientations];
            }
            else
            {
                [UIViewController attemptRotationToDeviceOrientation];
            }
        }
    }
}
