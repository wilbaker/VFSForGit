#include "stdafx.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "filesystem.h"

bool FileSystem_FileExists(const PATH_STRING& path)
{
    struct stat path_stat;
    if (0 != stat(path.c_str(), &path_stat))
    {
        return false;
    }
    
    return S_ISREG(path_stat.st_mode) || S_ISLNK(path_stat.st_mode);
}
