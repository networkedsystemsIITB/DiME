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

void protect_pages(struct mm_struct * mm);
struct task_struct* get_task_by_pid(pid_t pid);

/*****
 *
 *  Module params
 *
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abhishek Ghogare");
MODULE_DESCRIPTION("Disaggregation Emulator");

static int      pid             = 10;
static ulong    local_start     = 0;
static ulong    local_end       = 0;
static ulong    latency_ns      = 1000ULL;
static ulong    bandwidth_bps   = 10000000000ULL;

module_param(pid, int, 0);                  // pid cannot be changed directly from sysfs
module_param(local_start, ulong, 0755);
module_param(local_end, ulong, 0755);
module_param(latency_ns, ulong, 0755);
module_param(bandwidth_bps, ulong, 0755);
module_param(da_debug_flag, uint, 0755);    // debug level flags, can be set from sysfs
// TODO: unsigned long is 64bit in x86_64, need to change to ull

MODULE_PARM_DESC(pid, "PID of a process to track");
MODULE_PARM_DESC(local_start, "First byte of local memory area");
MODULE_PARM_DESC(local_end, "Next byte of last byte of local memory area");
MODULE_PARM_DESC(latency_ns, "One way latency in nano-sec");
MODULE_PARM_DESC(bandwidth_bps, "Bandwidth of network in bits-per-sec");
MODULE_PARM_DESC(da_debug_flag, "Module debug log level flags");


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
    protect_pages(ts->mm);                  // Set protected bit for all pages
    DA_EXIT();
    return 0;    // Non-zero return means that the module couldn't be loaded.
}
void cleanup_module(void)
{
    DA_ENTRY();
    HOOK_START_FN_NAME  = NULL; 
    HOOK_END_FN_NAME    = NULL;                    // Removing hook, setting to NULL
    DA_INFO("cleaning up module complete");
    DA_EXIT();
}

/*  get_ptep
 *
 *  Description:
 *      Returns pointer to PTE corresponding to given virtual address
 */
pte_t * get_ptep(struct mm_struct *mm, unsigned long virt) {
    struct page * page;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    pgd_t *pgd = pgd_offset(mm, virt);
    if (pgd_none(*pgd) || pgd_bad(*pgd))
        return NULL;
    pud = pud_offset(pgd, virt);
    if (pud_none(*pud) || pud_bad(*pud))
        return NULL;
    pmd = pmd_offset(pud, virt);
    if (pmd_none(*pmd) || pmd_bad(*pmd))
        return NULL;
    if (!(pte = pte_offset_map(pmd, virt)))
        return NULL;
    if (!(page = pte_page(*pte)))       // TODO:: Verify if required to check if page == NULL
        return NULL;

    return pte;
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
    pte_t* ptep = NULL;

    if(current->pid == pid) {
        // Start timer now, to calculate page fetch delay later
        timer_start = sched_clock();

        // Ceck if last faulted page is not same as current
        if(last_fault_addr && address != last_fault_addr) {
            ptep = get_ptep(current->mm, last_fault_addr);
            if(ptep && pte_present(*ptep)) {
                // Restore the bits
                set_pte( ptep , pte_clear_flags(*ptep, _PAGE_PRESENT) );
                set_pte( ptep , pte_set_flags(*ptep, _PAGE_PROTNONE) );
            }
        }

        ptep = get_ptep(current->mm, address);
        if(ptep) {
            // Check if page fault IS induced by us
            if ( (pte_flags(*ptep) & _PAGE_PROTNONE) && !(pte_flags(*ptep) & _PAGE_PRESENT) ) {
                //DA_INFO("page fault induced by clearing present bit %lu", address);
                // Set present bit 1, protnone 0
                set_pte( ptep , pte_set_flags(*ptep, _PAGE_PRESENT) );
                set_pte( ptep , pte_clear_flags(*ptep, _PAGE_PROTNONE) );
            }

            last_fault_addr = address;          // Remember this address for future
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
        // Check if local address range is valid and address IS NOT within it
        if(local_start < local_end && (address < local_start || local_end <= address)) {
            // Inject delays here
            DA_DEBUG("Waiting in delay");
            delay_ns = ((PAGE_SIZE * 8ULL) * 1000000000) / bandwidth_bps;   // Transmission delay
            delay_ns += 2*latency_ns;                                       // Two way latency
            while ((sched_clock() - timer_start) < delay_ns) {
                // Wait for delay
            }
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


/*  protect_pages
 *
 *  Description:
 *      Traverse all pages table entries, and sets _PAGE_PROTNONE bit, to make
 *      page fault for those pages on next page access
 */
void protect_pages(struct mm_struct * mm) {
    DA_ENTRY();
    if(mm) {
        struct vm_area_struct *vma = NULL;
        unsigned long vpage;

        for (vma=mm->mmap ; vma ; vma=vma->vm_next) {
            for (vpage = vma->vm_start; vpage < vma->vm_end; vpage += PAGE_SIZE) {
                pte_t *ptep = get_ptep(mm, vpage);

                if(ptep && pte_present(*ptep)) {
                    DA_INFO("protecting page %lu", vpage);
                    set_pte( ptep , pte_set_flags(*ptep, _PAGE_PROTNONE) );
                    set_pte( ptep , pte_clear_flags(*ptep, _PAGE_PRESENT) );
                }
            }
        }
    }
    DA_EXIT();
}
