#include <linux/kprobes.h>

#include "../common/da_debug.h"
#include "common.h"


int     pt_init_ptracker    (void);
void    pt_exit_ptracker    (void);
int     pt_add              (struct dime_instance_struct *dime_instance, pid_t pid);
int     pt_add_children     (struct dime_instance_struct *dime_instance, pid_t ppid);
int     pt_find             (struct dime_instance_struct *dime_instance, pid_t pid);