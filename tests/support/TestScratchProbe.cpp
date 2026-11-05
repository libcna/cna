// SPDX-License-Identifier: MS-PL
#include "CNA/TestSupport/TestScratchEnvironment.hpp"
#include <fstream>
#include <iostream>

const std::filesystem::path* TestScratchPeerRoot();

int Run()
{
    const auto root = CNA::TestSupport::FixtureRoot();
    if (&CNA::TestSupport::FixtureRoot() != TestScratchPeerRoot()) return 2;
    CNA::TestSupport::UseProcessTempRoot();
    {
        // Oracle: even a tool using only std::filesystem's temp API must write below this owner.
        std::ofstream out(std::filesystem::temp_directory_path() / "same-name.bin", std::ios::binary);
        out << "scratch-oracle";
        if (!out.good()) return 3;
    }
    // Fatal assertions can leave an unreadable fixture directory. Cleanup must restore only
    // owned entries and must not follow containment-test symlinks to another directory.
    std::filesystem::create_directory(root / "locked");
    { std::ofstream(root / "locked" / "marker") << "owned"; }
    std::filesystem::permissions(root / "locked", std::filesystem::perms::none);
    const auto utf8 = root.generic_u8string();
    std::cout.write(reinterpret_cast<const char*>(utf8.data()),
                    static_cast<std::streamsize>(utf8.size())) << std::endl;
    char command = 0;
    std::cin.get(command);
    // Returning a failure code still runs the static scratch destructor.
    return command == 'f' ? 23 : 0;
}

int main()
{
    try { return Run(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 2; }
}
