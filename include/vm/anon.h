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
	/* Initiate the struct page and maps the pa to the va */
	// bool (*page_initializer) (struct page *, enum vm_type, void *kva);


    
    // const struct page_operations *operations;
	// void *va;              
	// struct frame *frame;   

	
	// struct anon_page anon;
	
	// /* hash table 선언 */
	// /* --- project3-1 --- */
	// struct hash_elem hash_elem; /* Hash table element. */
	// bool writable;

    
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
