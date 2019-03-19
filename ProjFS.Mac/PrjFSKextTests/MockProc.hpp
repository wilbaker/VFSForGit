#include "../PrjFSKext/kernel-header-wrappers/vnode.h"

enum
{
    KAUTH_RESULT_ALLOW = 1,
    KAUTH_RESULT_DENY,
    KAUTH_RESULT_DEFER
};

extern "C" int proc_pid(proc_t);
extern "C" void proc_name(int pid, char * buf, int size);
extern "C" proc_t vfs_context_proc(vfs_context_t ctx);
extern "C" proc_t proc_self(void);
extern "C" kauth_cred_t kauth_cred_proc_ref(proc_t procp);
extern "C" uid_t kauth_cred_getuid(kauth_cred_t _cred);
extern "C" void kauth_cred_unref(kauth_cred_t *_cred);
extern "C" int proc_ppid(proc_t);
extern "C" proc_t proc_find(int pid);
extern "C" int proc_rele(proc_t p);
extern "C" int proc_selfpid(void);

void SetProcName(const char* procName);
