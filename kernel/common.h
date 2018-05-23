#ifndef __COMMON_H__
#define __COMMON_H__

#include <linux/mm.h>
#include "../common/da_debug.h"

//#define write_lock(lock) while(0){}
//#define write_unlock(lock) while(0){}


#define MAX_DIME_INSTANCES 50
/*
struct pagefault_request {
	unsigned long 					address;
	struct pid 						* pid;
	struct task_struct				* ts;
	struct mm_struct				* mm;
	pte_t							* ptep;
	struct page						* page;
	struct dime_instance_struct 	* dime_instance;

	unsigned long long 				timestamp;
	bool 							inject_delay;
};
*/
struct dime_instance_struct;

struct page_replacement_policy_struct {
	int		(*add_page)		(struct dime_instance_struct *dime_instance, struct pid * pid_s, ulong address);
	void	(*clean)		(struct dime_instance_struct *dime_instance);
};

struct dime_instance_struct {
	int				instance_id;
	int				pid[1000];
	int				pid_count;
	ulong			latency_ns;
	ulong			bandwidth_bps;
	ulong			local_npages;
	atomic_long_t	pc_pagefaults;
	atomic_long_t	an_pagefaults;
	atomic_long_t	cpu_cycles_used;
	atomic_long_t	duplecate_pfs;
	rwlock_t 		lock;

	struct page_replacement_policy_struct *prp;
};

struct dime_struct {
	struct dime_instance_struct dime_instances[MAX_DIME_INSTANCES];
	int dime_instances_size;
};

struct lpl_node_struct {
	struct list_head list_node;
	ulong address;
	struct pid *pid_s;
};

// local page list struct
struct lpl {
	struct list_head 	head;
	atomic_long_t		size;
	rwlock_t 			lock;
};


extern struct dime_struct dime;


void inject_delay(struct dime_instance_struct *dime_instance);
int register_page_replacement_policy(struct page_replacement_policy_struct *prp);
int deregister_page_replacement_policy(struct page_replacement_policy_struct *prp);

#endif //__COMMON_H__
