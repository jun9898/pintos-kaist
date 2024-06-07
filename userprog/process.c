#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/tss.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/mmu.h"
#include "threads/vaddr.h"
#include "intrinsic.h"
#define VM

#ifdef VM
#include "vm/vm.h"
#endif

#define POINTER_SIZE 8;

void process_exit_file(void);
int process_add_file(struct file *f);
struct file *process_get_file(int fd);
void process_close_file(int fd);
struct thread *get_child_process(int pid);
struct thread *remove_child_process(struct thread *cp);

static void argument_stack(char *parse[], int count, struct intr_frame *_if);
static void process_cleanup (void);
static bool load (const char *file_name, struct intr_frame *if_);
static void initd (void *f_name);
static void __do_fork (void *);
void process_exit(void);

/* General process initializer for initd and other process. */
static void
process_init (void) {
	struct thread *current = thread_current ();
}

/* Starts the first userland program, called "initd", loaded from FILE_NAME.
 * The new thread may be scheduled (and may even exit)
 * before process_create_initd() returns. Returns the initd's
 * thread id, or TID_ERROR if the thread cannot be created.
 * Notice that THIS SHOULD BE CALLED ONCE.
 * 현재 코드는 process가 종료되길 기다리지 않는다. -> 수정해야함
 * process_create_initd함수는 file_name을 인자로 받아 파일 이름을 가져온다 ex) a.out, ls
 * thread_create를 호출해 새 스레드를 생성한다 */
tid_t
process_create_initd (const char *file_name) {
	char *fn_copy;
	tid_t tid;

	/* Make a copy of FILE_NAME.
	 * Otherwise there's a race between the caller and load(). */
	fn_copy = palloc_get_page (PAL_ZERO);
	if (fn_copy == NULL)
		return TID_ERROR;
	strlcpy (fn_copy, file_name, PGSIZE);

	char *save_ptr, *token;

	token = strtok_r(file_name, " ", &save_ptr); 

	/* Create a new thread to execute FILE_NAME. */
	tid = thread_create (token, PRI_DEFAULT, initd, fn_copy);
	if (tid == TID_ERROR)
		palloc_free_page (fn_copy);
	return tid;
}

/* A thread function that launches first user process. */
static void
initd (void *f_name) {
#ifdef VM
	supplemental_page_table_init (&thread_current ()->spt);
#endif

	process_init ();

	if (process_exec (f_name) < 0)
		PANIC("Fail to launch initd\n");
	NOT_REACHED ();
}

/* Clones the current process as `name`. Returns the new process's thread id, or
 * TID_ERROR if the thread cannot be created. */
tid_t
process_fork (const char *name, struct intr_frame *if_ UNUSED) {
	/* Clone current thread to new thread.*/
	struct thread *cur = thread_current();

	tid_t pid = thread_create (name, PRI_DEFAULT, __do_fork, cur); // 자식 pid 반환
	if (pid == TID_ERROR)
		return TID_ERROR;

	struct thread *child = get_child_process(pid);

	sema_down(&child->load_sema);

	if (child->exit_status == TID_ERROR) {
		list_remove(&child->child_elem);
		sema_up(&child->exit_sema);
		return TID_ERROR;
	}

	return pid;
}

#ifndef VM
/* Duplicate the parent's address space by passing this function to the
 * pml4_for_each. This is only for the project 2. */
static bool
duplicate_pte (uint64_t *pte, void *va, void *aux) {
	struct thread *current = thread_current ();
	struct thread *parent = (struct thread *) aux;
	void *parent_page;
	void *newpage;
	bool writable;

	/* 1. TODO: If the parent_page is kernel page, then return immediately. */
	if (is_kernel_vaddr(va)) {
		return true;
	}

	/* 2. Resolve VA from the parent's page map level 4. */
	parent_page = pml4_get_page (parent->pml4, va);
	if (!parent_page) {
		return false;
	}

	/* 3. TODO: Allocate new PAL_USER page for the child and set result to
	 *    TODO: NEWPAGE. */
	newpage = palloc_get_page(PAL_USER | PAL_ZERO);
	if (newpage == NULL) {
		return false;
	}
	/* 4. TODO: Duplicate parent's page to the new page and
	 *    TODO: check whether parent's page is writable or not (set WRITABLE
	 *    TODO: according to the result). */
	memcpy(newpage, parent_page, PGSIZE);
	writable = is_writable(pte);


	/* 5. Add new page to child's page table at address VA with WRITABLE
	 *    permission. */
	if (!pml4_set_page(current->pml4, va, newpage, writable)) {
		return false;
	}
	return true;
}
#endif

