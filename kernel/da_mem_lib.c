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

#include <linux/pid.h>      // find_get_pid

#include "da_mem_lib.h"

void (*flush_tlb_mm_range_fp) (struct mm_struct *, unsigned long, unsigned long, unsigned long) = NULL;

int init_mem_lib (void) {
	unsigned long fp = 0;
	int ret = 0;
	DA_ENTRY();

	fp = kallsyms_lookup_name("flush_tlb_mm_range");
	if(fp==0) {
		DA_ERROR("could not find symbol flush_tlb_mm_range");
		flush_tlb_mm_range_fp = NULL;
		ret = -1;  // TODO:: Error codes
		goto INIT_EXIT;
	} else {
		flush_tlb_mm_range_fp = (void (*) (struct mm_struct *, unsigned long, unsigned long, unsigned long))fp;
		DA_INFO("registered flush_tlb_mm_range function pointer :%p", flush_tlb_mm_range_fp);
	}

INIT_EXIT:
	DA_EXIT();
	return ret;
}

int cleanup_mm_lib (void) {
	DA_ENTRY();
	DA_INFO("deregistering flush_tlb_mm_range function pointer :%p", flush_tlb_mm_range_fp);
	flush_tlb_mm_range_fp = NULL;
	DA_INFO("deregistered flush_tlb_mm_range function pointer :%p", flush_tlb_mm_range_fp);
	DA_EXIT();
	return 0;
}
// Function pointer to flush_tlb_page function. Since it is not exported symbol,
// it has to be extracted using kallsyms_lookup_name function.
void flush_tlb_page(struct vm_area_struct * vma, unsigned long a) {
	//DA_ENTRY();
	if(flush_tlb_mm_range_fp)
		flush_tlb_mm_range_fp(vma->vm_mm, a, a + PAGE_SIZE, VM_NONE);
	else
		DA_WARNING("flush_tlb_mm_range_fp is NULL, not flushing :%p", flush_tlb_mm_range_fp);
	//DA_EXIT();
}

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
	//DA_ENTRY();

	if(mm==NULL){
		DA_ERROR("mm is null");
		pte = NULL;
		goto EXIT;
	}
	pgd = pgd_offset(mm, virt);
	if(pgd == NULL)
	DA_ERROR("pgd is null : address:%lu", virt);
	if (pgd_none(*pgd) || pgd_bad(*pgd)) {
		pte = NULL;
		goto EXIT;
	}

	pud = pud_offset(pgd, virt);
	if(pud == NULL)
		DA_ERROR("pud is null : address:%lu", virt);

	if (pud_none(*pud) || pud_bad(*pud)) {
		pte = NULL;
		goto EXIT;
	}

	pmd = pmd_offset(pud, virt);
	if(pmd == NULL)
		DA_ERROR("pmd is null : address:%lu", virt);

	if (pmd_none(*pmd) || pmd_bad(*pmd)) {
		pte = NULL;
		goto EXIT;
	}
	if (!(pte = pte_offset_map(pmd, virt))) {
		pte = NULL;
		goto EXIT;
	}
	if (!(page = pte_page(*pte))) {      // TODO:: Verify if required to check if page == NULL
		pte = NULL;
		goto EXIT;
	}

EXIT:
	//DA_EXIT();
	return pte;
}
EXPORT_SYMBOL(ml_get_ptep);

struct page *ml_get_page_sruct(struct mm_struct *mm, unsigned long virt) {
	pte_t *ptep = ml_get_ptep(mm, virt);
	struct page * page = NULL;
	if(ptep) {
		page = pte_page(*ptep);
	}
	return page;
}
EXPORT_SYMBOL(ml_get_page_sruct);


 
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
				//DA_DEBUG("protecting page %lu", vpage);
				// ml_set_inlist(mm, vpage);
				ml_protect_page(mm, vpage);
			}
		}
	}
	DA_EXIT();
}
EXPORT_SYMBOL(ml_protect_all_pages);


int ml_protect_pte(struct mm_struct *mm, ulong address, pte_t *ptep) {
	struct vm_area_struct *vma = NULL;

	if(ptep && pte_present(*ptep)) {		// TODO:: why check if present
		// Protect page "address"
		set_pte( ptep , pte_clear_flags(*ptep, _PAGE_PRESENT) );
		set_pte( ptep , pte_set_flags(*ptep, _PAGE_PROTNONE) );

		vma = find_vma(mm, address);
		if(vma == NULL || address >= vma->vm_end)
			DA_WARNING("could not find vma for address: %lu", address);
		else //if(flush_tlb_page!=NULL)
			flush_tlb_page(vma, address);

		return 1;	// Success
	}

	return 0;		// Failure
}
EXPORT_SYMBOL(ml_protect_pte);

int ml_protect_page(struct mm_struct *mm, ulong address) {
	pte_t* ptep = ml_get_ptep(mm, address);
	return ml_protect_pte(mm, address, ptep);
}
EXPORT_SYMBOL(ml_protect_page);

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
EXPORT_SYMBOL(ml_unprotect_page);

