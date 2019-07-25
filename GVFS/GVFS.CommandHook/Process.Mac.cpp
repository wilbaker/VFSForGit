#include "stdafx.h"
 #include <sys/types.h>
 #include <unistd.h>

bool Process_IsElevated()
{
    int euid = geteuid();
    return euid == 0;
}

// TODO (hack): Use PATH_STRING
std::string Process_Run(const std::string& processName, const std::string& args, bool redirectOutput)
{
    return "";
}

bool Process_IsConsoleOutputRedirectedToFile()
{
    // TODO(POSIX): Implement proper check
    return false;
}

bool Process_IsProcessActive(int pid)
{
    return true;
}
