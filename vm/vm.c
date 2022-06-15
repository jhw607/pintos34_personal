/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "vm/anon.h"
#include "vm/file.h"
#include "userprog/process.h"

unsigned page_hash (const struct hash_elem *p_, void *aux);
bool page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux);
struct list frame_table;

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
	/* --- project3-1 --- */
	//frame table init 추가, 나중에 swap in, out할 때-> clock algorithm 사용할 때 어차피 순회해야하므로 해시를 사용하지 않음
	list_init (&frame_table);

}

/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, hash_elem);
  return hash_bytes (&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_,
           const struct hash_elem *b_, void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, hash_elem);
  const struct page *b = hash_entry (b_, struct page, hash_elem);

  return a->va < b->va;
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

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;
	bool (*initializer)(struct page *, enum vm_type, void *) ;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
	
		struct page *page =	(struct page *)malloc(sizeof (struct page));

		switch(VM_TYPE(type)){
			
			case VM_ANON:
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

		uninit_new(page, upage, init, type, aux, initializer);
		page->writable = writable;
		bool succ = spt_insert_page(spt, page);
		if (succ) return true;
		// vm_dealloc_page(page);
		return false;
		
	}
	

err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page page;
	struct hash_elem *e;
	page.va = pg_round_down(va); 

	/* TODO: Fill this function. */
	e = hash_find(&spt->hash, &page.hash_elem);
	return e != NULL ? hash_entry(e, struct page, hash_elem) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
		struct page *page UNUSED) {
	
	int succ = false;
	/* TODO: Fill this function. */
	if (page != NULL){
		
		if(hash_insert(&spt->hash, &page->hash_elem) == NULL) //insert 되면(원래는 NULL값 return) True로 반환받게 설정
			succ = true;
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

	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));

	if (frame != NULL){
		frame->kva = palloc_get_page(PAL_USER);
		frame->page = NULL;

		if (frame->kva != NULL){
			
			list_push_back(&frame_table, &frame->frame_elem);
			// fpt_insert_page(&frame_table,frame);
		}
	}
	else
	{
		PANIC("todo");
	}

	/* TODO: Fill this function. */
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
/*
/exception.c
fault_addr = (void *) rcr2();
if (vm_try_handle_fault (f, fault_addr, user, write, not_present))
		return;
*/
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	// 1. valid page fault check(lazy load 등)
	// if valid, invalid에 접근하는 fault를 의미
	// if bogus fault, page안으로 some contents를 로드, return control to the user prog
	
	// /* TODO: Your code goes here */
	page = spt_find_page(spt, addr);
	// // 2. bogus fault를 어떻게 처리해줘야하는지 ?
	if (page) 
	

		return vm_do_claim_page (page);
	return false;
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
	struct thread *curr = thread_current();
	// printf("vm_claim_page start\n");
	// uint64_t *pml4 = &thread_current()->pml4;
	// if(pml4_get_page (pml4, page->va) != NULL) return false;
	// printf("spt_find_page start\n");
	page = spt_find_page(&curr->spt, va);
	// printf("spt_find_page finish\n");
	if (page == NULL)
		return false;
	// if (page==NULL){
	// 	struct page *page =	(struct page *)malloc(sizeof *page); 
	// 	if (page != NULL) {
	// 		page->va = va;
	// 	}
	// }

	/* TODO: Fill this function */
	// printf("vm_claim_page finish\n");

	return vm_do_claim_page(page);
}
	

	

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	// printf("vm_do_claim_page start \n");
	// printf("vm_get_frame start \n");
	struct frame *frame = vm_get_frame ();
	// printf("vm_get_frame finish \n");
	struct thread *curr = thread_current();
	/* Set links */
	frame->page = page;
	page->frame = frame;
	// printf("pml4_set_page start \n");
	if (pml4_get_page(curr->pml4, page->va) == NULL
		&& pml4_set_page (curr->pml4, page->va, frame->kva, page->writable))
		// printf("pml4_set_page finish \n");
		return swap_in (page, frame->kva);
	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	return false;
}




/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	// struct hash *pages = &spt->hash;			

	// // NULL이 아니면 조건 추가
	// if(pages != NULL)
	hash_init(&spt->hash, page_hash, page_less, NULL); 

}

/* Copy supplemental page table from src to dst */

bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
		
	struct hash_iterator i;
	hash_first (&i, &src->hash);
	while (hash_next (&i)){
		struct page *p = hash_entry (hash_cur (&i), struct page, hash_elem);
		struct page *new_p;
		struct thread *curr = thread_current();
		
			if(p->operations->type == VM_UNINIT){		
				struct aux_lazy_load *aux = malloc (sizeof (struct aux_lazy_load));
			    memcpy (aux, p->uninit.aux, sizeof (struct aux_lazy_load));	// copy aux		
				if (!vm_alloc_page_with_initializer(page_get_type(p), 
					p->va, p->writable, p->uninit.init, aux)){
						return false;
					}

			}

			else{
				
				if(p->uninit.type & VM_MARKER_0){
					if(!setup_stack(&curr->tf)){
						return false;
					}
				}

				else{
					if(!vm_alloc_page(page_get_type(p), p->va, p->writable))
					{
						return false;
					}
					if(!vm_claim_page(p->va))
					{
						return false;
					}
				}
				new_p = spt_find_page(dst, p->va);
				memcpy (new_p->frame->kva, p->frame->kva, PGSIZE);
			}

		
	}
	return true;
		
	
}	
	// load 안된 친구들 
	// 1. lazy load를 해줘야함 
	// 2. aux를 공간할당 받고 넘겨줘야함
	// 3. 그리고 lazy load를 시킨다
	// load 된 친구들
	// 1. lazy load를 안해줘도됨
	// 2. 올라가 있는 애들 
	// 스택일 때, set up stack
	// 아니면 일단 할당 받고
	// alloc, vm_do_claim





void
spt_destructor (struct hash_elem *h, void *aux){

	struct page *p = hash_entry(h, struct page, hash_elem);
	vm_dealloc_page(p);
	
}
/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {

	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_destroy(&spt->hash, spt_destructor);
	
}
