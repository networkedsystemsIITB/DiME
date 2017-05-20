#include <linux/kprobes.h>

#include "../common/da_debug.h"


int     pt_init_ptracker    (void);
void    pt_exit_ptracker    (void);
int     pt_add              (pid_t pid);
int     pt_add_children     (pid_t ppid);
int     pt_find             (pid_t pid);