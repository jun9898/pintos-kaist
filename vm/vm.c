/* vm.c: Generic interface for virtual memory objects. */
/*가상 메모리 객체를 위한 일반적인 인터페이스*/

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "kernel/hash.h"
#include "userprog/process.h"
#include "include/vm/uninit.h"
#include "include/threads/vaddr.h"

/* project 3 : Virtual Memory*/
struct list frame_table;		// 물리메모리 내의 각 프레임 정보를 가지고 있는 table
/* project 3 : Virtual Memory*/

unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED);
bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED);
/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
/*각 서브시스템의 초기화 코드를 호출하여 가상 메모리 서브시스템을 초기화합니다.*/
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
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
/*페이지의 유형을 가져옵니다. 
이 함수는 페이지가 초기화된 후의 유형을 알고 싶을 때 유용합니다. 
이 함수는 현재 완전히 구현되었습니다*/
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
/*초기화기와 함께 대기 중인 페이지 객체를 생성합니다. 
페이지를 만들고자 할 때 직접 만들지 말고 이 함수나 vm_alloc_page를 통해 만드세요.*/
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	/*upage가 이미 사용 중인지 여부를 확인합니다.*/
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initialier according to the VM type,
		 * TODO: and then create "uninit" page struct by calling uninit_new. You
		 * TODO: should modify the field after calling the uninit_new. */
		/* TODO: Insert the page into the spt. */
		/* 할 일: 페이지를 생성하고, VM 유형에 따라 초기화기를 가져오고, 
		uninit_new를 호출하여 "uninit" 페이지 구조체를 만듭니다. 
		uninit_new를 호출한 후에 필드를 수정해야 합니다. */
		/* 할 일: 페이지를 spt에 삽입합니다. */

		// 1. 페이지 생성
		struct page *p = (struct page *)malloc(sizeof(struct page));

		// 2. type에 맞는 초기화 함수 대입
		bool (*page_initializer) (struct page *, enum vm_type, void *);

		switch (VM_TYPE(type))
		{
		case VM_ANON:
			page_initializer = anon_initializer;
			break;
		case VM_FILE:
			page_initializer = file_backed_initializer;
			break;
		}

		// 3. uninit 타입의 페이지로 초기화
		uninit_new(p, upage, init, type, aux, page_initializer);

		/* 필드 수정은 uninit_new 호출 이후에
		why? uninit_new 함수 안에서 구조체 내용이 전부 새로 할당되기 때문*/
		p->writable = writable;

		return spt_insert_page(spt, p);
	}
	
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
/*spt에서 VA를 찾아서 페이지를 반환합니다. 오류가 발생하면 NULL을 반환합니다*/
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function. */
	page = malloc(sizeof(struct page));
	struct hash_elem *e;

	// vm에 해당하는 hash_elem를 찾는다.
	page->va = va;
	e = hash_find(&spt, &page->hash_elem);

	// 있으면 e에 해당하는 페이지를 반환
	if (e != NULL){
		return hash_entry(e, struct page, hash_elem);
	}
	// 아니면 NULL
	else{
		return NULL;
	}
}

/* Insert PAGE into spt with validation. */
/*유효성을 검사하여 PAGE를 spt에 삽입합니다.*/
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,	// struct page를 주어진 spt에 삽입해라.
		struct page *page UNUSED) {								// 이 함수는 가상 주소가 주어진 spt에 존재하지 않음을 반드시 체크해야 한다.
	/* TODO: Fill this function. */
	if (hash_insert(&spt, &page->hash_elem) == NULL){
		return true;
	}
	else{
		return false;
	}
}