/* A thread function that copies parent's execution context.
 * Hint) parent->tf does not hold the userland context of the process.
 *       That is, you are required to pass second argument of process_fork to
 *       this function. */
static void
__do_fork (void *aux) {			
	struct intr_frame if_;
	struct thread *parent = (struct thread *) aux;
	struct thread *current = thread_current ();
	/* TODO: somehow pass the parent_if. (i.e. process_fork()'s if_) */
	struct intr_frame *parent_if = parent->parent_if; // 저장해둔 parent_if
	bool succ = true;

	/* 1. Read the cpu context to local stack. */
	memcpy (&if_, parent_if, sizeof (struct intr_frame));
	if_.R.rax = 0;

	/* 2. Duplicate PT */
	current->pml4 = pml4_create();
	if (current->pml4 == NULL)
		goto error;

	process_activate (current);
#ifdef VM
	supplemental_page_table_init (&current->spt);
	if (!supplemental_page_table_copy (&current->spt, &parent->spt))
		goto error;
#else
	if (!pml4_for_each (parent->pml4, duplicate_pte, parent))
		goto error;
#endif

	/* TODO: Your code goes here.
	 * TODO: Hint) To duplicate the file object, use `file_duplicate`
	 * TODO:       in include/filesys/file.h. Note that parent should not return
	 * TODO:       from the fork() until this function successfully duplicates
	 * TODO:       the resources of parent.*/

	// 부모의 모든 fdt를 순회하면서 file_duplicate() 함수로 file을 복제하고 current.fdt로 복제하면 된다.
	for (int i = FDT_PAGES; i < FDT_COUNT_LIMIT; i++) {
		struct file *file = parent->fdt[i];
		if (file == NULL) 
			continue;
		file = file_duplicate(file);
		current->fdt[i] = file;
	}

	current->next_fd = parent->next_fd;

	// 그렇게 로드가 끝나면 sema_up으로 대기중인 부모 프로세스의 lock을 풀어준다.
	sema_up(&current->load_sema);
	process_init ();

	/* Finally, switch to the newly created process. */
	if (succ)
		do_iret (&if_);
error:
	sema_up(&current->load_sema);
	exit(TID_ERROR);
}

/* Switch the current execution context to the f_name.
 * Returns -1 on fail. 
 * 1. 실행하려는 바이너리 파일의 이름을 인자로 받는다. */
int
process_exec (void *f_name) {
	char *file_name = f_name;
	bool success;

	/* We cannot use the intr_frame in the thread structure.
	 * This is because when current thread rescheduled,
	 * it stores the execution information to the member. */
	struct intr_frame _if;
	_if.ds = _if.es = _if.ss = SEL_UDSEG;
	_if.cs = SEL_UCSEG;
	_if.eflags = FLAG_IF | FLAG_MBS;

	/* We first kill the current context */
	process_cleanup ();

	char *token_arr[128];
	char *save_ptr, *token;
	int count = 0;

	for (token = strtok_r(file_name, " ", &save_ptr); 
		 token != NULL; token = strtok_r(NULL, " ", &save_ptr))
		 token_arr[count++] = token;
	/* And then load the binary 
 	 * 2. 디스크에서 해당 바이너리 파일을 메모리로 로드한다 -> load() */
	success = load (file_name, &_if);
	/* If load failed, quit. */
	if (!success)
		return -1;

	argument_stack(&token_arr, count, &_if);

	/* Debug Code */
	// hex_dump(_if.rsp, _if.rsp, USER_STACK - _if.rsp, true);

	palloc_free_page (file_name);
	/* Start switched process. */
	do_iret (&_if);
	NOT_REACHED ();
}


