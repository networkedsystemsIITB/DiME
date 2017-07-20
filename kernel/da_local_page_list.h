#ifndef __DA_LOCAL_PAGE_LIST_H__
#define __DA_LOCAL_PAGE_LIST_H__

struct lpl_node_struct {
	struct list_head list_node;
	
	ulong address;
	pid_t pid;
};

int 	test_list		(ulong address);
int		lpl_AddPage		(struct mm_struct * mm, ulong address);		// Returns 1 if delay should be injected, else 0
void 	lpl_CleanList	(void);

#endif//__DA_LOCAL_PAGE_LIST_H__
