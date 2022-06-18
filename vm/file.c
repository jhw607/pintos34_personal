/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
/* project 3 - mmap */
#include "filesys/file.h"
#include "userprog/syscall.h"
#include "userprog/process.h"
#include "filesys/file.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
	
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
	struct aux_lazy_load *aux_box = (struct aux_lazy_load *)page->uninit.aux;
	file_page->file = aux_box->file;
	file_page->ofs = aux_box->ofs;
	file_page->read_bytes = aux_box->read_bytes;
	file_page->zero_bytes = aux_box->zero_bytes;
	file_page->mmap_addr = aux_box->mmap_addr;

	return true;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
	struct thread *cur = thread_current ();
	if (pml4_is_dirty (cur->pml4, page->va)) {
		file_write_at (page->file.file, page->frame->kva, page->file.read_bytes, page->file.ofs);	// 쓰인 부분 다시 써줘야함
		pml4_set_dirty (&cur->pml4, page->va, false);
	}
	list_remove(&page->frame->frame_elem);
	free(page->frame);
	// if (page->uninit.aux != NULL) free (page->uninit.aux);
}


/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	// todo 3: On failure, it must return NULL.
	// count pages for mmap
	// printf ("===========================\n");
	struct file *r_file = file_reopen (file);
	if (r_file == NULL) return NULL;	// file reopen 실패시
	// char * buf1[100];
	// char * buf2[100];
	// file_read_at (file, buf1, 100, offset);
	// file_read_at (r_file, buf2, 100, offset);
	// printf("read file:  %s \n", buf1);
	// printf("read r_file:  %s \n", buf2);
	void *temp_addr = addr;
	int num_page = 0;
	off_t file_end_ofs = file_length (r_file);
	off_t file_rest_bytes = file_end_ofs - offset;
	length = file_rest_bytes < length ? file_rest_bytes : length;
	while (temp_addr <= pg_round_down (addr + length)) {
		num_page += 1;
		// todo 3: It must fail if the range of pages mapped overlaps any existing set of mapped pages, 
		// todo 3: including the stack or pages mapped at executable load time.
		if (spt_find_page (&thread_current ()->spt, temp_addr) != NULL)
			return NULL;
		temp_addr += PGSIZE;
	}
	// printf("addr : %p\n", addr);
	// printf("length : %d\n", length);
	// printf("file : %p\n", r_file);
	// printf("offset : %d\n", offset);
	// printf("page count : %d\n", num_page);
	// todo 3: Memory-mapped pages should be also allocated in a lazy manner just like anonymous pages. 
	// todo 3: You can use vm_alloc_page_with_initializer or vm_alloc_page to make a page object.
	temp_addr = addr;
	off_t temp_offset = offset;
	while (num_page > 0) {
		// printf ("	do mmap---------------condition check\n");
		size_t page_read_bytes = num_page > 1 ? PGSIZE : pg_ofs (addr + length);
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
		// printf("page_read_bytes : %d, temp_offset: %d\n", num_page, temp_offset);
		struct aux_lazy_load *aux = malloc(sizeof (struct aux_lazy_load));
		aux->mmap_addr = addr;
		aux->file = r_file;
		aux->ofs = temp_offset;
		aux->read_bytes = page_read_bytes;
		aux->zero_bytes = page_zero_bytes;
		bool succ = vm_alloc_page_with_initializer (VM_FILE, temp_addr, writable, lazy_load_segment, aux);
		if (!succ) return NULL;
		// printf ("	do mmap---------------after init\n");
		// for next page
		num_page -= 1;
		temp_addr += PGSIZE;
		temp_offset += PGSIZE;
	}
	// printf ("===========================\n");
	// printf("	do_mmap addr : %p\n", addr);
	return addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	// todo 3: Unmaps the mapping for the specified address range addr
	// mmap으로 한번에 매핑된 부분 전체 매핑 해제 필요
	struct thread *cur = thread_current();
	void *temp_addr = addr;
	struct file *file = NULL;
	struct page *page = spt_find_page (&cur->spt, temp_addr);
	if (page == NULL) return;
	while (page->operations->type == VM_FILE && page->file.mmap_addr == addr) {
		file = page->file.file;
		// todo 3: all pages written to by the process are written back to the file, and pages not written must not be.
		// todo 3: use the file_reopen function to obtain a separate and independent reference to the file for each of its mappings.
		if (pml4_is_dirty (cur->pml4, addr)) {
			file_write_at (page->file.file, page->frame->kva, page->file.read_bytes, page->file.ofs);	// 쓰인 부분 다시 써줘야함
			pml4_set_dirty (&cur->pml4, addr, false);
		}
		// todo 3: unmapp
		spt_remove_page (&cur->spt, page);
		// for next page
		temp_addr += PGSIZE;
		page = spt_find_page (&cur->spt, temp_addr);
		if (page == NULL) break;
	}
	// file close (file_close: null인 경우는 닫지 않음)
	file_close (file);
	return;

}