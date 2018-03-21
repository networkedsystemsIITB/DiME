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
MODULE_DESCRIPTION("DiME LRU page replacement policy");


static struct prp_lru_struct *to_prp_lru_struct(struct page_replacement_policy_struct *prp)
{
	return container_of(prp, struct prp_lru_struct, prp);
}

int lpl_AddPage(struct dime_instance_struct *dime_instance, struct mm_struct * mm, ulong address) {
	struct lpl_node_struct *node = NULL;
	int ret_execute_delay = 0;
	struct prp_lru_struct *prp_lru = to_prp_lru_struct(dime_instance->prp);
	struct page *page = NULL;


	// pages in local memory more than the configured dime instance quota, evict extra pages
	// possible when instance configuration is changed dynamically using proc file /proc/dime_config
	if(dime_instance->local_npages < prp_lru->lpl_count) {
		write_lock(&prp_lru->lock);
		while (dime_instance->local_npages < prp_lru->lpl_count) {
			struct list_head *first_node;
			if(!list_empty(prp_lru->lpl_pagecache_head.next))
				first_node = prp_lru->lpl_pagecache_head.next;
			else
				first_node = prp_lru->lpl_anon_head.next;
			list_del_rcu(first_node);
			prp_lru->lpl_count--;

			node = list_entry(first_node, struct lpl_node_struct, list_node);

			ml_protect_page(ml_get_mm_struct(node->pid), node->address);
			kfree(node);
			node = NULL;
			DA_INFO("remove extra local page, current count:%lu", prp_lru->lpl_count);
		}
		write_unlock(&prp_lru->lock);
	}

	// no need to add this address
	// we can treat this case as infinite local pages, and no need to inject delay on any of the page
	if (dime_instance->local_npages == 0) {
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
		struct list_head 	*iternode 					= NULL,
							*node_pc_naccessed 			= NULL,	// first not accessed page in pagecache local page list
							*node_pc_ndirty 			= NULL,	// first non dirty page in pagecache local page list
							*node_an_naccessed 			= NULL,	// first not accessed page in anonymous local page list
							*node_an_ndirty 			= NULL,	// first non dirty page in anonymous local page list
							*node_to_evict 				= NULL,
							*node_to_evict_list_head 	= NULL;

		write_lock(&prp_lru->lock);

		node_to_evict_list_head = &(prp_lru->lpl_pagecache_head);

RETRY_WITH_NEXT_LIST:
		if(node_to_evict_list_head == &(prp_lru->lpl_pagecache_head)) {
			//DA_ERROR("first scan in pagecache");
		} else if (node_to_evict_list_head == &(prp_lru->lpl_anon_head)) {
			//DA_ERROR("second scan in anonymous");
		}
		for(iternode = node_to_evict_list_head->next ; iternode != node_to_evict_list_head ; iternode = iternode->next) {
			struct mm_struct *mm;
			int accessed, dirty;

			node = list_entry(iternode, struct lpl_node_struct, list_node);
			if(current->pid == node->pid && address == node->address)
				// if faulting page is same as the one we want to evict, continue,
				// since it is going to get protected and will recursively called
				continue;

			mm = ml_get_mm_struct(node->pid);
			accessed = ml_is_accessed(mm, node->address);
			dirty = ml_is_dirty(mm, node->address);


			if(node_to_evict_list_head == &(prp_lru->lpl_pagecache_head)) {
				if(!node_pc_naccessed && !accessed) {
					node_pc_naccessed = iternode;
					break;
				}

				if(!node_pc_ndirty && !dirty) {
					node_pc_ndirty = iternode;
				}

			} else if(node_to_evict_list_head == &(prp_lru->lpl_anon_head)) {
				if(!node_an_naccessed && !accessed) {
					node_an_naccessed = iternode;
					break;
				}

				if(!node_an_ndirty && !dirty) {
					node_an_ndirty = iternode;
				}

			}

			ml_clear_accessed(mm, node->address);
		}

		if (node_to_evict_list_head == &(prp_lru->lpl_pagecache_head))
		{
			if (node_pc_naccessed) 
			{
				//DA_ERROR("found a page for replacement: pagecache non-accessed addr:%lu", node->address);
				node_to_evict = node_pc_naccessed;
				node_to_evict_list_head = &(prp_lru->lpl_pagecache_head);
			}
			else
			{
				//DA_ERROR("non-accessed page not found in pagecache, retrying with anonymous pages: %lu", address);
				node_to_evict_list_head = &(prp_lru->lpl_anon_head);
				goto RETRY_WITH_NEXT_LIST;
			} 
		}
		else if (node_to_evict_list_head == &(prp_lru->lpl_anon_head))
		{
			if (node_an_naccessed)
			{
				//DA_ERROR("found a page for replacement: anonymous non-accessed addr:%lu", node->address);	
				node_to_evict = node_an_naccessed;
				node_to_evict_list_head = &(prp_lru->lpl_anon_head);
			} 
			else if (node_pc_ndirty) 
			{
				//DA_ERROR("found a page for replacement: pagecache non-dirty addr:%lu", node->address);
				node_to_evict = node_pc_ndirty;
				node_to_evict_list_head = &(prp_lru->lpl_pagecache_head);
			} 
			else if (node_an_ndirty) 
			{
				//DA_ERROR("found a page for replacement: anonymous non-dirty addr:%lu", node->address);
				node_to_evict = node_an_ndirty;
				node_to_evict_list_head = &(prp_lru->lpl_anon_head);
			}
			else if(prp_lru->lpl_pagecache_head.next != &(prp_lru->lpl_pagecache_head)) 
			{
				//DA_ERROR("not found a page for replacement, taking last page of pagecache addr:%lu", node->address);
				node_to_evict = prp_lru->lpl_pagecache_head.next;
				node_to_evict_list_head = &(prp_lru->lpl_pagecache_head);
			} 
			else if(prp_lru->lpl_anon_head.next != &(prp_lru->lpl_anon_head)) 
			{
				//DA_ERROR("not found a page for replacement, taking last page of anonymous addr:%lu", node->address);
				node_to_evict = prp_lru->lpl_anon_head.next;
				node_to_evict_list_head = &(prp_lru->lpl_anon_head);
			} 
			else 
			{
				//DA_ERROR("not found a page for replacement, lists are empty addr:%lu", node->address);
				node_to_evict = NULL;
				node_to_evict_list_head = NULL;
			}
		}


		if(node_to_evict != NULL) {
			list_del_rcu(node_to_evict_list_head);
			list_add_rcu(node_to_evict_list_head, node_to_evict);
			list_del_rcu(node_to_evict);
		}

		write_unlock(&prp_lru->lock);

		if(node_to_evict == NULL) {
			DA_ERROR("no node to evict, error in local page list");
			return ret_execute_delay = 0;
		}

		// protect FIFO last address, so that it will be faulted in future
		node = list_entry(node_to_evict, struct lpl_node_struct, list_node);
		if(node->pid <= 0) {
			DA_ERROR("invalid pid: %d : address:%lu", node->pid, node->address);
			return ret_execute_delay = 0;
		}
		ml_protect_page(ml_get_mm_struct(node->pid), node->address);
		// Since local pages are occupied, delay should be injected
		ret_execute_delay = 1;
	}

	node->address = address;
	node->pid = current->pid;


	write_lock(&prp_lru->lock);
	
	page = ml_get_page_sruct(mm, address);

	if( ((unsigned long)(page->mapping) & (unsigned long)0x01) != 0 ) {
		list_add_tail_rcu(&(node->list_node), &prp_lru->lpl_anon_head);
		//DA_DEBUG("this is anonymous page: %lu, pid: %d", address, node->pid);
	} else {
		list_add_tail_rcu(&(node->list_node), &prp_lru->lpl_pagecache_head);
		//DA_DEBUG("this is pagecache page: %lu, pid: %d", address, node->pid);
	}
	write_unlock(&prp_lru->lock);
	if(current->pid <= 0)
		DA_ERROR("invalid pid: %d : address: %lu", current->pid, address);

	return ret_execute_delay;
}

