/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
 * 
 * 모든 페이지는 uninit page로 생성됨
 * 첫번째 page fault에서 핸들러가 uninit_initialize(page->operations.swap_in)을 호출
 * uninit_initialize는 페이지 개체를 초기화함으로써 페이지를 특정 개체(anon, file, page_cache)로 변환하고
 * vm_alloc_page_with_initializer 함수에서 전달된 initialization callback을 호출
 * 
 * */

#include "vm/vm.h"
#include "vm/uninit.h"

static bool uninit_initialize (struct page *page, void *kva);
static void uninit_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations uninit_ops = {
	.swap_in = uninit_initialize,
	.swap_out = NULL,
	.destroy = uninit_destroy,
	.type = VM_UNINIT,
};

/* DO NOT MODIFY this function */
void
uninit_new (struct page *page, void *va, vm_initializer *init,
		enum vm_type type, void *aux,
		bool (*initializer)(struct page *, enum vm_type, void *)) {
	// printf("\n ##### start uninit_new ##### \n");
	ASSERT (page != NULL);

	*page = (struct page) {
		.operations = &uninit_ops,
		.va = va,
		.frame = NULL, /* no frame for now */
		.uninit = (struct uninit_page) {
			.init = init,
			.type = type,
			.aux = aux,
			.page_initializer = initializer,
		}
	};
}

/* Initalize the page on first fault */
static bool
uninit_initialize (struct page *page, void *kva) {
	struct uninit_page *uninit = &page->uninit;
/* 	uninit_new에서 page->uninit에 세팅했던걸 꺼냄
	Initiate the contets of the page 
	vm_initializer *init;
	enum vm_type type;
	void *aux;
	* Initiate the struct page and maps the pa to the va *
	bool (*page_initializer) (struct page *, enum vm_type, void *kva); 	
	init, aux 꺼내놓고,
	page_initializer 호출할때 인자로 page, type, kva 넘겨주고
	init이 있으면 init(page, aux) 로 lazy_load_segment 호출
*/

	/* Fetch first, page_initialize may overwrite the values */
	vm_initializer *init = uninit->init;
	void *aux = uninit->aux;

	/* TODO: You may need to fix this function. */
	/* todo: 수정하라는 게 여기야? 호출될 함수들이야? */
	/* anon -> anon_initializer 호출 */
	/* file_backed -> file_backed_initializer 호출 */
	return uninit->page_initializer (page, uninit->type, kva) &&
		(init ? init (page, aux) : true);
		// lazy_load_segment(page, aux)
}

/* Free the resources hold by uninit_page. Although most of pages are transmuted
 * to other page objects, it is possible to have uninit pages when the process
 * exit, which are never referenced during the execution.
 * PAGE will be freed by the caller. */
static void
uninit_destroy (struct page *page) {
	struct uninit_page *uninit UNUSED = &page->uninit;
	/* TODO: Fill this function.
	 * TODO: If you don't have anything to do, just return. */
	free(uninit->aux);
	return;
}
