/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "vm/anon.h"
#include "vm/file.h"
#include "userprog/process.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "threads/malloc.h"
#include <string.h>
#include "userprog/process.h"


static bool vm_do_claim_page (struct page *page);
// bool vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
// 		bool user UNUSED, bool write UNUSED, bool not_present UNUSED)
		
static bool vm_handle_wp (struct page *page UNUSED); 
static void vm_stack_growth (void *addr UNUSED);


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
	frame_table.hash_table = calloc(1, sizeof(struct hash));
	if(frame_table.hash_table){
		hash_init(frame_table.hash_table, frame_hash, frame_less, NULL);
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
// 페이지를 이니셜라이저와 함께 만드는 함수
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	// printf("\n ##### start VM_initializer ##### \n");
	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;
	bool (*initializer)(struct page *, enum vm_type, void *) ;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		// printf("\n ##### after spt_find_page ##### \n");

		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * 페이지 유형에 따라 페이지 구조체를 할당, 적절한 이니셜라이저 설정, 새 페이지 초기화 
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		struct page *new_page = calloc(1, sizeof(struct page));
		if(!new_page)
			goto err;
	
		bool (*initializer)(struct page *, enum vm_type, void *);
		// struct aux_lazy *aux_lazy;


		switch(VM_TYPE(type)){
			case VM_ANON:
				// printf("\n ##### in ANON ##### \n");
				// aux_lazy = calloc(1, sizeof(struct aux_lazy));
				// memcpy(aux_lazy, aux, sizeof(struct aux_lazy));
				// aux = aux_lazy;
				initializer = &anon_initializer;
				break;
			case VM_FILE:
				initializer = &file_backed_initializer;
				break;
			// case VM_PAGE_CACHE:
			// 	initializer = &page_cache_initializer;
			// 	break;
			default:
				goto err;
		}
		uninit_new(new_page, upage, init, type, aux, initializer);
		new_page->writable = writable;
		
		/* TODO: Insert the page into the spt. */
		if(spt_insert_page(spt, new_page)){
			// printf("\n ##### in if ##### \n");

			return true;
		}
		free(new_page);
		return false;
		// vm_dealloc_page(new_page);

		// 어디서 true를 리턴해줌;;
	}
	// printf("\n ##### out of spt_find_page ##### \n");

err:
	// printf("\n ##### debug ##### vm_initializer | err \n");

	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	// printf("\n ##### start spt_FIND_page ##### \n");
	// struct page *page = NULL;
	/* TODO: Fill this function. */
	
	struct page p;
	struct hash_elem *e;

	// p.va = pg_round_down (va);	// 찾고자하는 va
	p.va = pg_round_down(va);
	e = hash_find(spt->hash_table, &p.hash_elem);

	if(e!=NULL){
		// printf("\n ##### e is not NULL ##### \n");

		return hash_entry(e, struct page, hash_elem);		
	}
	else{
		// printf("\n ##### e is NULL ##### \n");
		return NULL;
	}

}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	
	int succ = false;
	/* TODO: Fill this function. */
	// 확인해서 어쩌라고..?
	// 있었든 없었든 여기선 insert 호출하잖아..?
	// null 체크
	if(page){
		if(!hash_insert(spt->hash_table, &page->hash_elem)){
		// if(!hash_insert(spt->hash_table, &page->hash_elem))
			succ = true;
		}
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
static struct frame * // 여기서 얻은 frame을 담을 frame page table을 구현해줘야할 것 같은데?
vm_get_frame (void) {
	struct frame *frame = calloc(1, sizeof(struct frame));
	/* TODO: Fill this function. */
	// frame에 메모리 할당
	if(frame){
		frame->kva = palloc_get_page(PAL_USER|PAL_ZERO);
		frame->page = NULL;

		if(!hash_insert(frame_table.hash_table, &frame->hash_elem)){
			return frame;
		}
	}
	else{
		PANIC("TODO");
	}


	/* TODO: Fill this function. */
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);
	return NULL;
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
/*
/exception.c
fault_addr = (void *) rcr2();
if (vm_try_handle_fault (f, fault_addr, user, write, not_present))
		return;
*/
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	// vm_try_handle_fault (f, fault_addr, user, write, not_present)
	// printf("\n ##### start vm_try_handle_fault ##### \n");

	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
	/* todo: 폴트를 확인하고 */
	/* todo: 여기서부터 써라 */

	page = spt_find_page(spt, addr);

	// if(!page){
	// 	return false; 	// ?
	// }
	
	// user, write, not_present는 어떻게 넘겨줌
	if (page) 
		return vm_do_claim_page (page);
	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);	// 페이지 type별 ops의 destroy 호출
	free (page);	// page 구조체 메모리 반환
}

/* Claim the page that allocate on VA. */
/* va에 할당하는 페이지를 요청 */
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	struct supplemental_page_table *spt = &thread_current()->spt;
	page = spt_find_page(spt, va);
	
	/* TODO: Fill this function */
	if(page){
		return vm_do_claim_page(page);
	}

	return false;
}
	

	

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	// printf("\n ##### start vm_do_claim_page ##### \n");

	struct thread *curr = thread_current();
	struct frame *frame = vm_get_frame ();		// frame 만들고

	/* Set links */
	if(!frame){
		return false;
	}
	frame->page = page;							// 연결하고
	page->frame = frame;
	
	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	/* todo: pte를 삽입하여 page의 va를 frame의 pa에 매핑 */
	if(pml4_get_page(curr->pml4, page->va) == NULL && pml4_set_page(curr->pml4, page->va, frame->kva, page->writable)){
		return swap_in(page, frame->kva);
	}
	palloc_free_page(frame->kva);
	free(frame);
	free(page);
	// spt에 page 추가
	return false;
	
	// return swap_in (page, frame->kva);
	// => page->operations->swap_in (page, frame->kva)
	// => uninit_initialize(page, frame->kva)
}




