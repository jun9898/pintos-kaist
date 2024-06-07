/* uninit.c: Implementation of uninitialized page.
 *
 * All of the pages are born as uninit page. When the first page fault occurs,
 * the handler chain calls uninit_initialize (page->operations.swap_in).
 * The uninit_initialize function transmutes the page into the specific page
 * object (anon, file, page_cache), by initializing the page object,and calls
 * initialization callback that passed from vm_alloc_page_with_initializer
 * function.
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
/* uninit 타입으로 초기화*/
void
uninit_new (struct page *page, void *va, vm_initializer *init,		// 인자 : page :초기화할 page 구조체, upage : *p를 할당할 가상 주소
		enum vm_type type, void *aux,								// init : *p의 내용을 초기화하는 함수, aux : init에 필요한 보조 값
		bool (*initializer)(struct page *, enum vm_type, void *)) {	// initializer : p를 타입에 맞게 초기화하는 함수
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
/* 첫 fault에 페이지를 초기화한다.*/
/* 프로세스가 처음 만들어진(UNINIT) 페이지에 처음으로 접근할 때, page fault가 발생한다.
   그러면 page fault handler는 해당 페이지를 디스크에서 프레임으로 swap-in하는데, 
   ININIT type일때의 swap_in 함수가 바로 이 함수.
   즉 페이지 멤버를 초기화해줌으로써 페이지 타입을 인자로 주어진 타입(ANON, FILE, FILE_CACHE)로 변환
   여기서 만약 segment도 load되지 않은 상태라면 lazy load segment도 진행*/
static bool
uninit_initialize (struct page *page, void *kva) {
	struct uninit_page *uninit = &page->uninit;

	/* Fetch first, page_initialize may overwrite the values */
	/* 먼저 값을 가져온 후, page_initialize가 해당 값을 덮어쓸 수 있습니다.*/
	vm_initializer *init = uninit->init;
	void *aux = uninit->aux;

	/* TODO: You may need to fix this function. */


	return uninit->page_initializer (page, uninit->type, kva) &&
		(init ? init (page, aux) : true);
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
}
