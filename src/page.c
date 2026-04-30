#include "page.h"
#include <stdint.h>
#include <stddef.h> 

extern char _end;
// Change these numbers if you dare, bad things will happen. Buffer might be changable but really no point
#define PAGE_SIZE (4 * 1024)
#define NUM_PAGES 65536
#define MEMORY_START 0x00200000

// Global page directory and page table - must be 4096-byte aligned and global
struct page_directory_entry pd[1024] __attribute__((aligned(4096)));
struct page pt[1024] __attribute__((aligned(4096)));

struct ppage physical_page_array[NUM_PAGES];

static struct ppage *free_physical_pages_head = NULL;
void init_pfa_list(void) {
	for (int i = 0; i < NUM_PAGES; i++) {
		physical_page_array[i].physical_addr = (void *)(MEMORY_START + (i * PAGE_SIZE));
		if (i < NUM_PAGES - 1) {
		    physical_page_array[i].next = &physical_page_array[i + 1];
		} else {
		    physical_page_array[i].next = NULL;
		}

		if (i > 0) {
		    physical_page_array[i].prev = &physical_page_array[i - 1];
		} else {
		    physical_page_array[i].prev = NULL;
		}
	}
	free_physical_pages_head = &physical_page_array[0];
}

struct ppage *allocate_physical_pages(unsigned int npages) {
    if (npages == 0 || free_physical_pages_head == NULL) {
        return NULL;
    }
    
    struct ppage *current = free_physical_pages_head;
    unsigned int count = 0;
    
    while (current != NULL && count < npages) {
        current = current->next;
        count++;
    }
    
    if (count < npages) {
        return NULL;
    }
    
    struct ppage *allocated_list = free_physical_pages_head;
    struct ppage *last_allocated = free_physical_pages_head;
    
    for (unsigned int i = 1; i < npages; i++) {
        last_allocated = last_allocated->next;
    }

    free_physical_pages_head = last_allocated->next;

    if (free_physical_pages_head != NULL) {
        free_physical_pages_head->prev = NULL;
    }
    
    last_allocated->next = NULL;
    
    return allocated_list;
}


void free_physical_pages(struct ppage *ppage_list) {
    if (ppage_list == NULL) {
        return;
    }

    struct ppage *last = ppage_list;
    while (last->next != NULL) {
        last = last->next;
    }
    
    last->next = free_physical_pages_head;
    
    if (free_physical_pages_head != NULL) {
        free_physical_pages_head->prev = last;
    }
    
    ppage_list->prev = NULL;
    free_physical_pages_head = ppage_list;
}

// Maps physical pages to virtual address using 2-level page tables
void *map_pages(void *vaddr, struct ppage *pglist, struct page_directory_entry *pd) {
    if (vaddr == NULL || pglist == NULL || pd == NULL) {
        return NULL;
    }
    
    uint32_t addr = (uint32_t)vaddr;
    uint32_t pd_index = (addr >> 22) & 0x3FF;  // Bits 22-31
    uint32_t pt_index = (addr >> 12) & 0x3FF;  // Bits 12-21
    
    // Init page table if needed
    if (pd[pd_index].present == 0) {
        pd[pd_index].present = 1;
        pd[pd_index].rw = 1;
        pd[pd_index].user = 0;
        pd[pd_index].writethru = 0;
        pd[pd_index].cachedisabled = 0;
        pd[pd_index].accessed = 0;
        pd[pd_index].pagesize = 0;
        pd[pd_index].ignored = 0;
        pd[pd_index].os_specific = 0;
        
        uint32_t pt_phys_addr = (uint32_t)(&pt[0]);
        pd[pd_index].frame = pt_phys_addr >> 12;
    }
    
    // Map each physical page
    struct ppage *current = pglist;
    uint32_t current_pt_index = pt_index;
    
    while (current != NULL) {
        if (current_pt_index >= 1024) {
            return NULL;
        }
        
        uint32_t phys_addr = (uint32_t)current->physical_addr;
        
        pt[current_pt_index].present = 1;
        pt[current_pt_index].rw = 1;
        pt[current_pt_index].user = 0;
        pt[current_pt_index].accessed = 0;
        pt[current_pt_index].dirty = 0;
        pt[current_pt_index].unused = 0;
        pt[current_pt_index].frame = phys_addr >> 12;
        
        current = current->next;
        current_pt_index++;
    }
    
    return vaddr;
}

// Load page directory into CR3
void loadPageDirectory(struct page_directory_entry *pd) {
    uint32_t pd_addr = (uint32_t)pd;
    asm("mov %0, %%cr3" : : "r"(pd_addr) : );
}
