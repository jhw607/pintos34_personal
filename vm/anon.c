/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	
	
	swap_disk = NULL;
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	// printf("anon_initializer Start \n");
	page->operations = &anon_ops;
	// printf("\n ##### debug ##### anon_initializer | type : %d \n", type);


	struct anon_page *anon_page = &page->anon;
	if(type & VM_MARKER_0){
		// printf("\n ##### debug ##### anon_initializer | if \n");
		anon_page->is_stack = 1;
	}
	else{
		anon_page->is_stack = 0;
	}
	return true;
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
<<<<<<< HEAD
	// palloc_free_page(page->frame->kva);
	hash_delete(&frame_table, &page->frame->hash_elem);
=======
	list_remove(&page->frame->frame_elem);
	// palloc_free_page(page->frame->kva);
>>>>>>> 7c9cf50d9e092d256983bb4430f0d6c597757954
	free(page->frame);
	return;
}
