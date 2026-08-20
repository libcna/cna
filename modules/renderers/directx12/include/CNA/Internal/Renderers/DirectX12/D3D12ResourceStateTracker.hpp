#pragma once

// plans/plan_dx.md Phase DX12 (DX-106): explicit, deliberate per-resource D3D12_RESOURCE_STATES tracking
// -- the plan's own row calls this "the single biggest source of 'silently wrong' bugs in a first
// D3D12 renderer" and requires real state tracking, not ad-hoc barrier calls scattered through draw
// code. This class is the single source of truth for "what state is this ID3D12Resource currently
// in," and the only place that emits a D3D12_RESOURCE_BARRIER transition.
//
// Deliberately standalone and resource-content-agnostic: DX-109 (actual vertex/index buffers,
// textures, render targets) hasn't landed yet, so this is proven here against any ID3D12Resource
// (a throwaway committed buffer in the smoke test is sufficient to prove the tracking logic itself
// -- see that test's own comment for why this doesn't need a real render-target pipeline).

#include <d3d12.h>

#include <map>

namespace CNA::Internal::Renderers::DirectX12
{
    /// Tracks the current D3D12_RESOURCE_STATES of every ID3D12Resource it's been told about, and
    /// emits a transition barrier via ID3D12GraphicsCommandList::ResourceBarrier only when a
    /// requested state genuinely differs from the last-known one -- never a redundant barrier for a
    /// state the resource is already in.
    ///
    /// Not thread-safe (matches every other per-device D3D12/D3D11 renderer object in this project --
    /// GraphicsDevice usage is single-threaded).
    class D3D12ResourceStateTracker
    {
    public:
        /// Registers @p resource as currently being in @p initialState (its actual state right after
        /// creation, e.g. D3D12_RESOURCE_STATE_COMMON for CreateCommittedResource's own default, or
        /// whatever state a resource is documented to start in). Overwrites any prior tracked state
        /// for this pointer -- callers re-registering an already-tracked resource are treating it as
        /// freshly (re)created, which is a legitimate use (e.g. after a device-removed recreation,
        /// DX-110).
        void TrackResource(ID3D12Resource* resource, D3D12_RESOURCE_STATES initialState);

        /// If @p resource's last-known tracked state differs from @p desiredState, records and
        /// submits (via ResourceBarrier on @p commandList) a single D3D12_RESOURCE_BARRIER_TYPE_
        /// TRANSITION barrier from the tracked state to @p desiredState, then updates the tracked
        /// state to @p desiredState. If the tracked state already equals @p desiredState, does
        /// nothing (no barrier emitted) -- this is the actual point of the class, not an
        /// optimization afterthought.
        ///
        /// Throws std::runtime_error if @p resource was never registered via TrackResource() --
        /// transitioning an untracked resource's state is exactly the kind of "ad-hoc barrier call"
        /// this class exists to prevent; every resource must have a known starting state first.
        ///
        /// Returns true if a barrier was actually emitted, false if the resource was already in
        /// @p desiredState (real proof for tests that redundant barriers are genuinely skipped, not
        /// just documented as skipped).
        bool TransitionTo(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
                          D3D12_RESOURCE_STATES desiredState);

        /// Returns the last-known tracked state for @p resource. Throws std::runtime_error if it was
        /// never registered via TrackResource().
        [[nodiscard]] D3D12_RESOURCE_STATES GetTrackedStateEXT(ID3D12Resource* resource) const;

        /// Returns whether @p resource currently has any tracked state at all (CNAEXT diagnostics/
        /// tests -- distinguishes "never tracked" from "tracked, and happens to be in state X").
        [[nodiscard]] bool IsTrackedEXT(ID3D12Resource* resource) const;

        /// Drops all tracked state (CNAEXT -- device-lost recovery, DX-110, will need this once every
        /// tracked resource's D3D12 object is itself being recreated from scratch).
        void Clear() { states_.clear(); }

        /// Number of currently-tracked resources (CNAEXT diagnostics).
        [[nodiscard]] std::size_t GetTrackedCountEXT() const { return states_.size(); }

    private:
        std::map<ID3D12Resource*, D3D12_RESOURCE_STATES> states_;
    };
}
