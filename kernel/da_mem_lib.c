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
#include <linux/slab.h>
#include <asm/pgtable_types.h>

#include "da_mem_lib.h"

/*  get_ptep
 *
 *  Description:
 *      Returns pointer to PTE corresponding to given virtual address
 */
pte_t * ml_get_ptep(struct mm_struct *mm, unsigned long virt) {
    struct page * page;
    pud_t *pud;
    pmd_t *pmd;
    pte_t *pte;
    pgd_t *pgd;

    if(mm==NULL){
        DA_ERROR("mm is null");
        return NULL;
    }
    pgd = pgd_offset(mm, virt);
    if(pgd == NULL)
	DA_ERROR("pgd is null : address:%lu", virt);
    if (pgd_none(*pgd) || pgd_bad(*pgd))
        return NULL;

    pud = pud_offset(pgd, virt);
    if(pud == NULL)
        DA_ERROR("pud is null : address:%lu", virt);

    if (pud_none(*pud) || pud_bad(*pud))
        return NULL;

    pmd = pmd_offset(pud, virt);
    if(pmd == NULL)
        DA_ERROR("pmd is null : address:%lu", virt);

    if (pmd_none(*pmd) || pmd_bad(*pmd))
        return NULL;
    if (!(pte = pte_offset_map(pmd, virt)))
        return NULL;
    if (!(page = pte_page(*pte)))       // TODO:: Verify if required to check if page == NULL
        return NULL;

    return pte;
}



/*  protect_pages
 *
 *  Description:
 *      Traverse all pages table entries, and sets _PAGE_PROTNONE bit, to make
 *      page fault for those pages on next page access
 */
void ml_protect_all_pages(struct mm_struct * mm) {
    DA_ENTRY();
    if(mm) {
        struct vm_area_struct *vma = NULL;
        unsigned long vpage;

        for (vma=mm->mmap ; vma ; vma=vma->vm_next) {
            for (vpage = vma->vm_start; vpage < vma->vm_end; vpage += PAGE_SIZE) {
                DA_DEBUG("protecting page %lu", vpage);
                // ml_set_inlist(mm, vpage);
            	ml_protect_page(mm, vpage);
            }
        }
    }
    DA_EXIT();
}


int ml_protect_page(struct mm_struct *mm, ulong address) {
	pte_t* ptep = ml_get_ptep(mm, address);
	if(ptep && pte_present(*ptep)) {       // TODO:: why check if present
	    // Protect page "address"
	    set_pte( ptep , pte_clear_flags(*ptep, _PAGE_PRESENT) );
	    set_pte( ptep , pte_set_flags(*ptep, _PAGE_PROTNONE) );
	    return 1;	// Success
	}

	return 0;		// Failure
}

int ml_unprotect_page(struct mm_struct *mm, ulong address) {
    pte_t* ptep = ml_get_ptep(mm, address);
    if(ptep && pte_present(*ptep)) {
        // Check if page fault IS induced by us
        //if ( (pte_flags(*ptep) & _PAGE_PROTNONE) && !(pte_flags(*ptep) & _PAGE_PRESENT) ) {
            //DA_INFO("page fault induced by clearing present bit %lu", address);
            // Clear protection of page "address"
            set_pte( ptep , pte_set_flags(*ptep, _PAGE_PRESENT) );
            set_pte( ptep , pte_clear_flags(*ptep, _PAGE_PROTNONE) );
            return 1;	// Success
        //}
    }

    return 0;			// Failure
}

int ml_is_protected(struct mm_struct *mm, ulong address) {
    pte_t* ptep = ml_get_ptep(mm, address);
    if(ptep) {
        // Check if page fault IS induced by us
        if ( (pte_flags(*ptep) & _PAGE_PROTNONE) ) {
            return 1;   // Success
        }
    } else {
        DA_WARNING("ptep is NULL; address : %lu", address);
    }
    
    return 0;           // Failure
}

int ml_is_present(struct mm_struct *mm, ulong address) {
    pte_t* ptep = ml_get_ptep(mm, address);
    if(ptep) {
        // Check if page fault IS induced by us
        if ( (pte_flags(*ptep) & _PAGE_PRESENT) ) {
            return 1;   // Success
        }
    } else {
        DA_WARNING("ptep is NULL; address : %lu", address);
    }
    
    return 0;           // Failure
}

int ml_is_inlist(struct mm_struct *mm, ulong address) {
    pte_t* ptep = ml_get_ptep(mm, address);
    if(ptep) {
        // Check if page fault IS induced by us
        if ( (pte_flags(*ptep) & _PAGE_SOFTW2) ) {
            return 1;   // Success
        }
    } else {
        DA_WARNING("ptep is NULL; address : %lu", address);
    }
    
    return 0;           // Failure
}

int ml_set_inlist(struct mm_struct *mm, ulong address) {
    pte_t* ptep = ml_get_ptep(mm, address);
    if(ptep && pte_present(*ptep)) {
        set_pte( ptep , pte_set_flags(*ptep, _PAGE_SOFTW2) );
        return 1;   // Success
    }

    return 0;       // Failure
}

int ml_reset_inlist(struct mm_struct *mm, ulong address) {
    pte_t* ptep = ml_get_ptep(mm, address);
    if(ptep && pte_present(*ptep)) {
        set_pte( ptep , pte_clear_flags(*ptep, _PAGE_SOFTW2) );
        return 1;   // Success
    }
    
    return 0;           // Failure
}
