#include <linux/kprobes.h>
#include <linux/sched.h>
#include <linux/sort.h>
#include <linux/vmalloc.h>

#include "../common/da_debug.h"
#include "da_mem_lib.h"
#include "da_ptracker.h"

#define PID_LIST_PART_SIZE 100

struct pid_list {
    pid_t * list;
    int     size;
    int     max_size;
};

struct pid_list pid_list_s = {
    .list = NULL,
    .size = 0,
    .max_size = 0
};

// Compare pid_t function for sorting
int cmp_pid_t(const void *p1, const void *p2) {
    return *(pid_t*)p1 - *(pid_t*)p2;
}

void swap_pid_t(void *p1, void *p2, int size) {
    pid_t pt = *(pid_t*)p1;
    *(pid_t*)p1 = *(pid_t*)p2;
    *(pid_t*)p2 = pt;
}

int __pt_find(struct pid_list *pl, pid_t pid) {
    int i;
    for (i=0 ; i<pl->size ; ++i) {
        if(pl->list[i] == pid)
            return i;
    }

    return -1;
}

int pt_find(pid_t pid) {
    return __pt_find(&pid_list_s, pid);
}



int __pt_add(struct pid_list *pl, pid_t pid) {
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


    if(!pl->list) {
        pl->max_size = PID_LIST_PART_SIZE;
        pl->size = 0;
        pl->list = (pid_t *)vmalloc(sizeof(pid_t) * pl->max_size);
        if(!pl->list) {
            DA_ERROR("cannot allocate memory for PID:%d", pid);
            return -ENOMEM;  /* Out of memory*/
        }
    } 
    else if(pl->size >= pl->max_size) {
        pid_t *pid_list_new = (pid_t *)vmalloc(sizeof(pid_t) * (pl->max_size + PID_LIST_PART_SIZE));
        int i=0;

        if(!pid_list_new) {
            DA_ERROR("cannot allocate memory for PID:%d", pid);
            return -ENOMEM;  /* Out of memory*/
        }

        for(i=0 ; i<pl->size ; ++i)
            pid_list_new[i] = pl->list[i];

        vfree(pl->list);
        pl->list = pid_list_new;
        pl->max_size += PID_LIST_PART_SIZE;
    }

    pl->list[pl->size++] = pid;
    sort(   pl->list,
            pl->size,
            sizeof(pid_t),
            cmp_pid_t,
            swap_pid_t);

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

int pt_add(pid_t pid) {
    return __pt_add(&pid_list_s, pid);
}

// Add parent pid and all the children pids to tracking list
int pt_add_children(pid_t ppid) {
    struct list_head * p;
    struct pid *pid_struct = NULL;
    struct task_struct *ts, *tsk;
    pid_t tmp_pid;

    if(pt_find(ppid) < 0) {
        int retval = pt_add(ppid);
        if(retval!=0)
            return retval;
    }

    pid_struct = find_get_pid(ppid);
    if(!pid_struct) {
        DA_ERROR("could not find struct pid for PID:%d", ppid);
        return -ESRCH;   /* No such process */
    }

    ts = pid_task(pid_struct, PIDTYPE_PID);
    if(!ts) {
        DA_ERROR("could not find task_struct for PID:%d", ppid);
        return -ESRCH;   /* No such process */
    }

    list_for_each(p, &(ts->children)){
        tsk = list_entry(p, struct task_struct, sibling);
        tmp_pid = tsk->pid;
        pt_add_children(tmp_pid);
    }

    return 0;
}


int pt_find_parents(struct task_struct *tsk) {
    if (tsk) {
        int loc = pt_find(tsk->pid);
        if(loc >= 0) {
            DA_INFO("parent found in list : ppid:%d", tsk->pid);
            return loc;
        }
            
        if(tsk->pid > 1)
            return pt_find_parents(tsk->parent);
    }

    return -1;
}


static ssize_t pt_jprobe_wake_up_new_task(struct task_struct *tsk) {
    //DA_ERROR("Waking new pid : %d", tsk->pid);
    if(pt_find_parents(tsk) >= 0)
        pt_add_children(tsk->pid);
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

    DA_INFO("cleaning pid list...");
    if(pid_list_s.list) {
        vfree(pid_list_s.list);
        pid_list_s.list = NULL;
    }

    DA_INFO("cleaning process tracker complete");
}