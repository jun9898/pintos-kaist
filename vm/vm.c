/* vm.c: Generic interface for virtual memory objects. */

#include <stdio.h>
#include "threads/malloc.h"
#include "threads/vaddr.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "kernel/hash.h"
#include "threads/mmu.h"

uint64_t vm_hash_hash_func (const struct hash_elem *e, void *aux);
bool vm_hash_less_func (const struct hash_elem *a, const struct hash_elem *b, void *aux);

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
	list_init(&frame_table);
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
// upage = va
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {

	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;

	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
		/* TODO: Create the page, fetch the initializer according to the VM type, */
		// 페이지를 생성 = malloc
		struct page *p = (struct page *) malloc(sizeof(struct page));
		// switch문으로 page종류에 맞는 초기화 함수 세팅
		/* TODO: and then create "uninit" page struct by calling uninit_new. */
		switch (VM_TYPE(type))
		{
		case VM_ANON:
			uninit_new(p, upage, init, type, aux, anon_initializer);
			break;
		case VM_FILE:
			uninit_new(p, upage, init, type, aux, file_backed_initializer);
			break;
		}
		p->writable = writable;
		if (!spt_insert_page(spt, p)) {
			free(p);
			goto err;
		}
		return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
	struct page *p = (struct page *) malloc(sizeof(struct page));
	struct hash_elem *e;
	/* TODO: Fill this function. */

	p->va = pg_round_down(va);
	e = hash_find(&spt->spt_hash, &p->hash_elem);

	printf("Claim va: %p\n", p->va);
	struct hash_iterator it;
	for (hash_first(&it, &spt->spt_hash); hash_next(&it);) {
		struct page *p = hash_entry(hash_cur(&it), struct page, hash_elem);
        printf("Page va: %p\n", p->va);
	}

	free(p);
	
	return e == NULL ? NULL : hash_entry(e, struct page, hash_elem);
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
        struct page *page UNUSED) {
	return hash_insert(&spt->spt_hash, &page->hash_elem) == NULL ? true : false; 
}

void 
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	hash_delete(&spt->spt_hash, &page->hash_elem);
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

	/* 물리 메모리의 할당 -> frame */
	void *kpage = palloc_get_page(PAL_USER);

	/* 만약 페이지 할당에 성공했다면 */
	if (kpage == NULL) {
		// frame = vm_evict_frame();
 		PANIC ("todo");
	}
	frame = (struct frame *)malloc(sizeof(struct frame));
	frame->kva = kpage;
	frame->page = NULL;

	list_push_back(&frame_table, &frame->frame_elem);
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
vm_try_handle_fault(struct intr_frame *f UNUSED, void *addr UNUSED,
                         bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
    struct supplemental_page_table *spt UNUSED = &thread_current()->spt;

    /* TODO: Validate the fault */
    /* TODO: Your code goes here */
    // 진짜 페이지 폴트
 	if (addr == NULL || is_kernel_vaddr(addr)) {
        return false;
    }

    // 가짜 페이지 폴트
    if (not_present) {
        struct page *page = spt_find_page(spt, addr);
        if (page == NULL) {
            return false;
		}
  		if (write == true && page->writable == false) {
            return false;
        }
        return vm_claim_page(addr);
    }
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
	struct page *page = spt_find_page(&thread_current()->spt, va);

	/* TODO: Fill this function */
	if (page == NULL) {
		return false;
	}

	return vm_do_claim_page (page);
}

static bool
vm_do_claim_page(struct page *page)
{
    struct frame *frame = vm_get_frame();

    /* Set links */
    frame->page = page;
    page->frame = frame;

    /* TODO: Insert page table entry to map page's VA to frame's PA. */
    if (pml4_set_page(thread_current()->pml4, page->va, frame->kva, page->writable))
        return swap_in(page, frame->kva);
    else
        return false;
}

/* Initialize new supplemental page table */
// spt == vm_entry?
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
	/* hash_hash_func, hash_less_func 얘는 왜 해줘야할까 */
	hash_init(&spt->spt_hash, vm_hash_hash_func, vm_hash_less_func, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
	hash_clear(&spt->spt_hash, vm_hash_action_func);
}

/* page가 가지고 있는 va를 가지고 해시의 key값으로 변환하는 로직 */
uint64_t 
vm_hash_hash_func (const struct hash_elem *e, void *aux) {
	struct page *p = hash_entry(e, struct page, hash_elem);
	return hash_bytes(&p->va, sizeof(p->va));
}

bool 
vm_hash_less_func (const struct hash_elem *a,
				const struct hash_elem *b,
				void *aux) {
	struct page *tmp_a = hash_entry(a, struct page, hash_elem);
	struct page *tmp_b = hash_entry(b, struct page, hash_elem);
	return tmp_a->va < tmp_b->va;
}

void 
vm_hash_action_func (struct hash_elem *e, void *aux) {
	struct page *p = hash_entry(e, struct page, hash_elem);
	free(p);
}