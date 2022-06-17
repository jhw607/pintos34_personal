/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
/* project 3 - mmap */
#include "filesys/file.h"
#include "userprog/syscall.h"
#include <debug.h>
#include "filesys/inode.h"
#include "threads/malloc.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);
static bool lazy_load_file (struct page *page, void *aux);
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
}


/* Do the mmap */
// 1. 페이지에 대한 공간을 할당 받는다(va)
static bool
lazy_load_file (struct page *page, void *aux){

	struct file_page *aux_file = (struct file_page *)aux;
	struct file *file = aux_file->file;
	off_t aux_ofs = aux_file->ofs;
	size_t page_read_bytes = aux_file->read_bytes;
	size_t page_zero_bytes = aux_file->zero_bytes;

	file_seek(file, aux_ofs); 

	// 이 페이지 do claim은 언제? , mmap -> do_mmap하고 page fault 하면 -> do claim 진행하므로 page존재 
	if (file_read(file, page->frame->kva, page_read_bytes) != (int) page_read_bytes){
		return false;
	}
	
	memset (page->frame->kva + page_read_bytes, 0, page_zero_bytes);	
	free(aux);

	return true;
}

void *
do_mmap (void *addr, size_t length, int writable,
		struct file *file, off_t offset) {
	// bool success = false;
	// offset + length ==PGSIZE
	// if (file_length(file) != PGSIZE;

	// 1. length bytes가 page 크기보다 크다면, 연속된 가상페이지로 접근시에 읽을 수 있도록 해주어야 함.	
	// 2. 맨 마지막 페이지에서 안쓰는 부분이 남으면 0으로 세팅
	// 3. lazy load

	// 주소를 받아와야 spt에서 하나씩 확인할 수 있을거같긴함.
	struct file *r_file = file_reopen(file);
	if (r_file == NULL) return NULL;

	off_t file_end_ofs = file_length(r_file);
	off_t file_rest_bytes = file_end_ofs-offset;
	uint32_t read_bytes, zero_bytes;
	int needed_page_count;
	
	if (file_rest_bytes>length){
		read_bytes = length;
		zero_bytes = (ROUND_UP (read_bytes, PGSIZE)- read_bytes);
		needed_page_count = length/PGSIZE;
	}
	else{

		read_bytes = file_rest_bytes;
		zero_bytes = (ROUND_UP (read_bytes, PGSIZE)- read_bytes);
		needed_page_count = file_rest_bytes/PGSIZE;

	}

	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	// struct file_page *m_page = malloc(sizeof(struct file_page));
	// m_page->open_count = needed_page_count;
	
	for (int i=0;i<=needed_page_count;i++){
		
		if (spt_find_page(&thread_current()->spt, addr)){ 
			return NULL;

		}		

		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;
		
		struct file_page *aux_file = malloc(sizeof (struct file_page));

		aux_file->file = r_file;
		aux_file->ofs = offset;
		aux_file->read_bytes = page_read_bytes;
		aux_file->zero_bytes = page_zero_bytes;

		if(!vm_alloc_page_with_initializer(VM_FILE, addr, 
			writable, lazy_load_file, aux_file));
			return false;

		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		addr += PGSIZE;
		offset += page_read_bytes;
	}

	return addr;
}

/* Do the munmap */
// 이전에 mmap으로 매핑된 주소에 대해서 아직 unmap 되지 않았다면, 해당되는 mapping page들을 모두 해체해줘야함
// 모든 page unmap 해줘야함, 인자는 첫번째 주소만 들어오네
// 첫번째 주소만으로 연관된 page들을 모두 찾아야함
// 연관된 페이지들을 하나의 리스트에 넣고, 이 리스트들을 관리할 수 있게 자료구조 사용
// 총 페이지 수로 계산하자. -> next만 넣고 안되는 이유 알아보기
// unmap이 명시적으로 호출되지 않았따고 해도, 프로세스가 종료되면 묵시적으로 모든 매핑 unmap
// -> process exit (unmap)-> spt kill 시 VM_FILE 타입 어떻게 destroy? -> file_backed_destroy

// unmap될 때, dirty인 페이지들을 모두 파일에 write해야하고, not diry인 페이지들은 그냥 버리면됨
// -> dirty를 확인할 필요
// -> pml4 is dirty라는 함수를 사용하기 위해서 pml4를 어디서 받아와야함

// spt 안에서 제거해야하고(spt remove page), frame도 지워줘야하고(ft delete, free),
// pte에서도 mapping 해제해주어야함(palloc free, pml4_clear_page)
void
do_munmap (void *addr) {

	struct thread
	struct page *page = spt_find_()

}
