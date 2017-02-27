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

unsigned int da_debug_flag =    DA_DEBUG_ALERT_FLAG | 
                                DA_DEBUG_INFO_FLAG | 
                                DA_DEBUG_WARNING_FLAG | 
                                DA_DEBUG_ERROR_FLAG | 
                                DA_DEBUG_ENTRYEXIT_FLAG;

/*****
 *
 *  Two hooks are required to set up in do_page_fault function in fault.c,
 *  one at the start of the function and other at the end of the function.
 *  e.g. :
 *      do_page_fault(struct pt_regs *regs, unsigned long error_code)
 *      {
 *          <start_hook>
 *          .....
 *          __do_page_fault(regs, error_code, address);
 *          .....
 *          <end_hook>
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

extern int (*HOOK_START_FN_NAME) (struct pt_regs *regs, 
                                    unsigned long error_code, 
                                    unsigned long address);
extern int (*HOOK_END_FN_NAME) (struct pt_regs *regs, 
                                    unsigned long error_code, 
                                    unsigned long address);
int do_page_fault_hook_start_new (struct pt_regs *regs, 
                                    unsigned long error_code, 
                                    unsigned long address);
int do_page_fault_hook_end_new (struct pt_regs *regs, 
                                    unsigned long error_code, 
                                    unsigned long address);

struct task_struct* get_task_by_pid(pid_t pid);

/*****
 *
 *  Module params
 *
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abhishek Ghogare, Trishal Patel");
MODULE_DESCRIPTION("Disaggregation Emulator");

static int      pid             = 10;
static ulong    latency_ns      = 1000ULL;
static ulong    bandwidth_bps   = 10000000000ULL;
       ulong    local_npages    = 2000ULL;

module_param(pid, int, 0444);               // pid cannot be changed but read directly from sysfs
module_param(latency_ns, ulong, 0644);
module_param(bandwidth_bps, ulong, 0644);
module_param(da_debug_flag, uint, 0644);    // debug level flags, can be set from sysfs
module_param(local_npages, ulong, 0644);    // number of local pages, acts as a cache for remote memory 
// TODO: unsigned long is 64bit in x86_64, need to change to ull

MODULE_PARM_DESC(pid, "PID of a process to track");
MODULE_PARM_DESC(latency_ns, "One way latency in nano-sec");
MODULE_PARM_DESC(bandwidth_bps, "Bandwidth of network in bits-per-sec");
MODULE_PARM_DESC(da_debug_flag, "Module debug log level flags");
MODULE_PARM_DESC(local_npages, "Number of available local pages");


/*****
 *
 *  Local variables
 *
 */
ulong *local_page_list = NULL;  // Circular list of local pages
ulong local_last_page = 0;      // Head of the circular list




/*****
 *
 *  init_module & cleanup_module
 *
 *  Description:
 *      initialize & cleanup modules
 */
int init_module(void)
{
    struct task_struct *ts;
    DA_ENTRY();

    HOOK_START_FN_NAME  = do_page_fault_hook_start_new;
    HOOK_END_FN_NAME    = do_page_fault_hook_end_new;
    DA_INFO("hook insertion complete, tracking on %d", pid);

    DA_INFO("clearing pages");
    ts = get_task_by_pid(pid);              // Get task struct of tracking pid
    ml_protect_all_pages(ts->mm);           // Set protected bit for all pages
    DA_EXIT();
    return 0;    // Non-zero return means that the module couldn't be loaded.
}
void cleanup_module(void)
{
    DA_ENTRY();
    HOOK_START_FN_NAME  = NULL; 
    HOOK_END_FN_NAME    = NULL;                    // Removing hook, setting to NULL
    lpl_CleanList();
    DA_INFO("cleaning up module complete");
    DA_EXIT();
}


/*  do_page_fault_hook_start_new
 *
 *  Description:
 *      do_page_fault hook function
 */
ulong last_fault_addr   = 0;
ulong timer_start       = 0;
int do_page_fault_hook_start_new (struct pt_regs *regs, 
                            unsigned long error_code, 
                            unsigned long address) {
    if(current->pid == pid) {
        // Start timer now, to calculate page fetch delay later
        timer_start = sched_clock();

        // Check if last faulted page is not same as current
        if(address != last_fault_addr) {
            lpl_AddPage(current->mm, address);
            last_fault_addr = address;          // Remember this address for future
        } else {
            DA_WARNING("Page fault on same page in series; addr : %lu", address);
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
                            unsigned long address) {
    ulong delay_ns;

    if(current->pid == pid) {
        // Inject delays here
        DA_DEBUG("Waiting in delay");
        delay_ns = ((PAGE_SIZE * 8ULL) * 1000000000) / bandwidth_bps;   // Transmission delay
        delay_ns += 2*latency_ns;                                       // Two way latency
        while ((sched_clock() - timer_start) < delay_ns) {
            // Wait for delay
        }
    }

    return 0;
}


/*  get_task_by_pid
 *
 *  Description:
 *      Returns task_struct corresponding to a process with given pid
 */
struct task_struct* get_task_by_pid(pid_t pid) {
    struct task_struct *ts = NULL;
    DA_ENTRY();

    for_each_process (ts) {
        if (ts->pid == pid) {
            return ts;
        }
    }

    DA_EXIT();
    return NULL;
}
