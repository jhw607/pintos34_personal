/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include "lib/kernel/bitmap.h"

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

struct swap_table {
	struct lock lock;		// 동기화를 위한 lock
	struct bitmap *bitmap;	// 스왑슬롯의 가용여부를 나타내기위한 bitmap
};

struct swap_table swap_table;

/* Initialize the data for anonymous pages */
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get (1, 1); 
	if (swap_disk == NULL) return;
	lock_init (&swap_table.lock);
	swap_table.bitmap = bitmap_create (disk_size (swap_disk)/8);	// page align

	// printf ("swap disk size: %d\n", disk_size(swap_disk));
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	// printf("anon_initializer Start \n");
	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;
	if(type & VM_MARKER_0)
		anon_page->is_stack = 1;
	else
		anon_page->is_stack = 0;
	// printf("anon_initializer FINISH \n");
	anon_page->sec_no_idx = NULL;
	// printf("==============start==============\n");
	// printf("anon_swap_in: %p:\n", anon_swap_in);
	// printf("sibal: %p:\n", page->operations->swap_in);

	return true;
	
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	// printf ("im in anon swap in!\n");
	// printf("help me--: %p\n", page);
	struct anon_page *anon_page = &page->anon;
	int cnt = 0;
	uint64_t temp_sec_idx = anon_page->sec_no_idx;
	void *temp_kva = kva;
	// 0x8004263918
	while (cnt < 8) {
		disk_read (swap_disk, temp_sec_idx * 8 + cnt, temp_kva);
		cnt += 1;
		temp_kva += 512;
	}
	// disk_read (swap_disk, anon_page->sec_no_idx, kva);
	bitmap_set_multiple (swap_table.bitmap, anon_page->sec_no_idx, 1, false);
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	lock_acquire (&swap_table.lock);
	// printf("4");
	uint64_t sec_no_idx = bitmap_scan_and_flip (swap_table.bitmap, 0, 1, false);
	// printf ("swap out va : %p\n",page->va);
	// 0x4747ff88
	// printf("sec_no_idx: %d\n", sec_no_idx);
	lock_release (&swap_table.lock);

	if (sec_no_idx != BITMAP_ERROR) {
		int cnt = 0;
		void *temp_kva = page->frame->kva;
		while (cnt < 8) {
			disk_write (swap_disk, sec_no_idx * 8 + cnt, temp_kva);
			cnt += 1;
			temp_kva += 512;
		}
		// disk_write (swap_disk, sec_no_idx, page->frame->kva);
		struct thread *cur = thread_current ();
		pml4_clear_page (cur->pml4, page->va);
		anon_page->sec_no_idx = sec_no_idx;
		// printf ("im in anon swap out!\n");
		return true;
	}
	else
		PANIC ("swap out error!\n");
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
	// void *addr = NULL; addr = "1"; // debugging
	// printf("	anon_destroy\n");
	// printf("		after list remove\n");
	if (page->frame != NULL) {
		lock_acquire (&frame_table.lock);
		list_remove(&page->frame->frame_elem);
		lock_release (&frame_table.lock);
		free(page->frame);
	}
	// palloc_free_page(page->frame->kva); -> pml4에서 pte로 받아서 없애는데 여기서 없애버리면 오류난다!
	// if (page->uninit.aux != NULL) free (page->uninit.aux);
	return;
}
