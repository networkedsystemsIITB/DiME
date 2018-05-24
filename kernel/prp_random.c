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
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/random.h>
#include <linux/spinlock_types.h>

#include "da_mem_lib.h"

#include "prp_random.h"
#include "../common/da_debug.h"
#include "common.h"



/*****
 *
 *  Module params
 *
 */
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Abhishek Ghogare, Dhantu");
MODULE_DESCRIPTION("Dime Random page replacement policy");


static struct prp_random_struct *to_prp_random_struct(struct page_replacement_policy_struct *prp) {
	return container_of(prp, struct prp_random_struct, prp);
}

int add_page (struct dime_instance_struct *dime_instance, struct pid * c_pid, ulong c_addr) {
	struct task_struct		* c_ts				= pid_task(c_pid, PIDTYPE_PID);
	struct mm_struct		* c_mm				= (c_ts == NULL ? NULL : c_ts->mm);
	pte_t					* c_ptep			= (c_mm == NULL ? NULL : ml_get_ptep(c_mm, c_addr));
	struct page				* c_page			= (c_ptep == NULL ? NULL : pte_page(*c_ptep));

	struct lpl_node_struct	* node_to_replace	= NULL;
	struct prp_random_struct* prp_random		= to_prp_random_struct(dime_instance->prp);
	int 					ret_execute_delay 	= 0;

	if (dime_instance->local_npages == 0) {
		// no need to add this address
		// we can treat this case as infinite local pages, and no need to inject delay on any of the page
		ret_execute_delay = 1;
		return ret_execute_delay;
	} else {
		unsigned long rnd = 0;
		do {
			get_random_bytes(&rnd, sizeof(unsigned long));
			rnd %= prp_random->lpl_size;
		} while(!spin_trylock(&prp_random->lpl[rnd].lock));

		// protect random last address, so that it will be faulted in future
		node_to_replace = &prp_random->lpl[rnd];
		if(node_to_replace->address) {
			if(node_to_replace->pid_s->numbers[0].nr <= 0)
				 DA_ERROR("invalid pid: %d : address:%lu", node_to_replace->pid_s->numbers[0].nr, node_to_replace->address);

			ml_protect_page(ml_get_mm_struct(node_to_replace->pid_s->numbers[0].nr), node_to_replace->address);
		}

		node_to_replace->address = c_addr;
		node_to_replace->pid_s = c_pid;

		spin_unlock(&prp_random->lpl[rnd].lock);

		ml_set_accessed_pte(c_mm, c_addr, c_ptep);
		ml_set_inlist_pte(c_mm, c_addr, c_ptep);

		// Since local pages are occupied, delay should be injected
		ret_execute_delay = 1;

		if(c_page) {
			if( ((unsigned long)(c_page->mapping) & (unsigned long)0x01) != 0 ) {
				atomic_long_inc(&dime_instance->an_pagefaults);
				//DA_DEBUG("this is anonymous page: %lu, pid: %d", address, node->pid);
			} else {
				atomic_long_inc(&dime_instance->pc_pagefaults);
				//DA_DEBUG("this is pagecache page: %lu, pid: %d", address, node->pid);
			}
		} else {
			DA_ERROR("invalid page mapping %lx : %p : %p", c_addr, c_page, c_page->mapping);
		}
	}

	return ret_execute_delay;
}

void clean_list (struct dime_instance_struct *dime_instance) {
	struct prp_random_struct *prp_random = NULL;
	DA_ENTRY();
	prp_random = to_prp_random_struct(dime_instance->prp);
	dime_instance->prp->add_page = NULL;
	kfree(prp_random->lpl);
	dime_instance->prp = NULL;
	DA_EXIT();
}


int init_module (void) {
	int ret = 0;
	int i, j;
	DA_ENTRY();

	for(i=0 ; i<dime.dime_instances_size ; ++i) {
		struct prp_random_struct *prp_random = (struct prp_random_struct *) kmalloc(sizeof(struct prp_random_struct), GFP_KERNEL);
		if(!prp_random) {
			DA_ERROR("unable to allocate memory");
			return -1; // TODO:: Error codes
		}

		*prp_random = (struct prp_random_struct) {
			.prp = {
				.add_page 	= add_page,
				.clean 		= clean_list,
			},
			.lpl			= NULL,
			.lpl_size		= dime.dime_instances[i].local_npages,
			.lock			= __RW_LOCK_UNLOCKED(prp_random->lock),
		};

		prp_random->lpl = (struct lpl_node_struct *) kmalloc(sizeof(struct lpl_node_struct) * dime.dime_instances[i].local_npages, GFP_KERNEL);
		if(!prp_random->lpl) {
			DA_ERROR("unable to allocate memory");
			return -1; // TODO:: Error codes
		}
		for(j=0 ; j<prp_random->lpl_size ; ++j) {
			prp_random->lpl[j] = (struct lpl_node_struct) {0};
			prp_random->lpl[j].lock = __SPIN_LOCK_UNLOCKED(prp_random->lpl[j].lock);
		}
//		rwlock_init(&(prp_random->lock));
		dime.dime_instances[i].prp = &(prp_random->prp);
	}

	ret = register_page_replacement_policy(NULL);

	DA_INFO("initializing random prp module complete");
	DA_EXIT();
	return ret;    // Non-zero return means that the module couldn't be loaded.
}
void cleanup_module(void) {
	int i;
	DA_ENTRY();

	for(i=0 ; i<dime.dime_instances_size ; ++i) {
		clean_list(&dime.dime_instances[i]);
	}

	deregister_page_replacement_policy(NULL);

	DA_INFO("cleaning up random prp module complete");
	DA_EXIT();
}
