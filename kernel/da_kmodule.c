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


#include "../common/da_debug.h"

#include "da_mem_lib.h"
#include "da_local_page_list.h"
#include "da_ptracker.h"

unsigned int da_debug_flag =    DA_DEBUG_ALERT_FLAG | 
                                DA_DEBUG_INFO_FLAG | 
                                DA_DEBUG_WARNING_FLAG | 
                                DA_DEBUG_ERROR_FLAG | 
                                DA_DEBUG_ENTRYEXIT_FLAG;

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

static int      pid             = 10;
static ulong    latency_ns      = 10000ULL;
static ulong    bandwidth_bps   = 10000000000ULL;
       ulong    local_npages    = 20ULL;
static ulong    page_fault_count= 0ULL;

module_param(pid, int, 0444);               // pid cannot be changed but read directly from sysfs
module_param(latency_ns, ulong, 0644);
module_param(bandwidth_bps, ulong, 0644);
module_param(da_debug_flag, uint, 0644);    // debug level flags, can be set from sysfs
module_param(local_npages, ulong, 0644);    // number of local pages, acts as a cache for remote memory
module_param(page_fault_count, ulong, 0444);               // pid cannot be changed but read directly from sysfs 
// TODO: unsigned long is 64bit in x86_64, need to change to ull

MODULE_PARM_DESC(pid, "PID of a process to track");
MODULE_PARM_DESC(latency_ns, "One way latency in nano-sec");
MODULE_PARM_DESC(bandwidth_bps, "Bandwidth of network in bits-per-sec");
MODULE_PARM_DESC(da_debug_flag, "Module debug log level flags");
MODULE_PARM_DESC(local_npages, "Number of available local pages");
MODULE_PARM_DESC(page_fault_count, "Number of total page faults");


/*****
 *
 *  init_module & cleanup_module
 *
 *  Description:
 *      initialize & cleanup modules
 */
int init_module(void)
{
    DA_ENTRY();

    HOOK_START_FN_NAME  = do_page_fault_hook_start_new;
    HOOK_END_FN_NAME    = do_page_fault_hook_end_new;
    DA_INFO("hook insertion complete, tracking on %d", pid);

    if (pt_init_ptracker() != 0) {
        goto init_reset_hook;
    }

    DA_INFO("clearing pages");
    pt_add_children(pid);
    goto init_good;

init_reset_hook:
    HOOK_START_FN_NAME  = NULL;
    HOOK_END_FN_NAME    = NULL;

init_good:

    DA_EXIT();
    return 0;    // Non-zero return means that the module couldn't be loaded.
}
void cleanup_module(void)
{
    DA_ENTRY();
    // TODO:: Unprotect all pages before exiting
    HOOK_START_FN_NAME  = NULL; 
    HOOK_END_FN_NAME    = NULL;                    // Removing hook, setting to NULL
    pt_exit_ptracker();
    lpl_CleanList();
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
    *hook_flag = 0;
    
    if(pt_find(current->tgid) >= 0) {
        // Start timer now, to calculate page fetch delay later
        *hook_timestamp = sched_clock();


        if (!ml_is_present(current->mm, address)) { // TODO :: pages are not seen as protected, so add all the pages to list, problem is there might be duplecate entries in the local page list
            /*pte_t* ptep = ml_get_ptep(current->mm, address);
            if (ptep)
                DA_WARNING("duplecate entry :: flags : prot:%-4lu present:%-4lu inlist:%-4lu %lu",
                                                    pte_flags(*ptep) & _PAGE_PROTNONE,
                                                    pte_flags(*ptep) & _PAGE_PRESENT,
                                                    pte_flags(*ptep) & _PAGE_SOFTW2,
                                                    address);
            else
                DA_WARNING("duplecate entry :: ptep entry is null");*/

            if(lpl_AddPage(current->mm, address) == 1)
                *hook_flag = 1;                     // Set flag to execute delay in end hook
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
    ulong delay_ns;
    //int count=0;

    // Check if hook_flag was set in start hook
    if(*hook_flag != 0) {
        // Inject delays here
        // ml_set_inlist(current->mm, address);
        ml_unprotect_page(current->mm, address);     // no page fault for pages in list
        page_fault_count++;

        delay_ns = 0;
        delay_ns = ((PAGE_SIZE * 8ULL) * 1000000000ULL) / bandwidth_bps;   // Transmission delay
        delay_ns += 2*latency_ns;                                       // Two way latency
        while ((sched_clock() - *hook_timestamp) < delay_ns) {
            // Wait for delay
            //count++;
        }
    }

    return 0;
}