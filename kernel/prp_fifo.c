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


static struct prp_fifo_struct *to_prp_fifo_struct(struct page_replacement_policy_struct *prp) {
	struct prp_fifo_struct *ret = NULL;
	//DA_ENTRY();
	ret = container_of(prp, struct prp_fifo_struct, prp);
	//DA_EXIT();
	return ret;
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
		//											 1  2           3
		procfs_buffer_size = sprintf(procfs_buffer, "id pc_pf_count an_pf_count\n");
		for(i=0 ; i<dime.dime_instances_size ; ++i) {
			struct prp_fifo_struct *prp = to_prp_fifo_struct(dime.dime_instances[i].prp);
			procfs_buffer_size += sprintf(procfs_buffer+procfs_buffer_size, 
											//1  2     3
											"%2d %11lu %11lu\n", 
																		dime.dime_instances[i].instance_id,	// 1
																		prp->stats.pc_pagefaults,			// 2
																		prp->stats.an_pagefaults);			// 3
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


int add_page(struct dime_instance_struct *dime_instance, struct pid * c_pid, ulong address) {
	struct task_struct		* c_ts				= pid_task(c_pid, PIDTYPE_PID);
	struct mm_struct		* c_mm				= c_ts->mm;
	struct page				* c_page			= NULL;
	struct lpl_node_struct	* node_to_replace	= NULL;
	struct prp_fifo_struct	* prp_fifo			= to_prp_fifo_struct(dime_instance->prp);
	int ret_execute_delay = 0;

	// if local page size is changed dynamically, delete extra nodes from the list
	while (dime_instance->local_npages < prp_fifo->lpl_count) {
		struct list_head * first_node;
		struct lpl_node_struct * node;

		write_lock(&prp_fifo->lock);
		first_node = prp_fifo->lpl_head.next;
		list_del_rcu(first_node);
		prp_fifo->lpl_count--;
		write_unlock(&prp_fifo->lock);

		node = list_entry(first_node, struct lpl_node_struct, list_node);

		ml_protect_page(ml_get_mm_struct(node->pid_s->numbers[0].nr), node->address);
		kfree(node);
		node = NULL;
		DA_INFO("remove extra local page, current count:%lu", prp_fifo->lpl_count);
	}

	if (dime_instance->local_npages == 0) {
		// no need to add this address
		// we can treat this case as infinite local pages, and no need to inject delay on any of the page
		ret_execute_delay = 1;
		return ret_execute_delay;
	} else if (dime_instance->local_npages > prp_fifo->lpl_count) {
		// Since there is still free space locally for remote pages, delay should not be injected
		ret_execute_delay = 1;
		node_to_replace = (struct lpl_node_struct*) kmalloc(sizeof(struct lpl_node_struct), GFP_KERNEL);

		if(!node_to_replace) {
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
		node_to_replace = list_entry(first_node, struct lpl_node_struct, list_node);
		if(node_to_replace->pid_s->numbers[0].nr <= 0)
			 DA_ERROR("invalid pid: %d : address:%lu", node_to_replace->pid_s->numbers[0].nr, node_to_replace->address);
		// ml_reset_inlist(mm, addr);
		ml_protect_page(ml_get_mm_struct(node_to_replace->pid_s->numbers[0].nr), node_to_replace->address);
		// Since local pages are occupied, delay should be injected
		ret_execute_delay = 1;
	}

	*node_to_replace = (struct lpl_node_struct){0};
	node_to_replace->address = address;
	node_to_replace->pid_s = c_pid;

	write_lock(&prp_fifo->lock);
	list_add_tail_rcu(&(node_to_replace->list_node), &prp_fifo->lpl_head);
	write_unlock(&prp_fifo->lock);


	c_page = ml_get_page_sruct(c_mm, address);
	
	if( ((unsigned long)(c_page->mapping) & (unsigned long)0x01) != 0 ) {
		write_lock(&prp_fifo->stats.lock);
		prp_fifo->stats.an_pagefaults++;
		write_unlock(&prp_fifo->stats.lock);
		//DA_DEBUG("this is anonymous page: %lu, pid: %d", address, node->pid);
	} else {
		write_lock(&prp_fifo->stats.lock);
		prp_fifo->stats.pc_pagefaults++;
		write_unlock(&prp_fifo->stats.lock);
		//DA_DEBUG("this is pagecache page: %lu, pid: %d", address, node->pid);
	}

	return ret_execute_delay;
}

void lpl_Init(struct dime_instance_struct *dime_instance) {
	struct prp_fifo_struct *prp_fifo = to_prp_fifo_struct(dime_instance->prp);
	*prp_fifo = (struct prp_fifo_struct) {
		.prp = {
			.add_page 	= add_page,
			.clean 		= lpl_CleanList,
		},
		.lpl_count = 0,
	};

	prp_fifo->lpl_head = (struct list_head) { &(prp_fifo->lpl_head), &(prp_fifo->lpl_head) };
	rwlock_init(&(prp_fifo->lock));
	rwlock_init(&(prp_fifo->stats.lock));
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
	int i;
    DA_ENTRY();

    for(i=0 ; i<dime.dime_instances_size ; ++i) {
    	struct prp_fifo_struct *prp_fifo = (struct prp_fifo_struct*) kmalloc(sizeof(struct prp_fifo_struct), GFP_KERNEL);
		if(!prp_fifo) {
			DA_ERROR("unable to allocate memory");
			return -1; // TODO:: Error codes
		}

		dime.dime_instances[i].prp = &(prp_fifo->prp);
	    lpl_Init(&dime.dime_instances[i]);
	}

    ret = register_page_replacement_policy(NULL);

	if(init_dime_prp_config_procfs()<0) {
		ret = -1;
	}

    DA_EXIT();
    return ret;    // Non-zero return means that the module couldn't be loaded.
}
void cleanup_module(void) {
	int i;
    DA_ENTRY();

	cleanup_dime_prp_config_procfs();

    for(i=0 ; i<dime.dime_instances_size ; ++i) {
    	lpl_CleanList(&dime.dime_instances[i]);
		dime.dime_instances[i].prp = NULL;
	}

    deregister_page_replacement_policy(NULL);

    DA_INFO("cleaning up module complete");
    DA_EXIT();
}
