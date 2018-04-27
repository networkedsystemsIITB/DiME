#ifndef __COMMON_H__
#define __COMMON_H__

#include <linux/mm.h>
#include "../common/da_debug.h"

//#define write_lock(lock) while(0){}
//#define write_unlock(lock) while(0){}


#define MAX_DIME_INSTANCES 50

struct dime_instance_struct;

struct page_replacement_policy_struct {
	int		(*add_page)		(struct dime_instance_struct *dime_instance, struct pid * pid_s, ulong address);
	void	(*clean)		(struct dime_instance_struct *dime_instance);
};

struct dime_instance_struct {
	int		instance_id;
	int		pid[1000];
	int		pid_count;
	ulong	latency_ns;
	ulong	bandwidth_bps;
	ulong	local_npages;
	ulong	page_fault_count;
	rwlock_t lock;

	struct page_replacement_policy_struct *prp;
};

struct dime_struct {
	struct dime_instance_struct dime_instances[MAX_DIME_INSTANCES];
	int dime_instances_size;
};

struct lpl_node_struct {
	struct list_head list_node;
	
	ulong address;
	pid_t pid;

	struct pid *pid_s;
	pte_t *ptep;
	struct page *page;
	struct task_struct *ts;
};

// local page list struct
struct lpl {
	struct list_head 	head;
	int 				size;
	rwlock_t 			lock;
};


extern struct dime_struct dime;


int register_page_replacement_policy(struct page_replacement_policy_struct *prp);
int deregister_page_replacement_policy(struct page_replacement_policy_struct *prp);

#endif //__COMMON_H__
