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
#include <linux/kthread.h>

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

#define DA_LRU_STAT(void) do { \
	int i=0;\
	for(i=0 ; i<dime.dime_instances_size ; ++i) { \
		struct dime_instance_struct *dime_instance = &(dime.dime_instances[i]); \
		struct prp_lru_struct *prp_lru = to_prp_lru_struct(dime_instance->prp); \
		\
		DA_ERROR("inst: %d free: %d \tin_pc: %d \ta_pc: %d \tin_an: %d \ta_an: %d \tcount: %lu",  i, \
																prp_lru->free.size, \
																prp_lru->inactive_pc.size, \
																prp_lru->active_pc.size, \
																prp_lru->inactive_an.size, \
																prp_lru->active_an.size, \
																prp_lru->lpl_count); \
	} \
}while(0)

int lpl_AddPage(struct dime_instance_struct *dime_instance, struct mm_struct * mm, ulong address) {
	struct lpl_node_struct *node = NULL;
	int ret_execute_delay = 0;
	struct prp_lru_struct *prp_lru = to_prp_lru_struct(dime_instance->prp);
	struct page *page = NULL;

	//DA_ERROR("pagefault for %lu", address);
	//DA_LRU_STAT();
	// pages in local memory more than the configured dime instance quota, evict extra pages
	// possible when instance configuration is changed dynamically using proc file /proc/dime_config
	if(dime_instance->local_npages < prp_lru->lpl_count) {
		// TODO:: not required till dynamic changes, need to apply locks
		write_lock(&prp_lru->lock);
		while (dime_instance->local_npages < prp_lru->lpl_count) {
			struct list_head *first_node = NULL;
			if(!list_empty(prp_lru->inactive_pc.head.next))
				first_node = prp_lru->inactive_pc.head.next;
			else if(!list_empty(prp_lru->inactive_an.head.next))
				first_node = prp_lru->inactive_an.head.next;
			else if(!list_empty(prp_lru->active_pc.head.next))
				first_node = prp_lru->active_pc.head.next;
			else if(!list_empty(prp_lru->active_an.head.next))
				first_node = prp_lru->active_an.head.next;
			else
				DA_ERROR("all lists are empty, unable to select node");
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
							*node_to_evict 				= NULL;
		//int old_accessed;
		int old_dirty;
		struct mm_struct * old_mm;

retry_node_search:

		// search in free list
		write_lock(&prp_lru->free.lock);
		if(! list_empty(&prp_lru->free.head)) {
			node_to_evict = prp_lru->free.head.next;
			list_del_rcu(node_to_evict);
			prp_lru->free.size--;
		}
		write_unlock(&prp_lru->free.lock);

		if(!node_to_evict) {
			// search from pagecache inactive list
			// TODO:: decide based on some criterion whether to evict pages from this list
			struct list_head temp_list = LIST_HEAD_INIT(temp_list);

			write_lock(&prp_lru->inactive_pc.lock);
			for(iternode = prp_lru->inactive_pc.head.next ; iternode != &prp_lru->inactive_pc.head ; iternode = iternode->next) {
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

				if(accessed) {
					iternode = iternode->prev;
					list_del_rcu(&(node->list_node));
					prp_lru->inactive_pc.size--;
					//ml_clear_accessed(mm, node->address);
					list_add_tail_rcu(&(node->list_node), &temp_list);
				} else {
					node_to_evict = iternode;

					// reposition list head to this point, so that next time we wont scan again previously scanned nodes
					list_del_rcu(&prp_lru->inactive_pc.head);
					list_add_rcu(&prp_lru->inactive_pc.head, node_to_evict);

					list_del_rcu(node_to_evict);
					prp_lru->inactive_pc.size--;

					//DA_INFO("found a page from inactive pc, %lu, %d", node->address, prp_lru->inactive_pc.size);
					//DA_LRU_STAT();
					break;
				}

			}
			write_unlock(&prp_lru->inactive_pc.lock);


			write_lock(&prp_lru->active_pc.lock);
			while(!list_empty(&temp_list)) {
				struct list_head *h = temp_list.next;
				list_del_rcu(h);
				list_add_tail_rcu(h, &prp_lru->active_pc.head);
				prp_lru->active_pc.size++;
			}
			write_unlock(&prp_lru->active_pc.lock);
		}

		if(!node_to_evict) {
			// search from anon inactive list
			// TODO:: decide based on some criterion whether to evict pages from this list
			struct list_head temp_list = LIST_HEAD_INIT(temp_list);

			write_lock(&prp_lru->inactive_an.lock);
			for(iternode = prp_lru->inactive_an.head.next ; iternode != &prp_lru->inactive_an.head ; iternode = iternode->next) {
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

				if(accessed) {
					iternode = iternode->prev;
					list_del_rcu(&(node->list_node));
					prp_lru->inactive_an.size--;
					//ml_clear_accessed(mm, node->address);
					list_add_tail_rcu(&(node->list_node), &temp_list);
				} else {
					node_to_evict = iternode;

					// reposition list head to this point, so that next time we wont scan again previously scanned nodes
					list_del_rcu(&prp_lru->inactive_an.head);
					list_add_rcu(&prp_lru->inactive_an.head, node_to_evict);

					list_del_rcu(node_to_evict);

					prp_lru->inactive_an.size--;
					//DA_INFO("found a page from inactive an, %lu, %d", node->address, prp_lru->inactive_an.size);
					//DA_LRU_STAT();
					break;
				}

			}
			write_unlock(&prp_lru->inactive_an.lock);


			write_lock(&prp_lru->active_an.lock);
			while(!list_empty(&temp_list)) {
				struct list_head *h = temp_list.next;
				list_del_rcu(h);
				list_add_tail_rcu(h, &prp_lru->active_an.head);
				prp_lru->active_an.size++;
			}
			write_unlock(&prp_lru->active_an.lock);
		}

		if(!node_to_evict) {
			// search from pagecache active list
			// TODO:: decide based on some criterion whether to evict pages from this list
			struct list_head temp_list = LIST_HEAD_INIT(temp_list);

			write_lock(&prp_lru->active_pc.lock);
			for(iternode = prp_lru->active_pc.head.next ; iternode != &prp_lru->active_pc.head ; iternode = iternode->next) {
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

				if(accessed) {// || dirty) {
					iternode = iternode->prev;
					list_del_rcu(&(node->list_node));
					//ml_clear_accessed(mm, node->address);
					//ml_clear_dirty(mm, node->address);		// TODO:: create new list of dirty pages, kswapd will flush these pages
					//list_add_tail_rcu(&(node->list_node), &prp_lru->active_pc.head);
					list_add_tail_rcu(&(node->list_node), &temp_list);
					//DA_INFO("not selecting this page since accessed or dirty active pc, %lu, %d", node->address, prp_lru->active_pc.size);
				} else {
					node_to_evict = iternode;

					// reposition list head to this point, so that next time we wont scan again previously scanned nodes
					list_del_rcu(&prp_lru->active_pc.head);
					list_add_rcu(&prp_lru->active_pc.head, node_to_evict);

					list_del_rcu(node_to_evict);
					prp_lru->active_pc.size--;
					//DA_INFO("found a page from active pc, %lu, %d", node->address, prp_lru->active_pc.size);
					//DA_LRU_STAT();
					break;
				}
			}

			while(!list_empty(&temp_list)) {
				struct list_head *h = temp_list.next;
				list_del_rcu(h);
				list_add_tail_rcu(h, &prp_lru->active_pc.head);
			}
			write_unlock(&prp_lru->active_pc.lock);
		}

		if(!node_to_evict) {
			// search from anon active list
			// TODO:: decide based on some criterion whether to evict pages from this list
			struct list_head temp_list = LIST_HEAD_INIT(temp_list);

			write_lock(&prp_lru->active_an.lock);
			for(iternode = prp_lru->active_an.head.next ; iternode != &prp_lru->active_an.head ; iternode = iternode->next) {
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

				if(accessed) {// || dirty) {
					iternode = iternode->prev;
					list_del_rcu(&(node->list_node));
					//ml_clear_accessed(mm, node->address);
					//ml_clear_dirty(mm, node->address);		// TODO:: create new list of dirty pages, kswapd will flush these pages
					list_add_tail_rcu(&(node->list_node), &temp_list);
				} else {
					node_to_evict = iternode;

					// reposition list head to this point, so that next time we wont scan again previously scanned nodes
					list_del_rcu(&prp_lru->active_an.head);
					list_add_rcu(&prp_lru->active_an.head, node_to_evict);

					list_del_rcu(node_to_evict);
					prp_lru->active_an.size--;
					//DA_INFO("found a page from active an, %lu, %d", node->address, prp_lru->active_an.size);
					//DA_LRU_STAT();
					break;
				}

			}

			while(!list_empty(&temp_list)) {
				struct list_head *h = temp_list.next;
				list_del_rcu(h);
				list_add_tail_rcu(h, &prp_lru->active_an.head);
			}
			write_unlock(&prp_lru->active_an.lock);
		}

		if(!node_to_evict) {
			// forcefully select any page
			write_lock(&prp_lru->inactive_pc.lock);
			if(!list_empty(&prp_lru->inactive_pc.head)) {
				node_to_evict = prp_lru->inactive_pc.head.next;
				list_del_rcu(node_to_evict);
					prp_lru->inactive_pc.size--;
					DA_WARNING("could not find a page to evict, selecting first page from inactive pagecache");
					//DA_LRU_STAT();
			}
			write_unlock(&prp_lru->inactive_pc.lock);

			if(!node_to_evict) {
				write_lock(&prp_lru->inactive_an.lock);
				if(!list_empty(&prp_lru->inactive_an.head)) {
					node_to_evict = prp_lru->inactive_an.head.next;
					list_del_rcu(node_to_evict);
					prp_lru->inactive_an.size--;
					DA_WARNING("could not find a page to evict, selecting first page from inactive anon");
					//DA_LRU_STAT();
				}
				write_unlock(&prp_lru->inactive_an.lock);
			}

			if(!node_to_evict) {
				write_lock(&prp_lru->active_pc.lock);
				if(!list_empty(&prp_lru->active_pc.head)) {
					node_to_evict = prp_lru->active_pc.head.next;
					list_del_rcu(node_to_evict);
					prp_lru->active_pc.size--;
					DA_WARNING("could not find a page to evict, selecting first page from active pagecache");
					//DA_LRU_STAT();
				}
				write_unlock(&prp_lru->active_pc.lock);

			}

			if(!node_to_evict) {
				write_lock(&prp_lru->active_an.lock);
				if(!list_empty(&prp_lru->active_an.head)) {
					node_to_evict = prp_lru->active_an.head.next;
					list_del_rcu(node_to_evict);
					prp_lru->active_an.size--;
					DA_WARNING("could not find a page to evict, selecting first page from active anon");
					//DA_LRU_STAT();
				}
				write_unlock(&prp_lru->active_an.lock);
			}

			if(!node_to_evict)
				goto retry_node_search;
		}

		// protect page, so that it will get faulted in future
		node = list_entry(node_to_evict, struct lpl_node_struct, list_node);
		if(node->pid <= 0) {
			DA_ERROR("invalid pid: %d : address:%lu", node->pid, node->address);
			return ret_execute_delay = 0;
		}

		old_mm = ml_get_mm_struct(node->pid);
		old_dirty = ml_is_dirty(old_mm, node->address);
		if(old_dirty) {
			ret_execute_delay = 1;
			ml_clear_dirty(old_mm, node->address);
			// TODO:: instead insert address to flush dirty pages list
		}

		ml_protect_page(ml_get_mm_struct(node->pid), node->address);
	}


	node->address = address;
	node->pid = current->pid;
	
	page = ml_get_page_sruct(mm, address);
	
	// Sometimes bulk pagefault requests come and evicting any page from these requests will again trigger pagefault.
	// This happens recursively if accessed bit is not set for each requested page.
	// So, set accessed bit to all requested pages
	ml_set_accessed(mm, address);

	if( ((unsigned long)(page->mapping) & (unsigned long)0x01) != 0 ) {
		write_lock(&prp_lru->active_an.lock);
		list_add_tail_rcu(&(node->list_node), &prp_lru->active_an.head);
		prp_lru->active_an.size++;
		write_unlock(&prp_lru->active_an.lock);
		//DA_DEBUG("this is anonymous page: %lu, pid: %d", address, node->pid);
	} else {
		write_lock(&prp_lru->active_pc.lock);
		list_add_tail_rcu(&(node->list_node), &prp_lru->active_pc.head);
		prp_lru->active_pc.size++;
		write_unlock(&prp_lru->active_pc.lock);
		//DA_DEBUG("this is pagecache page: %lu, pid: %d", address, node->pid);
	}
	if(current->pid <= 0)
		DA_ERROR("invalid pid: %d : address: %lu", current->pid, address);

	return ret_execute_delay;
}
/*
LRU pages belong to one of two linked list, the "active" and the "inactive" list. 
Page movement is driven by memory pressure. Pages are taken from the end of the inactive list to be freed. 
If the page has the reference bit set, it is moved to the beginning of the active list and the reference bit is cleared. 
If the page is dirty, writeback is commenced and the page is moved to the beginning of the inactive list. 
If the page is unreferenced and clean, it can be reused.

The kswapd thread in woken up by the physical page allocator only when the number of available free pages is less than pages_low.
zone->pages_low = (zone->pages_min * 5) / 4;   // in file mm/page_alloc.c

pagecache list size: As long as the working set is smaller than half of the file cache, it is completely protected from the page eviction code.
size of anonymous inactive list = Maybe 30% of anonymous pages on a 1GB system, but 1% of anonymous pages on a 1TB system?
*/

#define MIN_FREE_PAGES 10	// percentage of local memory available in free list

int balance_lists(struct lpl *active_list, struct lpl *inactive_list, int target, struct lpl *free) {
	int moved_inactive=0, moved_free=0;
	struct list_head 	*iternode 					= NULL,
						*iternode_free 				= NULL;
	struct lpl_node_struct *node = NULL;
	struct list_head local_free_list = LIST_HEAD_INIT(local_free_list);
	struct list_head local_inactive_list = LIST_HEAD_INIT(local_inactive_list);
	struct list_head local_active_list = LIST_HEAD_INIT(local_active_list);

	// move non-accessed pages to inactive list
	write_lock(&active_list->lock);
	for(iternode = active_list->head.next ; iternode != &active_list->head && target > 0; iternode = iternode->next) {
		struct mm_struct *mm;
		int accessed, dirty;

		node = list_entry(iternode, struct lpl_node_struct, list_node);
		mm = ml_get_mm_struct(node->pid);
		if(!mm) {
			iternode = iternode->prev;
			list_del_rcu(&node->list_node);
			active_list->size--;
			list_add_tail_rcu(&node->list_node, &local_free_list);
			target--;
			moved_free++;
			continue;
		}
		accessed = ml_is_accessed(mm, node->address);
		dirty = ml_is_dirty(mm, node->address);

		/*if(dirty) {
			// emulate page flush, inject delay
			ulong delay_ns = 0, start_time = sched_clock();
			delay_ns = ((PAGE_SIZE * 8ULL) * 1000000000ULL) / dime_instance->bandwidth_bps;  // Transmission delay
			delay_ns += 2*dime_instance->latency_ns;                                         // Two way latency
			while ((sched_clock() - start_time) < delay_ns) {
			    // Wait for delay
			}
			ml_clear_dirty(mm, node->address);
		}*/
		if(!accessed) {
			iternode_free = iternode;
			iternode = iternode->prev;
			list_del_rcu(iternode_free);
			active_list->size--;
			target--;
			list_add_tail_rcu(iternode_free, &local_inactive_list);
			moved_inactive++;
			//DA_LRU_STAT();
		}

		ml_clear_accessed(mm, node->address);
	}
	// reposition list head to this point, so that next time we wont scan again previously scanned nodes
	if(iternode != &active_list->head) {
		list_del_rcu(&active_list->head);
		list_add_tail_rcu(&active_list->head, iternode);
	}
	write_unlock(&active_list->lock);


	// move accessed pages to active list
	write_lock(&inactive_list->lock);
	for(iternode = inactive_list->head.next ; iternode != &inactive_list->head; iternode = iternode->next) {
		struct mm_struct *mm;
		int accessed;

		node = list_entry(iternode, struct lpl_node_struct, list_node);
		mm = ml_get_mm_struct(node->pid);
		if(!mm) {
			iternode = iternode->prev;
			list_del_rcu(&node->list_node);
			inactive_list->size--;
			list_add_tail_rcu(&node->list_node, &local_free_list);
			target--;
			moved_free++;
			continue;
		}
		accessed = ml_is_accessed(mm, node->address);

		if(accessed) {
			iternode_free = iternode;
			iternode = iternode->prev;
			list_del_rcu(iternode_free);
			inactive_list->size--;
			list_add_tail_rcu(iternode_free, &local_active_list);
			moved_inactive--;
			//DA_LRU_STAT();
		}

		ml_clear_accessed(mm, node->address);
	}
	write_unlock(&inactive_list->lock);



	write_lock(&free->lock);
	while(!list_empty(&local_free_list)) {
		struct list_head *h = local_free_list.next;
		list_del_rcu(h);
		list_add_tail_rcu(h, &free->head);
		free->size++;
	}
	write_unlock(&free->lock);

	write_lock(&active_list->lock);
	while(!list_empty(&local_active_list)) {
		struct list_head *h = local_active_list.next;
		list_del_rcu(h);
		list_add_tail_rcu(h, &active_list->head);
		active_list->size++;
	}
	write_unlock(&active_list->lock);

	write_lock(&inactive_list->lock);
	while(!list_empty(&local_inactive_list)) {
		struct list_head *h = local_inactive_list.next;
		list_del_rcu(h);
		list_add_tail_rcu(h, &inactive_list->head);
		inactive_list->size++;
	}
	write_unlock(&inactive_list->lock);

	DA_LRU_STAT();
	DA_ERROR("moved_inactive: %d \tmoved_free: %d \tremaining: %d", moved_inactive, moved_free, target);
	return 0;
}

int try_to_free_pages(struct dime_instance_struct *dime_instance, struct lpl *pl, int target, struct lpl *free) {
	int moved_free=0;
	struct list_head 	*iternode 					= NULL,
						*iternode_free 				= NULL;
	struct lpl_node_struct *node = NULL;
	struct list_head local_free_list = LIST_HEAD_INIT(local_free_list);


	write_lock(&pl->lock);
	for(iternode = pl->head.next ; iternode != &pl->head && target > 0; iternode = iternode->next) {
		struct mm_struct *mm;
		int accessed, dirty;

		node = list_entry(iternode, struct lpl_node_struct, list_node);
		mm = ml_get_mm_struct(node->pid);
		if(!mm) {
			iternode = iternode->prev;
			list_del_rcu(&node->list_node);
			pl->size--;
			list_add_tail_rcu(&node->list_node, &local_free_list);
			target--;
			moved_free++;
			continue;
		}
		accessed = ml_is_accessed(mm, node->address);
		dirty = ml_is_dirty(mm, node->address);

		if(dirty) {
			// emulate page flush, inject delay
			ulong delay_ns = 0, start_time = sched_clock();
			delay_ns = ((PAGE_SIZE * 8ULL) * 1000000000ULL) / dime_instance->bandwidth_bps;  // Transmission delay
			delay_ns += 2*dime_instance->latency_ns;                                         // Two way latency
			while ((sched_clock() - start_time) < delay_ns) {
			    // Wait for delay
			}
			ml_clear_dirty(mm, node->address);
			ml_clear_accessed(mm, node->address);
		} 
		
		if(!accessed) {
			iternode_free = iternode;
			iternode = iternode->prev;
			list_del_rcu(iternode_free);
			pl->size--;
			target--;
			ml_protect_page(mm, node->address);
			list_add_tail_rcu(iternode_free, &local_free_list);
			moved_free++;
			//DA_LRU_STAT();
		}
	}
	// reposition list head to this point, so that next time we wont scan again previously scanned nodes
	if(iternode != &pl->head) {
		list_del_rcu(&pl->head);
		list_add_tail_rcu(&pl->head, iternode);
	}
	write_unlock(&pl->lock);

	write_lock(&free->lock);
	while(!list_empty(&local_free_list)) {
		struct list_head *h = local_free_list.next;
		list_del_rcu(h);
		list_add_tail_rcu(h, &free->head);
		free->size++;
	}
	write_unlock(&free->lock);

	DA_LRU_STAT();
	DA_ERROR("moved_free: %d \tremaining: %d", moved_free, target);
	return moved_free;
}


// move inactive page from active list
int balance_local_page_lists(void) {
	int i = 0;
	struct prp_lru_struct *prp_lru = NULL;
	struct dime_instance_struct *dime_instance;

	for(i=0 ; i<dime.dime_instances_size ; ++i) {
		int free_target = 0;
		dime_instance = &(dime.dime_instances[i]);
		prp_lru = to_prp_lru_struct(dime_instance->prp);
		if(prp_lru->lpl_count < dime_instance->local_npages)
			//|| prp_lru->free.size >= (MIN_FREE_PAGES * dime_instance->local_npages)/100)
			// no need to evict pages for this dime instance
			continue;

		free_target = (MIN_FREE_PAGES * dime_instance->local_npages)/100 - prp_lru->free.size;
		if(free_target > 0) {
			/*int target_pi = prp_lru->inactive_pc.size - (18 * dime_instance->local_npages)/100;
			int target_pa = prp_lru->active_pc.size   - (27 * dime_instance->local_npages)/100;
			int target_ai = prp_lru->inactive_an.size - (18 * dime_instance->local_npages)/100;
			int target_aa = prp_lru->active_an.size   - (27 * dime_instance->local_npages)/100;
			int total_diff;

			target_pi = target_pi < 0 ? 0 : target_pi;
			target_pa = target_pa < 0 ? 0 : target_pa;
			target_ai = target_ai < 0 ? 0 : target_ai;
			target_aa = target_aa < 0 ? 0 : target_aa;

			total_diff = target_pi + target_pa + target_ai + target_aa;

			target_pi = (free_target*target_pi)/total_diff;
			target_pa = (free_target*target_pa)/total_diff;
			target_ai = (free_target*target_ai)/total_diff;
			target_aa = (free_target*target_aa)/total_diff;
			
			try_to_free_pages(dime_instance, &prp_lru->inactive_pc, target_pi, &prp_lru->free);
			try_to_free_pages(dime_instance, &prp_lru->inactive_an, target_ai, &prp_lru->free);
			try_to_free_pages(dime_instance, &prp_lru->active_pc, target_pa, &prp_lru->free);
			try_to_free_pages(dime_instance, &prp_lru->active_pc, target_aa, &prp_lru->free);
			*/
			free_target = (MIN_FREE_PAGES * dime_instance->local_npages)/100 - prp_lru->free.size;
			free_target > 0 ? try_to_free_pages(dime_instance, &prp_lru->inactive_pc, free_target, &prp_lru->free) : 0;
			
			free_target = (MIN_FREE_PAGES * dime_instance->local_npages)/100 - prp_lru->free.size;
			free_target > 0 ? try_to_free_pages(dime_instance, &prp_lru->inactive_an, free_target, &prp_lru->free) : 0;
			
			free_target = (MIN_FREE_PAGES * dime_instance->local_npages)/100 - prp_lru->free.size;
			free_target > 0 ? try_to_free_pages(dime_instance, &prp_lru->active_pc, free_target, &prp_lru->free) : 0;
			
			free_target = (MIN_FREE_PAGES * dime_instance->local_npages)/100 - prp_lru->free.size;
			free_target > 0 ? try_to_free_pages(dime_instance, &prp_lru->active_pc, free_target, &prp_lru->free) : 0;
		}

		/*if(prp_lru->inactive_pc.size < (prp_lru->active_pc.size+prp_lru->inactive_pc.size)*40/100)*/ {
			// need to move passive pages from active pagecache list to inactive list
			int target = prp_lru->active_pc.size;//(prp_lru->active_pc.size+prp_lru->inactive_pc.size)*40/100 - prp_lru->inactive_pc.size;
			if(target>0)
				balance_lists(&prp_lru->active_pc, &prp_lru->inactive_pc, target, &prp_lru->free);
		}

		/*if(prp_lru->inactive_an.size < (prp_lru->active_an.size+prp_lru->inactive_an.size)*40/100)*/ {
			// need to move passive pages from active pagecache list to inactive list
			int target = prp_lru->active_an.size;//(prp_lru->active_an.size+prp_lru->inactive_an.size)*40/100 - prp_lru->inactive_an.size;
			if(target>0)
				balance_lists(&prp_lru->active_an, &prp_lru->inactive_an, target, &prp_lru->free);
		}
	}

	return 0;
}


static struct task_struct *dime_kswapd;
static int dime_kswapd_fn(void *unused) {
	allow_signal(SIGKILL);
	while (!kthread_should_stop()) {
		DA_LRU_STAT();
		msleep(10);
		if (signal_pending(dime_kswapd))
			break;
		balance_local_page_lists();
	}
	DA_INFO("dime_kswapd thread STOPPING");
	do_exit(0);
	return 0;
}



void __lpl_CleanList (struct list_head *prp) {
	DA_ENTRY();

	while (!list_empty(prp)) {
		struct lpl_node_struct *node = list_first_entry(prp, struct lpl_node_struct, list_node);
		list_del_rcu(&node->list_node);
		kfree(node);
	}

	DA_EXIT();
}

void lpl_CleanList (struct dime_instance_struct *dime_instance) {
	struct prp_lru_struct *prp_lru = to_prp_lru_struct(dime_instance->prp);

	__lpl_CleanList(&prp_lru->free.head);
	__lpl_CleanList(&prp_lru->active_an.head);
	__lpl_CleanList(&prp_lru->inactive_an.head);
	__lpl_CleanList(&prp_lru->active_pc.head);
	__lpl_CleanList(&prp_lru->inactive_pc.head);
}


int init_module(void) {
	int ret = 0;
	int i;
	DA_ENTRY();

	DA_INFO("creating dime_kswapd thread");
	dime_kswapd = kthread_create(dime_kswapd_fn, NULL, "dime_kswapd");
	if(dime_kswapd) {
		DA_INFO("dime_kswapd thread created successfully");
	} else {
		DA_ERROR("dime_kswapd thread creation failed");
		return -1;
	}

	for(i=0 ; i<dime.dime_instances_size ; ++i) {
		struct prp_lru_struct *prp_lru = (struct prp_lru_struct*) kmalloc(sizeof(struct prp_lru_struct), GFP_KERNEL);
		if(!prp_lru) {
			DA_ERROR("unable to allocate memory");
			return -1; // TODO:: Error codes
		}

		rwlock_init(&(prp_lru->lock));
		rwlock_init(&(prp_lru->free.lock));
		rwlock_init(&(prp_lru->active_an.lock));
		rwlock_init(&(prp_lru->inactive_an.lock));
		rwlock_init(&(prp_lru->active_pc.lock));
		rwlock_init(&(prp_lru->inactive_pc.lock));
		write_lock(&prp_lru->lock);
		INIT_LIST_HEAD(&prp_lru->free.head);
		INIT_LIST_HEAD(&prp_lru->active_an.head);
		INIT_LIST_HEAD(&prp_lru->active_pc.head);
		INIT_LIST_HEAD(&prp_lru->inactive_an.head);
		INIT_LIST_HEAD(&prp_lru->inactive_pc.head);
		prp_lru->lpl_count = 0;
		prp_lru->free.size = 0;
		prp_lru->active_pc.size = 0;
		prp_lru->active_an.size = 0;
		prp_lru->inactive_pc.size = 0;
		prp_lru->inactive_an.size = 0;

		prp_lru->prp.add_page = lpl_AddPage;
		prp_lru->prp.clean = lpl_CleanList;


		// Set policy pointer at the end of initialization
		dime.dime_instances[i].prp = &(prp_lru->prp);
		write_unlock(&prp_lru->lock);
	}

	ret = register_page_replacement_policy(NULL);
	DA_INFO("waking up dime_kswapd thread");
	wake_up_process(dime_kswapd);

	DA_EXIT();
	return ret;    // Non-zero return means that the module couldn't be loaded.
}
void cleanup_module(void) {
	int i;
	DA_ENTRY();

	if(dime_kswapd) {
		kthread_stop(dime_kswapd);
		DA_INFO("dime_kswapd thread stopped");
	}

	for(i=0 ; i<dime.dime_instances_size ; ++i) {
		lpl_CleanList(&dime.dime_instances[i]);
		dime.dime_instances[i].prp = NULL;
	}

	deregister_page_replacement_policy(NULL);


	DA_INFO("cleaning up module complete");
	DA_EXIT();
}