/* Waits for thread TID to die and returns its exit status.  If
 * it was terminated by the kernel (i.e. killed due to an
 * exception), returns -1.  If TID is invalid or if it was not a
 * child of the calling process, or if process_wait() has already
 * been successfully called for the given TID, returns -1
 * immediately, without waiting.
 *
 * This function will be implemented in problem 2-2.  For now, it
 * does nothing. */
// 구현해야 할 함수
int
process_wait (tid_t child_tid UNUSED) {
	/* XXX: Hint) The pintos exit if process_wait (initd), we recommend you
	 * XXX:       to add infinite loop here before
	 * XXX:       implementing the process_wait. */
	struct thread *child_thread = get_child_process(child_tid);
	if (child_thread == NULL) {
		return -1;
	}
	sema_down(&child_thread->wait_sema);
	list_remove(&child_thread->child_elem);
	sema_up(&child_thread->exit_sema);
	return child_thread->exit_status;
}

/* Exit the process. This function is called by thread_exit (). */
void
process_exit (void) {
	struct thread *cur = thread_current ();
	/* TODO: Your code goes here.
	 * TODO: Implement process termination message (see
	 * TODO: project2/process_termination.html).
	 * TODO: We recommend you to implement process resource cleanup here. */

	process_cleanup ();

	process_exit_file();
	palloc_free_multiple(cur->fdt, FDT_PAGES);

	sema_up(&cur->wait_sema);
	sema_down(&cur->exit_sema);
}

/* Free the current process's resources. */
static void
process_cleanup (void) {
	struct thread *curr = thread_current ();

#ifdef VM
	supplemental_page_table_kill (&curr->spt);
#endif

	uint64_t *pml4;
	/* Destroy the current process's page directory and switch back
	 * to the kernel-only page directory. */
	pml4 = curr->pml4;
	if (pml4 != NULL) {
		/* Correct ordering here is crucial.  We must set
		 * cur->pagedir to NULL before switching page directories,
		 * so that a timer interrupt can't switch back to the
		 * process page directory.  We must activate the base page
		 * directory before destroying the process's page
		 * directory, or our active page directory will be one
		 * that's been freed (and cleared). */
		curr->pml4 = NULL;
		pml4_activate (NULL);
		pml4_destroy (pml4);
	}
}

/* Sets up the CPU for running user code in the nest thread.
 * This function is called on every context switch. */
void
process_activate (struct thread *next) {
	/* Activate thread's page tables. */
	pml4_activate (next->pml4);

	/* Set thread's kernel stack for use in processing interrupts. */
	tss_update (next);
}

/* We load ELF binaries.  The following definitions are taken
 * from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
#define EI_NIDENT 16

#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
 * This appears at the very beginning of an ELF binary. */
struct ELF64_hdr {
	unsigned char e_ident[EI_NIDENT];
	uint16_t e_type;
	uint16_t e_machine;
	uint32_t e_version;
	uint64_t e_entry;
	uint64_t e_phoff;
	uint64_t e_shoff;
	uint32_t e_flags;
	uint16_t e_ehsize;
	uint16_t e_phentsize;
	uint16_t e_phnum;
	uint16_t e_shentsize;
	uint16_t e_shnum;
	uint16_t e_shstrndx;
};

struct ELF64_PHDR {
	uint32_t p_type;
	uint32_t p_flags;
	uint64_t p_offset;
	uint64_t p_vaddr;
	uint64_t p_paddr;
	uint64_t p_filesz;
	uint64_t p_memsz;
	uint64_t p_align;
};

/* Abbreviations */
#define ELF ELF64_hdr
#define Phdr ELF64_PHDR

