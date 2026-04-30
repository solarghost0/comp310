#ifndef PAGE_H
#define PAGE_H

#include <stdint.h>

struct ppage {
	struct ppage *next;
	struct ppage *prev;
	void *physical_addr;
};

struct page_directory_entry {
	uint32_t present       : 1;
	uint32_t rw            : 1;
	uint32_t user          : 1;
	uint32_t writethru     : 1;
	uint32_t cachedisabled : 1;
	uint32_t accessed      : 1;
	uint32_t pagesize      : 1;
	uint32_t ignored       : 2;
	uint32_t os_specific   : 3;
	uint32_t frame         : 20;
};

struct page {
	uint32_t present    : 1;
	uint32_t rw         : 1;
	uint32_t user       : 1;
	uint32_t accessed   : 1;
	uint32_t dirty      : 1;
	uint32_t unused     : 7;
	uint32_t frame      : 20;
};

void init_pfa_list(void);
struct ppage *allocate_physical_pages(unsigned int npages);
void free_physical_pages(struct ppage *ppage_list);
void *map_pages(void *vaddr, struct ppage *pglist, struct page_directory_entry *pd);
void loadPageDirectory(struct page_directory_entry *pd);

#endif
