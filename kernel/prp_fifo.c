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

#include "prp_fifo.h"
#include "../common/da_debug.h"
#include "common.h"



/*****
 *
 *  Module params
 *
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abhishek Ghogare, Dhantu");
MODULE_DESCRIPTION("Dime FIFO page replacement policy");


static struct prp_fifo_struct *to_prp_fifo_struct(struct page_replacement_policy_struct *prp)
{
	return container_of(prp, struct prp_fifo_struct, prp);
}

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

/*
int test_list(struct dime_instance_struct *dime_instance, ulong address) {

	int location=0;
	struct list_head *lnode = NULL;
	struct lpl_node_struct *node;

	location=0;
	list_for_each(lnode, &lpl_head) {
		location++;
		node = list_entry(lnode, struct lpl_node_struct, list_node);
		if (node->pid <= 0) {
			DA_ERROR("\tPID value invalid : %d,  address : %lu", node->pid, node->address); 
		}
	}

	
	pte_t* ptep = ml_get_ptep(current->mm, address);
	//DA_WARNING(" for address found in list : address = %lu", address);
	if (ptep)
		DA_WARNING("inserted :: flags : prot:%-4lu present:%-4lu inlist:%-4lu %lu",
											pte_flags(*ptep) & _PAGE_PROTNONE,
											pte_flags(*ptep) & _PAGE_PRESENT,
											pte_flags(*ptep) & _PAGE_SOFTW2,
											address);
	return 0;
}
*/


int lpl_AddPage(struct dime_instance_struct *dime_instance, struct mm_struct * mm, ulong address) {
	struct lpl_node_struct *node = NULL;
	int ret_execute_delay = 0;
	struct prp_fifo_struct *prp_fifo = to_prp_fifo_struct(dime_instance->prp);
	//struct list_head *lnode = NULL;

	while (dime_instance->local_npages < prp_fifo->lpl_count) {
		struct list_head *first_node;
		write_lock(&prp_fifo->lock);
		first_node = prp_fifo->lpl_head.next;
		list_del_rcu(first_node);
		prp_fifo->lpl_count--;
		write_unlock(&prp_fifo->lock);

		node = list_entry(first_node, struct lpl_node_struct, list_node);

		ml_protect_page(ml_get_mm_struct(node->pid), node->address);
		kfree(node);
		node = NULL;
		DA_INFO("remove extra local page, current count:%lu", prp_fifo->lpl_count);
	}

	if (dime_instance->local_npages == 0) {
		// no need to add this address
		// we can treat this case as infinite local pages, and no need to inject delay on any of the page
		ret_execute_delay = 0;
		return ret_execute_delay;
	} else if (dime_instance->local_npages > prp_fifo->lpl_count) {
		// Since there is still free space locally for remote pages, delay should not be injected
		ret_execute_delay = 0;
		node = (struct lpl_node_struct*) kmalloc(sizeof(struct lpl_node_struct), GFP_KERNEL);

		if(!node) {
			DA_ERROR("unable to allocate memory");
			return ret_execute_delay;
		} else {
			write_lock(&prp_fifo->lock);
			prp_fifo->lpl_count++;
			write_unlock(&prp_fifo->lock);
		}
	} else {
		struct list_head *first_node;
		write_lock(&prp_fifo->lock);
		first_node = prp_fifo->lpl_head.next;
		list_del_rcu(first_node);
		write_unlock(&prp_fifo->lock);
		// protect FIFO last address, so that it will be faulted in future
		node = list_entry(first_node, struct lpl_node_struct, list_node);
		if(node->pid <= 0)
			 DA_ERROR("invalid pid: %d : address:%lu", node->pid, node->address);
		// ml_reset_inlist(mm, addr);
		ml_protect_page(ml_get_mm_struct(node->pid), node->address);
		// Since local pages are occupied, delay should be injected
		ret_execute_delay = 1;
	}

	node->address = address;
	node->pid = current->pid;
	write_lock(&prp_fifo->lock);
	list_add_tail_rcu(&(node->list_node), &prp_fifo->lpl_head);
	write_unlock(&prp_fifo->lock);
	if(current->pid <= 0)
		DA_ERROR("invalid pid: %d : address:%lu", current->pid, address);
	// ml_set_inlist(mm, address);
	// ml_unprotect_page(mm, address);		// no page fault for pages in list // might be reason for crash, bad swap entry

	return ret_execute_delay;
}

struct prp_fifo_struct prp_fifo;

void lpl_Init(struct dime_instance_struct *dime_instance) {
	prp_fifo = (struct prp_fifo_struct) {
		.prp = {
			.add_page 	= lpl_AddPage,
			.clean 		= lpl_CleanList,
		},
		.lpl_count = 0,
	};

	prp_fifo.lpl_head = (struct list_head) { &(prp_fifo.lpl_head), &(prp_fifo.lpl_head) };
	rwlock_init(&prp_fifo.lock);
}


void __lpl_CleanList (struct prp_fifo_struct *prp_fifo) {
    DA_ENTRY();

	while (!list_empty(&prp_fifo->lpl_head)) {
		struct lpl_node_struct *node = list_first_entry(&prp_fifo->lpl_head, struct lpl_node_struct, list_node);
		list_del_rcu(&node->list_node);
		kfree(node);
	}

    DA_EXIT();
}

void lpl_CleanList (struct dime_instance_struct *dime_instance) {
	struct prp_fifo_struct *prp_fifo = to_prp_fifo_struct(dime_instance->prp);
	__lpl_CleanList(prp_fifo);
}


int init_module(void) {
	int ret = 0;
    DA_ENTRY();

    lpl_Init(NULL);

    ret = register_page_replacement_policy(&prp_fifo.prp);

    DA_EXIT();
    return ret;    // Non-zero return means that the module couldn't be loaded.
}
void cleanup_module(void) {
    DA_ENTRY();

    deregister_page_replacement_policy(&prp_fifo.prp);
    __lpl_CleanList(&prp_fifo);

    DA_INFO("cleaning up module complete");
    DA_EXIT();
}
