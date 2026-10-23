// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/OcclusionSampleCount.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>

namespace CNA::Internal::Renderers
{
    class LogicalOcclusionQuery;

    struct OcclusionIntervalSegment
    {
        std::unique_ptr<IOcclusionQueryRenderer> native;
        bool ended = false;
        bool complete = false;
        bool precise = true;
        int count = 0;

        bool Poll()
        {
            if (!ended) return false;
            if (!complete && native->IsComplete())
            {
                count = native->PixelCount();
                complete = true;
                // Other logical intervals may still need the cached count, not its native slot.
                native.reset();
            }
            return complete;
        }
    };

    // Recovered XNA OcclusionQuery.Begin and the genuine overlap oracle allow independent
    // intervals. GL and several queued renderers can measure only one native interval at once.
    // Partition the command stream at logical Begin/End boundaries: every member of the active
    // set shares that segment's result. No draw is replayed and no depth/stencil side effect changes.
    class OcclusionQueryIntervals
    {
    public:
        OcclusionQueryIntervals(IGraphicsRenderer& renderer,
                                std::unique_ptr<IOcclusionQueryRenderer> native)
            : renderer(renderer), spare(std::make_shared<OcclusionIntervalSegment>())
        {
            spare->native = std::move(native);
            precise = spare->native->PixelCountIsPreciseEXT();
        }
        IGraphicsRenderer& renderer;
        std::vector<LogicalOcclusionQuery*> active;
        std::shared_ptr<OcclusionIntervalSegment> current;
        std::shared_ptr<OcclusionIntervalSegment> spare;
        bool precise = true;
        std::exception_ptr failure;

        void Check() const
        {
            if (failure) std::rethrow_exception(failure);
        }

        std::shared_ptr<OcclusionIntervalSegment> MakeSegment()
        {
            auto segment = std::make_shared<OcclusionIntervalSegment>();
            segment->native = renderer.CreateOcclusionQuery();
            if (!segment->native)
                throw System::NotSupportedException("The renderer did not create an occlusion query.");
            return segment;
        }

        void CloseCurrent()
        {
            if (!current) return;
            current->native->End();
            current->ended = true;
            current.reset();
        }

        void Prepare(LogicalOcclusionQuery* additional);
        void Start(const std::shared_ptr<OcclusionIntervalSegment>& segment);
    };

    class LogicalOcclusionQuery final : public IOcclusionQueryRenderer
    {
    public:
        explicit LogicalOcclusionQuery(std::shared_ptr<OcclusionQueryIntervals> intervals)
            : intervals_(std::move(intervals)), precise_(intervals_->precise) {}

        ~LogicalOcclusionQuery() override
        {
            const auto lease = intervals_->renderer.AcquireThreadContextLeaseEXT();
            if (active_)
            {
                try { End(); }
                catch (...)
                {
                    // Destruction cannot report an allocation/native failure. Keep it observable
                    // on surviving queries instead of returning a plausible but incomplete tally.
                    intervals_->failure = std::current_exception();
                    std::erase(intervals_->active, this);
                    if (intervals_->active.empty()) intervals_->current.reset();
                }
            }
            segments_.clear();
            // The last logical object also releases the group's unused construction probe.
            // Keep that native destruction inside the owning context lease.
            intervals_.reset();
        }

        void Begin() override
        {
            const auto lease = intervals_->renderer.AcquireThreadContextLeaseEXT();
            intervals_->Check();
            intervals_->Prepare(this);
            auto next = intervals_->spare ? intervals_->spare : intervals_->MakeSegment();
            intervals_->CloseCurrent();
            segments_.clear();
            count_ = OcclusionSampleCount();
            anySamples_ = false;
            precise_ = true;
            ended_ = false;
            active_ = true;
            intervals_->active.push_back(this);
            intervals_->spare.reset();
            intervals_->Start(next);
        }

        void End() override
        {
            const auto lease = intervals_->renderer.AcquireThreadContextLeaseEXT();
            intervals_->Check();
            if (!active_) return;
            intervals_->Prepare(nullptr);
            auto next = intervals_->active.size() > 1 ? intervals_->MakeSegment() : nullptr;
            intervals_->CloseCurrent();
            std::erase(intervals_->active, this);
            active_ = false;
            ended_ = true;
            if (next) intervals_->Start(next);
        }

        bool IsComplete() const override
        {
            const auto lease = intervals_->renderer.AcquireThreadContextLeaseEXT();
            intervals_->Check();
            Compact();
            return ended_ && !active_ && segments_.empty();
        }

        int PixelCount() const override
        {
            if (!IsComplete()) return 0;
            // XNA exposes the native DWORD bits as Int32, including wrap at 2^32.
            return precise_ ? count_.Value() : int(anySamples_);
        }

        bool PixelCountIsPreciseEXT() const noexcept override
        {
            return active_ || ended_ ? precise_ : intervals_->precise;
        }

    private:
        friend class OcclusionQueryIntervals;
        std::shared_ptr<OcclusionQueryIntervals> intervals_;
        mutable std::vector<std::shared_ptr<OcclusionIntervalSegment>> segments_;
        mutable OcclusionSampleCount count_;
        mutable bool anySamples_ = false;
        bool precise_ = true;
        bool active_ = false;
        bool ended_ = false;

        void Compact() const
        {
            std::erase_if(segments_, [this](const auto& segment)
            {
                if (!segment->Poll()) return false;
                // Keep visibility separately: a precise sum may wrap to zero before
                // a later segment selects a boolean-only native query target.
                anySamples_ = anySamples_ || segment->count != 0;
                count_.Add(static_cast<std::uint32_t>(segment->count));
                return true;
            });
        }
    };

    void OcclusionQueryIntervals::Prepare(LogicalOcclusionQuery* additional)
    {
        // Retire completed segments without waiting. Long-lived queries do not retain every
        // historical native handle when shorter overlapping intervals repeatedly begin/end.
        active.reserve(active.size() + (additional != nullptr));
        for (auto* query : active)
        {
            query->Compact();
            query->segments_.reserve(query->segments_.size() + 1);
        }
        if (additional)
        {
            additional->Compact();
            additional->segments_.reserve(1);
        }
    }

    void OcclusionQueryIntervals::Start(const std::shared_ptr<OcclusionIntervalSegment>& segment)
    {
        try
        {
            segment->native->Begin();
            segment->precise = segment->native->PixelCountIsPreciseEXT();
            precise = segment->precise;
            current = segment;
            for (auto* query : active)
            {
                query->precise_ = query->precise_ && segment->precise;
                query->segments_.push_back(segment);
            }
        }
        catch (...)
        {
            failure = std::current_exception();
            throw;
        }
    }

    std::unique_ptr<IOcclusionQueryRenderer> IGraphicsRenderer::CreateLogicalOcclusionQueryEXT()
    {
        const auto lease = AcquireThreadContextLeaseEXT();
        auto intervals = occlusionQueryIntervals_.lock();
        if (!intervals)
        {
            // Probe support once per live group. Unstarted logical queries must not consume
            // one scarce native slot each; only command-stream segments need native handles.
            auto native = CreateOcclusionQuery();
            if (!native) return nullptr;
            intervals = std::make_shared<OcclusionQueryIntervals>(*this, std::move(native));
            occlusionQueryIntervals_ = intervals;
        }
        return std::make_unique<LogicalOcclusionQuery>(std::move(intervals));
    }
}
