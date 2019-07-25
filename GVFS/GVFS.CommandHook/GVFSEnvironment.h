#pragma once

inline bool GVFSEnvironment_IsUnattended()
{
    char* unattendedEnvVariable = getenv("GVFS_UNATTENDED");
    if (unattendedEnvVariable != nullptr)
    {
        return 0 == strcmp(unattendedEnvVariable, "1");
    }

    return false;
}
