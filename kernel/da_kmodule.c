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


// Set your hook function name, which is exported from fault.c, here
#define HOOK_FN_NAME do_page_fault_hook_start
extern int (*HOOK_FN_NAME) (struct pt_regs *regs, 
                            unsigned long error_code, 
                            unsigned long address);
int do_page_fault_hook_new (struct pt_regs *regs, 
                            unsigned long error_code, 
                            unsigned long address);

/*****
 *
 *  Module params
 *
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abhishek Ghogare");
MODULE_DESCRIPTION("Disaggregation Emulator");

int pid = 10;
module_param(pid, int, 0);


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
    HOOK_FN_NAME = do_page_fault_hook_new;
    DA_INFO("hook insertion complete, tracking on %d", pid);
    DA_EXIT();
    return 0;    // Non-zero return means that the module couldn't be loaded.
}
void cleanup_module(void)
{
    DA_ENTRY();
    HOOK_FN_NAME = NULL;                    // Removing hook, setting to NULL
    DA_INFO("cleaning up module complete");
    DA_EXIT();
}

/*  get_ptep
 *
 *  Description:
 *      Returns pointer to PTE corresponding to given virtual address
 */
pte_t * get_ptep(struct mm_struct *mm, unsigned long virt) {
    //struct page * page;
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
    //if (!(page = pte_page(*pte)))
    //    return NULL;

    return pte;
}


/*  do_page_fault_hook_new
 *
 *  Description:
 *      Page fault function hook function
 */
unsigned long last_fault_addr = 0;
int do_page_fault_hook_new (struct pt_regs *regs, 
                            unsigned long error_code, 
                            unsigned long address) {
    pte_t * ptep = NULL;

    if(current->pid == pid) {
        ptep = get_ptep(current->mm, address);
        if(ptep) {
            // page fault IS induced by us
            if ( (pte_flags(*ptep) & _PAGE_PROTNONE) && !(pte_flags(*ptep) & _PAGE_PRESENT) ) {
                DA_INFO("page fault induced by clearing present bit %lu", address);
                // Set present bit 1, protnone 0
                set_pte( ptep , pte_set_flags(*ptep, _PAGE_PRESENT) );
                set_pte( ptep , pte_clear_flags(*ptep, _PAGE_PROTNONE) );
            }
            // Remember this address for future
            last_fault_addr = address;
        }

        // Check if last faulted page is not same as current
        if(last_fault_addr && address != last_fault_addr) {
            ptep = get_ptep(current->mm, last_fault_addr);
            if(ptep && pte_present(*ptep)) {
                // Restore the bits
                set_pte( ptep , pte_clear_flags(*ptep, _PAGE_PRESENT) );
                set_pte( ptep , pte_set_flags(*ptep, _PAGE_PROTNONE) );
            }
        }
    }

    return 0;
}
