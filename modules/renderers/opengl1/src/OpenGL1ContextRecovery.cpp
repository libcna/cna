#include "CNA/Internal/Renderers/OpenGL1/OpenGL1ContextRecovery.hpp"
#include <algorithm>

namespace CNA::Internal::Renderers::OpenGL1
{
    void OpenGL1ResourceRegistry::Add(IOpenGL1Recoverable* resource)
    {
        if (resource) resources_.push_back(resource);
    }

    void OpenGL1ResourceRegistry::Remove(IOpenGL1Recoverable* resource)
    {
        resources_.erase(std::remove(resources_.begin(), resources_.end(), resource), resources_.end());
    }

    void OpenGL1ResourceRegistry::NotifyContextLost()
    {
        for (auto* resource : resources_) resource->ReleaseGLHandleOnly();
    }

    void OpenGL1ResourceRegistry::NotifyContextRestored()
    {
        for (auto* resource : resources_) resource->RecreateGLResource();
    }
}
