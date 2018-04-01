#ifndef __DA_LOCAL_PAGE_LIST_H__
#define __DA_LOCAL_PAGE_LIST_H__

#include "common.h"

struct prp_lru_struct {
	struct page_replacement_policy_struct prp;

	struct list_head lpl_pagecache_head;
	struct list_head lpl_anon_head;
	ulong lpl_count;
	rwlock_t lock;
};

int		test_list		(ulong address);
int		lpl_AddPage		(struct dime_instance_struct *dime_instance, struct mm_struct * mm, ulong address);		// Returns 1 if delay should be injected, else 0
void	lpl_CleanList	(struct dime_instance_struct *dime_instance);
void	__lpl_CleanList	(struct prp_lru_struct *prp_lru);

#endif//__DA_LOCAL_PAGE_LIST_H__