#include <linux/kprobes.h>
#include <linux/sched.h>

#include "../common/da_debug.h"
#include "da_mem_lib.h"
#include "da_ptracker.h"

int pt_add_pid(pid_t pid) {
    struct pid *pid_struct = NULL;
    struct task_struct *task = NULL;

    pid_struct = find_get_pid(pid);
    if(!pid_struct) {
        DA_ERROR("could not find struct pid for PID:%d", pid);
        return -1;
    }

    task = pid_task(pid_struct, PIDTYPE_PID);
    if(!task) {
        DA_ERROR("could not find task_struct for PID:%d", pid);
        return -2;
    }

    ml_protect_all_pages(task->mm);

    return 0;
}


static void pt_list_pid(pid_t pid) {
    struct list_head * p;
    struct task_struct *ts, *tsk;
    pid_t tmp_pid;

    ts = pid_task(find_get_pid(pid), PIDTYPE_PID);

    list_for_each(p, &(ts->children)){
         tsk = list_entry(p, struct task_struct, sibling);
         tmp_pid = tsk->pid;
         DA_INFO("child pid : %d", tmp_pid);
         pt_list_pid(tmp_pid);
    }
}


void print_parent(struct task_struct *tsk) {
    if (tsk) {
        DA_INFO("Parent pid : %d", tsk->pid);
        if(tsk->pid > 1)
            print_parent(tsk->parent);
    }
}


static ssize_t pt_jprobe_wake_up_new_task(struct task_struct *tsk) {
    DA_ERROR("Waking new pid : %d", tsk->pid);
    print_parent(tsk);
    jprobe_return();
    return 0;
}

static struct jprobe jprobe_wake_up_new_task_struct = {
    .entry  =   pt_jprobe_wake_up_new_task,
    .kp     =   {
                    .symbol_name = "wake_up_new_task",
                },
};

int pt_init_ptracker(pid_t pid) {
    int ret;

    ret = register_jprobe(&jprobe_wake_up_new_task_struct);
    if (ret < 0) {
        DA_ERROR("wake_up_new_task jprobe failed, returned %d", ret);
        return ret;
    }
    DA_INFO("planted wake_up_new_task jprobe at %p, handler addr %p", jprobe_wake_up_new_task_struct.kp.addr, jprobe_wake_up_new_task_struct.entry);

    pt_list_pid(pid);

    return 0;
}

void pt_exit_ptracker(void) {
    unregister_jprobe(&jprobe_wake_up_new_task_struct);
    DA_INFO("unregistered wake_up_new_task jprobe at %p", jprobe_wake_up_new_task_struct.kp.addr);
}