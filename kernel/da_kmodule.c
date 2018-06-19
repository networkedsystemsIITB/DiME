#include <linux/module.h>    // included for all kernel modules
#include <linux/kernel.h>    // included for KERN_INFO
#include <linux/init.h>      // included for __init and __exit macros
#include <linux/syscalls.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <asm/paravirt.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <linux/stat.h>
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,11,0)
#include <linux/sched/clock.h>
#endif


#include "../common/da_debug.h"

#include "da_mem_lib.h"
#include "da_ptracker.h"
#include "da_config.h"
#include "common.h"

EXPORT_SYMBOL(dime);
unsigned int da_debug_flag =    DA_DEBUG_ALERT_FLAG
                                | DA_DEBUG_INFO_FLAG
                                | DA_DEBUG_WARNING_FLAG
                                | DA_DEBUG_ERROR_FLAG
                                //| DA_DEBUG_ENTRYEXIT_FLAG
                                | DA_DEBUG_DEBUG_FLAG
                                ;
EXPORT_SYMBOL(da_debug_flag);

/*****
 *
 *  Two hooks are required to set up in do_page_fault function in fault.c,
 *  one at the start of the function and other at the end of the function.
 *  e.g. : fault.c file looks like this
 *
 *      int do_page_fault_hook_start (struct pt_regs *regs, 
 *                                          unsigned long error_code, 
 *                                          unsigned long address,
 *                                          int * hook_flag);
 *      int do_page_fault_hook_end (struct pt_regs *regs, 
 *                                          unsigned long error_code, 
 *                                          unsigned long address,
 *                                          int * hook_flag);
 *          .....
 *          .....
 *
 *      do_page_fault(struct pt_regs *regs, unsigned long error_code)
 *      {
 *          int hook_flag = 0;
 *          if(do_page_fault_hook_start != NULL)
 *              do_page_fault_hook_start(regs, error_code, address, &hook_flag);
 *          .....
 *          __do_page_fault(regs, error_code, address);
 *          .....
 *          if(do_page_fault_hook_end != NULL)
 *              do_page_fault_hook_end(regs, error_code, address, &hook_flag);
 *      }
 *
 *  We are starting a timer in start hook function, and waiting in end hook
 *  function, so that we can wait for desired delay more accurately.
 *
 *  Set your hook function names to appropriate macros below.
 *
 */

// Set your hook function name, which is exported from fault.c, here
#define HOOK_START_FN_NAME  do_page_fault_hook_start    // Called before __do_page_fault
#define HOOK_END_FN_NAME    do_page_fault_hook_end      // Called after __do_page_fault

extern int (*HOOK_START_FN_NAME) (struct pt_regs *  regs,
                                    unsigned long   error_code,
                                    unsigned long   address,
                                    int *           hook_flag,
                                    ulong *         hook_timestamp);
extern int (*HOOK_END_FN_NAME) (struct pt_regs *    regs,
                                    unsigned long   error_code,
                                    unsigned long   address,
                                    int *           hook_flag,
                                    ulong *         hook_timestamp);
int do_page_fault_hook_start_new (struct pt_regs *  regs,
                                    unsigned long   error_code,
                                    unsigned long   address,
                                    int *           hook_flag,
                                    ulong *         hook_timestamp);
int do_page_fault_hook_end_new (struct pt_regs *    regs,
                                    unsigned long   error_code,
                                    unsigned long   address,
                                    int *           hook_flag,
                                    ulong *         hook_timestamp);

struct task_struct* get_task_by_pid(pid_t pid);

