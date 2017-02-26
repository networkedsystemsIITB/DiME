#ifndef __DA_LOCAL_PAGE_LIST_H__
#define __DA_LOCAL_PAGE_LIST_H__

struct lpl_node_struct {
	struct list_head list_node;
	
	ulong address;
};

void lpl_AddPage(struct mm_struct * mm, ulong address);
void lpl_CleanList(void);

#endif//__DA_LOCAL_PAGE_LIST_H__