#ifndef VM_FILE_H
#define VM_FILE_H
#include "filesys/file.h"
#include "vm/vm.h"

struct page;
enum vm_type;

struct file_page {
	uintptr_t mmap_addr;	// mmap으로 매핑한 주소
	// int mmap_page_cnt;		// mmap된 전체 페이지 수
	struct file *file;	// mapping한 파일
	off_t ofs;			// file에서의 offset
	size_t read_bytes;	// 읽어온 바이트 수
	size_t zero_bytes;	// 나머지 바이트 수 // ? 필요 ?
};

void vm_file_init (void);
bool file_backed_initializer (struct page *page, enum vm_type type, void *kva);
void *do_mmap(void *addr, size_t length, int writable,
		struct file *file, off_t offset);
void do_munmap (void *va);
#endif
