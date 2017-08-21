#ifndef __DA_PTRACKER_H__
#define __DA_PTRACKER_H__

#include <linux/kprobes.h>

#include "../common/da_debug.h"
#include "common.h"


int     pt_init_ptracker    (void);
void    pt_exit_ptracker    (void);
int     pt_add              (struct dime_instance_struct *dime_instance, pid_t pid);
int     pt_add_children     (struct dime_instance_struct *dime_instance, pid_t ppid);
int     pt_find             (struct dime_instance_struct *dime_instance, pid_t pid);

struct dime_instance_struct * pt_get_dime_instance_of_pid (struct dime_struct *dime, pid_t pid);

#endif