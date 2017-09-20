#include <linux/kprobes.h>
#include <linux/sched.h>
#include <linux/sort.h>
#include <linux/vmalloc.h>

#include "../common/da_debug.h"
#include "da_mem_lib.h"
#include "da_ptracker.h"

struct dime_instance_struct * pt_get_dime_instance_of_pid (struct dime_struct *dime, pid_t pid) {
    int i, j;
    for (i=0 ; i<dime->dime_instances_size ; ++i) {
        for (j=0 ; j<dime->dime_instances[i].pid_count ; ++j) {
            if(dime->dime_instances[i].pid[j] == pid)
                return &(dime->dime_instances[i]);
        }
    }

    return NULL;
}

int pt_find(struct dime_instance_struct *dime_instance, pid_t pid) {
    int i;
    for (i=0 ; i<dime_instance->pid_count ; ++i) {
        if(dime_instance->pid[i] == pid)
            return i;
    }

    return -1;
}


int pt_add(struct dime_instance_struct *dime_instance, pid_t pid) {
    struct mm_struct *mm = NULL;

    mm = ml_get_mm_struct(pid);
    if(!mm) {
        DA_ERROR("unable to add process, no such process : pid:%d", pid);
        return -ESRCH;   /* No such process */
    }

    DA_INFO("protecting all pages of process : pid:%d", pid);
    ml_protect_all_pages(mm);

    // TODO:: check if list limit is reached

    if(pt_find(dime_instance, pid) < 0) {
        dime_instance->pid[dime_instance->pid_count++] = pid;
        DA_INFO("process added to tracking list : pid:%d", pid);
    } else {
        DA_INFO("processs was already in tracking list : pid:%d", pid);
    }

    return 0;
}


// Add parent pid and all the children pids to tracking list
int pt_add_children(struct dime_instance_struct *dime_instance, pid_t ppid) {
    struct list_head * p;
    struct task_struct *ts;
    pid_t tmp_pid;
    int retval;

    retval = pt_add(dime_instance, ppid);
    if(retval!=0)
        return retval;

    ts = ml_get_task_struct(ppid);
    if(!ts) {
        DA_ERROR("unable to add process, no such process : ppid:%d", ppid);
        return -ESRCH;   /* No such process */
    }

    DA_INFO("adding all processes of parent process : ppid:%d", ppid);
    list_for_each(p, &(ts->children)){
        struct task_struct *tsk = list_entry(p, struct task_struct, sibling);
        // TODO:: take tgid instead of pid
        tmp_pid = tsk->pid;
        pt_add_children(dime_instance, tmp_pid);
    }

    return 0;
}


struct dime_instance_struct * pt_find_parents(struct dime_struct *dime, struct task_struct *tsk) {
    if (tsk) {
        struct dime_instance_struct *dime_instance = pt_get_dime_instance_of_pid(dime, tsk->pid);
        if(dime_instance) {
            DA_INFO("parent found in list : ppid:%d", tsk->pid);
            return dime_instance;
        }
            
        if(tsk->pid > 1)
            return pt_find_parents(dime, tsk->parent);
    }

    return NULL;
}


// jprobe handler function
static ssize_t pt_jprobe_wake_up_new_task(struct task_struct *tsk) {
    // TODO:: Traverse list of instances to find out in which instance the parent of this process belongs
    struct dime_instance_struct *dime_instance = pt_find_parents(&dime, tsk);

    if(dime_instance)
        pt_add_children(dime_instance, tsk->pid);
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