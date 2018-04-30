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
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
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



#define PROCFS_MAX_SIZE		102400
#define PROCFS_NAME			"dime_prp_config"
static char procfs_buffer[PROCFS_MAX_SIZE];     // The buffer used to store character for this module
static unsigned long procfs_buffer_size = 0;    // The size of the buffer

static ssize_t procfile_read(struct file*, char*, size_t, loff_t*);
static ssize_t procfile_write(struct file *, const char *, size_t, loff_t *);

struct proc_dir_entry *dime_config_entry;

static struct file_operations cmd_file_ops = {  
	.owner = THIS_MODULE,
	.read = procfile_read,
	.write = procfile_write,
};

int init_dime_prp_config_procfs(void) {
	dime_config_entry = proc_create(PROCFS_NAME, S_IFREG | S_IRUGO, NULL, &cmd_file_ops);

	if (dime_config_entry == NULL) {
		remove_proc_entry(PROCFS_NAME, NULL);

		DA_ALERT("could not initialize /proc/%s\n", PROCFS_NAME);
		return -ENOMEM;
	}

	/*
	 * KUIDT_INIT is a macro defined in the file 'linux/uidgid.h'. KGIDT_INIT also appears here.
	 */
	proc_set_user(dime_config_entry, KUIDT_INIT(0), KGIDT_INIT(0));
	proc_set_size(dime_config_entry, 37);

	DA_INFO("proc entry \"/proc/%s\" created\n", PROCFS_NAME);
	return 0;
}

void cleanup_dime_prp_config_procfs(void) {
	remove_proc_entry(PROCFS_NAME, NULL);
	DA_INFO("proc entry \"/proc/%s\" removed\n", PROCFS_NAME);
}

static ssize_t procfile_read(struct file *file, char *buffer, size_t length, loff_t *offset) {
	int ret;
	int seg_size;
	
	if(*offset == 0) {
		// offset is 0, so first call to read the file.
		// Initialize buffer with config parameters currently set
		int i;
		//											 1  2           3           4         5        6         7        8         9          10        11         12        13         14         15          16         17          18        19         20        21         22        23        24        25	     26           27
		procfs_buffer_size = sprintf(procfs_buffer, "id pc_pf_count an_pf_count free_size apc_size inpc_size aan_size inan_size free_evict apc_evict inpc_evict aan_evict inan_evict fapc_evict finpc_evict faan_evict finan_evict apc->free inpc->free aan->free inan->free apc->inpc inpc->apc aan->inan inan->aan inpc->apc_pf inan->aan_pf\n");
		for(i=0 ; i<dime.dime_instances_size ; ++i) {
			struct prp_lru_struct *prp = to_prp_lru_struct(dime.dime_instances[i].prp);
			procfs_buffer_size += sprintf(procfs_buffer+procfs_buffer_size, 
											//1  2     3     4   5   6   7   8   9     10   11    12   13    14    15    16    17    18   19    20   21    22   23   24   25   26    27
											"%2d %11lu %11lu %9d %8d %9d %8d %9d %10lu %9lu %10lu %9lu %10lu %10lu %11lu %10lu %11lu %9lu %10lu %9lu %10lu %9lu %9lu %9lu %9lu %12lu %12lu\n", 
																		dime.dime_instances[i].instance_id,	// 1
																		prp->stats.pc_pagefaults,			// 2
																		prp->stats.an_pagefaults,			// 3
																		prp->free.size,						// 4
																		prp->active_pc.size,				// 5
																		prp->inactive_pc.size,				// 6
																		prp->active_an.size,				// 7
																		prp->inactive_an.size,				// 8
																		prp->stats.free_evict,				// 9
																		prp->stats.active_pc_evict,			// 10
																		prp->stats.inactive_pc_evict,		// 11
																		prp->stats.active_an_evict,			// 12
																		prp->stats.inactive_an_evict,		// 13
																		prp->stats.force_active_pc_evict,	// 14
																		prp->stats.force_inactive_pc_evict,	// 15
																		prp->stats.force_active_an_evict,	// 16
																		prp->stats.force_inactive_an_evict,	// 17
																		prp->stats.pc_active_to_free_moved,	// 18
																		prp->stats.pc_inactive_to_free_moved,	// 19
																		prp->stats.an_active_to_free_moved,		// 20
																		prp->stats.an_inactive_to_free_moved,	// 21
																		prp->stats.pc_active_to_inactive_moved, // 22
																		prp->stats.pc_inactive_to_active_moved,	// 23
																		prp->stats.an_active_to_inactive_moved,	// 24
																		prp->stats.an_inactive_to_active_moved,	// 25
																		prp->stats.pc_inactive_to_active_pf_moved,	// 26
																		prp->stats.an_inactive_to_active_pf_moved);	// 27
		}
	}

	// calculate max size of block that can be read
	seg_size = length < procfs_buffer_size ? length : procfs_buffer_size;
	if (*offset >= procfs_buffer_size) {
		ret  = 0;   // offset value beyond the available data to read, finish reading
	} else {
		memcpy(buffer, procfs_buffer, seg_size);
		*offset += seg_size;    // increment offset value
		ret = seg_size;         // return number of bytes read
	}

	return ret;
}

