// The second file of tools/tests/test_cpu_check.sh's program (and of its Windows build in tools/wine-host-tests.sh):
// a C++ static initializer and main(), compiled for x86-64-v3 like the rest of KZ1HD, beside port/kz1hd/cpu_check.c.
// The check must stop the program before the initializer prints anything.
#include <cstdio>

static struct Init {
    Init() { std::printf("static initializer\n"); std::fflush(stdout); }
} init;

int main()
{
    std::printf("main\n");
    return 0;
}
