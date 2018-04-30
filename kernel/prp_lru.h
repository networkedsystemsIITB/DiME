#ifndef __DA_LOCAL_PAGE_LIST_H__
#define __DA_LOCAL_PAGE_LIST_H__

#include "common.h"

struct stats_struct {
	// TODO:: use atomic_t type for atomic increment, Ref:https://www.kernel.org/doc/html/v4.12/core-api/atomic_ops.html
	// # of pagefaults handled in different cases
	ulong	free_evict;
	ulong	active_pc_evict;
	ulong	active_an_evict;
	ulong	inactive_pc_evict;
	ulong	inactive_an_evict;
	ulong	force_active_pc_evict;
	ulong	force_active_an_evict;
	ulong	force_inactive_pc_evict;
	ulong	force_inactive_an_evict;

	// # of pages moved acros lists
	ulong	pc_active_to_inactive_moved;	// by kswapd thread
	ulong	an_active_to_inactive_moved;
	ulong	pc_inactive_to_active_moved;
	ulong	an_inactive_to_active_moved;
	ulong	pc_inactive_to_active_pf_moved;	// by pagefault handler
	ulong	an_inactive_to_active_pf_moved;
	ulong	pc_active_to_free_moved;
	ulong	an_active_to_free_moved;
	ulong	pc_inactive_to_free_moved;
	ulong	an_inactive_to_free_moved;

	ulong	pc_pagefaults;
	ulong	an_pagefaults;

	rwlock_t lock;
};

struct prp_lru_struct {
	struct page_replacement_policy_struct prp;

	struct lpl active_pc;
	struct lpl active_an;
	struct lpl inactive_pc;
	struct lpl inactive_an;
	struct lpl free;
	ulong lpl_count;

	struct stats_struct stats;

	rwlock_t lock;
};

int		test_list		(ulong address);
int		add_page		(struct dime_instance_struct *dime_instance, struct pid * pid_s, ulong address);		// Returns 1 if delay should be injected, else 0
void	lpl_CleanList	(struct dime_instance_struct *dime_instance);
void	__lpl_CleanList	(struct list_head *prp);

#endif//__DA_LOCAL_PAGE_LIST_H__