static ssize_t procfile_write(struct file *file, const char *buffer, size_t length, loff_t *offset) {
	return length;
}



struct lpl_node_struct * evict_first_page(struct lpl *from_list) {
	struct lpl_node_struct * node_to_evict = NULL;

	write_lock(&from_list->lock);
	
	node_to_evict = list_first_entry_or_null(&from_list->head, struct lpl_node_struct, list_node);
	
	if(node_to_evict) {
		struct task_struct		* i_ts		= NULL;
		struct mm_struct		* i_mm		= NULL;
		pte_t					* i_ptep	= NULL;

		// remove node from list
		list_del_rcu(&node_to_evict->list_node);
		from_list->size--;

		write_unlock(&from_list->lock);

		// get pte pointer
		i_ts	= pid_task(node_to_evict->pid_s, PIDTYPE_PID);
		i_mm	= (i_ts == NULL ? NULL : i_ts->mm);
		i_ptep	= (i_mm == NULL ? NULL : ml_get_ptep(i_mm, node_to_evict->address));

		// protect page
		ml_protect_pte(i_mm, node_to_evict->address, i_ptep);
	} else {
		write_unlock(&from_list->lock);
	}

	return node_to_evict;
}


struct lpl_node_struct * evict_single_page(struct lpl *from_list, struct lpl *active_list, int * from_to_active_moved) {
	struct lpl_node_struct * node_to_evict = NULL;
	struct lpl tmp_list =	{
								.head = LIST_HEAD_INIT(tmp_list.head),
								.size = 0
							};
	struct list_head *iternode;

	write_lock(&from_list->lock);
	for(iternode = from_list->head.next ; iternode != &from_list->head ; iternode = iternode->next) {
		struct lpl_node_struct	* i_node	= NULL;
		struct task_struct		* i_ts		= NULL;
		struct mm_struct		* i_mm		= NULL;
		pte_t					* i_ptep	= NULL;
		int accessed, dirty;


		i_node = list_entry(iternode, struct lpl_node_struct, list_node);

		//if(c_pid == i_pid && c_addr == node->address)
			// if faulting page is same as the one we want to evict, continue,
			// since it is going to get protected and will recursively called
			// LESS likely happen
		//	continue;

		// remove iter node from list
		iternode = iternode->prev;
		list_del_rcu(&(i_node->list_node));
		from_list->size--;

		i_ts	= pid_task(i_node->pid_s, PIDTYPE_PID);
		i_mm	= (i_ts == NULL ? NULL : i_ts->mm);
		i_ptep	= (i_mm == NULL ? NULL : ml_get_ptep(i_mm, i_node->address));
		if(!i_ptep) {
			node_to_evict = i_node;
			break;
		}

		accessed = pte_young(*i_ptep);
		dirty = pte_dirty(*i_ptep);
		// TODO:: what to do with dirty page?

		if(accessed) {
			list_add_tail_rcu(&(i_node->list_node), &tmp_list.head);
			tmp_list.size++;
		} else {
			node_to_evict = i_node;
			ml_protect_pte(i_mm, i_node->address, i_ptep);
			break;
		}
	}

	// reposition list head to this point, so that next time we wont scan again previously scanned nodes
	//	NO NEED TO REPOSITION HEAD, since it always will be pointing to latest
	//if(node_to_evict && iternode != &from_list->head) {
	//	list_del_rcu(&from_list->head);
	//	list_add_rcu(&from_list->head, iternode);
	//}
	write_unlock(&from_list->lock);


