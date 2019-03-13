#include "MockProc.hpp"
#include <string.h>

int proc_pid(proc_t)
{
    return 1;
}

char* return_proc_name;

void proc_name(int pid, char * buf, int size)
{
    if (return_proc_name != nullptr)
    {
        strcpy(buf, return_proc_name);
    }
}

void SetProcName(char* procName)
{
    return_proc_name = strdup(procName);
}

