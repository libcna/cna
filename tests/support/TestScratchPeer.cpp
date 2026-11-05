// SPDX-License-Identifier: MS-PL
#include "CNA/TestSupport/TestScratch.hpp"

const std::filesystem::path* TestScratchPeerRoot()
{
    return &CNA::TestSupport::FixtureRoot();
}