	// append all temp list nodes to active list
	write_lock(&active_list->lock);
	while(!list_empty(&tmp_list.head)) {
		struct list_head *h = tmp_list.head.next;
		list_del_rcu(h);
		list_add_tail_rcu(h, &active_list->head);
		active_list->size++;
		(*from_to_active_moved)++;
	}
	write_unlock(&active_list->lock);

	return node_to_evict;
}


int add_page(struct dime_instance_struct *dime_instance, struct pid * c_pid, ulong c_addr) {
	struct task_struct		* c_ts				= pid_task(c_pid, PIDTYPE_PID);
	struct mm_struct		* c_mm				= (c_ts == NULL ? NULL : c_ts->mm);
	pte_t					* c_ptep			= (c_mm == NULL ? NULL : ml_get_ptep(c_mm, c_addr));
	struct page				* c_page			= (c_ptep == NULL ? NULL : pte_page(*c_ptep));

	struct lpl_node_struct	* node_to_evict		= NULL;
	struct prp_lru_struct	* prp_lru			= to_prp_lru_struct(dime_instance->prp);
	struct stats_struct 	local_stats			= {0};
	int 					ret_execute_delay	= 1;


	//c_addr = c_addr - c_addr%PAGE_SIZE; // start address of a page that this address belongs

	// pages in local memory more than the configured dime instance quota, evict extra pages
	// possible when instance configuration is changed dynamically using proc file /proc/dime_config
	if(dime_instance->local_npages < prp_lru->lpl_count) {
		struct lpl_node_struct * node = NULL;
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

	if (dime_instance->local_npages == 0) {
		// no need to add this address
		// we can treat this case as infinite local pages, and no need to inject delay on any of the page
		ret_execute_delay = 1;
		goto EXIT_ADD_PAGE;
	} else if (dime_instance->local_npages > prp_lru->lpl_count) {
		// Since there is still free space locally for remote pages, delay should not be injected
		ret_execute_delay = 1;
		node_to_evict = (struct lpl_node_struct*) kmalloc(sizeof(struct lpl_node_struct), GFP_KERNEL);

		if(!node_to_evict) {
			DA_ERROR("unable to allocate memory");
			goto EXIT_ADD_PAGE;
		} else {
			write_lock(&prp_lru->lock);
			prp_lru->lpl_count++;
			write_unlock(&prp_lru->lock);

			local_stats.free_evict++;
		}
	} else {
		while(node_to_evict == NULL) {
			int from_to_active_moved = 0;

			// TODO:: stats lock use

			// search in free list
			write_lock(&prp_lru->free.lock);
			node_to_evict = list_first_entry_or_null(&prp_lru->free.head, struct lpl_node_struct, list_node);
			if(node_to_evict) {
				list_del_rcu(&node_to_evict->list_node);
				prp_lru->free.size--;
				*node_to_evict = (struct lpl_node_struct) {0};
				local_stats.free_evict++;
				write_unlock(&prp_lru->free.lock);
				goto FREE_NODE_FOUND;
			} else {
				write_unlock(&prp_lru->free.lock);
			}

			// search from pagecache inactive list
			from_to_active_moved = 0;
			node_to_evict = evict_single_page(&prp_lru->inactive_pc, &prp_lru->active_pc, &from_to_active_moved);
			if(node_to_evict) {
				local_stats.inactive_pc_evict++;
				goto FREE_NODE_FOUND;
			}

			// search from anon inactive list
			from_to_active_moved = 0;
			node_to_evict = evict_single_page(&prp_lru->inactive_an, &prp_lru->active_an, &from_to_active_moved);
			local_stats.an_inactive_to_active_pf_moved += from_to_active_moved;
			if(node_to_evict) {
				local_stats.inactive_an_evict++;
				goto FREE_NODE_FOUND;
			}

			// search from pagecache active list
			from_to_active_moved = 0;
			node_to_evict = evict_single_page(&prp_lru->active_pc, &prp_lru->active_pc, &from_to_active_moved);
			local_stats.pc_inactive_to_active_pf_moved += from_to_active_moved;
			if(node_to_evict) {
				local_stats.active_pc_evict++;
				goto FREE_NODE_FOUND;
			}

			// search from anon active list
			from_to_active_moved = 0;
			node_to_evict = evict_single_page(&prp_lru->active_an, &prp_lru->active_an, &from_to_active_moved);
			local_stats.an_inactive_to_active_pf_moved += from_to_active_moved;
			if(node_to_evict) {
				local_stats.active_an_evict++;
				goto FREE_NODE_FOUND;
			}

			// forcefully select from pagecache inactive list
			node_to_evict = evict_first_page(&prp_lru->inactive_pc);
			if(node_to_evict) {
				local_stats.force_inactive_pc_evict++;
				goto FREE_NODE_FOUND;
			}

			// forcefully select from anon inactive list
			node_to_evict = evict_first_page(&prp_lru->inactive_an);
			if(node_to_evict) {
				local_stats.force_inactive_an_evict++;
				goto FREE_NODE_FOUND;
			}
			
			// forcefully select from pagecache active list
			node_to_evict = evict_first_page(&prp_lru->active_pc);
			if(node_to_evict) {
				local_stats.force_active_pc_evict++;
				goto FREE_NODE_FOUND;
			}

			// forcefully select from anon active list
			node_to_evict = evict_first_page(&prp_lru->active_an);
			if(node_to_evict) {
				local_stats.force_active_an_evict++;
				goto FREE_NODE_FOUND;
			}
			
			DA_WARNING("retrying to evict a page");
		}
	}


FREE_NODE_FOUND:

	node_to_evict->address = c_addr;
	node_to_evict->pid_s = c_pid;
	node_to_evict->pid = c_pid->numbers[0].nr;
	
	// Sometimes bulk pagefault requests come and evicting any page from these requests will again trigger pagefault.
	// This happens recursively if accessed bit is not set for each requested page.
	// So, set accessed bit to all requested pages
	ml_set_accessed_pte(c_mm, c_addr, c_ptep);

	if( ((unsigned long)(c_page->mapping) & (unsigned long)0x01) != 0 ) {
		write_lock(&prp_lru->active_an.lock);
		list_add_tail_rcu(&(node_to_evict->list_node), &prp_lru->active_an.head);
		prp_lru->active_an.size++;
		write_unlock(&prp_lru->active_an.lock);

		local_stats.an_pagefaults++;
	} else {
		write_lock(&prp_lru->active_pc.lock);
		list_add_tail_rcu(&(node_to_evict->list_node), &prp_lru->active_pc.head);
		prp_lru->active_pc.size++;
		write_unlock(&prp_lru->active_pc.lock);

		local_stats.pc_pagefaults++;
	}

//	if(current->pid <= 0)
//		DA_ERROR("invalid pid: %d : address: %lu", current->pid, c_addr);


EXIT_ADD_PAGE:
	write_lock(&prp_lru->stats.lock);
	prp_lru->stats.pc_pagefaults 					+= local_stats.pc_pagefaults;
	prp_lru->stats.an_pagefaults 					+= local_stats.an_pagefaults;
	prp_lru->stats.free_evict 						+= local_stats.free_evict;
	prp_lru->stats.active_pc_evict 					+= local_stats.active_pc_evict;
	prp_lru->stats.inactive_pc_evict 				+= local_stats.inactive_pc_evict;
	prp_lru->stats.active_an_evict 					+= local_stats.active_an_evict;
	prp_lru->stats.inactive_an_evict 				+= local_stats.inactive_an_evict;
	prp_lru->stats.force_active_pc_evict 			+= local_stats.force_active_pc_evict;
	prp_lru->stats.force_inactive_pc_evict 			+= local_stats.force_inactive_pc_evict;
	prp_lru->stats.force_active_an_evict 			+= local_stats.force_active_an_evict;
	prp_lru->stats.force_inactive_an_evict 			+= local_stats.force_inactive_an_evict;
	prp_lru->stats.pc_active_to_free_moved 			+= local_stats.pc_active_to_free_moved;
	prp_lru->stats.pc_inactive_to_free_moved 		+= local_stats.pc_inactive_to_free_moved;
	prp_lru->stats.an_active_to_free_moved 			+= local_stats.an_active_to_free_moved;
	prp_lru->stats.an_inactive_to_free_moved 		+= local_stats.an_inactive_to_free_moved;
	prp_lru->stats.pc_active_to_inactive_moved 		+= local_stats.pc_active_to_inactive_moved;
	prp_lru->stats.pc_inactive_to_active_moved 		+= local_stats.pc_inactive_to_active_moved;
	prp_lru->stats.an_active_to_inactive_moved 		+= local_stats.an_active_to_inactive_moved;
	prp_lru->stats.an_inactive_to_active_moved 		+= local_stats.an_inactive_to_active_moved;
	prp_lru->stats.pc_inactive_to_active_pf_moved 	+= local_stats.pc_inactive_to_active_pf_moved;
	prp_lru->stats.an_inactive_to_active_pf_moved 	+= local_stats.an_inactive_to_active_pf_moved;
	write_unlock(&prp_lru->stats.lock);

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
#define MIN_FREE_PAGES_PERCENT 	10		// percentage of local memory available in free list
#define MIN_FREE_PAGES 			5000	// min # of local memory available in free list

/*
 *	Returns statistics of moved pages around active/inactive lists.
 *	This function always sets values for pagecache statistic variables, calling function should update correct stats in original prp_struct.
 */
struct stats_struct balance_lists(struct lpl *active_list, struct lpl *inactive_list, int target, struct lpl *free) {
	struct stats_struct stats = {0};
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
			*node = (struct lpl_node_struct) {0};
			list_add_tail_rcu(&node->list_node, &local_free_list);
			target--;
			stats.pc_active_to_free_moved++;
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
			stats.pc_active_to_inactive_moved++;
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
			*node = (struct lpl_node_struct) {0};
			list_add_tail_rcu(&node->list_node, &local_free_list);
			target--;
			stats.pc_inactive_to_free_moved++;
			continue;
		}
		accessed = ml_is_accessed(mm, node->address);

		if(accessed) {
			iternode_free = iternode;
			iternode = iternode->prev;
			list_del_rcu(iternode_free);
			inactive_list->size--;
			list_add_tail_rcu(iternode_free, &local_active_list);
			stats.pc_inactive_to_active_moved++;
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

	return stats;
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
			*node = (struct lpl_node_struct) {0};
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
			ml_clear_accessed(mm, node->address);
		} */
		
		if(!accessed) {
			iternode_free = iternode;
			iternode = iternode->prev;
			list_del_rcu(iternode_free);
			pl->size--;
			target--;
			ml_protect_page(mm, node->address);
			*node = (struct lpl_node_struct) {0};
			list_add_tail_rcu(iternode_free, &local_free_list);
			moved_free++;
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

	return moved_free;
}


// move inactive page from active list
int balance_local_page_lists(void) {
	int i = 0;
	struct prp_lru_struct *prp_lru = NULL;
	struct dime_instance_struct *dime_instance;

	for(i=0 ; i<dime.dime_instances_size ; ++i) {
		int free_target = 0;
		int required_free_size = 0;
		dime_instance = &(dime.dime_instances[i]);
		prp_lru = to_prp_lru_struct(dime_instance->prp);
		//if(prp_lru->lpl_count < dime_instance->local_npages)
			//|| prp_lru->free.size >= (MIN_FREE_PAGES_PERCENT * dime_instance->local_npages)/100)
			// no need to evict pages for this dime instance
		//	continue;

		required_free_size = (MIN_FREE_PAGES_PERCENT * dime_instance->local_npages)/100;
		required_free_size = required_free_size < MIN_FREE_PAGES ? required_free_size : MIN_FREE_PAGES;
		free_target = required_free_size - (prp_lru->free.size + dime_instance->local_npages - prp_lru->lpl_count);
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

			free_target = required_free_size - prp_lru->free.size;
			free_target = free_target > 0 ? try_to_free_pages(dime_instance, &prp_lru->inactive_pc, free_target, &prp_lru->free) : 0;
			write_lock(&prp_lru->stats.lock);
			prp_lru->stats.pc_inactive_to_free_moved += free_target;
			write_unlock(&prp_lru->stats.lock);
			
			free_target = required_free_size - prp_lru->free.size;
			free_target = free_target > 0 ? try_to_free_pages(dime_instance, &prp_lru->inactive_an, free_target, &prp_lru->free) : 0;
			write_lock(&prp_lru->stats.lock);
			prp_lru->stats.an_inactive_to_free_moved += free_target;
			write_unlock(&prp_lru->stats.lock);

			
			free_target = required_free_size - prp_lru->free.size;
			free_target = free_target > 0 ? try_to_free_pages(dime_instance, &prp_lru->active_pc, free_target, &prp_lru->free) : 0;
			write_lock(&prp_lru->stats.lock);
			prp_lru->stats.pc_active_to_free_moved += free_target;
			write_unlock(&prp_lru->stats.lock);
			
			free_target = required_free_size - prp_lru->free.size;
			free_target = free_target > 0 ? try_to_free_pages(dime_instance, &prp_lru->active_pc, free_target, &prp_lru->free) : 0;
			write_lock(&prp_lru->lock);
			prp_lru->stats.an_active_to_free_moved += free_target;
			write_unlock(&prp_lru->lock);
		}

		/*if(prp_lru->inactive_pc.size < (prp_lru->active_pc.size+prp_lru->inactive_pc.size)*40/100)*/ {
			// need to move passive pages from active pagecache list to inactive list
			int target = prp_lru->active_pc.size;//(prp_lru->active_pc.size+prp_lru->inactive_pc.size)*40/100 - prp_lru->inactive_pc.size;
			if(target>0) {
				struct stats_struct stats = balance_lists(&prp_lru->active_pc, &prp_lru->inactive_pc, target, &prp_lru->free);
				write_lock(&prp_lru->stats.lock);
				prp_lru->stats.pc_inactive_to_free_moved += stats.pc_inactive_to_free_moved;
				prp_lru->stats.pc_active_to_free_moved += stats.pc_active_to_free_moved;
				prp_lru->stats.pc_inactive_to_active_moved += stats.pc_inactive_to_active_moved;
				prp_lru->stats.pc_active_to_inactive_moved += stats.pc_active_to_inactive_moved;
				write_unlock(&prp_lru->stats.lock);
			}
		}

		/*if(prp_lru->inactive_an.size < (prp_lru->active_an.size+prp_lru->inactive_an.size)*40/100)*/ {
			// need to move passive pages from active pagecache list to inactive list
			int target = prp_lru->active_an.size;//(prp_lru->active_an.size+prp_lru->inactive_an.size)*40/100 - prp_lru->inactive_an.size;
			if(target>0) {
				struct stats_struct stats = balance_lists(&prp_lru->active_an, &prp_lru->inactive_an, target, &prp_lru->free);
				write_lock(&prp_lru->stats.lock);
				prp_lru->stats.an_inactive_to_free_moved += stats.pc_inactive_to_free_moved;
				prp_lru->stats.an_active_to_free_moved += stats.pc_active_to_free_moved;
				prp_lru->stats.an_inactive_to_active_moved += stats.pc_inactive_to_active_moved;
				prp_lru->stats.an_active_to_inactive_moved += stats.pc_active_to_inactive_moved;
				write_unlock(&prp_lru->stats.lock);
			}
		}
	}

	return 0;
}


static struct task_struct *dime_kswapd;
static int dime_kswapd_fn(void *unused) {
	allow_signal(SIGKILL);
	while (!kthread_should_stop()) {
		usleep_range(10,20);
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
		ret = -1;
		goto init_exit;
	}

	for(i=0 ; i<dime.dime_instances_size ; ++i) {
		struct prp_lru_struct *prp_lru = (struct prp_lru_struct*) kmalloc(sizeof(struct prp_lru_struct), GFP_KERNEL);
		if(!prp_lru) {
			DA_ERROR("unable to allocate memory");
			ret = -1; // TODO:: Error codes
			goto init_clean_kswapd;
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
		prp_lru->stats = (struct stats_struct) {0};
		rwlock_init(&(prp_lru->stats.lock));

		prp_lru->prp.add_page = add_page;
		prp_lru->prp.clean = lpl_CleanList;


		// Set policy pointer at the end of initialization
		dime.dime_instances[i].prp = &(prp_lru->prp);
		write_unlock(&prp_lru->lock);
	}

	if(init_dime_prp_config_procfs()<0) {
		ret = -1;
		goto init_clean_instances;
	}

	ret = register_page_replacement_policy(NULL);
	DA_INFO("waking up dime_kswapd thread");
	wake_up_process(dime_kswapd);
	goto init_exit;

init_clean_instances:
	// TODO:: free allocated prp structs & deregister policy

init_clean_kswapd:
	if(dime_kswapd) {
		kthread_stop(dime_kswapd);
		DA_INFO("dime_kswapd thread stopped");
	}

init_exit:
	DA_EXIT();
	return ret;    // Non-zero return means that the module couldn't be loaded.
}
void cleanup_module(void) {
	int i;
	DA_ENTRY();
	
	cleanup_dime_prp_config_procfs();

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
