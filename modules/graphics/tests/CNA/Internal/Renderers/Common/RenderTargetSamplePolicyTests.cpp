// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/Renderers/Common/RenderTargetSamplePolicy.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <memory>
#include <vector>

using namespace CNA::Internal::Renderers;
namespace
{
    struct CannedTarget
    {
        int applied;
        int GetMultiSampleCount() const { return applied; }
    };
    struct CannedRenderer
    {
        std::vector<int> supported, visited;
        int received=-1, format=-1;
        std::unique_ptr<CannedTarget> Allocate(int count,int colorFormat)
        {
            received=count; format=colorFormat; visited.clear();
            int applied=NegotiateRenderTargetSamples(count,[&](int candidate) {
                visited.push_back(candidate);
                return std::find(supported.begin(),supported.end(),candidate)!=supported.end();
            });
            return std::make_unique<CannedTarget>(CannedTarget{applied});
        }
        auto CreateRenderTarget2DEXT(int,int,int,bool,bool,int count,int colorFormat)
        { return Allocate(count,colorFormat); }
        auto CreateRenderTargetCubeEXT(int,int,bool,bool,int count,int colorFormat)
        { return Allocate(count,colorFormat); }
    };
}
TEST(RenderTargetSamplePolicyTest, IntegerSearchHasExactCandidatesAndAppliedCounts)
{
    // XNA Refresh GraphicsAdapter.QueryMultiSampleCount IL_0000-0046: test min(request,16),
    // decrement by ONE, return first success, never test 1. All answers below are derived
    // from authored capability sets; the requested list is NOT produced by CNA.
    struct Row { int request; std::vector<int> support; int selected; std::vector<int> visited; };
    const std::array rows{
        Row{3,{3},3,{3}}, Row{7,{6},6,{7,6}}, Row{7,{4,6},6,{7,6}},
        Row{7,{4},4,{7,6,5,4}}, Row{3,{2},2,{3,2}},
        Row{17,{4},4,{16,15,14,13,12,11,10,9,8,7,6,5,4}},
        Row{std::numeric_limits<int>::max(),{},0,{16,15,14,13,12,11,10,9,8,7,6,5,4,3,2}},
        Row{std::numeric_limits<int>::max(),{16},16,{16}},
        Row{2,{1},0,{2}}, Row{1,{16,4,1},0,{}}, Row{0,{4},0,{}}, Row{-7,{4},0,{}}
    };
    for(const auto& row : rows)
    {
        SCOPED_TRACE(row.request);
        std::vector<int> visited;
        const int selected=NegotiateRenderTargetSamples(row.request,[&](int candidate) {
            visited.push_back(candidate);
            return std::find(row.support.begin(),row.support.end(),candidate)!=row.support.end();
        });
        EXPECT_EQ(selected,row.selected);
        EXPECT_EQ(visited,row.visited);
    }
}
TEST(RenderTargetSamplePolicyTest, BothAllocationRoutesPreserveOddPreferencesAndReportAppliedCount)
{
    // Refresh RT2D/RTCube.CreateRenderTarget IL_002a-0037 forwards the preference to
    // QueryFormat; IL_0048-0056 / IL_0045-0053 passes its SELECTED count to the helper.
    // The real public property reads helper.multiSampleCount (both getters IL_0000-000b).
    // This canned allocation seam checks forwarding and applied count, while the existing
    // RenderTargetCreationRefreshTest sample matrix checks the public properties on SOFTWARE.
    for(bool cube : {false,true}) for(const auto row : {std::array{3,3,3},std::array{7,6,7},
        std::array{17,6,16},std::array{1,0,0},std::array{-7,0,0}})
    {
        SCOPED_TRACE(cube);
        SCOPED_TRACE(row[0]);
        CannedRenderer renderer; renderer.supported={3,6};
        auto target=cube ? CreateRenderTargetCubeWithSamplePreference(renderer,4,0,true,false,row[0],9)
            : CreateRenderTarget2DWithSamplePreference(renderer,4,4,0,true,false,row[0],9);
        ASSERT_NE(target,nullptr);
        EXPECT_EQ(renderer.received,row[2]);
        EXPECT_EQ(renderer.format,9); // Selected format, not a hard-coded Color query.
        EXPECT_EQ(target->GetMultiSampleCount(),row[1]);
    }
}
