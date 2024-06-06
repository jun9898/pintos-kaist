/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "include/lib/kernel/hash.h"
#include "include/threads/vaddr.h"
#include "threads/mmu.h"

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
}

//page의 vaddr을 인자값으로 hash_int() 함수를 사용하여 해시 값 반환
static unsigned
vm_hash_func (const struct hash_elem *e, void *aux) {
	
	// hash_entry()로 element에 대한 page 구조체 검색
	struct page *p = hash_entry(e, struct page, hash_elem);

	// hash_int()를 이용해서 vm_entry의 멤버 va에 대한 해시값을 구하고 반환
	return hash_int(p->va);
}

/* 입력된 두 hash_elem의 vaddr 비교
	 a의 va < b의 va 이면 true 반환 
	 a의 va > b의 va 이면 false 반환  */
static bool
vm_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux) {
	struct page *a_page = hash_entry(a, struct page, hash_elem);
	struct page *b_page = hash_entry(b, struct page, hash_elem);
  
	return a_page->va < b_page->va;
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
		struct page *page = (struct page*)malloc(sizeof(page));

		if (page == NULL){
			goto err;
		}
		
	typedef bool (*initializerFunc)(struct page *, enum vm_type, void *);
  initializerFunc initializer = NULL;

  switch(VM_TYPE(type)) {
    	case VM_ANON:
        initializer = anon_initializer;
      	break;
      case VM_FILE:
        initializer = file_backed_initializer;
        break;
	}

  uninit_new(page, upage, init, type, aux, initializer);

  page->writable = writable;

		/* TODO: Insert the page into the spt. */
		return spt_insert_page(spt, page);
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *page = (struct page *)malloc(sizeof(struct page));
	page->va = pg_round_down(va);

	// spt 주소로 줘야하는지??
	struct hash_elem *e = hash_find(&spt->spt_hash, &page->hash_elem);

	free(page);

	return e == NULL ? NULL : hash_entry(e, struct page, hash_elem);
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
	if (!hash_insert(spt->spt_hash, &page->hash_elem)){		
		return true;
	}

	return false;
}

// 지울 대상이 없으면 삭제 실패 //dealloc???? //bool type으로 변환???
void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	// if (!hash_delete(spt->spt_hash, &page->hash_elem)){
	// 	return false;
	// }
	return true;
}

// 지울 놈이 없으면 hash_delete이 NULL pointer return
bool
spt_delete_page (struct hash *hash, struct page *page){
	if (!hash_delete(hash, &page->hash_elem)){
		return false;
	}
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

//frame만 받아오고 page랑 연결은 안되어있음
static struct frame *
vm_get_frame (void) {
	struct frame *frame = (struct frame*)malloc(sizeof(struct frame));
	if (frame == NULL) {
		PANIC("Failed to allocate memory for frame");
	}

	// don't need to handle swap out for now in case of page allocation failure. Just mark those case with PANIC("todo") for now.
	void *kva = palloc_get_page(PAL_USER);
	if(kva == NULL) {
		PANIC("todo");
	}

	frame->kva = kva;
	frame->page = NULL;

	// if(frame->kva == NULL) {
	// 	frame = vm_evict_frame();
	// 	frame->page = NULL;

	// 	return frame;
	// }

	ASSERT (frame != NULL);
	ASSERT (frame->kva != NULL);  // 추가로 kva 잘 할당되었는지 확인
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
//프레임을 페이지에 할당
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
	
	page = spt_find_page(&thread_current()->spt, va);

	if (page == NULL)
		return false;

	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
// 실제로 프레임을 페이지에 할당하는 함수
// page는 user page에 있고, frame은 kernel page에 존재한다
// 가상 메모리 주소(page)와 물리 메모리 주소(frame)를 매핑
static bool
vm_do_claim_page (struct page *page) {
	struct thread *curr = thread_current();
	struct frame *frame = vm_get_frame ();
	if (frame == NULL){
		return false;
	}

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
	// bool success = (pml4_get_page (curr->pml4, page->va) == NULL 
	// 				&& pml4_set_page (curr->pml4, page->va, frame->kva, page->writable));

	pml4_set_page (curr->pml4, page->va, frame->kva, page->writable);

	// if (success)
	// {
	// 	return swap_in (page, frame->kva);
	// }

	return swap_in (page, frame->kva);
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	
	hash_init(spt, vm_hash_func, vm_less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

void
free_pages (struct hash_elem *e, void *aux) {
	struct page *page = hash_entry(e, struct page, hash_elem);

	//page type에 맞는 destroy function을 부른다
	// destroy(page);
	free(page);
	// free(e);
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	* TODO: writeback all the modified contents to the storage. */
	
	// hash destory()로 제거하고	 
	// writeback 어케하지

	hash_destroy(spt->spt_hash, free_pages);
}


