#ifndef __DA_LOCAL_PAGE_LIST_H__
#define __DA_LOCAL_PAGE_LIST_H__

#include "common.h"

struct lpl_node_struct {
	struct list_head list_node;
	
	ulong address;
	pid_t pid;
};

struct prp_fifo_struct {
	struct page_replacement_policy_struct prp;

	struct list_head lpl_head;
	ulong lpl_count;
};

int		test_list		(ulong address);
int		lpl_AddPage		(struct dime_instance_struct *dime_instance, struct mm_struct * mm, ulong address);		// Returns 1 if delay should be injected, else 0
void	lpl_CleanList	(struct dime_instance_struct *dime_instance);

#endif//__DA_LOCAL_PAGE_LIST_H__
