#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"

enum vm_type {
	/* page not initialized */
	/* 페이지가 초기화되지 않음 */
	VM_UNINIT = 0,
	/* page not related to the file, aka anonymous page */
	/* 파일과 관련 없는 페이지 */
	VM_ANON = 1,
	/* page that realated to the file */
	/* 파일과 관련된 페이지 */
	VM_FILE = 2,
	/* page that hold the page cache, for project 4 */
	/* 페이지 캐시가 들어있는 페이지 - project 4 */
	VM_PAGE_CACHE = 3,

	/* Bit flags to store state */
	/* 저장 상태에 대한 비트 플래그 */

	/* Auxillary bit flag marker for store information. You can add more
	 * markers, until the value is fit in the int. */
	/* 저장소 정보에 대한 보조 비트 플래그 마커.
	 * int 사이즈 내에서(?) 마커를 추가할 수 있음?? */
	VM_MARKER_0 = (1 << 3),
	VM_MARKER_1 = (1 << 4),

	/* DO NOT EXCEED THIS VALUE. */
	/* 이 값을 초과할 수 없음 */
	VM_MARKER_END = (1 << 31),
};

#include "kernel/hash.h"
#include "vm/uninit.h"
#include "vm/anon.h"
#include "vm/file.h"
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;
struct frame_table frame_table;

#define VM_TYPE(type) ((type) & 7)

/* The representation of "page".
 * This is kind of "parent class", which has four "child class"es, which are
 * uninit_page, file_page, anon_page, and page cache (project4).
 * DO NOT REMOVE/MODIFY PREDEFINED MEMBER OF THIS STRUCTURE. */
struct page {
	const struct page_operations *operations;
	void *va;              /* Address in terms of user space */
	struct frame *frame;   /* Back reference for frame */

	/* Your implementation */
	struct hash_elem hash_elem;
	bool writable;
	/* Per-type data are binded into the union.
	 * Each function automatically detects the current union */
	union {
		struct uninit_page uninit;
		struct anon_page anon;
		struct file_page file;
#ifdef EFILESYS
		struct page_cache page_cache;
#endif
	};
};

/* The representation of "frame" */
struct frame {
	void *kva;	// physical
	struct page *page;

	struct hash_elem hash_elem;
};

struct frame_table{
	struct hash *hash_table;
};

/* The function table for page operations.
 * This is one way of implementing "interface" in C.
 * Put the table of "method" into the struct's member, and
 * call it whenever you needed. */
struct page_operations {
	bool (*swap_in) (struct page *, void *);
	bool (*swap_out) (struct page *);
	void (*destroy) (struct page *);
	enum vm_type type;
};

#define swap_in(page, v) (page)->operations->swap_in ((page), v)
#define swap_out(page) (page)->operations->swap_out (page)
#define destroy(page) \
	if ((page)->operations->destroy) (page)->operations->destroy (page)

/* Representation of current process's memory space.
 * We don't want to force you to obey any specific design for this struct.
 * All designs up to you for this. */
struct supplemental_page_table {
	
	struct hash *hash_table;
	
};

#include "threads/thread.h"
void supplemental_page_table_init (struct supplemental_page_table *spt);
bool supplemental_page_table_copy (struct supplemental_page_table *dst,
		struct supplemental_page_table *src);
void supplemental_page_table_kill (struct supplemental_page_table *spt);
struct page *spt_find_page (struct supplemental_page_table *spt,
		void *va);
bool spt_insert_page (struct supplemental_page_table *spt, struct page *page);
void spt_remove_page (struct supplemental_page_table *spt, struct page *page);

void vm_init (void);
bool vm_try_handle_fault (struct intr_frame *f, void *addr, bool user,
		bool write, bool not_present);

#define vm_alloc_page(type, upage, writable) \
	vm_alloc_page_with_initializer ((type), (upage), (writable), NULL, NULL)
bool vm_alloc_page_with_initializer (enum vm_type type, void *upage,
		bool writable, vm_initializer *init, void *aux);
void vm_dealloc_page (struct page *page);
bool vm_claim_page (void *va);
enum vm_type page_get_type (struct page *page);

unsigned page_hash(const struct hash_elem *h, void *aux UNUSED);
bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);
unsigned frame_hash(const struct hash_elem *h, void *aux UNUSED);
bool frame_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);

#endif  /* VM_VM_H */
