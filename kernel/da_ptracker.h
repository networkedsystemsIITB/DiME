#include <linux/kprobes.h>

#include "../common/da_debug.h"


int     pt_init_ptracker    (pid_t pid);
void    pt_exit_ptracker    (void);
int     pt_add_pid          (pid_t pid);