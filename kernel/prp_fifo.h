#ifndef __DA_LOCAL_PAGE_LIST_H__
#define __DA_LOCAL_PAGE_LIST_H__

#include "common.h"

struct prp_fifo_struct {
	struct page_replacement_policy_struct prp;

	struct lpl local;
	atomic_long_t lpl_count;
};

int		test_list		(ulong address);
int		add_page		(struct dime_instance_struct *dime_instance, struct pid * pid_s, ulong address);		// Returns 1 if delay should be injected, else 0
void	lpl_CleanList	(struct dime_instance_struct *dime_instance);
void	__lpl_CleanList (struct list_head *head);

#endif//__DA_LOCAL_PAGE_LIST_H__
