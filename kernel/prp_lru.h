#ifndef __DA_LOCAL_PAGE_LIST_H__
#define __DA_LOCAL_PAGE_LIST_H__

#include "common.h"

struct stats_struct {
	// TODO:: use atomic_t type for atomic increment, Ref:https://www.kernel.org/doc/html/v4.12/core-api/atomic_ops.html
	// # of pagefaults handled in different cases
	atomic_long_t	free_evict;
	atomic_long_t	active_pc_evict;
	atomic_long_t	active_an_evict;
	atomic_long_t	inactive_pc_evict;
	atomic_long_t	inactive_an_evict;
	atomic_long_t	force_active_pc_evict;
	atomic_long_t	force_active_an_evict;
	atomic_long_t	force_inactive_pc_evict;
	atomic_long_t	force_inactive_an_evict;

	// # of pages moved acros lists
	atomic_long_t	pc_active_to_inactive_moved;	// by kswapd thread
	atomic_long_t	an_active_to_inactive_moved;
	atomic_long_t	pc_inactive_to_active_moved;
	atomic_long_t	an_inactive_to_active_moved;
	atomic_long_t	pc_inactive_to_active_pf_moved;	// by pagefault handler
	atomic_long_t	an_inactive_to_active_pf_moved;
	atomic_long_t	pc_active_to_free_moved;
	atomic_long_t	an_active_to_free_moved;
	atomic_long_t	pc_inactive_to_free_moved;
	atomic_long_t	an_inactive_to_free_moved;
};

struct prp_lru_struct {
	struct page_replacement_policy_struct prp;

	struct lpl active_pc;
	struct lpl active_an;
	struct lpl inactive_pc;
	struct lpl inactive_an;
	struct lpl free;
	atomic_long_t lpl_count;

	struct stats_struct stats;

	rwlock_t lock;
};


int		add_page		(struct dime_instance_struct *dime_instance, struct pid * pid_s, ulong address);		// Returns 1 if delay should be injected, else 0
void	lpl_CleanList	(struct dime_instance_struct *dime_instance);
void	__lpl_CleanList	(struct list_head *prp);

#endif//__DA_LOCAL_PAGE_LIST_H__
