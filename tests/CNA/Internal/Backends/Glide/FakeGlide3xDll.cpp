#include <windows.h>

extern "C" __declspec(dllexport) int GlideAbiPlainProbe()
{
    return 17;
}

extern "C" __declspec(dllexport) int WINAPI GlideAbiStdcallProbe(int value)
{
    return value + 5;
}
