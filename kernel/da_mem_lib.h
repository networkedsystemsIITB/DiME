#ifndef __DA_MEM_LIB_H__
#define __DA_MEM_LIB_H__

#include "../common/da_debug.h"
/*  get_ptep
 *
 *  Description:
 *      Returns pointer to PTE corresponding to given virtual address
 */
pte_t * ml_get_ptep(struct mm_struct *mm, unsigned long virt);

/*  protect_pages
 *
 *  Description:
 *      Traverse all pages table entries, and sets _PAGE_PROTNONE bit, to make
 *      page fault for those pages on next page access
 */
void ml_protect_all_pages(struct mm_struct * mm);


int ml_unprotect_page(struct mm_struct *mm, ulong address);
int ml_protect_page(struct mm_struct *mm, ulong address);
int ml_is_protected(struct mm_struct *mm, ulong address);
int ml_is_present(struct mm_struct *mm, ulong address);

#endif//__DA_MEM_LIB_H__