/*****
 *
 *  Module params
 *
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abhishek Ghogare, Dhantu");
MODULE_DESCRIPTION("Disaggregation Emulator");

static int      pid[1000]       = {10};
static int      pid_count       = 0;
static ulong    latency_ns      = 10000ULL;
static ulong    bandwidth_bps   = 10000000000ULL;
       ulong    local_npages    = 20ULL;
static ulong    page_fault_count= 0ULL;

module_param_array(pid, int, &pid_count, 0444);    // Array of pids to run an emulator instance on
//module_param(pid, int, 0444);                     // pid cannot be changed but read directly from sysfs
module_param(latency_ns, ulong, 0644);
module_param(bandwidth_bps, ulong, 0644);
module_param(da_debug_flag, uint, 0644);            // debug level flags, can be set from sysfs
module_param(local_npages, ulong, 0644);            // number of local pages, acts as a cache for remote memory
module_param(page_fault_count, ulong, 0444);        // pid cannot be changed but read directly from sysfs 
// TODO: unsigned long is 64bit in x86_64, need to change to ull

MODULE_PARM_DESC(pid, "List of PIDs of a processes to track");
MODULE_PARM_DESC(latency_ns, "One way latency in nano-sec");
MODULE_PARM_DESC(bandwidth_bps, "Bandwidth of network in bits-per-sec");
MODULE_PARM_DESC(da_debug_flag, "Module debug log level flags");
MODULE_PARM_DESC(local_npages, "Number of available local pages");
MODULE_PARM_DESC(page_fault_count, "Number of total page faults");


struct dime_struct dime = {
    .dime_instances_size = 0
};

void inject_delay(struct dime_instance_struct *dime_instance, unsigned long long diff) {
    unsigned long long delay_ns = 0, curr;
    delay_ns = ((PAGE_SIZE * 8ULL) * 1000000000ULL) / dime_instance->bandwidth_bps;  // Transmission delay
    delay_ns += 2*dime_instance->latency_ns;                                         // Two way latency

    /*
    diff = atomic_long_read(&dime_instance->pagefaults)*delay_ns;
    curr = atomic_long_read(&dime_instance->time_pfh_ap_inject);
    if( curr > diff ) {
        diff = curr - diff;
    } else {
        diff = 0;
    }

    if(delay_ns > diff) {
        delay_ns -= diff;
    } else {
        delay_ns = 0;
    }
    */
    
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    // inject diff worth delay : testing
    //delay_ns = diff;



    if(delay_ns < 100000) {                                     // use custome busy loop for < 100us
        unsigned long long hook_timestamp = sched_clock();
        while ((sched_clock() - hook_timestamp) < delay_ns) {
            // Wait for delay
        }
    } else if(delay_ns < 20000000) {                            // use usleep_range for 100us to 20ms, since msleep minimum sleep is of 20ms
        usleep_range(delay_ns/1000, delay_ns/1000 + 5);
    } else {                                                    // use msleep for > 20ms
        msleep(delay_ns / 1000000);
    }
}
EXPORT_SYMBOL(inject_delay);


/*****
 *
 *  init_module & cleanup_module
 *
 *  Description:
 *      initialize & cleanup modules
 */
int init_module(void) {
    int ret = 0;
    int i;
    DA_ENTRY();

    if(init_mem_lib()) {
        ret = -1; // TODO:: Error codes
        goto init_bad;
    }

    if(init_dime_config_procfs()) {
        ret = -1; // TODO:: Error codes
        goto init_bad;
    }

    HOOK_START_FN_NAME  = do_page_fault_hook_start_new;
    HOOK_END_FN_NAME    = do_page_fault_hook_end_new;
    DA_INFO("hook insertion complete");

    rwlock_init(&(dime.dime_instances[0].lock));

    write_lock(&(dime.dime_instances[0].lock));

    for(i=0 ; i<pid_count ; ++i) {
        dime.dime_instances[0].pid[i] = pid[i];
        dime.dime_instances[0].pid_count++;
    }

    dime.dime_instances[0].latency_ns       = latency_ns;
    dime.dime_instances[0].bandwidth_bps    = bandwidth_bps;
    dime.dime_instances[0].local_npages     = local_npages;
    atomic_long_set(&dime.dime_instances[0].pc_pagefaults, 0);
    atomic_long_set(&dime.dime_instances[0].an_pagefaults, 0);
    atomic_long_set(&dime.dime_instances[0].pagefaults, 0);
    atomic_long_set(&dime.dime_instances[0].time_pfh, 0);
    atomic_long_set(&dime.dime_instances[0].time_ap, 0);
    atomic_long_set(&dime.dime_instances[0].time_inject, 0);
    atomic_long_set(&dime.dime_instances[0].time_pfh_ap, 0);
    atomic_long_set(&dime.dime_instances[0].time_pfh_ap_inject, 0);
    atomic_long_set(&dime.dime_instances[0].duplecate_pfs, 0);
    dime.dime_instances_size                = 1;

    write_unlock(&(dime.dime_instances[0].lock));
    goto init_good;

init_bad:
    HOOK_START_FN_NAME  = NULL;
    HOOK_END_FN_NAME    = NULL;
    DA_ERROR("failed to initialize, exiting");

init_good:
    DA_EXIT();
    return ret;    // Non-zero return means that the module couldn't be loaded.
}

