// SPDX-License-Identifier: MS-PL

#include "RlglResources.hpp"

#include "RlglBridge.hpp"

namespace CNA::Internal::Renderers::Rlgl
{
    namespace
    {
        class RlglOcclusionQueryRenderer final : public IOcclusionQueryRenderer
        {
        public:
            RlglOcclusionQueryRenderer()
                : query_(Bridge::CreateOcclusionQuery())
            {
            }

            ~RlglOcclusionQueryRenderer() override
            {
                Bridge::DestroyOcclusionQuery(query_);
            }

            void Begin() override
            {
                hasBeenBegun_ = Bridge::BeginOcclusionQuery(query_) || hasBeenBegun_;
            }

            void End() override
            {
                Bridge::EndOcclusionQuery();
            }

            [[nodiscard]] bool IsComplete() const override
            {
                return hasBeenBegun_ && !Bridge::IsOcclusionQueryActive(query_) &&
                    Bridge::IsOcclusionQueryComplete(query_);
            }

            [[nodiscard]] int PixelCount() const override
            {
                if (!IsComplete()) return 0;
                return Bridge::GetOcclusionQueryPixelCount(query_);
            }

        private:
            unsigned int query_ = 0;
            bool hasBeenBegun_ = false;
        };
    }

    std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQueryRenderer()
    {
        return std::make_unique<RlglOcclusionQueryRenderer>();
    }
}