static bool setup_stack (struct intr_frame *if_);
static bool validate_segment (const struct Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes,
		bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
 * Stores the executable's entry point into *RIP
 * and its initial stack pointer into *RSP.
 * Returns true if successful, false otherwise. 
 * if_ -> 인터럽트 프레임 인터럽트가 발생했을때 레지스터의 정보를 저장함 */
static bool
load (const char *file_name, struct intr_frame *if_) {
	struct thread *t = thread_current ();
	// ELF 헤더
	struct ELF ehdr;
	struct file *file = NULL;
	off_t file_ofs;
	bool success = false;
	int i;

	/* Allocate and activate page directory. */
	t->pml4 = pml4_create ();
	if (t->pml4 == NULL)
		goto done;
	process_activate (thread_current ());

	/* Open executable file. 
	 * 파일을 Open한다 */
	file = filesys_open (file_name);
	if (file == NULL) {
		printf ("load: %s: open failed\n", file_name);
		goto done;
	}

	/* 
	 * Read and verify executable header. 
	 * header 파일을 읽고 유효한지 체크한다.
	 */
	if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
			|| memcmp (ehdr.e_ident, "\177ELF\2\1\1", 7)
			|| ehdr.e_type != 2
			|| ehdr.e_machine != 0x3E // amd64
			|| ehdr.e_version != 1
			|| ehdr.e_phentsize != sizeof (struct Phdr)
			|| ehdr.e_phnum > 1024) {
		printf ("load: %s: error loading executable\n", file_name);
		goto done;
	}

	/* Read program headers. 
	 * header파일 구문을 분석한다 
	 * 파일 오프셋 설정 및 초기화 */
	file_ofs = ehdr.e_phoff;
	/* 헤더 테이블의 구조가 고정된 크기의 엔트리 배열로 되어있기 때문에
	 * 이런식의 순회가 가능하다 */
	for (i = 0; i < ehdr.e_phnum; i++) {
		struct Phdr phdr;

		// 파일 offset이 유효한지 확인
		if (file_ofs < 0 || file_ofs > file_length (file))
			goto done;

		// 그때 메모장으로 예시로 들었던 file_seek
		file_seek (file, file_ofs);
		if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
			goto done;

		// file_ofs 갱신
		file_ofs += sizeof phdr;
		// 프로그램 헤더의 타입에 따라 다른 처리를 합니다.
		switch (phdr.p_type) {
			// 아래 3개는 무시합니다.
			case PT_NULL:
			case PT_NOTE:
			case PT_PHDR:
			case PT_STACK:
			default:
				/* Ignore this segment. */
				break;
			// 아래 3개는 로드할 수 없는 타입으로 goto done으로 이동합니다.
			case PT_DYNAMIC:
			case PT_INTERP:
			case PT_SHLIB:
				goto done;
			// 실제로 메모리에 적재해야 하는 타입입니다.
			case PT_LOAD:
				if (validate_segment (&phdr, file)) { // 세그먼트가 유효한지 검증
					bool writable = (phdr.p_flags & PF_W) != 0; // 세그먼트 쓰기가 가능한지 확인

					// 파일과 메모리 페이지의 위치를 설정
					uint64_t file_page = phdr.p_offset & ~PGMASK;
					uint64_t mem_page = phdr.p_vaddr & ~PGMASK;
					uint64_t page_offset = phdr.p_vaddr & PGMASK;

					uint32_t read_bytes, zero_bytes;
					if (phdr.p_filesz > 0) {	// read_bytes와 zero_bytes를 계산한다. 
						/* Normal segment.
						 * Read initial part from disk and zero the rest. */
						read_bytes = page_offset + phdr.p_filesz;
						zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
								- read_bytes);
					} else {
						/* Entirely zero.
						 * Don't read anything from disk. */
						read_bytes = 0;
						zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
					}
					if (!load_segment (file, file_page, (void *) mem_page, // 세그먼트를 메모리에 로드
								read_bytes, zero_bytes, writable))
						goto done;
				}
				else
					goto done;
				break;
		}
	}

	/* Set up stack. 
	 * 사용자 스택의 최상위 포인터를 가져온다 -> 여기다가 매개변수 파싱한걸 쌓기 위해 */
	if (!setup_stack (if_))
		goto done;

	/* Start address. 
	 * 해당 바이너리 파일에서 실행할 명령어의 위치를 가져온다. 
	 * rip = 다음 실행할 명령어의 주소 보관함 */
	if_->rip = ehdr.e_entry;

	/* TODO: Your code goes here.
	 * TODO: Implement argument passing (see project2/argument_passing.html). */
	
	success = true;

done:
	/* We arrive here whether the load is successful or not. */
	file_close (file);

	return success;
}


