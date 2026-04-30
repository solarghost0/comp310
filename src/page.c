#include "page.h"
#include <stddef.h>

#define NUM_PAGES     128
#define PAGE_SIZE     (2 * 1024 * 1024) 
#define MEM_BASE_ADDR 0x0

/* Static array of all physical page descriptors */
struct ppage physical_page_array[NUM_PAGES];

/* Head of the global free list */
static struct ppage *free_list = NULL;


/* Linked list helpers
 * All allocator functions must go through these — never manipulate
 * next/prev pointers directly outside of here. */

/*
 * list_insert
 *
 * Prepends node onto the list pointed to by *head.
 *
 *  Before:  head → [A] ↔ [B] ↔ [C]
 *  After:   head → [node] ↔ [A] ↔ [B] ↔ [C]
 */
void list_insert(struct ppage **head, struct ppage *node) {
    if (node == NULL) return;

    node->next = *head;
    node->prev = NULL;

    if (*head != NULL) {
        (*head)->prev = node;
    }

    *head = node;
}

/*
 * list_unlink
 *
 * Removes node from wherever it sits in the list pointed to by *head.
 * Repairs the prev/next pointers of its neighbours.
 *
 *  Before:  head → [A] ↔ [node] ↔ [B]
 *  After:   head → [A] ↔ [B]        node→next/prev = NULL
 */
void list_unlink(struct ppage **head, struct ppage *node) {
    if (node == NULL) return;

    if (node->prev != NULL) {
        node->prev->next = node->next;  /* wire left neighbour past node */
    } else {
        *head = node->next;             /* node was the head — update head */
    }

    if (node->next != NULL) {
        node->next->prev = node->prev;  /* wire right neighbour past node */
    }

    /* Leave node clean so it can be safely inserted elsewhere */
    node->next = NULL;
    node->prev = NULL;
}


/* Page frame allocator */

/*
 * init_pfa_list
 *
 * Walks physical_page_array, assigns each entry its physical address,
 * and inserts it into the free list via list_insert.
 *
 * We insert in reverse order so that page 0 ends up at the head.
 */
void init_pfa_list(void) {
    free_list = NULL;

    for (int i = NUM_PAGES - 1; i >= 0; i--) {
        physical_page_array[i].physical_addr =
            (void *)(MEM_BASE_ADDR + (unsigned long)i * PAGE_SIZE);
        physical_page_array[i].next = NULL;
        physical_page_array[i].prev = NULL;

        list_insert(&free_list, &physical_page_array[i]);
    }
}


/*
 * allocate_physical_pages
 *
 * Unlinks npages pages from the front of free_list one at a time and
 * builds a new separate list (allocd_list) from them using list_insert.
 *
 * Returns a pointer to allocd_list, or NULL if not enough pages exist.
 *
 *  free_list before (npages = 2):
 *    head → [p0] ↔ [p1] ↔ [p2] ↔ [p3] ↔ ...
 *
 *  free_list after:
 *    head → [p2] ↔ [p3] ↔ ...
 *
 *  allocd_list returned:
 *    head → [p1] ↔ [p0]   (prepend order — caller should not rely on order)
 */
struct ppage *allocate_physical_pages(unsigned int npages) {
    /* Verify enough pages are available before touching anything */
    unsigned int count = 0;
    struct ppage *check = free_list;
    while (check != NULL && count < npages) {
        count++;
        check = check->next;
    }
    if (count < npages) {
        return NULL;  /* not enough free pages — free list is untouched */
    }

    /* Pull pages off free_list and build allocd_list */
    struct ppage *allocd_list = NULL;

    for (unsigned int i = 0; i < npages; i++) {
        struct ppage *page = free_list;        /* take from head of free list */
        list_unlink(&free_list, page);         /* remove from free list      */
        list_insert(&allocd_list, page);       /* prepend onto allocd list   */
    }

    return allocd_list;
}

void free_physical_pages(struct ppage *ppage_list) {
    while (ppage_list != NULL) {
        struct ppage *page = ppage_list;       /* take from head of input list */
        list_unlink(&ppage_list, page);        /* remove from input list       */
        list_insert(&free_list, page);         /* return to free list          */
    }
}