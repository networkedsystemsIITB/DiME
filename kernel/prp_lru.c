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

#include "prp_lru.h"
#include "../common/da_debug.h"
#include "common.h"



/*****
 *
 *  Module params
 *
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abhishek Ghogare, Dhantu");
MODULE_DESCRIPTION("Dime LRU page replacement policy");


static struct prp_lru_struct *to_prp_lru_struct(struct page_replacement_policy_struct *prp)
{
	return container_of(prp, struct prp_lru_struct, prp);
}

int lpl_AddPage(struct dime_instance_struct *dime_instance, struct mm_struct * mm, ulong address) {
	struct lpl_node_struct *node = NULL;
	int ret_execute_delay = 0;
	struct prp_lru_struct *prp_lru = to_prp_lru_struct(dime_instance->prp);
	//struct list_head *lnode = NULL;

	while (dime_instance->local_npages < prp_lru->lpl_count) {
		struct list_head *first_node;
		write_lock(&prp_lru->lock);
		first_node = prp_lru->lpl_head.next;
		list_del_rcu(first_node);
		prp_lru->lpl_count--;
		write_unlock(&prp_lru->lock);

		node = list_entry(first_node, struct lpl_node_struct, list_node);

		ml_protect_page(ml_get_mm_struct(node->pid), node->address);
		kfree(node);
		node = NULL;
		DA_INFO("remove extra local page, current count:%lu", prp_lru->lpl_count);
	}

	if (dime_instance->local_npages == 0) {
		// no need to add this address
		// we can treat this case as infinite local pages, and no need to inject delay on any of the page
		ret_execute_delay = 1;
		return ret_execute_delay;
	} else if (dime_instance->local_npages > prp_lru->lpl_count) {
		// Since there is still free space locally for remote pages, delay should not be injected
		ret_execute_delay = 1;
		node = (struct lpl_node_struct*) kmalloc(sizeof(struct lpl_node_struct), GFP_KERNEL);

		if(!node) {
			DA_ERROR("unable to allocate memory");
			return ret_execute_delay;
		} else {
			write_lock(&prp_lru->lock);
			prp_lru->lpl_count++;
			write_unlock(&prp_lru->lock);
		}
	} else {
		struct list_head *iternode = NULL;
		int found = 0;
		write_lock(&prp_lru->lock);
		
		while(found<2) {
			// TODO::Optimization possible, store first node of future criteria
			for(iternode = prp_lru->lpl_head.next ; iternode != &(prp_lru->lpl_head) ; iternode = iternode->next) {
				struct mm_struct *mm;
				int accessed, dirty;

				node = list_entry(iternode, struct lpl_node_struct, list_node);
				mm = ml_get_mm_struct(node->pid);
				accessed = ml_is_accessed(mm, node->address);
				dirty = ml_is_dirty(mm,node->address);

				if( !accessed && ((found==0 && !dirty) || found==1)) {
					found = 3;
					DA_INFO("found a page for replacement: A:%d D:%d addr:%lu", accessed, dirty, node->address);
				}
			}
			if(found<2) found++;
		}

		if(found != 3) {
			struct mm_struct *mm = ml_get_mm_struct(node->pid);
			iternode = prp_lru->lpl_head.next;
			node = list_entry(iternode, struct lpl_node_struct, list_node);
			//DA_INFO("could not find a page for replacement: A:%d D:%d addr:%lu", ml_is_accessed(mm, node->address), ml_is_dirty(mm,node->address), node->address);
		}
		prp_lru->lpl_head = *iternode->next;

		list_del_rcu(iternode);
		write_unlock(&prp_lru->lock);
		// protect FIFO last address, so that it will be faulted in future
		node = list_entry(iternode, struct lpl_node_struct, list_node);
		if(node->pid <= 0)
			 DA_ERROR("invalid pid: %d : address:%lu", node->pid, node->address);
		// ml_reset_inlist(mm, addr);
		ml_protect_page(ml_get_mm_struct(node->pid), node->address);
		// Since local pages are occupied, delay should be injected
		ret_execute_delay = 1;
	}

	node->address = address;
	node->pid = current->pid;
	write_lock(&prp_lru->lock);
	list_add_tail_rcu(&(node->list_node), &prp_lru->lpl_head);
	write_unlock(&prp_lru->lock);
	if(current->pid <= 0)
		DA_ERROR("invalid pid: %d : address:%lu", current->pid, address);
	// ml_set_inlist(mm, address);
	// ml_unprotect_page(mm, address);		// no page fault for pages in list // might be reason for crash, bad swap entry


	return ret_execute_delay;
}

//struct prp_lru_struct prp_lru;

void lpl_Init(struct dime_instance_struct *dime_instance) {
	struct prp_lru_struct *prp_lru = to_prp_lru_struct(dime_instance->prp);
	*prp_lru = (struct prp_lru_struct) {
		.prp = {
			.add_page 	= lpl_AddPage,
			.clean 		= lpl_CleanList,
		},
		.lpl_count = 0,
	};

	prp_lru->lpl_head = (struct list_head) { &(prp_lru->lpl_head), &(prp_lru->lpl_head) };
	rwlock_init(&(prp_lru->lock));
}


void __lpl_CleanList (struct prp_lru_struct *prp_lru) {
	DA_ENTRY();

	while (!list_empty(&prp_lru->lpl_head)) {
		struct lpl_node_struct *node = list_first_entry(&prp_lru->lpl_head, struct lpl_node_struct, list_node);
		list_del_rcu(&node->list_node);
		kfree(node);
	}

	DA_EXIT();
}

void lpl_CleanList (struct dime_instance_struct *dime_instance) {
	struct prp_lru_struct *prp_lru = to_prp_lru_struct(dime_instance->prp);
	__lpl_CleanList(prp_lru);
}


int init_module(void) {
	int ret = 0;
	int i;
	DA_ENTRY();

	for(i=0 ; i<dime.dime_instances_size ; ++i) {
		struct prp_lru_struct *prp_lru = (struct prp_lru_struct*) kmalloc(sizeof(struct prp_lru_struct), GFP_KERNEL);
		if(!prp_lru) {
			DA_ERROR("unable to allocate memory");
			return -1; // TODO:: Error codes
		}

		dime.dime_instances[i].prp = &(prp_lru->prp);
		lpl_Init(&dime.dime_instances[i]);
	}

	ret = register_page_replacement_policy(NULL);

	DA_EXIT();
	return ret;    // Non-zero return means that the module couldn't be loaded.
}
void cleanup_module(void) {
	int i;
	DA_ENTRY();


	for(i=0 ; i<dime.dime_instances_size ; ++i) {
		lpl_CleanList(&dime.dime_instances[i]);
		dime.dime_instances[i].prp = NULL;
	}

	deregister_page_replacement_policy(NULL);

	DA_INFO("cleaning up module complete");
	DA_EXIT();
}