/* Checks whether PHDR describes a valid, loadable segment in
 * FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Phdr *phdr, struct file *file) {
	/* p_offset and p_vaddr must have the same page offset. */
	if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK))
		return false;

	/* p_offset must point within FILE. */
	if (phdr->p_offset > (uint64_t) file_length (file))
		return false;

	/* p_memsz must be at least as big as p_filesz. */
	if (phdr->p_memsz < phdr->p_filesz)
		return false;

	/* The segment must not be empty. */
	if (phdr->p_memsz == 0)
		return false;

	/* The virtual memory region must both start and end within the
	   user address space range. */
	if (!is_user_vaddr ((void *) phdr->p_vaddr))
		return false;
	if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
		return false;

	/* The region cannot "wrap around" across the kernel virtual
	   address space. */
	if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
		return false;

	/* Disallow mapping page 0.
	   Not only is it a bad idea to map page 0, but if we allowed
	   it then user code that passed a null pointer to system calls
	   could quite likely panic the kernel by way of null pointer
	   assertions in memcpy(), etc. */
	if (phdr->p_vaddr < PGSIZE)
		return false;

	/* It's okay. */
	return true;
}

#ifndef VM
/* Codes of this block will be ONLY USED DURING project 2.
 * If you want to implement the function for whole project 2, implement it
 * outside of #ifndef macro. */

/* load() helpers. */
static bool install_page (void *upage, void *kpage, bool writable);

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
	ASSERT (pg_ofs (upage) == 0);
	ASSERT (ofs % PGSIZE == 0);

	file_seek (file, ofs);
	while (read_bytes > 0 || zero_bytes > 0) {
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* Get a page of memory. */
		uint8_t *kpage = palloc_get_page (PAL_USER);
		if (kpage == NULL)
			return false;

		/* Load this page. */
		if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes) {
			palloc_free_page (kpage);
			return false;
		}
		memset (kpage + page_read_bytes, 0, page_zero_bytes);

		/* Add the page to the process's address space. */
		if (!install_page (upage, kpage, writable)) {
			printf("fail\n");
			palloc_free_page (kpage);
			return false;
		}

		/* Advance. */
		read_bytes -= page_read_bytes;
		zero_bytes -= page_zero_bytes;
		upage += PGSIZE;
	}
	return true;
}

/* Create a minimal stack by mapping a zeroed page at the USER_STACK */
static bool
setup_stack (struct intr_frame *if_) {
	uint8_t *kpage;
	bool success = false;

	kpage = palloc_get_page (PAL_USER | PAL_ZERO);
	if (kpage != NULL) {
		success = install_page (((uint8_t *) USER_STACK) - PGSIZE, kpage, true);
		if (success)
			if_->rsp = USER_STACK;
		else
			palloc_free_page (kpage);
	}
	// 실패하면 -1 return
	return success;
}

/* Adds a mapping from user virtual address UPAGE to kernel
 * virtual address KPAGE to the page table.
 * If WRITABLE is true, the user process may modify the page;
 * otherwise, it is read-only.
 * UPAGE must not already be mapped.
 * KPAGE should probably be a page obtained from the user pool
 * with palloc_get_page().
 * Returns true on success, false if UPAGE is already mapped or
 * if memory allocation fails. */
/**/
static bool
install_page (void *upage, void *kpage, bool writable) {
	struct thread *t = thread_current ();

	/* Verify that there's not already a page at that virtual
	 * address, then map our page there. */
	return (pml4_get_page (t->pml4, upage) == NULL
			&& pml4_set_page (t->pml4, upage, kpage, writable));
}
#else
/* From here, codes will be used after project 3.
 * If you want to implement the function for only project 2, implement it on the
 * upper block. */


/* lazy_loading 방식으로 메모리에 세그먼트를 로드하는 기능을 한다
   이 함수는 파일 기반 페이지(file-backed-page)의 경우에 사용되며,
   페이지 폴트가 발생했을 떄 필요한 데이터만 메모리에 로드하는 역할을 한다.
   즉, 프로그램이 실제로 해당 페이지를 접근할 때까지 물리 메모리에 데이터를 로드하지 않는다.*/
/* 이 함수는 실행 파일의 내용을 페이지로 로딩하는 함수이며 첫번째 page fault가 발생할 때 호출된다.
   이 함수가 호출되기 이전에 물리 프레임 매핑이 진행되므로, 
   여기서는 물리 프레임에 내용을 로딩하는 작업만 하면 된다. */