void cleanup_module(void)
{
    int i;
    DA_ENTRY();
    cleanup_dime_config_procfs();
    // TODO:: Unprotect all pages before exiting
    HOOK_START_FN_NAME  = NULL; 
    HOOK_END_FN_NAME    = NULL;                    // Removing hook, setting to NULL
    pt_exit_ptracker();
    for(i=0 ; i<dime.dime_instances_size ; ++i) {
        if (dime.dime_instances[i].prp)
            dime.dime_instances[i].prp->clean(&dime.dime_instances[i]);
    }
    cleanup_mm_lib();
    DA_INFO("cleaning up module complete");
    DA_EXIT();
}



/*  do_page_fault_hook_start_new
 *
 *  Description:
 *      do_page_fault hook function
 */
int do_page_fault_hook_start_new (struct pt_regs *regs, 
                            unsigned long error_code, 
                            unsigned long address,
                            int * hook_flag,
                            ulong * hook_timestamp) {
    struct dime_instance_struct *dime_instance = pt_get_dime_instance_of_pid(&dime, current->tgid);

    *hook_flag = 0;
    *hook_timestamp = sched_clock();

    if(address != 0ul && dime_instance) {
        // Inject delays here
        pte_t *ptep = ml_get_ptep(current->mm, address);
        if(ml_is_inlist_pte(current->mm, address, ptep)) {
            atomic_long_inc(&dime_instance->duplecate_pfs);
            *hook_flag = 0;
        } else {
            *hook_flag = 1;
        }
    }

    return 0;
}

/*  do_page_fault_hook_end_new
 *
 *  Description:
 *      do_page_fault hook function, simulates page fetch delay over network
 */
int do_page_fault_hook_end_new (struct pt_regs *regs, 
                            unsigned long error_code, 
                            unsigned long address,
                            int * hook_flag,
                            ulong * hook_timestamp) {
    if(*hook_flag == 1) {
        struct dime_instance_struct *dime_instance = pt_get_dime_instance_of_pid(&dime, current->tgid);
        unsigned long long time_pfh = 0,
            time_ap = 0,
            time_inject = 0,
            time_pfh_ap = 0,
            time_pfh_ap_inject = 0;

        if(address != 0ul && dime_instance) {
            // Inject delays here
            time_pfh = sched_clock() - *hook_timestamp;
            atomic_long_add(time_pfh, &dime_instance->time_pfh);
            
            time_ap = sched_clock();
            
            if(dime_instance->prp && dime_instance->prp->add_page && dime_instance->prp->add_page(dime_instance, task_pid(current), address) == 1) {
            }

            time_ap = sched_clock() - time_ap;
            atomic_long_add(time_ap, &dime_instance->time_ap);

            time_pfh_ap = sched_clock() - *hook_timestamp;
            atomic_long_add(time_pfh_ap, &dime_instance->time_pfh_ap);

            time_inject = sched_clock();

            inject_delay(dime_instance, time_pfh_ap);
            //inject_delay(dime_instance, time_pfh);

            time_inject = sched_clock() - time_inject;
            atomic_long_add(time_inject, &dime_instance->time_inject);

            time_pfh_ap_inject = sched_clock() - *hook_timestamp;
            atomic_long_add(time_pfh_ap_inject, &dime_instance->time_pfh_ap_inject);

            atomic_long_inc(&dime_instance->pagefaults);
        }
    }
    
    return 0;
}

// TODO:: no use of prp here, remove or rename function
int register_page_replacement_policy(struct page_replacement_policy_struct *prp) {
    int i, j;
    
    // initialize processes
    // TODO:: register unregister functions can be developed with more simplification,
    //        separate policy for separate instances 
    if (pt_init_ptracker() != 0) {
        return -1; // TODO:: valid error code
    }

    for (j=0 ; j<dime.dime_instances_size ; ++j) {
        for(i=0 ; i<dime.dime_instances[j].pid_count ; ++i) {
            DA_INFO("adding process %d to tracking", dime.dime_instances[j].pid[i]);

            pt_add_children(&dime.dime_instances[j], dime.dime_instances[j].pid[i]);
        }
    }
    return 0;
}

int deregister_page_replacement_policy(struct page_replacement_policy_struct *prp) {
    /*int j;

    for (j=0 ; j<dime.dime_instances_size ; ++j) {
        if(dime.dime_instances[j].prp != prp) {
            DA_ERROR("the policy given is not registered with dime, please provide correct policy");
            //return -EPERM;  // Operation not permitted
        } else {
            dime.dime_instances[j].prp = NULL;
        }
    }*/

    pt_exit_ptracker();
    return 0;
}

EXPORT_SYMBOL(register_page_replacement_policy);
EXPORT_SYMBOL(deregister_page_replacement_policy);