/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	// printf("\n ##### debug ##### start supplemental_page_table_init \n");
	// if(spt){
		spt->hash_table = calloc(1, sizeof(struct hash));
		hash_init (&spt->hash_table, page_hash, page_less, NULL);
	// }
}

// bool
// supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
// 		struct supplemental_page_table *src UNUSED) {
// 	// todo 2: This is used when a child needs to inherit the execution context of its parent (i.e. fork()). 
// 	// todo 2: Iterate through each page in the src's supplemental page table and make a exact copy of the entry in the dst's supplemental page table. 
// 	// todo 2: You will need to allocate uninit page and claim them immediately.
// 	struct hash_iterator i;
// 	hash_first (&i, src->hash_table);
// 	while (hash_next (&i)) {
// 		struct page *p = hash_entry (hash_cur (&i), struct page, hash_elem);
// 		struct page *new_p;
// 		// ? 이렇게 해도 되나 ? -> ok: current thread == dst (child)
// 		if (p->operations->type == VM_UNINIT) {	// 초기 페이지
// 			struct aux_lazy *aux = malloc (sizeof (struct aux_lazy));
// 			memcpy (aux, p->uninit.aux, sizeof (struct aux_lazy));	// copy aux
// 			if (!vm_alloc_page_with_initializer (page_get_type (p), p->va, p->writable, p->uninit.init, aux)) {
// 				return false;
// 			}
// 		}
// 		else {	// lazy load 된 페이지
// 			if (p->uninit.type & VM_MARKER_0) {	// 스택
// 				if (!setup_stack (&thread_current ()->tf)) {
// 					return false;
// 				}
// 			}
// 			else {	// 스택 이외
// 				if (!vm_alloc_page (page_get_type(p), p->va, p->writable)) {	// page 할당
// 					return false;
// 				}
// 				if (!vm_claim_page (p->va)) {	// mapping
// 					return false;
// 				}
// 			}
// 			new_p = spt_find_page (dst, p->va);
// 			memcpy (new_p->frame->kva, p->frame->kva, PGSIZE);	// 메모리 복사
// 		}
// 	}
// 	return true;
// }

/* Copy supplemental page table from src to dst */

bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
	// printf("\n ##### debug ##### start supplemental_page_table_copy \n");
	struct hash_iterator i;
	hash_first (&i, src->hash_table);
	while (hash_next (&i)) {
		// printf("\n ##### debug ##### copy : in while  cnt : %d \n", cnt++);

		struct page *p = hash_entry (hash_cur (&i), struct page, hash_elem);
		struct page *new_p;
		struct thread *curr = thread_current();
		enum vm_type type = page_get_type(p);
		
		if(p->operations->type == VM_UNINIT){	// 초기화된 UNINIT
			struct aux_lazy *aux = calloc(1, sizeof(struct aux_lazy));
			memcpy(aux, p->uninit.aux, sizeof(struct aux_lazy));
			if(!vm_alloc_page_with_initializer(type, p->va, p->writable, p->uninit.init, aux)){
				return false;
			}
		}
		else{					// 메모리 할당, 매핑된 ANON(지금은 이거만)
			// bool flag = ;
			if(p->anon.is_stack){	// 스택
			// if(p->uninit.type & VM_MARKER_0){	// 스택
				// printf("\n ##### debug ##### copy | stk | type : %d \n", p->uninit.type);
				// printf("\n ##### debug ##### copy | stk | type : %d \n", p->anon.is_stack);

				if(!setup_stack(&curr->tf)){
					return false;
				}
			}	
			else{					// 스택 X
				// printf("\n ##### debug ##### copy | no_stk | type : %d \n", p->uninit.type);
				// printf("\n ##### debug ##### copy | no_stk | type : %d \n", p->anon.is_stack);
				if(!vm_alloc_page(type, p->va, p->writable)){
					return false;
				}
				if(!vm_claim_page(p->va)){
					return false;
				}
			}
			// new_p = spt_find_page(&curr->spt, p->va);
			new_p = spt_find_page(&dst->hash_table, p->va);
			// new_p = spt_find_page(dst, p->va);
			memcpy(new_p->frame->kva, p->frame->kva, PGSIZE);
		}		
	}
	return true;
}

void destructor(struct hash_elem *h, void *aux){
	struct page *p = hash_entry(h, struct page, hash_elem);
	vm_dealloc_page(p);
}


/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {

	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	// struct hash *pages = &spt->hash_table;
	hash_destroy(spt->hash_table, destructor);
	// hash_destroy(&spt->hash, spt_destructor);

}

unsigned page_hash(const struct hash_elem *h, void *aux UNUSED){
	const struct page *p = hash_entry(h, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof p->va);
}

bool page_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
	const struct page *pa = hash_entry(a, struct page, hash_elem);
	const struct page *pb = hash_entry(b, struct page, hash_elem);

	return pa->va < pb->va;

}

unsigned frame_hash(const struct hash_elem *h, void *aux UNUSED){
	const struct frame *f = hash_entry(h, struct frame, hash_elem);
	return hash_bytes(&f->kva, sizeof f->kva);
}

bool frame_less(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED){
	const struct frame *fa = hash_entry(a, struct frame, hash_elem);
	const struct frame *fb = hash_entry(b, struct frame, hash_elem);

	return fa->kva < fb->kva;
}