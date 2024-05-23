#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"
#include "process.h"
#include "filesys/filesys.h"
#include "file.h"
#include "devices/input.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);
void check_address(char *addr);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	printf ("system call!\n");
	thread_exit ();
}

// Implements
void
check_address(char *addr) {
	struct thread* cur = thread_current();
	if (addr == NULL || is_kernel_vaddr(addr) || pml4_get_page(cur->pml4, addr)) exit(-1);
}

void
halt(void) {
	power_off();
}

void
exit(int status) {
	struct thread *cur = thread_current();
	printf("%s: exit(%d)\n", cur->name, status);
	thread_exit();
}

bool 
create(const char *file, unsigned initial_size) {
	check_address(file);
	return filesys_create(file, initial_size);
}

bool
remove(const char *file) {
	check_address(file);
	return filesys_remove(file);
}

int 
open (const char *file) {
	// 예외 처리 필요
	struct thread *cur = thread_current();
	struct file *_file = filesys_open(file);
	check_address(file);
	int fd = process_add_file(_file);
	return fd;
}

int 
filesize (int fd) {
	struct file *file = process_get_file(fd);
	check_address(file);
	return file_length(file);
}

int
read (int fd, void *buffer, unsigned size) {
	// 시작
}