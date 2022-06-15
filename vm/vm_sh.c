/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "userprog/process.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include <string.h>

// todo 1: Frame Table and Supplemental Page Table (hash table)
struct frame_table {
	struct hash *frames;
};

struct frame_table frame_table;

// returns a hash value for frame f
unsigned
frame_hash (const struct hash_elem *f_, void *aux UNUSED) {
	const struct frame *f = hash_entry (f_, struct frame, hash_elem);
	return hash_bytes (&f->kva, sizeof f->kva);
}

// returns true if frame a precedes frame b
bool
frame_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
	const struct frame *a = hash_entry (a_, struct frame, hash_elem);
	const struct frame *b = hash_entry (b_, struct frame, hash_elem);

	return a->kva < b->kva;
}

// returns a hash value for page p
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
	const struct page *p = hash_entry (p_, struct page, hash_elem);
	return hash_bytes (&p->va, sizeof p->va);
}

// returns true if page a precedes page b
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
	const struct page *a = hash_entry (a_, struct page, hash_elem);
	const struct page *b = hash_entry (b_, struct page, hash_elem);

	return a->va < b->va;
}

void
page_destructor (struct hash_elem *e, void *aux UNUSED){
	struct page *p = hash_entry (e, struct page, hash_elem);
	vm_dealloc_page (p);
}

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
	frame_table.frames = (struct hash *) calloc (1, sizeof (struct hash));	// (gitbook) To simplify your design, you may store these data structures in non-pageable memory (e.g., memory allocated by calloc or malloc). That means that you can be sure that pointers among them will remain valid.
	if (frame_table.frames != NULL) {
		hash_init (frame_table.frames, frame_hash, frame_less, NULL);	// frame table init
	}
	
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		// todo 2: page 구조체 생성 
		struct page *page = (struct page *) calloc (1, sizeof (struct page));
		if (page) {
			// todo 2: 페이지 초기화 함수 설정 (VM type에 따라서)
			bool *init_func;
			switch (VM_TYPE(type)) // type에 맞는 초기화함수 가져오기
			{
			case (VM_ANON):
			
				init_func = &anon_initializer;
				break;
			case (VM_FILE):
				init_func = &file_backed_initializer;
				break;
			default:
				goto err;
			}
			// todo 2: uninit_new 호출로 "uninit" page struct를 생성
			uninit_new (page, upage, init, type, aux, init_func);
			// todo 2: page 필드 수정
			page->frame = NULL;
			page->writable = writable;
			
			// ? (gitbook) and return the control back to the user program.
			/* TODO: Insert the page into the spt. */
			// todo 2: spt에 추가
			bool succ = spt_insert_page (spt, page);
			if (succ) return true;
			vm_dealloc_page (page); // spt에 추가 실패시 page 구조체 반환
		}
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function. */
	// todo 1: find element (Supplemental Page Table)
	struct page p;
	struct hash_elem *e;

	p.va = pg_round_down (va);	// 찾고자하는 va
	e = hash_find (spt->pages, &p.hash_elem);	// 탐색
	if (e != NULL) {
		page = hash_entry (e, struct page, hash_elem);	// 찾은 경우 페이지 리턴
	}

	return page;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	int succ = false;
	/* TODO: Fill this function. */
	// todo 1: insert page (Supplemental Page Table)
	if (page != NULL) {	// 유효한 페이지인지 확인
		struct hash_elem *temp = hash_insert (spt->pages, &page->hash_elem);	// 삽입 성공(테이블에 중복 없음)시 NULL리턴
		if (temp == NULL) succ = true;	// temp가 NULL이면 삽입 성공
	}

	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = NULL;
	/* TODO: Fill this function. */
	// todo 1: palloc으로 페이지(PAL_USER) 취득 frame table에 추가
	void *kva = palloc_get_page (PAL_USER | PAL_ZERO);			// (gitbook) Pintos works around this by mapping kernel virtual memory directly to physical memory
	if (kva != NULL) {	// 페이지 할당 성공
		// printf("im in get frame: %p\n", kva); //vm debugging
		frame = (struct frame *) calloc (1, sizeof (struct frame));	// (gitbook) To simplify your design, you may store these data structures in non-pageable memory (e.g., memory allocated by calloc or malloc). That means that you can be sure that pointers among them will remain valid.
		// 구조체 설정
		frame->kva = kva;
		frame->page = NULL;
		// printf("im in get frame: after page = null\n"); //vm debugging
		struct hash_elem *temp = hash_insert (frame_table.frames, &frame->hash_elem);	// ! (gitbook) frame table -> Allows efficient implementation of eviction policy of physical frames.
		if (temp != NULL) { // frame_table에 삽입 실패 (중복) // ? 중복하는 상황이 있을 수 있나??
			free (frame);	// 구조체 반환
			return NULL;
		}
	}
	else {
		// todo 5: 모두 가득 차있는 경우 eviction (addr == NULL 인 경우), swap out handling
		PANIC("TODO");
	}
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	if (not_present) return false;
	page = spt_find_page (spt, addr);
	if (page == NULL) return false;

	return vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	// todo 1: Claims the page to allocate va. You will first need to get a page.
	// ? get a page의 의미 ? 페이지를 palloc으로 받아라? 구조체를 할당해라? pml4_get_page? vm_alloc_page_with_initializer?
	// ? -> 페이지 구조체 자체를 위한 페이지가 필요한가? 페이지를 통해서 프레임에 접근하는데 그냥 구조체만 있으면되는거 아니야?
	// ! 시도 1 : pml4_get_page
	// struct thread *cur = thread_current();
	// void *kva = pml4_get_page (cur->pml4, va);	// 이미 할당되어있는지 확인 // ? 필요 ?
	// if (kva != NULL) return false;				// 사용되고 있음
	// page = (struct page *) calloc (1, sizeof (struct page));	// 페이지 구조체 메모리 할당
	// if (page != NULL) {
	// 	page->va = va;						// 구조체에 원하는 va로 설정
	// }
	// ! 시도 2 : 검색후 page 구조체 할당
	// page = spt_find_page (&cur->spt, va);
	// if (page == NULL) {
	// 	page = (struct page *) calloc (1, sizeof (struct page));
	// 	if (page != NULL) {
	// 		page->va = va;						// 구조체에 원하는 va로 설정
	// 	}
	// }
	// ! 시도 3: vm_alloc_page_with_initializer 사용 -> 아님
	// bool succ = vm_alloc_page_with_initializer (VM_UNINIT, va, 1, NULL, NULL); // ? init 함수 NULL?
	// ! 시도 4 : spt에서 찾아오기
	page = spt_find_page (&thread_current ()->spt, va);
	if (page == NULL) return false;
	// todo 1: then calls vm_do_claim_page with the page.
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// todo 1: Claims == allocate a physical frame, a page. (위의 코드)
	// todo 1: set up the mmu => add the mapping from the virtual address to the physical address in the page table. 
	// todo 1: The return value should indicate whether the operation was successful or not.
	struct thread *cur = thread_current ();			// 현재 thread
	bool succ = pml4_set_page (cur->pml4, page->va, frame->kva, page->writable);	// thread의 pml4에 페이지 mapping 
	if (!succ) { // mapping 실패
		palloc_free_page (frame->kva);	// 해당 kva(페이지)를 반환
		free (frame);					// frame 구조체 삭제
		vm_dealloc_page (page);			// ? page 삭제
		return false;
	}
	return swap_in (page, frame->kva); // todo 2
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	// todo 1: Implement Supplemental Page Table
	// ! initd(__do_fork)에서 사용
	spt->pages = (struct hash *) calloc (1, sizeof (struct hash));	// ? 지역변수로 선언하지 않고 malloc으로 할당하는게 맞을까? -> 취향
	// ? 할당되지 않은 경우를 assert로 걸러야할까?
	if (spt->pages != NULL) {
		hash_init (spt->pages, page_hash, page_less, NULL);	// 페이지 테이블 생성 (hash table)
	}
	// printf ("create spt %p\n", spt->pages); // vm debugging
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	// todo 2: This is used when a child needs to inherit the execution context of its parent (i.e. fork()). 
	// todo 2: Iterate through each page in the src's supplemental page table and make a exact copy of the entry in the dst's supplemental page table. 
	// todo 2: You will need to allocate uninit page and claim them immediately.
	struct hash_iterator i;
	hash_first (&i, src);
	while (hash_next (&i)) {
		struct page *p = hash_entry (hash_cur (&i), struct page, hash_elem);
		struct page *copy_p = calloc (1, sizeof (struct page));
		if (copy_p == NULL) {						// 할당 실패시
			// supplemental_page_table_kill (dst);		// 테이블 삭제 // ? 해도 되나?
			return false;
		}
		memcpy (copy_p, p, sizeof (struct page));	// 복사
		hash_insert (dst, &copy_p->hash_elem);		// 삽입
		vm_do_claim_page (copy_p->va);				// mapping
	}
	return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	// todo 2: You need to iterate through the page entries and call destroy(page) for the pages in the table.
	hash_destroy (spt->pages, &page_destructor);	// 테이블 삭제
}
