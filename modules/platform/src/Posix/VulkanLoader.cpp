// SPDX-License-Identifier: MS-PL

#include "VulkanLoader.hpp"

#include <initializer_list>

#include <dlfcn.h>

namespace CNA::Platform::Posix {

    VkGetInstanceProcAddrFn ResolveVulkanProcAddr()
    {
        static VkGetInstanceProcAddrFn resolved = [] {
            if (auto* fromProcess = reinterpret_cast<VkGetInstanceProcAddrFn>(dlsym(RTLD_DEFAULT, "vkGetInstanceProcAddr")))
            {
                return fromProcess;
            }
            // The versioned SONAME first: it is what an application links, and the unversioned
            // name exists only where the development package is installed.
            for (const char* name : {"libvulkan.so.1", "libvulkan.so"})
            {
                if (void* handle = dlopen(name, RTLD_NOW | RTLD_LOCAL))
                {
                    if (auto* entry = reinterpret_cast<VkGetInstanceProcAddrFn>(dlsym(handle, "vkGetInstanceProcAddr")))
                    {
                        return entry;
                    }
                    dlclose(handle);
                }
            }
            return static_cast<VkGetInstanceProcAddrFn>(nullptr);
        }();
        return resolved;
    }

} // namespace CNA::Platform::Posix
