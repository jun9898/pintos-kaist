#ifndef VM_UNINIT_H
#define VM_UNINIT_H
#include "vm/vm.h"

struct page;
enum vm_type;

typedef bool vm_initializer (struct page *, void *aux);

/* Uninitlialized page. The type for implementing the
 * "Lazy loading". */
/* struct uninit_page는 초기화되지 않은 페이지를 나타내며, 
   "지연 로딩(Lazy loading)"을 구현하는 데 사용됩니다. 
   이 구조체는 페이지의 내용을 초기화하고, 
   페이지 구조체를 초기화하며 가상 주소(VA)를 물리 주소(PA)에 매핑하는 기능을 포함합니다.*/
struct uninit_page {
	/* Initiate the contets of the page */
	vm_initializer *init;	// 페이지 내용을 초기화하는 함수 포인터 
	enum vm_type type;		// 페이지 타입을 나타내는 열거형
	void *aux;				
	/* Initiate the struct page and maps the pa to the va */
	bool (*page_initializer) (struct page *, enum vm_type, void *kva);
};

void uninit_new (struct page *page, void *va, vm_initializer *init,
		enum vm_type type, void *aux,
		bool (*initializer)(struct page *, enum vm_type, void *kva));
#endif