int ml_clear_accessed(struct mm_struct *mm, ulong address) {
	struct vm_area_struct *vma = NULL;
	pte_t* ptep = ml_get_ptep(mm, address);
	if(ptep && pte_present(*ptep)) {		// TODO:: why check if present
		// Protect page "address"
		set_pte( ptep , pte_clear_flags(*ptep, _PAGE_ACCESSED) );

		vma = find_vma(mm, address);
		if(vma == NULL || address >= vma->vm_end)
			DA_WARNING("could not find vma for address: %lu", address);
		else //if(flush_tlb_page_fp!=NULL)
			flush_tlb_page(vma, address);

		return 1;	// Success
	}

	return 0;		// Failure
}
EXPORT_SYMBOL(ml_clear_accessed);

// TODO:: make inline functions

int ml_set_accessed_pte(struct mm_struct *mm, ulong address, pte_t* ptep) {
	if(ptep && pte_present(*ptep)) {		// TODO:: why check if present
		pte_mkyoung(*ptep);
		return 1;	// Success
	}

	return 0;		// Failure
}
EXPORT_SYMBOL(ml_set_accessed_pte);

int ml_set_accessed(struct mm_struct *mm, ulong address) {
	struct vm_area_struct *vma = NULL;
	pte_t* ptep = ml_get_ptep(mm, address);
	if(ptep && pte_present(*ptep)) {		// TODO:: why check if present
		pte_mkyoung(*ptep);

		vma = find_vma(mm, address);
		if(vma == NULL || address >= vma->vm_end)
			DA_WARNING("could not find vma for address: %lu", address);
		else //if(flush_tlb_page_fp!=NULL)
			flush_tlb_page(vma, address);

		return 1;	// Success
	}

	return 0;		// Failure
}
EXPORT_SYMBOL(ml_set_accessed);

int ml_clear_dirty(struct mm_struct *mm, ulong address) {
	struct vm_area_struct *vma = NULL;
	pte_t* ptep = ml_get_ptep(mm, address);
	if(ptep && pte_present(*ptep)) {		// TODO:: why check if present
		set_pte( ptep , pte_clear_flags(*ptep, _PAGE_DIRTY) );

		vma = find_vma(mm, address);
		if(vma == NULL || address >= vma->vm_end)
			DA_WARNING("could not find vma for address: %lu", address);
		else //if(flush_tlb_page_fp!=NULL)
			flush_tlb_page(vma, address);

		return 1;	// Success
	}

	return 0;		// Failure
}
EXPORT_SYMBOL(ml_clear_dirty);

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
EXPORT_SYMBOL(ml_is_protected);

int ml_is_present(struct mm_struct *mm, ulong address) {
	pte_t* ptep = ml_get_ptep(mm, address);
	if(ptep) {
		// Check if page fault IS induced by us
		if ( (pte_flags(*ptep) & _PAGE_PRESENT) ) {
			return 1;	// Success
		}
	} else {
		DA_WARNING("ptep is NULL; address : %lu", address);
	}
	
	return 0;			// Failure
}
EXPORT_SYMBOL(ml_is_present);

int ml_is_dirty(struct mm_struct *mm, ulong address) {
	pte_t* ptep = ml_get_ptep(mm, address);
	if(ptep) {
		if ( (pte_flags(*ptep) & _PAGE_DIRTY) ) {
			return 1;	// Success
		}
	} else {
		DA_WARNING("ptep is NULL; address : %lu", address);
	}
	
	return 0;			// Failure
}
EXPORT_SYMBOL(ml_is_dirty);


//////////////////////////////////////////////////////////////////////////////////////////// use pte_young & pte_old macros
int ml_is_accessed(struct mm_struct *mm, ulong address) {
	pte_t* ptep = ml_get_ptep(mm, address);
	if(ptep) {
		if ( (pte_flags(*ptep) & _PAGE_ACCESSED) ) {
			return 1;	// Success
		}
	} else {
		DA_WARNING("ptep is NULL; address : %lu", address);
	}
	
	return 0;			// Failure
}
EXPORT_SYMBOL(ml_is_accessed);

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

struct task_struct * ml_get_task_struct(pid_t pid) {
	struct pid *ps = NULL;
	struct task_struct *ts;

	ps = find_get_pid(pid);
	if(!ps) {
		//DA_ERROR("could not find struct pid for PID:%d", pid);
		return NULL;   /* No such process */
	}

	ts = pid_task(ps, PIDTYPE_PID);
	if(!ts) {
		//DA_ERROR("could not find task_struct for PID:%d", pid);
		return NULL;   /* No such process */
	}

	return ts;
}
EXPORT_SYMBOL(ml_get_task_struct);


struct mm_struct * ml_get_mm_struct(pid_t pid) {
	struct task_struct *ts;
	
	ts = ml_get_task_struct(pid);

	return ts==NULL ? NULL : ts->mm;
}
EXPORT_SYMBOL(ml_get_mm_struct);
