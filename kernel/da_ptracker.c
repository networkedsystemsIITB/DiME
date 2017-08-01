#include <linux/kprobes.h>
#include <linux/sched.h>
#include <linux/sort.h>
#include <linux/vmalloc.h>

#include "../common/da_debug.h"
#include "da_mem_lib.h"
#include "da_ptracker.h"

int pt_find(struct dime_instance_struct *dime_instance, pid_t pid) {
    int i;
    for (i=0 ; i<dime_instance->pid_count ; ++i) {
        if(dime_instance->pid[i] == pid)
            return i;
    }

    return -1;
}


int pt_add(struct dime_instance_struct *dime_instance, pid_t pid) {
    struct pid *pid_struct = NULL;
    struct task_struct *task = NULL;

    pid_struct = find_get_pid(pid);
    if(!pid_struct) {
        DA_ERROR("could not find struct pid for PID:%d", pid);
        return -ESRCH;   /* No such process */
    }

    task = pid_task(pid_struct, PIDTYPE_PID);
    if(!task) {
        DA_ERROR("could not find task_struct for PID:%d", pid);
        return -ESRCH;   /* No such process */
    }

    // TODO:: check if list limit is reached

    dime_instance->pid[dime_instance->pid_count++] = pid;

    ml_protect_all_pages(task->mm);
    DA_INFO("process added to tracking list : pid:%d", pid);

    // DEBUG
/*    {
        int i;
        DA_INFO("Printing pid list : ");
        for(i=0 ; i<pl->size ; ++i) {
            DA_INFO("\t%d", pl->list[i]);
        }
    }*/

    return 0;
}


// Add parent pid and all the children pids to tracking list
int pt_add_children(struct dime_instance_struct *dime_instance, pid_t ppid) {
    struct list_head * p;
    struct task_struct *ts;
    pid_t tmp_pid;

    if(pt_find(dime_instance, ppid) < 0) {
        int retval = pt_add(dime_instance, ppid);
        if(retval!=0)
            return retval;
    }

    ts = ml_get_task_struct(ppid);
    if(!ts) {
        return -ESRCH;   /* No such process */
    }

    list_for_each(p, &(ts->children)){
        struct task_struct *tsk = list_entry(p, struct task_struct, sibling);
        // TODO:: take tgid instead of pid
        tmp_pid = tsk->pid;
        pt_add_children(dime_instance, tmp_pid);
    }

    return 0;
}


int pt_find_parents(struct dime_instance_struct *dime_instance, struct task_struct *tsk) {
    if (tsk) {
        int loc = pt_find(dime_instance, tsk->pid);
        if(loc >= 0) {
            DA_INFO("parent found in list : ppid:%d", tsk->pid);
            return loc;
        }
            
        if(tsk->pid > 1)
            return pt_find_parents(dime_instance, tsk->parent);
    }

    return -1;
}


// jprobe handler function
static ssize_t pt_jprobe_wake_up_new_task(struct task_struct *tsk) {
    // TODO:: Traverse list of instances to find out in which instance the parent of this process belongs
    if(pt_find_parents(&dime_instance, tsk) >= 0)
        pt_add_children(&dime_instance, tsk->pid);
    jprobe_return();
    return 0;
}

static struct jprobe jprobe_wake_up_new_task_struct = {
    .entry  =   pt_jprobe_wake_up_new_task,
    .kp     =   {
                    .symbol_name = "wake_up_new_task",
                },
};

int pt_init_ptracker(void) {
    int ret;

    ret = register_jprobe(&jprobe_wake_up_new_task_struct);
    if (ret < 0) {
        DA_ERROR("wake_up_new_task jprobe failed, returned %d", ret);
        return ret;
    }
    DA_INFO("planted wake_up_new_task jprobe at %p, handler addr %p", jprobe_wake_up_new_task_struct.kp.addr, jprobe_wake_up_new_task_struct.entry);

    return 0;
}

void pt_exit_ptracker(void) {
    DA_INFO("unregistering wake_up_new_task jprobe at %p...", jprobe_wake_up_new_task_struct.kp.addr);
    unregister_jprobe(&jprobe_wake_up_new_task_struct);

    DA_INFO("cleaning process tracker complete");
}