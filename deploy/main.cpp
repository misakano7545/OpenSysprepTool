#include <cstdio>

#include "core/version.hpp"

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    std::printf("osptd %s\n", osp::versionString());
    return 0;
}
