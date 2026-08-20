// plans/plan_dx.md Phase DX12 (DX-106).
#include "CNA/Internal/Renderers/DirectX12/D3D12ResourceStateTracker.hpp"

#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX12
{
    void D3D12ResourceStateTracker::TrackResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES initialState)
    {
        states_[resource] = initialState;
    }

    bool D3D12ResourceStateTracker::TransitionTo(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
                                                  D3D12_RESOURCE_STATES desiredState)
    {
        auto it = states_.find(resource);
        if (it == states_.end())
            throw std::runtime_error("D3D12ResourceStateTracker::TransitionTo: resource was never registered via TrackResource()");

        if (it->second == desiredState)
            return false; // Already in the requested state -- no redundant barrier, per this class's own contract.

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = it->second;
        barrier.Transition.StateAfter = desiredState;

        commandList->ResourceBarrier(1, &barrier);
        it->second = desiredState;
        return true;
    }

    D3D12_RESOURCE_STATES D3D12ResourceStateTracker::GetTrackedStateEXT(ID3D12Resource* resource) const
    {
        auto it = states_.find(resource);
        if (it == states_.end())
            throw std::runtime_error("D3D12ResourceStateTracker::GetTrackedStateEXT: resource was never registered via TrackResource()");
        return it->second;
    }

    bool D3D12ResourceStateTracker::IsTrackedEXT(ID3D12Resource* resource) const
    {
        return states_.find(resource) != states_.end();
    }
}
