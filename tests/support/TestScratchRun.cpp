// SPDX-License-Identifier: MS-PL
#include "CNA/TestSupport/TestScratchEnvironment.hpp"
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char** argv)
{
    if (argc < 2) return 2;
    const auto& root = CNA::TestSupport::FixtureRoot();
    CNA::TestSupport::UseProcessTempRoot();
    if (::setenv("CNA_TEST_SCRATCH_ROOT", root.c_str(), 1) != 0) return 2;
    const auto child = ::fork();
    if (child < 0) return 2;
    if (child == 0)
    {
        ::execvp(argv[1], argv + 1);
        ::_exit(127);
    }
    int status = 0;
    while (::waitpid(child, &status, 0) < 0)
        if (errno != EINTR) return 2;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
