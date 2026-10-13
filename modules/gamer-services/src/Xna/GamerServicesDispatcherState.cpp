// SPDX-License-Identifier: MS-PL
// GamerServicesDispatcher's state, which the rest of Gamer Services reads (Guide.IsVisible asks
// IsInitialized).
//
// Link boundary: Initialize and Update (GamerServicesDispatcher.cpp) install and drive the whole
// system Guide; asking whether they ran must not link them.
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"

namespace Microsoft::Xna::Framework::GamerServices {
bool GamerServicesDispatcher::isInitialized_=false;
SharpRuntime::IntPtr GamerServicesDispatcher::windowHandle_=0;
std::size_t GamerServicesDispatcher::freedGamerCount_=0;
System::EventHandler<System::EventArgs> GamerServicesDispatcher::InstallingTitleUpdate;
bool GamerServicesDispatcher::getIsInitializedProperty(){return isInitialized_;}
SharpRuntime::IntPtr GamerServicesDispatcher::getWindowHandleProperty(){return windowHandle_;}
void GamerServicesDispatcher::setWindowHandleProperty(SharpRuntime::IntPtr value){windowHandle_=value;}
std::size_t GamerServicesDispatcher::GetFreedGamerCountForTesting(){return freedGamerCount_;}
}
