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
#include <linux/list.h>
#include <linux/slab.h>
#include <asm/pgtable_types.h>

#include "da_mem_lib.h"

#include "da_local_page_list.h"


static LIST_HEAD(lpl_head);
static ulong lpl_count = 0;


extern ulong local_npages;

/*int lpl_Initialize(ulong size) {
	if (!lpl_local_pages) {
		// TODO:: protect all pages first
		kfree(lpl_local_pages);
		lpl_local_pages = NULL;
		lpl_lasti = 0;
		lpl_local_npages = 0;
	}

	lpl_local_pages = (ulong *)kmalloc(size, GFP_KERNEL);

	if(lpl_local_pages) {
		lpl_local_npages = size;
		return 0;
	} else {
		return -ENOMEM;
	}
}*/
int test_list(ulong address) {

	int location=0;
	struct list_head *lnode = NULL;

	location=0;
	list_for_each(lnode, &lpl_head) {
		location++;
		if (list_entry(lnode, struct lpl_node_struct, list_node)->address == address) {
			//break;
			return location;
		}
	}

	/*
	pte_t* ptep = ml_get_ptep(current->mm, address);
	//DA_WARNING(" for address found in list : address = %lu", address);
	if (ptep)
		DA_WARNING("inserted :: flags : prot:%-4lu present:%-4lu inlist:%-4lu %lu",
											pte_flags(*ptep) & _PAGE_PROTNONE,
											pte_flags(*ptep) & _PAGE_PRESENT,
											pte_flags(*ptep) & _PAGE_SOFTW2,
											address);*/
	return 0;
}

int lpl_AddPage(struct mm_struct * mm, ulong address) {
	struct lpl_node_struct *node = NULL;
	int ret_execute_delay = 0;
	//struct list_head *lnode = NULL;

	while (local_npages < lpl_count) 
	{
		ulong addr;
		struct list_head *first_node = lpl_head.next;

		addr = list_entry(first_node, struct lpl_node_struct, list_node)->address;
		// ml_reset_inlist(mm, addr);
		ml_protect_page(mm, addr);
		list_del(first_node);
		kfree(first_node);	// TODO :: free not the list node, but container of list node
		lpl_count--;
		DA_INFO("remove extra local page, current count:%lu", lpl_count);
	}

	if (local_npages == 0)
	{
		// no need to add this address
		// we can treat this case as infinite local pages, and no need to inject delay on any of the page
		ret_execute_delay = 0;
		return ret_execute_delay;
	}
	else if (local_npages > lpl_count) 
	{
		// Since there is still free space locally for remote pages, delay should not be injected
		ret_execute_delay = 0;
		node = (struct lpl_node_struct*) kmalloc(sizeof(struct lpl_node_struct), GFP_KERNEL);

		if(node) 
		{
			list_add(&(node->list_node), &lpl_head);
			lpl_count++;
			//DA_INFO("add extra local page, current count:%lu", lpl_count);
		}
		else
		{
			DA_ERROR("unable to allocate memory");
			return ret_execute_delay;
		}
	}
	else
	{	
		// protect FIFO last address, so that it will be faulted in future
		ulong addr = list_first_entry(&lpl_head, struct lpl_node_struct, list_node)->address;
		// ml_reset_inlist(mm, addr);
		ml_protect_page(mm, addr);
		// Since local pages are occupied, delay should be injected
		ret_execute_delay = 1;
	}


	list_rotate_left(&lpl_head);
	list_last_entry(&lpl_head, struct lpl_node_struct, list_node)->address = address;
	// ml_set_inlist(mm, address);
	ml_unprotect_page(mm, address);		// no page fault for pages in list

	return ret_execute_delay;
}

void lpl_CleanList() {
    DA_ENTRY();

	while (!list_empty(&lpl_head)) {
		ulong addr = 0;
		struct list_head *first_node = lpl_head.next;
		addr = list_entry(first_node, struct lpl_node_struct, list_node)->address;
		//DA_INFO("removing from local mem : address:%lu", addr);
		list_del(first_node);
		kfree(first_node);
	}

    DA_EXIT();
}