// 해시 테이블 요소 삭제 함수
bool page_delete (struct hash *h, struct page *p){
	if(!hash_delete(h, &p->hash_elem)){
		return true;
	}
	else{
		return false;
	}
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
/* 삭제될 구조 프레임을 가져옵니다.*/
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */
	 /* 배제 정책은 너에게 달려있다.*/
	// struct thread *curr = thread_current();					// LRU
	// struct list_elem *start = list_begin(&frame_table);

	// for (start; start != list_end(&frame_table); start = list_next(start)) {
	// 	victim = list_entry(start, struct frame, frame_elem);
	// 	if (pml4_is_accessed(curr->pml4, victim->page->va))
	// 		pml4_set_accessed(curr->pml4, victim->page->va, 0);
	// 	else
	// 		return victim;
	// }

	victim = list_pop_front(&frame_table);						// fifo
	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
/*한 페이지를 삭제하고 해당하는 프레임을 반환합니다. 
	오류 시 NULL을 반환합니다.*/
/* page에 달려있는 frame 공간을 디스크로 내리는 swap out 진행 함수*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */
	/* 희생자를 스왑아웃하고, 쫓겨낸 프레임을 반환합니다.*/
	swap_out(victim->page);
	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/*palloc() 및 프레임 가져오기. 
사용 가능한 페이지가 없는 경우 페이지를 제거하고 반환합니다. 
이 함수는 항상 유효한 주소를 반환합니다. 
즉, 사용자 풀 메모리가 가득 찬 경우 이 함수는 사용 가능한 메모리 공간을 얻기 위해 프레임을 제거합니다.*/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame *)malloc(sizeof(struct frame));
	/* TODO: Fill this function. */
	ASSERT (frame != NULL);
	ASSERT (frame->page == NULL);

	void *kva = palloc_get_page(PAL_USER);	// user pool에서 커널 가상 주소 공간으로 1page 할당

	frame->kva = kva;						// frame 초기화
	if (frame->kva == NULL){				// 유저 풀 공간이 하나도 없다면
		frame = vm_evict_frame();			// frame에서 공간 내리고 새로 할당받아 오기
		frame->page = NULL;					
		return frame;
	}

	return frame;
}

/* Growing the stack. */
/* 스택 확장*/
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
/* 쓰기 보호된 페이지에서 발생한 오류를 처리합니다.*/
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
/* 성공시 true 반환*/
/* page fault가 발생하면 제어권을 전달 받는 함수.
   이 함수에서 할당된 물리 프레임이 존재하지 않아서 발생한 예외일 경우에는 매개변수인 not_present에 true를 전달 받는다.
   그럴 경우, SPT에서 해당 주소에 해당하는 페이지가 있는지 확인해서 존재한다면, 해당 페이지에 물리 프레임 할당을 요청하는 vm_do_claim_page 함수를 호출한다.
   */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED, bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* 오류를 유효성 검사합니다.*/
	/* TODO: Your code goes here */
	if (addr == NULL || !is_user_vaddr(addr)){
		return false;
	}

	page = spt_find_page(spt, addr);

	if (page == NULL){
		return false;
	}

	if (not_present){
		if (vm_do_claim_page(page)){
			return true;
		}
		else{
			return false;
		}
	}
	else{
		if (write && !page->writable){
			return false;
		}
	}
	return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
/*
페이지를 해제합니다.

이 함수를 건들지 말것.*/
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
/* VA에 할당된 페이지를 요청합니다.*/
/* 물리 프레임을 페이지에 할당*/
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	/* TODO: Fill this function */
	page = spt_find_page(&thread_current()->spt, va);
	if (page ==NULL){
		return false;
	}

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
/* 페이지를 요청하고 MMU를 설정합니다.*/
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	/* 페이지 테이블 항목을 삽입하여 페이지의 가상 주소(VA) 프레임의 물리 주소(PA)에 매핑한다.*/
	struct thread *curr = thread_current();
	if (pml4_set_page(curr->pml4, page->va, frame->kva, page->writable)){
		return swap_in(page, frame->kva);
	}
	else{
		return false;
	}
}

/* Initialize new supplemental page table */
/* 새로운 spt 초기화*/
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) { // 이 함수는 새로운 프로세스가 시작할때, 프로세스가 fork될 때 호출 된다.
	hash_init(spt, page_hash, page_less, NULL);
}

/* 해당 페이지 내 hash_elem을 받아와서 
그 페이지의 가상 주소에 대해 hash_byte를 반환한다.*/
unsigned page_hash(const struct hash_elem *p_, void *aux UNUSED){
	const struct page *p = hash_entry(p_, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof(p->va));
}

/* 해시 테이블 초기화에 필요한 키값 비교 함수*/
bool page_less(const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED){
	const struct page *a = hash_entry(a_, struct page, hash_elem);
    const struct page *b = hash_entry(b_, struct page, hash_elem);
	return a->va < b->va;
}


/* Copy supplemental page table from src to dst */
/* src에서 dst로 spt를 복사한다.*/
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
/* spt에 보유된 리소스를 해제한다.*/
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	/* TODO: 스레드에 의해 보유된 모든 보충 페이지 테이블을 파괴하고 
		수정된 내용을 저장소에 다시 기록합니다.*/
}
