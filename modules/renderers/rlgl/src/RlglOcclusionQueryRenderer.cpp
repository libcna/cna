// SPDX-License-Identifier: MS-PL

#include "RlglResources.hpp"

#include "RlglBridge.hpp"

#include <utility>

namespace CNA::Internal::Renderers::Rlgl
{
    namespace
    {
        class RlglOcclusionQueryRenderer final
            : public IOcclusionQueryRenderer, public IRlglNativeResource
        {
        public:
            explicit RlglOcclusionQueryRenderer(
                std::shared_ptr<RlglResourceLifetime> lifetime)
                : query_(Bridge::CreateOcclusionQuery())
                , lifetime_(std::move(lifetime))
            {
                try
                {
                    (void)lifetime_->Register(*this);
                }
                catch (...)
                {
                    ReleaseNativeResource();
                    throw;
                }
            }

            ~RlglOcclusionQueryRenderer() override
            {
                lifetime_->Dispose(*this);
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
            void ReleaseNativeResource() noexcept override
            {
                Bridge::DestroyOcclusionQuery(query_);
            }

            unsigned int query_ = 0;
            bool hasBeenBegun_ = false;
            std::shared_ptr<RlglResourceLifetime> lifetime_;
        };
    }

    std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQueryRenderer(
        const std::shared_ptr<RlglResourceLifetime>& lifetime)
    {
        return std::make_unique<RlglOcclusionQueryRenderer>(lifetime);
    }
}