static bool
lazy_load_segment (struct page *page, void *aux) {
	/* TODO: Load the segment from the file */
	/* TODO: This called when the first page fault occurs on address VA. */
	/* TODO: VA is available when calling this function. */
	/* TODO: 파일에서 세그먼트를 로드합니다 */
	/* TODO: 이 함수는 주소 VA에서 첫 번째 페이지 폴트가 발생했을 때 호출됩니다. */
	/* TODO: 이 함수를 호출할 때 VA는 유효합니다. */

	// aux는 load_segment에서 로딩을 위해 설정해둔 정보인 lazy_load_arg이다.
	struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)aux;

	// 1. 열린 파일에서 다음에 읽거나 쓸 바이트를 파일 시작부터 position 바이트로 변경
	// 파일의 position을 ofs로 지정
	file_seek(lazy_load_arg->file, lazy_load_arg->ofs);

	if (file_read(lazy_load_arg->file, page->frame->kva, lazy_load_arg->read_bytes) != (int)(lazy_load_arg->read_bytes)){
		palloc_free_page(page->frame->kva);
		return false;
	}
	memset(page->frame->kva + lazy_load_arg->read_bytes, 0, lazy_load_arg->zero_bytes);

	return true;
}

/* Loads a segment starting at offset OFS in FILE at address
 * UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
 * memory are initialized, as follows:
 *
 * - READ_BYTES bytes at UPAGE must be read from FILE
 * starting at offset OFS.
 *
 * - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.
 *
 * The pages initialized by this function must be writable by the
 * user process if WRITABLE is true, read-only otherwise.
 *
 * Return true if successful, false if a memory allocation error
 * or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
		uint32_t read_bytes, uint32_t zero_bytes, bool writable) {
	ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);	// read_bytes + zero_bytes가 페이지 크기(PGSIZE)의 배수인지 확인
	ASSERT (pg_ofs (upage) == 0);						// upage가 페이지 정렬되어 있는지 확인
	ASSERT (ofs % PGSIZE == 0);							// ofs가 페이지 정렬되어 있는지 확인

	while (read_bytes > 0 || zero_bytes > 0) {	// read_bytes와 zero_bytes가 0보다 큰 동안 루프를 실행
		/* Do calculate how to fill this page.
		 * We will read PAGE_READ_BYTES bytes from FILE
		 * and zero the final PAGE_ZERO_BYTES bytes. */
		/* 이 페이지를 채우는 방법을 계산합니다.
		   우리는 파일에서 PAGE_READ_BYTES 바이트를 읽고,
		   마지막 PAGE_ZERO_BYTES 바이트를 0으로 채웁니다.*/
		size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
		size_t page_zero_bytes = PGSIZE - page_read_bytes;

		/* TODO: Set up aux to pass information to the lazy_load_segment. */
		/* lazy_load_segment에 정보를 전달하기 위한 aux 설정*/

		// loading을 위해 필요한 정보를 포함하는 구조체 lazy_load_arg 만들어서 할당
		struct lazy_load_arg *lazy_load_arg = (struct lazy_load_arg *)malloc(sizeof(struct lazy_load_arg));
		lazy_load_arg->file = file;
		lazy_load_arg->ofs = ofs;
		lazy_load_arg->read_bytes = read_bytes;
		lazy_load_arg->zero_bytes = zero_bytes;

		if (!vm_alloc_page_with_initializer (VM_ANON, upage,
					writable, lazy_load_segment, lazy_load_arg))
			return false;

		/* Advance. */
		/* 다음 반복을 위해서 읽어들인 만큼 값을 갱신해준다.*/
		read_bytes -= page_read_bytes;	// 읽은 만큼 읽어야하는 바이트 수에서 빼주기
		zero_bytes -= page_zero_bytes;	// 0으로 채운만큼 0으로 채워야하는 바이트 수에서 빼주기
		upage += PGSIZE;				// 페이지 사이즈 만큼 주소 포인터 이동? 
		ofs += page_read_bytes;			// 파일에서 읽기 시작할 위치 갱신
	}
	return true;
}

