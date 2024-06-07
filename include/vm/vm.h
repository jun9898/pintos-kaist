#ifndef VM_VM_H
#define VM_VM_H
#include <stdbool.h>
#include "threads/palloc.h"
#include "kernel/hash.h"

enum vm_type {
	/* page not initialized */
	VM_UNINIT = 0,
	/* page not related to the file, aka anonymous page */
	VM_ANON = 1,
	/* page that realated to the file */
	VM_FILE = 2,
	/* page that hold the page cache, for project 4 */
	VM_PAGE_CACHE = 3,

	/* Bit flags to store state */

	/* Auxillary bit flag marker for store information. You can add more
	 * markers, until the value is fit in the int. */
	VM_MARKER_0 = (1 << 3),
	VM_MARKER_1 = (1 << 4),

	/* DO NOT EXCEED THIS VALUE. */
	VM_MARKER_END = (1 << 31),
};

#include "vm/uninit.h"
#include "vm/anon.h"
#include "vm/file.h"
#ifdef EFILESYS
#include "filesys/page_cache.h"
#endif

struct page_operations;
struct thread;

#define VM_TYPE(type) ((type) & 7)

/* The representation of "page".
 * This is kind of "parent class", which has four "child class"es, which are
 * uninit_page, file_page, anon_page, and page cache (project4).
 * DO NOT REMOVE/MODIFY PREDEFINED MEMBER OF THIS STRUCTURE. */
 /*
"페이지"의 표현.
이것은 네 개의 "하위 클래스"인 uninit_page, file_page, anon_page, 
그리고 페이지 캐시(project4)를 가지고 있는 "상위 클래스"의 형태입니다.
이 구조체의 미리 정의된 멤버를 제거하거나 수정하지 마십시오.*/
struct page {
	const struct page_operations *operations;	// 페이지가 수행할 수 있는 작업을 정의하는 함수 포인터 테이블, 각 페이지 유형에 따라 다른 작업을 수행할 수 있다.
	void *va;              						// 사용자 공간에서의 주소, 가상 주소 공간에서 페이지의 위치를 나타냄
	struct frame *frame;   						// 프레임에 대한 역참조, 물리 메모리에서 페이지가 저장된 위치를 나타냄

	/* Your implementation */
	struct hash_elem hash_elem;					// 해시 테이블에 페이지를 저장하기 위한 요소, 이걸 통해 해시 테이블에 삽입하거나 검색할 수 있다.
	bool writable;
	/* Per-type data are binded into the union.
	 * Each function automatically detects the current union */
	/*각 유형의 데이터가 유니온에 바인딩됩니다. 
	각 함수는 현재 유니온을 자동으로 감지합니다.*/
	union {
		struct uninit_page uninit;	// 초기화되지 않은 페이지
		struct anon_page anon;		// 익명 페이지
		struct file_page file;		// file 페이지
#ifdef EFILESYS
		struct page_cache page_cache;
#endif
	};
};

/* The representation of "frame" */
struct frame {
	/* 커널 가상 주소를 가리키는 포인터, 
	이 포인터는 물리 메모리의 커널 주소 공간에서 매핑된 위치를 나타냄,
	커널이 이 프레임에 접근할 때 사용하는 주소*/
	void *kva;							
	/* 이 멤버는 이 프레임과 연결된 페이지 구조체를 가리킴,
	이 프레임에 매핑된 가상 페이지의 정보를 포함
	이를 통해 페이지와 프레임 간의 매핑을 관리*/ 
	struct page *page;					

	/* project 3 : Virtual Memory*/
	/* frame table에 넣을 리스트 원소,
	프레임 테이블을 linked list 형태로 관리
	프레임 테이블에서 프레임을 추가하거나 제거할 때 리스트 원소로 사용*/
	struct list_elem frame_elem;		
	/* project 3 : Virtual Memory*/
};

/* The function table for page operations.
 * This is one way of implementing "interface" in C.
 * Put the table of "method" into the struct's member, and
 * call it whenever you needed. */
/*페이지 작업을 위한 함수 테이블입니다.
이것은 C에서 "인터페이스"를 구현하는 한 가지 방법입니다.
"메서드"의 테이블을 구조체 멤버에 넣고, 필요할 때 호출합니다.*/
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
/* 현재 프로세스의 메모리 공간 표현입니다.
이 구조체에 대해 특정 디자인을 강요하고 싶지 않습니다.
이 구조체에 대한 모든 디자인은 당신에게 달려 있습니다.*/
struct supplemental_page_table {
	struct hash spt_hash;
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

#endif  /* VM_VM_H */
