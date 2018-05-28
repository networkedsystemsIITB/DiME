#ifndef __DA_MEM_LIB_H__
#define __DA_MEM_LIB_H__

#include "../common/da_debug.h"

int init_mem_lib (void);
int cleanup_mm_lib (void);
/*  get_ptep
 *
 *  Description:
 *      Returns pointer to PTE corresponding to given virtual address
 */
pte_t * ml_get_ptep(struct mm_struct *mm, unsigned long virt);
//struct page *ml_get_page_sruct(struct mm_struct *mm, unsigned long virt);

/*  protect_pages
 *
 *  Description:
 *      Traverse all pages table entries, and sets _PAGE_PROTNONE bit, to make
 *      page fault for those pages on next page access
 */
void ml_protect_all_pages(struct mm_struct * mm);


//int ml_unprotect_page(struct mm_struct *mm, ulong address);
//int ml_protect_pte(struct mm_struct *mm, ulong address, pte_t *ptep);
//int ml_protect_page(struct mm_struct *mm, ulong address);
//int ml_clear_accessed_pte(struct mm_struct *mm, ulong address, pte_t* ptep);
//int ml_clear_accessed(struct mm_struct *mm, ulong address);
//int ml_set_accessed_pte(struct mm_struct *mm, ulong address, pte_t* ptep);
//int ml_set_accessed(struct mm_struct *mm, ulong address);
//int ml_clear_dirty(struct mm_struct *mm, ulong address);
//int ml_is_protected(struct mm_struct *mm, ulong address);
//int ml_is_present(struct mm_struct *mm, ulong address);
//int ml_is_dirty(struct mm_struct *mm, ulong address);
//int ml_is_accessed(struct mm_struct *mm, ulong address);

//int ml_is_inlist_pte(struct mm_struct *mm, ulong address, pte_t *ptep);
//int ml_is_inlist(struct mm_struct *mm, ulong address);
//int ml_set_inlist_pte(struct mm_struct *mm, ulong address, pte_t *ptep);
//int ml_set_inlist(struct mm_struct *mm, ulong address);
//int ml_reset_inlist_pte(struct mm_struct *mm, ulong address, pte_t* ptep);
//int ml_reset_inlist(struct mm_struct *mm, ulong address);

struct task_struct * ml_get_task_struct(pid_t pid);
struct mm_struct * ml_get_mm_struct(pid_t pid);

extern void (*flush_tlb_mm_range_fp) (struct mm_struct *, unsigned long, unsigned long, unsigned long);

// Function pointer to flush_tlb_page function. Since it is not exported symbol,
// it has to be extracted using kallsyms_lookup_name function.
static inline void flush_tlb_page(struct mm_struct *mm, unsigned long a) {
	a -= (a % PAGE_SIZE);	// page address start
	flush_tlb_mm_range_fp(mm, a, a + PAGE_SIZE, VM_NONE);
}

static inline int ml_protect_pte(struct mm_struct *mm, ulong address, pte_t *ptep) {
	if(ptep && pte_present(*ptep)) {		// TODO:: why check if present
		// Protect page "address"
		set_pte( ptep , pte_clear_flags(*ptep, _PAGE_SOFTW2) ); // Reset inlist flag
		set_pte( ptep , pte_clear_flags(*ptep, _PAGE_PRESENT) );
		set_pte( ptep , pte_set_flags(*ptep, _PAGE_PROTNONE) );

		flush_tlb_page(mm, address);

		return 1;	// Success
	}

	return 0;		// Failure
}

static inline int ml_protect_page(struct mm_struct *mm, ulong address) {
	pte_t* ptep = ml_get_ptep(mm, address);
	return ml_protect_pte(mm, address, ptep);
}

static inline int ml_is_inlist_pte(struct mm_struct *mm, ulong address, pte_t *ptep) {
	if(ptep &&
		pte_present(*ptep) && 					// if pte is not present, page is definitely not in local list
		(pte_flags(*ptep) & _PAGE_SOFTW2)) {	// Check if page fault IS induced by us
		return 1;   // Success
	}
	return 0;           // Failure
}

static inline int ml_set_inlist_pte(struct mm_struct *mm, ulong address, pte_t *ptep) {
	if(ptep && pte_present(*ptep)) {
		set_pte( ptep , pte_set_flags(*ptep, _PAGE_SOFTW2) );
		return 1;   // Success
	}

	return 0;       // Failure
}

static inline int ml_reset_inlist_pte(struct mm_struct *mm, ulong address, pte_t* ptep) {
	if(ptep && pte_present(*ptep)) {
		set_pte( ptep , pte_clear_flags(*ptep, _PAGE_SOFTW2) );
		return 1;   // Success
	}
	
	return 0;           // Failure
}

#endif//__DA_MEM_LIB_H__