/* Create a PAGE of stack at the USER_STACK. Return true on success. */
/* 사용자 스택을 설정하는 함수.
   이 함수는 성공시 true를 실패시 false를 반환한다.
   구체적으로, USER_STACK 주소에 페이지를 할당하고, 스택의 시작 주소를 설정한다.*/
static bool
setup_stack (struct intr_frame *if_) {
	bool success = false;
	void *stack_bottom = (void *) (((uint8_t *) USER_STACK) - PGSIZE);	// 유저스

	/* TODO: Map the stack on stack_bottom and claim the page immediately.
	 * TODO: If success, set the rsp accordingly.
	 * TODO: You should mark the page is stack. */
    /* TODO: stack_bottom에 스택을 매핑하고 페이지를 즉시 할당합니다.
     * TODO: 성공 시, rsp를 적절히 설정합니다.
     * TODO: 페이지를 스택으로 표시해야 합니다. */
	if (vm_alloc_page(VM_ANON | VM_MARKER_0, stack_bottom, true)){	// ANON 페이지로 할당, VM_MARKER_0 : 스택임을 표시하는 마커, stack_bottom에 할당, writable :  
		/*첫 번째 스택 페이지는 lazy_loading을 할 필요가 없다. 
		프로세스가 실행될 때 이 함수가 불리고 나서 command line arguments를 
		바로 스택에 추가하기 위해서 바로 접근하기 때문*/
		if (vm_claim_page(stack_bottom)){	// 페이지 즉시 할당
			success = true;
			if_->rsp = USER_STACK;			// 스택 포인터 최상단 가리키게
		}
	}

	return success;
}
#endif /* VM */

static void
argument_stack(char *parse[], int count, struct intr_frame *_if) {

	for (int i = count - 1; i >= 0; i--) {
		size_t len = strlen(parse[i]) + 1;
		_if->rsp -= len;
		memcpy(_if->rsp, parse[i], len);
		parse[i] = _if->rsp;
	}

	uint8_t padding = _if->rsp % 8;
	if (padding) {
		memset(_if->rsp -= (sizeof(uint8_t) * padding), 0, sizeof(uint8_t) * padding);
	}

	_if->rsp -= sizeof(uintptr_t);
	memset(_if->rsp, 0, sizeof(uintptr_t));

	for (int i = count - 1; i >= 0; i--) {
		_if->rsp -= sizeof(uintptr_t);
		_if->rsp = memcpy(_if->rsp, &parse[i], sizeof(uintptr_t));
	}

	_if->R.rsi = _if->rsp;
	_if->R.rdi = count;

	_if->rsp -= sizeof(uintptr_t);
	memset(_if->rsp, 0, sizeof(uintptr_t));
}

void 
process_exit_file(void) {
	struct thread *cur = thread_current();
    for (int i = FDT_PAGES; i <= FDT_COUNT_LIMIT; i++) {
		if (cur->fdt[i] != NULL) {
			process_close_file(i);
		}
    }
}

int 
process_add_file(struct file *f) {

	struct thread *cur = thread_current();
 	int tmp_fd;

    for (tmp_fd = FDT_PAGES; tmp_fd <= FDT_COUNT_LIMIT; tmp_fd++) {
		if (cur->fdt[tmp_fd] == NULL) {
			cur->fdt[tmp_fd] = f;
			cur->next_fd = tmp_fd;
			return cur->next_fd;
		}
    }
	return -1;
}

struct file 
*process_get_file(int fd) {
	if (fd < FDT_PAGES || fd > FDT_COUNT_LIMIT)
		return NULL;
	return thread_current()->fdt[fd];
}

void 
process_close_file(int fd) {
	if (fd < FDT_PAGES || fd > FDT_COUNT_LIMIT || fd == NULL)
		return NULL;

	struct thread *cur = thread_current();
	struct file *open_file = process_get_file(fd);
	if (open_file == NULL) return NULL;

	cur->fdt[fd] = NULL;
	file_close(open_file);
}

struct thread *get_child_process(int pid) {
	struct thread *cur = thread_current();
	for (struct list_elem *e = list_begin(&cur->children_list); e != list_end(&cur->children_list); e = list_next(e)) {
		struct thread *t = list_entry(e, struct thread, child_elem);
		if (t->tid == pid)
			return t;
	}
	return NULL;
}
