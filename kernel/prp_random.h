#ifndef __DA_LOCAL_PAGE_LIST_H__
#define __DA_LOCAL_PAGE_LIST_H__

#include "common.h"

struct prp_random_struct {
	struct page_replacement_policy_struct prp;

	struct lpl_node_struct **lpl;
	unsigned long lpl_size;			// Max size of array allocated
	
	rwlock_t lock;
};

int		add_page	(struct dime_instance_struct *dime_instance, struct pid * pid_s, ulong c_addr);		// Returns 1 if delay should be injected, else 0
void	clean_list	(struct dime_instance_struct *dime_instance);

#endif//__DA_LOCAL_PAGE_LIST_H__
