#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
struct page;
enum vm_type;

struct anon_page {

    /* Initiate the contets of the page */
	vm_initializer *init;
	bool is_stack;
	void *aux;

	uint64_t sec_no_idx;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