void lpl_Init(struct dime_instance_struct *dime_instance) {
	//struct prp_lru_struct *prp_lru = NULL;

	DA_ENTRY();

	/*prp_lru = to_prp_lru_struct(dime_instance->prp);
	rwlock_init(&(prp_lru->lock));
	prp_lru->lpl_anon_head = (struct list_head) { &(prp_lru->lpl_anon_head), &(prp_lru->lpl_anon_head) };
	prp_lru->lpl_pagecache_head = (struct list_head) { &(prp_lru->lpl_pagecache_head), &(prp_lru->lpl_pagecache_head) };
	*prp_lru = (struct prp_lru_struct) {
		.prp = {
			.add_page 	= lpl_AddPage,
			.clean 		= lpl_CleanList,
		},
		.lpl_count = 0,
	};*/

	DA_EXIT();
}


void __lpl_CleanList (struct prp_lru_struct *prp_lru) {
	DA_ENTRY();

	while (!list_empty(&prp_lru->lpl_pagecache_head)) {
		struct lpl_node_struct *node = list_first_entry(&prp_lru->lpl_pagecache_head, struct lpl_node_struct, list_node);
		list_del_rcu(&node->list_node);
		kfree(node);
	}
	while (!list_empty(&prp_lru->lpl_anon_head)) {
		struct lpl_node_struct *node = list_first_entry(&prp_lru->lpl_anon_head, struct lpl_node_struct, list_node);
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

		rwlock_init(&(prp_lru->lock));
		write_lock(&prp_lru->lock);
		prp_lru->lpl_anon_head = (struct list_head) { &(prp_lru->lpl_anon_head), &(prp_lru->lpl_anon_head) };
		prp_lru->lpl_pagecache_head = (struct list_head) { &(prp_lru->lpl_pagecache_head), &(prp_lru->lpl_pagecache_head) };
		prp_lru->lpl_count = 0;
		prp_lru->prp.add_page = lpl_AddPage;
		prp_lru->prp.clean = lpl_CleanList;


		// Set policy pointer at the end of initialization
		dime.dime_instances[i].prp = &(prp_lru->prp);
		lpl_Init(&dime.dime_instances[i]);
		write_unlock(&prp_lru->lock);
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
